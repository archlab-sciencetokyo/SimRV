/**
 * @file Clint.cpp
 * @brief Core Local Interruptor (CLINT) MMIO device implementation.
 */
#include "simrv/device/Clint.hpp"

#include <cstdint>
#include <limits>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::device {

namespace {

constexpr Counter kWord32Mask = static_cast<Counter>(kLower32Mask);
constexpr Counter kWord32Shift = 32;

constexpr Address kClintMtimecmpOffset = 0x4000;
constexpr Address kClintMtimeOffset = 0xbff8;

}  // namespace

Clint::Clint(core::CPU& cpu) : cpu_(cpu) {
    for (auto& cmp : hart_mtimecmp) {
        cmp.store(std::numeric_limits<Counter>::max(), std::memory_order_relaxed);
    }
    for (auto& timer : hart_supervisor_timer) {
        timer.store(false, std::memory_order_relaxed);
    }
}

void Clint::reset() {
    mtime.store(1, std::memory_order_release);
    mtimecmp.store(std::numeric_limits<Counter>::max(), std::memory_order_release);
    supervisor_timer.store(false, std::memory_order_release);
    for (auto& cmp : hart_mtimecmp) {
        cmp.store(std::numeric_limits<Counter>::max(), std::memory_order_release);
    }
    for (auto& timer : hart_supervisor_timer) {
        timer.store(false, std::memory_order_release);
    }
    mcycle = 1;
    rtc_divider = 0;
}

auto Clint::handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool {
    const Address off = offset(req.address.raw());
    const int req_bytes = 1 << (req.size & 3);

    if (req.opcode == memory::TlOpcodeA::Get) {
        if (req_bytes == 8) {
            if (off == kClintMtimeOffset || off == 0x7ff8) {
                resp.data = static_cast<Word>(mtime.load(std::memory_order_relaxed));
            } else if (off >= kClintMtimecmpOffset &&
                       off < kClintMtimecmpOffset + (kMaxClintHarts * 8)) {
                const size_t hid = (off - kClintMtimecmpOffset) / 8;
                if (hid == 0) {
                    resp.data = static_cast<Word>(mtimecmp.load(std::memory_order_relaxed));
                } else {
                    resp.data =
                        static_cast<Word>(hart_mtimecmp.at(hid).load(std::memory_order_relaxed));
                }
            } else {
                const Word lo = mmio_read(off);
                const Word hi = mmio_read(off + 4);
                const uint64_t combined =
                    static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32U);
                resp.data = static_cast<Word>(combined);
            }
        } else {
            resp.data = mmio_read(off);
        }
    } else {
        if (req_bytes == 8) {
            if (off == kClintMtimeOffset || off == 0x7ff8) {
                supervisor_timer.store(false, std::memory_order_release);
                mtime.store(static_cast<Counter>(req.data), std::memory_order_release);
                cpu_.evaluate_timer_interrupt();
                if (cpu_.machine_) {
                    for (size_t hart = 1; hart < cpu_.machine_->num_harts(); ++hart) {
                        cpu_.machine_->hart(hart).evaluate_timer_interrupt();
                    }
                }
            } else if (off >= kClintMtimecmpOffset &&
                       off < kClintMtimecmpOffset + (kMaxClintHarts * 8)) {
                const size_t hid = (off - kClintMtimecmpOffset) / 8;
                if (hid == 0) {
                    supervisor_timer.store(false, std::memory_order_release);
                    mtimecmp.store(static_cast<Counter>(req.data), std::memory_order_release);
                    cpu_.evaluate_timer_interrupt();
                } else {
                    hart_supervisor_timer.at(hid).store(false, std::memory_order_release);
                    hart_mtimecmp.at(hid).store(static_cast<Counter>(req.data),
                                                std::memory_order_release);
                    if (cpu_.machine_ && hid < cpu_.machine_->num_harts()) {
                        cpu_.machine_->hart(hid).evaluate_timer_interrupt();
                    }
                }
            } else {
                mmio_write(off, static_cast<Word>(req.data & kWord32Mask));
                const uint64_t wide_data = static_cast<uint64_t>(req.data);
                mmio_write(off + 4, static_cast<Word>((wide_data >> 32U) & kWord32Mask));
            }
        } else {
            mmio_write(off, req.data);
        }
    }
    return true;
}

auto Clint::mmio_read(Address offset) const -> Word {
    if (offset < 0x4000 || (offset >= 0x80000 && offset < 0x84000)) {
        const size_t hart_id = (offset >= 0x80000) ? ((offset - 0x80000) / 4) : (offset / 4);
        if (cpu_.machine_ && hart_id < cpu_.machine_->num_harts()) {
            return (cpu_.machine_->hart(hart_id).state().mip & enum_mask(core::MipBit::Msip)) != 0
                       ? 1
                       : 0;
        }
        if (hart_id == 0) {
            return (cpu_.state().mip & enum_mask(core::MipBit::Msip)) != 0 ? 1 : 0;
        }
        return 0;
    }
    if (offset >= kClintMtimecmpOffset && offset < kClintMtimecmpOffset + (kMaxClintHarts * 8)) {
        const size_t hart_id = (offset - kClintMtimecmpOffset) / 8;
        const bool is_hi = ((offset - kClintMtimecmpOffset) % 8) == 4;
        Counter const cmp = (hart_id == 0)
                                ? mtimecmp.load(std::memory_order_relaxed)
                                : hart_mtimecmp.at(hart_id).load(std::memory_order_relaxed);
        return static_cast<Word>(is_hi ? ((cmp >> 32) & kWord32Mask) : (cmp & kWord32Mask));
    }
    if (offset == kClintMtimeOffset || offset == 0x7ff8) {
        return static_cast<Word>(mtime.load(std::memory_order_relaxed) & kWord32Mask);
    }
    if (offset == kClintMtimeOffset + 4 || offset == 0x7ff8 + 4) {
        return static_cast<Word>((mtime.load(std::memory_order_relaxed) >> 32) & kWord32Mask);
    }
    return 0;
}

void Clint::mmio_write(Address offset, Word wdata) {
    const Counter wdata_64 = static_cast<Counter>(wdata) & kWord32Mask;
    if (offset < 0x4000 || (offset >= 0x80000 && offset < 0x84000)) {
        const size_t hart_id = (offset >= 0x80000) ? ((offset - 0x80000) / 4) : (offset / 4);
        if (cpu_.machine_ && hart_id < cpu_.machine_->num_harts()) {
            cpu_.machine_->set_hart_irq(static_cast<HartId>(hart_id), core::InterruptType::Software,
                                        PrivilegeLevel::Machine, (wdata & 1) != 0);
        } else if (hart_id == 0) {
            if ((wdata & 1) != 0) {
                cpu_.state().mip |= enum_mask(core::MipBit::Msip);
            } else {
                cpu_.state().mip &= ~enum_mask(core::MipBit::Msip);
            }
        }
        return;
    }
    if (offset >= kClintMtimecmpOffset && offset < kClintMtimecmpOffset + (kMaxClintHarts * 8)) {
        const size_t hart_id = (offset - kClintMtimecmpOffset) / 8;
        const bool is_hi = ((offset - kClintMtimecmpOffset) % 8) == 4;
        if (hart_id == 0) {
            supervisor_timer.store(false, std::memory_order_release);
            const Counter cur = mtimecmp.load(std::memory_order_relaxed);
            mtimecmp.store(is_hi ? ((cur & kWord32Mask) | (wdata_64 << kWord32Shift))
                                 : ((cur & ~kWord32Mask) | wdata_64),
                           std::memory_order_release);
            cpu_.evaluate_timer_interrupt();
        } else {
            hart_supervisor_timer.at(hart_id).store(false, std::memory_order_release);
            const Counter cur = hart_mtimecmp.at(hart_id).load(std::memory_order_relaxed);
            hart_mtimecmp.at(hart_id).store(is_hi
                                                ? ((cur & kWord32Mask) | (wdata_64 << kWord32Shift))
                                                : ((cur & ~kWord32Mask) | wdata_64),
                                            std::memory_order_release);
            if (cpu_.machine_ && hart_id < cpu_.machine_->num_harts()) {
                cpu_.machine_->hart(hart_id).evaluate_timer_interrupt();
            }
        }
        return;
    }
    if (offset == kClintMtimeOffset) {
        supervisor_timer.store(false, std::memory_order_release);
        const Counter cur = mtime.load(std::memory_order_relaxed);
        mtime.store((cur & ~kWord32Mask) | wdata_64, std::memory_order_release);
        cpu_.evaluate_timer_interrupt();
        return;
    }
    if (offset == kClintMtimeOffset + 4) {
        supervisor_timer.store(false, std::memory_order_release);
        const Counter cur = mtime.load(std::memory_order_relaxed);
        mtime.store((cur & kWord32Mask) | (wdata_64 << kWord32Shift), std::memory_order_release);
        cpu_.evaluate_timer_interrupt();
        return;
    }
}

}  // namespace simrv::device
