/**
 * @file Aclint.cpp
 * @brief Implementation of RISC-V ACLINT MTIMER and MSWI controllers.
 */
#include "simrv/device/Aclint.hpp"

#include <limits>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"

namespace simrv::device {

// =========================================================================
// AclintMtimer Implementation
// =========================================================================

AclintMtimer::AclintMtimer(simrv::core::Machine* machine) : memory::MmioDevice(machine) { reset(); }

void AclintMtimer::reset() {
    mtime_ = 0;
    for (auto& cmp : mtimecmp_) {
        cmp.store(std::numeric_limits<uint64_t>::max(), std::memory_order_relaxed);
    }
}

auto AclintMtimer::read32(Address offset) -> uint32_t {
    if (offset >= 0x4000U && offset < 0x4000U + (kMaxHarts * 8U)) {
        const size_t hart_idx = (offset - 0x4000U) / 8U;
        const uint64_t cmp = mtimecmp_[hart_idx].load(std::memory_order_relaxed);
        const bool is_hi = ((offset - 0x4000U) % 8U) >= 4U;
        return is_hi ? static_cast<uint32_t>(cmp >> 32) : static_cast<uint32_t>(cmp & 0xffffffffU);
    }
    if (offset == 0x7ff8U || offset == 0xbff8U) {
        return static_cast<uint32_t>(mtime_ & 0xffffffffU);
    }
    if (offset == 0x7ffcU || offset == 0xbffcU) {
        return static_cast<uint32_t>(mtime_ >> 32);
    }
    return 0;
}

auto AclintMtimer::read64(Address offset) -> uint64_t {
    if (offset >= 0x4000U && offset <= 0x4000U + ((kMaxHarts - 1U) * 8U)) {
        const size_t hart_idx = (offset - 0x4000U) / 8U;
        return mtimecmp_[hart_idx].load(std::memory_order_relaxed);
    }
    if (offset == 0x7ff8U || offset == 0xbff8U) {
        return mtime_;
    }
    const uint64_t lo = read32(offset);
    const uint64_t hi = read32(offset + 4);
    return lo | (hi << 32);
}

void AclintMtimer::write32(Address offset, uint32_t val) {
    if (offset >= 0x4000U && offset < 0x4000U + (kMaxHarts * 8U)) {
        const size_t hart_idx = (offset - 0x4000U) / 8U;
        uint64_t cur = mtimecmp_[hart_idx].load(std::memory_order_relaxed);
        if (((offset - 0x4000U) % 8U) >= 4U) {
            cur = (cur & 0xffffffffULL) | (static_cast<uint64_t>(val) << 32);
        } else {
            cur = (cur & 0xffffffff00000000ULL) | static_cast<uint64_t>(val);
        }
        mtimecmp_[hart_idx].store(cur, std::memory_order_relaxed);
        tick();
    } else if (offset == 0x7ff8U || offset == 0xbff8U) {
        mtime_ = (mtime_ & 0xffffffff00000000ULL) | static_cast<uint64_t>(val);
        tick();
    } else if (offset == 0x7ffcU || offset == 0xbffcU) {
        mtime_ = (mtime_ & 0xffffffffULL) | (static_cast<uint64_t>(val) << 32);
        tick();
    }
}

void AclintMtimer::write64(Address offset, uint64_t val) {
    if (offset >= 0x4000U && offset <= 0x4000U + ((kMaxHarts - 1U) * 8U)) {
        const size_t hart_idx = (offset - 0x4000U) / 8U;
        mtimecmp_[hart_idx].store(val, std::memory_order_relaxed);
        tick();
    } else if (offset == 0x7ff8U || offset == 0xbff8U) {
        mtime_ = val;
        tick();
    } else {
        write32(offset, static_cast<uint32_t>(val & 0xffffffffU));
        write32(offset + 4, static_cast<uint32_t>(val >> 32));
    }
}

void AclintMtimer::tick() {
    if (machine_ == nullptr) {
        return;
    }
    const size_t total_harts = machine_->num_harts();
    for (size_t i = 0; i < total_harts && i < kMaxHarts; ++i) {
        auto& hart = machine_->hart(HartId{static_cast<uint32_t>(i)});
        const uint64_t cmp = mtimecmp_[i].load(std::memory_order_relaxed);
        if (mtime_ >= cmp) {
            hart.state().mip |= enum_mask(core::MipBit::Mtip);
        } else {
            hart.state().mip &= ~enum_mask(core::MipBit::Mtip);
        }
    }
}

// =========================================================================
// AclintMswi Implementation
// =========================================================================

AclintMswi::AclintMswi(simrv::core::Machine* machine) : memory::MmioDevice(machine) { reset(); }

void AclintMswi::reset() {
    for (auto& s : msip_) {
        s.store(0, std::memory_order_relaxed);
    }
}

auto AclintMswi::read32(Address offset) -> uint32_t {
    if (offset < (kMaxHarts * 4U)) {
        const size_t hart_idx = offset / 4U;
        return msip_[hart_idx].load(std::memory_order_relaxed);
    }
    return 0;
}

void AclintMswi::write32(Address offset, uint32_t val) {
    if (offset < (kMaxHarts * 4U)) {
        const size_t hart_idx = offset / 4U;
        const uint32_t bit = val & 1U;
        msip_[hart_idx].store(bit, std::memory_order_relaxed);
        if (machine_ != nullptr && hart_idx < machine_->num_harts()) {
            auto& hart = machine_->hart(HartId{static_cast<uint32_t>(hart_idx)});
            if (bit != 0) {
                hart.state().mip |= enum_mask(core::MipBit::Msip);
            } else {
                hart.state().mip &= ~enum_mask(core::MipBit::Msip);
            }
        }
    }
}

}  // namespace simrv::device
