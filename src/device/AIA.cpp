/**
 * @file AIA.cpp
 * @brief Implementation of RISC-V AIA (APLIC and IMSIC) interrupt controllers.
 */
#include "simrv/device/AIA.hpp"

#include <bit>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/CsrTypes.hpp"
#include "simrv/core/Machine.hpp"

namespace simrv::device {

// =========================================================================
// Imsic Implementation
// =========================================================================

Imsic::Imsic(simrv::core::Machine* machine, Privilege priv, Address base_addr, Address size)
    : memory::MmioDevice(machine), priv_(priv), base_addr_(base_addr), size_(size) {
    reset();
}

void Imsic::reset() {
    for (auto& file : files_) {
        file.eidelivery = 0;
        file.eithreshold = 0;
        file.eip = 0;
        file.eie = 0;
    }
}

auto Imsic::read32(Address /*offset*/) -> uint32_t { return 0; }

void Imsic::write32(Address offset, uint32_t val) {
    const size_t hart_idx = static_cast<size_t>(offset / 0x4000U);
    if (hart_idx >= kMaxHarts) {
        return;
    }
    const Address reg_offset = offset % 0x4000U;
    if (reg_offset == 0x000U) {  // seteipnum_le
        const uint32_t id = val & 0x7ffU;
        if (id > 0 && id < kNumInterrupts) {
            files_[hart_idx].eip |= (1ULL << id);
            update_hart(hart_idx);
        }
    }
}

auto Imsic::csr_read(size_t hart_idx, uint32_t reg_idx) -> Word {
    if (hart_idx >= kMaxHarts) {
        return 0;
    }
    const auto& file = files_[hart_idx];
    switch (reg_idx) {
        case 0x70:  // eidelivery
            return file.eidelivery;
        case 0x72:  // eithreshold
            return file.eithreshold;
        case 0x80:  // eip0
            return static_cast<Word>(file.eip & 0xffffffffULL);
        case 0x81:  // eip1
            return static_cast<Word>(file.eip >> 32);
        case 0xC0:  // eie0
            return static_cast<Word>(file.eie & 0xffffffffULL);
        case 0xC1:  // eie1
            return static_cast<Word>(file.eie >> 32);
        case 0x44: {  // topei
            if (file.eidelivery == 0) {
                return 0;
            }
            const uint64_t active = file.eip & file.eie;
            if (active == 0) {
                return 0;
            }
            const unsigned id = static_cast<unsigned>(std::countr_zero(active));
            if (id > 0 && id < kNumInterrupts) {
                if (file.eithreshold == 0 || id < file.eithreshold) {
                    return (static_cast<Word>(id) << 16) | static_cast<Word>(id);
                }
            }
            return 0;
        }
        default:
            return 0;
    }
}

void Imsic::csr_write(size_t hart_idx, uint32_t reg_idx, Word val) {
    if (hart_idx >= kMaxHarts) {
        return;
    }
    auto& file = files_[hart_idx];
    switch (reg_idx) {
        case 0x70:  // eidelivery
            file.eidelivery = static_cast<uint32_t>(val & 1U);
            update_hart(hart_idx);
            break;
        case 0x72:  // eithreshold
            file.eithreshold = static_cast<uint32_t>(val & 0xffU);
            update_hart(hart_idx);
            break;
        case 0x80:  // eip0
            file.eip =
                (file.eip & 0xffffffff00000000ULL) | (static_cast<uint64_t>(val) & 0xffffffffULL);
            update_hart(hart_idx);
            break;
        case 0x81:  // eip1
            file.eip = (file.eip & 0xffffffffULL) | (static_cast<uint64_t>(val) << 32);
            update_hart(hart_idx);
            break;
        case 0xC0:  // eie0
            file.eie =
                (file.eie & 0xffffffff00000000ULL) | (static_cast<uint64_t>(val) & 0xffffffffULL);
            update_hart(hart_idx);
            break;
        case 0xC1:  // eie1
            file.eie = (file.eie & 0xffffffffULL) | (static_cast<uint64_t>(val) << 32);
            update_hart(hart_idx);
            break;
        default:
            break;
    }
}

void Imsic::trigger_msi(size_t hart_idx, uint32_t interrupt_id) {
    if (hart_idx < kMaxHarts && interrupt_id > 0 && interrupt_id < kNumInterrupts) {
        files_[hart_idx].eip |= (1ULL << interrupt_id);
        update_hart(hart_idx);
    }
}

void Imsic::update_hart(size_t hart_idx) {
    if (machine_ == nullptr || hart_idx >= machine_->num_harts() || hart_idx >= kMaxHarts) {
        return;
    }
    auto& hart = machine_->hart(hart_idx);
    const auto& file = files_[hart_idx];
    const bool delivery = (file.eidelivery != 0);
    const uint64_t active = file.eip & file.eie;
    bool has_pending = false;

    if (delivery && active != 0) {
        const unsigned id = static_cast<unsigned>(std::countr_zero(active));
        if (id > 0 && id < kNumInterrupts && (file.eithreshold == 0 || id < file.eithreshold)) {
            has_pending = true;
        }
    }

    const CSRValue irq_bit = (priv_ == Privilege::Machine) ? enum_mask(core::MipBit::Meip)
                                                           : enum_mask(core::MipBit::Seip);
    if (has_pending) {
        hart.state().mip |= irq_bit;
    } else {
        hart.state().mip &= ~irq_bit;
    }
}

// =========================================================================
// Aplic Implementation
// =========================================================================

Aplic::Aplic(simrv::core::Machine* machine, Privilege priv, Address base_addr, Address size,
             Imsic* imsic)
    : memory::MmioDevice(machine), priv_(priv), base_addr_(base_addr), size_(size), imsic_(imsic) {
    reset();
}

void Aplic::reset() {
    domaincfg_ = 0x100U;  // Default IE enabled
    mmsiaddrcfg_ = 0;
    smsiaddrcfg_ = 0;
    sourcecfg_.fill(0);
    target_.fill(0);
    input_wires_.fill(false);
    pending_ = 0;
    enabled_ = 0;
}

auto Aplic::read32(Address offset) -> uint32_t {
    if (offset == 0x0000U) {
        return domaincfg_;
    }
    if (offset >= 0x0004U && offset < 0x0004U + (kMaxSources * 4U)) {
        const size_t src = (offset - 0x0004U) / 4U + 1U;
        return sourcecfg_[src];
    }
    if (offset == 0x1bc0U) {
        return static_cast<uint32_t>(mmsiaddrcfg_ & 0xffffffffU);
    }
    if (offset == 0x1bc4U) {
        return static_cast<uint32_t>(mmsiaddrcfg_ >> 32);
    }
    if (offset == 0x1bc8U) {
        return static_cast<uint32_t>(smsiaddrcfg_ & 0xffffffffU);
    }
    if (offset == 0x1bccU) {
        return static_cast<uint32_t>(smsiaddrcfg_ >> 32);
    }
    if (offset == 0x1c00U) {
        return static_cast<uint32_t>(pending_ & 0xffffffffU);
    }
    if (offset == 0x1c04U) {
        return static_cast<uint32_t>(pending_ >> 32);
    }
    if (offset == 0x1d00U) {
        uint32_t wire_lo = 0;
        for (size_t i = 1; i < 32 && i < kMaxSources; ++i) {
            if (input_wires_[i]) wire_lo |= (1U << i);
        }
        return wire_lo;
    }
    if (offset == 0x1d04U) {
        uint32_t wire_hi = 0;
        for (size_t i = 32; i < 64 && i < kMaxSources; ++i) {
            if (input_wires_[i]) wire_hi |= (1U << (i - 32));
        }
        return wire_hi;
    }
    if (offset == 0x1e00U) {
        return static_cast<uint32_t>(enabled_ & 0xffffffffU);
    }
    if (offset == 0x1e04U) {
        return static_cast<uint32_t>(enabled_ >> 32);
    }
    if (offset >= 0x3004U && offset < 0x3004U + (kMaxSources * 4U)) {
        const size_t src = (offset - 0x3004U) / 4U + 1U;
        return target_[src];
    }
    return 0;
}

void Aplic::write32(Address offset, uint32_t val) {
    if (offset == 0x0000U) {
        domaincfg_ = val;
        update();
    } else if (offset >= 0x0004U && offset < 0x0004U + (kMaxSources * 4U)) {
        const size_t src = (offset - 0x0004U) / 4U + 1U;
        sourcecfg_[src] = val;
    } else if (offset == 0x1bc0U) {
        mmsiaddrcfg_ = (mmsiaddrcfg_ & 0xffffffff00000000ULL) | static_cast<uint64_t>(val);
    } else if (offset == 0x1bc4U) {
        mmsiaddrcfg_ = (mmsiaddrcfg_ & 0xffffffffULL) | (static_cast<uint64_t>(val) << 32);
    } else if (offset == 0x1bc8U) {
        smsiaddrcfg_ = (smsiaddrcfg_ & 0xffffffff00000000ULL) | static_cast<uint64_t>(val);
    } else if (offset == 0x1bccU) {
        smsiaddrcfg_ = (smsiaddrcfg_ & 0xffffffffULL) | (static_cast<uint64_t>(val) << 32);
    } else if (offset == 0x1c00U) {
        pending_ |= static_cast<uint64_t>(val);
        update();
    } else if (offset == 0x1c04U) {
        pending_ |= (static_cast<uint64_t>(val) << 32);
        update();
    } else if (offset == 0x1cdcU) {  // setipnum
        if (val > 0 && val < kMaxSources) {
            pending_ |= (1ULL << val);
            update();
        }
    } else if (offset == 0x1d00U) {  // in_clrip (clear bits)
        pending_ &= ~(static_cast<uint64_t>(val));
        update();
    } else if (offset == 0x1d04U) {
        pending_ &= ~(static_cast<uint64_t>(val) << 32);
        update();
    } else if (offset == 0x1ddcU) {  // clripnum
        if (val > 0 && val < kMaxSources) {
            pending_ &= ~(1ULL << val);
            update();
        }
    } else if (offset == 0x1e00U) {  // setie
        enabled_ |= static_cast<uint64_t>(val);
        update();
    } else if (offset == 0x1e04U) {
        enabled_ |= (static_cast<uint64_t>(val) << 32);
        update();
    } else if (offset == 0x1edcU) {  // setienum
        if (val > 0 && val < kMaxSources) {
            enabled_ |= (1ULL << val);
            update();
        }
    } else if (offset == 0x1f00U) {  // clr_ie
        enabled_ &= ~(static_cast<uint64_t>(val));
        update();
    } else if (offset == 0x1f04U) {
        enabled_ &= ~(static_cast<uint64_t>(val) << 32);
        update();
    } else if (offset == 0x1fdcU) {  // clrienum
        if (val > 0 && val < kMaxSources) {
            enabled_ &= ~(1ULL << val);
            update();
        }
    } else if (offset >= 0x3004U && offset < 0x3004U + (kMaxSources * 4U)) {
        const size_t src = (offset - 0x3004U) / 4U + 1U;
        target_[src] = val;
    }
}

void Aplic::set_irq(uint32_t irq_source, bool active) {
    if (irq_source > 0 && irq_source < kMaxSources) {
        input_wires_[irq_source] = active;
        if (active) {
            pending_ |= (1ULL << irq_source);
        }
        update();
    }
}

void Aplic::update() {
    if ((domaincfg_ & 0x100U) == 0) {  // IE disabled
        return;
    }
    const bool msi_mode = (domaincfg_ & 0x04U) != 0;

    for (size_t src = 1; src < kMaxSources; ++src) {
        if ((pending_ & (1ULL << src)) != 0 && (enabled_ & (1ULL << src)) != 0) {
            if (msi_mode && imsic_ != nullptr) {
                const uint32_t tgt = target_[src];
                const size_t hart_idx = (tgt >> 18) & 0x3fffU;
                const uint32_t eiid = tgt & 0x7ffU;
                imsic_->trigger_msi(hart_idx, eiid ? eiid : static_cast<uint32_t>(src));
                pending_ &= ~(1ULL << src);  // In MSI mode, message has been dispatched
            } else if (!msi_mode && machine_ != nullptr) {
                const uint32_t tgt = target_[src];
                const size_t hart_idx = (tgt >> 18) & 0x3fffU;
                if (hart_idx < machine_->num_harts()) {
                    auto& hart = machine_->hart(hart_idx);
                    if (priv_ == Privilege::Machine) {
                        hart.state().mip |= enum_mask(core::MipBit::Meip);
                    } else {
                        hart.state().mip |= enum_mask(core::MipBit::Seip);
                    }
                }
            }
        }
    }
}

}  // namespace simrv::device
