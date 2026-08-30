/**
 * @file StateControl.cpp
 * @brief CPU state control, trap handling, and privilege management.
 */
#include "simrv/core/StateControl.hpp"

#include <cstdint>
#include <print>
#include <ranges>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/Sbi.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

constexpr Counter kWord32Mask = static_cast<Counter>(kLower32Mask);
constexpr Counter kWord32Shift = 32;

constexpr int kLogHexWidth = static_cast<int>(kXLenHexDigits);

constexpr Address kClintMtimecmpOffset = 0x4000;
constexpr Address kClintMtimeOffset = 0xbff8;

void InterruptController::updateMip(PlicMmio& plic, ArchState& state) {
    if (plic.cpu_.machine_ != nullptr) {
        const size_t n_harts = plic.cpu_.machine_->num_harts();
        for (size_t h = 0; h < n_harts; ++h) {
            auto& target_cpu = plic.cpu_.machine_->hart(h);
            auto& target_state = target_cpu.state();

            const size_t m_ctx = 2 * h;
            const size_t s_ctx = 2 * h + 1;

            bool m_ext = false;
            bool s_ext = false;

            if (m_ctx < PlicMmio::kMaxPlicContexts) {
                Word m_max_prio = 0;
                for (int i : std::views::iota(1, 32)) {
                    const auto idx = static_cast<std::size_t>(i);
                    if ((plic.plic_pending[0] & (1u << i)) != 0 &&
                        (plic.plic_enables[m_ctx][0] & (1u << i)) != 0) {
                        if (plic.plic_priorities[idx] > m_max_prio) {
                            m_max_prio = plic.plic_priorities[idx];
                        }
                    }
                }
                if (m_max_prio > plic.plic_threshold[m_ctx]) {
                    m_ext = true;
                }
            }

            if (s_ctx < PlicMmio::kMaxPlicContexts) {
                Word s_max_prio = 0;
                for (int i : std::views::iota(1, 32)) {
                    const auto idx = static_cast<std::size_t>(i);
                    if ((plic.plic_pending[0] & (1u << i)) != 0 &&
                        (plic.plic_enables[s_ctx][0] & (1u << i)) != 0) {
                        if (plic.plic_priorities[idx] > s_max_prio) {
                            s_max_prio = plic.plic_priorities[idx];
                        }
                    }
                }
                if (s_max_prio > plic.plic_threshold[s_ctx]) {
                    s_ext = true;
                }
            }

            if (m_ext)
                target_state.mip |= enum_mask(MipBit::Meip);
            else
                target_state.mip &= ~enum_mask(MipBit::Meip);

            target_state.seip_external = s_ext;
            target_state.refresh_supervisor_pending();
        }
        return;
    }

    // Evaluate Context 0 (M-mode)
    bool m_ext = false;
    Word m_max_prio = 0;
    for (int i : std::views::iota(1, 32)) {
        const auto idx = static_cast<std::size_t>(i);
        if ((plic.plic_pending[0] & (1u << i)) != 0 && (plic.plic_enables[0][0] & (1u << i)) != 0) {
            if (plic.plic_priorities[idx] > m_max_prio) {
                m_max_prio = plic.plic_priorities[idx];
            }
        }
    }
    if (m_max_prio > plic.plic_threshold[0]) {
        m_ext = true;
    }

    // Evaluate Context 1 (S-mode)
    bool s_ext = false;
    Word s_max_prio = 0;
    for (int i : std::views::iota(1, 32)) {
        const auto idx = static_cast<std::size_t>(i);
        if ((plic.plic_pending[0] & (1u << i)) != 0 && (plic.plic_enables[1][0] & (1u << i)) != 0) {
            if (plic.plic_priorities[idx] > s_max_prio) {
                s_max_prio = plic.plic_priorities[idx];
            }
        }
    }
    if (s_max_prio > plic.plic_threshold[1]) {
        s_ext = true;
    }

    if (m_ext)
        state.mip |= enum_mask(MipBit::Meip);
    else
        state.mip &= ~enum_mask(MipBit::Meip);

    state.seip_external = s_ext;
    state.refresh_supervisor_pending();
}

void InterruptController::setIrq(PlicMmio& plic, int irq_num, int state_val) {
    if (irq_num <= 0 || irq_num >= 32) return;
    const Word mask = static_cast<Word>(1) << irq_num;
    if (state_val != 0) {
        plic.plic_pending[0] |= mask;
    } else {
        plic.plic_pending[0] &= ~mask;
    }
    plic.cpu_.plic_update_mip();
}

void PlicMmio::reset() {
    plic_pending.fill(0);
    plic_priorities.fill(0);
    for (auto& enables : plic_enables) enables.fill(0);
    plic_threshold.fill(0);
    plic_claim.fill(0);
}

auto PlicMmio::handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool {
    if (req.opcode == memory::TlOpcodeA::Get) {
        resp.data = mmio_read(offset(req.address));
    } else {
        mmio_write(offset(req.address), req.data);
    }
    return true;
}

auto PlicMmio::get_context_for_offset(Address offset) const -> int {
    if (offset >= 0x200000 && offset < 0x200000 + (kMaxPlicContexts * 0x1000)) {
        return static_cast<int>((offset - 0x200000) / 0x1000);
    }
    return -1;
}

auto PlicMmio::mmio_read(Address offset) -> Word {
    if (offset < 0x1000) {
        // PLIC interrupt source zero is reserved and its priority is hardwired to zero.
        if (offset == 0) return 0;
        return plic_priorities[offset / 4];
    }
    if (offset >= 0x1000 && offset < 0x1080) {
        return plic_pending[(offset - 0x1000) / 4];
    }
    if (offset >= 0x2000 && offset < 0x2000 + (kMaxPlicContexts * 0x80)) {
        const auto ctx = (offset - 0x2000) / 0x80;
        const auto word = ((offset - 0x2000) % 0x80) / 4;
        const Word value = plic_enables[ctx][word];
        return (word == 0) ? (value & ~Word{1}) : value;
    }

    int context = get_context_for_offset(offset);
    if (context >= 0) {
        const auto ctx_idx = static_cast<std::size_t>(context);
        Address ctx_base = 0x200000 + (context * 0x1000);
        if (offset == ctx_base) {
            return plic_threshold[ctx_idx];
        }
        if (offset == ctx_base + 4) {
            // Claim: evaluate highest priority pending & enabled for this context
            Word max_prio = 0;
            int claim_id = 0;
            for (int i : std::views::iota(1, 32)) {
                const auto idx = static_cast<std::size_t>(i);
                if ((plic_pending[0] & (1u << i)) != 0 &&
                    (plic_enables[ctx_idx][0] & (1u << i)) != 0) {
                    if (plic_priorities[idx] > max_prio) {
                        max_prio = plic_priorities[idx];
                        claim_id = i;
                    }
                }
            }
            if (max_prio <= plic_threshold[ctx_idx]) {
                claim_id = 0;
            }
            if (claim_id > 0) {
                // Clear the pending bit on claim
                plic_pending[0] &= ~(1u << claim_id);
                plic_claim[ctx_idx] = static_cast<Word>(claim_id);
                cpu_.plic_update_mip();
            }
            return static_cast<Word>(claim_id);
        }
    }
    return 0;
}

void PlicMmio::mmio_write(Address offset, Word wdata) {
    if (offset < 0x1000) {
        // Source zero means "no interrupt" and is not configurable.
        if (offset != 0) plic_priorities[offset / 4] = wdata;
    } else if (offset >= 0x2000 && offset < 0x2000 + (kMaxPlicContexts * 0x80)) {
        const auto ctx = (offset - 0x2000) / 0x80;
        const auto word = ((offset - 0x2000) % 0x80) / 4;
        plic_enables[ctx][word] = (word == 0) ? (wdata & ~Word{1}) : wdata;
    } else {
        int context = get_context_for_offset(offset);
        if (context >= 0) {
            const auto ctx_idx = static_cast<std::size_t>(context);
            Address ctx_base = 0x200000 + (context * 0x1000);
            if (offset == ctx_base) {
                plic_threshold[ctx_idx] = wdata;
            } else if (offset == ctx_base + 4) {
                // Complete: indicates the handler has finished with the IRQ
                if (plic_claim[ctx_idx] == wdata && wdata != 0) {
                    plic_claim[ctx_idx] = 0;
                    if (cpu_.machine_ && wdata == 3 && cpu_.machine_->uart_device() &&
                        cpu_.machine_->uart_device()->is_interrupt_pending()) {
                        InterruptController::setIrq(*this, static_cast<int>(wdata), 1);
                    }
                }
            }
        }
    }
    cpu_.plic_update_mip();
}

auto ClintMmio::handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool {
    const Address off = offset(req.address);
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

void ClintMmio::reset() {
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

auto ClintMmio::mmio_read(Address offset) const -> Word {
    if (offset < 0x4000 || (offset >= 0x80000 && offset < 0x84000)) {
        const size_t hart_id = (offset >= 0x80000) ? ((offset - 0x80000) / 4) : (offset / 4);
        if (cpu_.machine_ && hart_id < cpu_.machine_->num_harts()) {
            return (cpu_.machine_->hart(hart_id).state().mip & enum_mask(MipBit::Msip)) != 0 ? 1
                                                                                             : 0;
        }
        if (hart_id == 0) {
            return (cpu_.state().mip & enum_mask(MipBit::Msip)) != 0 ? 1 : 0;
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

void ClintMmio::mmio_write(Address offset, Word wdata) {
    const Counter wdata_64 = static_cast<Counter>(wdata) & kWord32Mask;
    if (offset < 0x4000 || (offset >= 0x80000 && offset < 0x84000)) {
        const size_t hart_id = (offset >= 0x80000) ? ((offset - 0x80000) / 4) : (offset / 4);
        if (cpu_.machine_ && hart_id < cpu_.machine_->num_harts()) {
            if ((wdata & 1) != 0) {
                cpu_.machine_->hart(hart_id).state().mip |= enum_mask(MipBit::Msip);
            } else {
                cpu_.machine_->hart(hart_id).state().mip &= ~enum_mask(MipBit::Msip);
            }
        } else if (hart_id == 0) {
            if ((wdata & 1) != 0) {
                cpu_.state().mip |= enum_mask(MipBit::Msip);
            } else {
                cpu_.state().mip &= ~enum_mask(MipBit::Msip);
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

void TrapController::mret(ArchState& state) {
    CSRValue mstatus = state.mstatus;
    const CSRValue mpp = (mstatus & enum_mask(MstatusBit::Mpp)) >> 11;
    const CSRValue mpie = (mstatus & enum_mask(MstatusBit::Mpie)) >> 7;

    // Restore MIE (bit 3) from MPIE (bit 7)
    if (mpie != 0) {
        mstatus |= enum_mask(MstatusBit::Mie);
    } else {
        mstatus &= ~enum_mask(MstatusBit::Mie);
    }
    // Set MPIE (bit 7) to 1
    mstatus |= enum_mask(MstatusBit::Mpie);
    // xRET resets xPP to the least-privileged supported mode.
    const bool has_s = isa::misa_has_extension(state.misa, isa::IsaExtension::S);
    const bool has_u = isa::misa_has_extension(state.misa, isa::IsaExtension::U);
    mstatus = (mstatus & ~enum_mask(MstatusBit::Mpp)) | (least_supported_mpp(has_s, has_u) << 11U);

    if (static_cast<PrivilegeLevel>(mpp) < kPrivMachine) {
        mstatus &= ~enum_mask(MstatusBit::Mprv);
    }

    state.mstatus = mstatus;
    state.priv = static_cast<PrivilegeLevel>(mpp);
    state.update_xlen();
    state.pc = isa::epc_read_value(state.mepc, state.misa);
    if (state.regs.xlen == 32) {
        state.pc = static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(state.pc)));
    }
    state.reserved = 0;
}

void TrapController::sret(ArchState& state) {
    CSRValue mstatus = state.mstatus;
    const CSRValue spp = (mstatus & enum_mask(MstatusBit::Spp)) >> 8;
    const CSRValue spie = (mstatus & enum_mask(MstatusBit::Spie)) >> 5;

    // Restore SIE (bit 1) from SPIE (bit 5)
    if (spie != 0) {
        mstatus |= enum_mask(MstatusBit::Sie);
    } else {
        mstatus &= ~enum_mask(MstatusBit::Sie);
    }
    // Set SPIE (bit 5) to 1
    mstatus |= enum_mask(MstatusBit::Spie);
    // Clear SPP (bit 8) to U-mode (0)
    mstatus &= ~enum_mask(MstatusBit::Spp);

    // Returning to a less privileged mode than M-mode always clears MPRV
    mstatus &= ~enum_mask(MstatusBit::Mprv);

    state.mstatus = mstatus;
    state.priv = static_cast<PrivilegeLevel>(spp);
    state.update_xlen();
    state.pc = isa::epc_read_value(state.sepc, state.misa);
    if (state.regs.xlen == 32) {
        state.pc = static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(state.pc)));
    }
    state.reserved = 0;
}

namespace {
auto trap_cause_name(TrapCause cause) -> std::string {
    const bool is_interrupt = trap_is_interrupt(cause);
    const uint64_t code = trap_exception_code(cause);
    if (is_interrupt) {
        switch (code) {
            case 1:
                return "Supervisor software interrupt";
            case 3:
                return "Machine software interrupt";
            case 5:
                return "Supervisor timer interrupt";
            case 7:
                return "Machine timer interrupt";
            case 9:
                return "Supervisor external interrupt";
            case 11:
                return "Machine external interrupt";
            default:
                return "Unknown interrupt " + std::to_string(code);
        }
    } else {
        switch (code) {
            case 0:
                return "Instruction address misaligned";
            case 1:
                return "Instruction access fault";
            case 2:
                return "Illegal instruction";
            case 3:
                return "Breakpoint";
            case 4:
                return "Load address misaligned";
            case 5:
                return "Load access fault";
            case 6:
                return "Store/AMO address misaligned";
            case 7:
                return "Store/AMO access fault";
            case 8:
                return "Environment call from U-mode";
            case 9:
                return "Environment call from S-mode";
            case 11:
                return "Environment call from M-mode";
            case 12:
                return "Instruction page fault";
            case 13:
                return "Load page fault";
            case 15:
                return "Store/AMO page fault";
            default:
                return "Unknown exception " + std::to_string(code);
        }
    }
}
}  // namespace

void TrapController::raiseException(CPU& cpu, TrapCause cause, CSRValue tval) {
    ArchState& state = cpu.state();
    const Address trap_pc = state.pc;

    if (cpu.trap_log_stream != nullptr && cpu.trap_log_stream->is_open()) {
        std::println(
            *cpu.trap_log_stream,
            "TRAP mtime={} cause={:0{}x} ({}) pc={:0{}x} priv={} ra={:0{}x} sp={:0{}x} tp={:0{}x} "
            "a0={:0{}x} "
            "a1={:0{}x} mtvec={:0{}x} stvec={:0{}x} mepc={:0{}x} sepc={:0{}x} satp={:0{}x} "
            "tval={:0{}x}",
            static_cast<Counter>(cpu.clint_mmio.mtime.load()), static_cast<uint64_t>(cause),
            kLogHexWidth, trap_cause_name(cause), static_cast<uint64_t>(trap_pc), kLogHexWidth,
            static_cast<unsigned>(state.priv), static_cast<uint64_t>(state.regs.read(RegId::Ra)),
            kLogHexWidth, static_cast<uint64_t>(state.regs.read(RegId::Sp)), kLogHexWidth,
            static_cast<uint64_t>(state.regs.read(RegId::Tp)), kLogHexWidth,
            static_cast<uint64_t>(state.regs.read(RegId::A0)), kLogHexWidth,
            static_cast<uint64_t>(state.regs.read(RegId::A1)), kLogHexWidth,
            static_cast<uint64_t>(state.mtvec), kLogHexWidth, static_cast<uint64_t>(state.stvec),
            kLogHexWidth, static_cast<uint64_t>(state.mepc), kLogHexWidth,
            static_cast<uint64_t>(state.sepc), kLogHexWidth, static_cast<uint64_t>(state.satp),
            kLogHexWidth, static_cast<uint64_t>(tval), kLogHexWidth);
        cpu.trap_log_stream->flush();
    }

    if (cpu.sbi.handle_ecall(cause)) {
        state.reserved = 0;
        cpu.pipeline_context.pending_exception = std::nullopt;
        cpu.pipeline_context.pending_tval = 0;
        return;
    }

    CSRValue deleg = 0;
    if (state.priv <= kPrivSupervisor && misa_has_extension(state.misa, isa::IsaExtension::S)) {
        const auto cause_code = static_cast<CSRValue>(trap_exception_code(cause));
        if (cause_code < xlen::kXLenBits) {
            if (trap_is_interrupt(cause)) {
                deleg = (state.mideleg >> cause_code) & 1;
            } else {
                deleg = (state.medeleg >> cause_code) & 1;
            }
        }
    } else {
        deleg = 0;
    }

    if (deleg != 0u) {
        state.scause = cause;
        state.sepc = trap_pc;
        state.stval = tval;
        state.mstatus = (state.mstatus & ~enum_mask(MstatusBit::Spie)) |
                        (((state.mstatus & enum_mask(MstatusBit::Sie)) >> 1) << 5);
        state.mstatus = (state.mstatus & ~enum_mask(MstatusBit::Spp)) |
                        (static_cast<CSRValue>(std::to_underlying(state.priv)) << 8);
        state.mstatus &= ~enum_mask(MstatusBit::Sie);
        state.priv = kPrivSupervisor;
        state.update_xlen();

        const Address tvec_base = state.stvec & ~Address{3};
        const Word tvec_mode = state.stvec & 3;
        if (tvec_mode == 1 && trap_is_interrupt(cause)) {
            state.pc = tvec_base + 4 * trap_exception_code(cause);
        } else {
            state.pc = tvec_base;
        }
    } else {
        state.mcause = cause;
        state.mepc = trap_pc;
        state.mtval = tval;
        state.mstatus = (state.mstatus & ~enum_mask(MstatusBit::Mpie)) |
                        (((state.mstatus & enum_mask(MstatusBit::Mie)) >> 3) << 7);
        state.mstatus = (state.mstatus & ~enum_mask(MstatusBit::Mpp)) |
                        (static_cast<CSRValue>(std::to_underlying(state.priv)) << 11);
        state.mstatus &= ~enum_mask(MstatusBit::Mie);
        state.priv = kPrivMachine;
        state.update_xlen();

        const Address tvec_base = state.mtvec & ~Address{3};
        const Word tvec_mode = state.mtvec & 3;
        if (tvec_mode == 1 && trap_is_interrupt(cause)) {
            state.pc = tvec_base + 4 * trap_exception_code(cause);
        } else {
            state.pc = tvec_base;
        }
    }
    if (state.regs.xlen == 32) {
        state.pc = static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(state.pc)));
    }
    // A delivered trap resumes through its guest handler.  The simulator only stops when no
    // handler exists; debugger breakpoints and watchpoints pause independently in CPU::run_cycle.
    if (state.pc == 0) {
        simrv::log::error(
            "[FATAL] Trap vector (mtvec/stvec) is 0. Cannot handle trap: {} (0x{:x}) at PC 0x{:x}. "
            "Halting simulator.",
            trap_cause_name(cause), static_cast<uint64_t>(cause), static_cast<uint64_t>(trap_pc));
        if (cpu.machine_) {
            if (cpu.machine_->tui_enabled() && cpu.machine_->tui_controller()) {
                cpu.machine_->tui_controller()->set_persistent_status_override(
                    "\033[1;31m UNHANDLED TRAP \033[0m");
                cpu.machine_->tui_controller()->pause_loop();
            }
            cpu.machine_->stop(Machine::StopReason::UnhandledTrap);
        }
    }
    state.reserved = 0;
    cpu.pipeline_context.pending_exception = std::nullopt;
    cpu.pipeline_context.pending_tval = 0;
}

auto TrapController::canExecutePrivilegedInstruction(PrivilegeLevel current_priv, CSRValue misa,
                                                     CSRValue mstatus, Instruction funct12,
                                                     Word funct7) -> bool {
    if (funct12 == static_cast<Instruction>(isa::Funct12Priv::Mret)) {
        return current_priv >= kPrivMachine;
    }
    if (funct12 == static_cast<Instruction>(isa::Funct12Priv::Sret)) {
        return misa_has_extension(misa, isa::IsaExtension::S) && current_priv >= kPrivSupervisor &&
               !(current_priv == kPrivSupervisor && (mstatus & enum_mask(MstatusBit::Tsr)) != 0);
    }
    if (funct12 == static_cast<Instruction>(isa::Funct12Priv::Uret)) {
        return false;
    }
    if (funct12 == static_cast<Instruction>(isa::Funct12Priv::Wfi)) {
        return current_priv >= kPrivSupervisor &&
               !(current_priv == kPrivSupervisor && (mstatus & enum_mask(MstatusBit::Tw)) != 0);
    }
    if (funct7 == static_cast<Instruction>(isa::Funct7Priv::SfenceVma)) {
        return misa_has_extension(misa, isa::IsaExtension::S) && current_priv >= kPrivSupervisor &&
               !(current_priv == kPrivSupervisor && (mstatus & enum_mask(MstatusBit::Tvm)) != 0);
    }
    return true;
}

auto TrapController::canAccessCsr(PrivilegeLevel current_priv, CSRValue misa, CSRAddress csr_addr,
                                  bool is_write) -> bool {
    // Debug CSRs require architectural Debug Mode, which the guest execution
    // engine does not implement. M-mode also cannot override CSR nonexistence.
    return csr_access_permitted(current_priv, isa::misa_has_extension(misa, isa::IsaExtension::S),
                                isa::misa_has_extension(misa, isa::IsaExtension::U), csr_addr,
                                is_write);
}

}  // namespace simrv::core
