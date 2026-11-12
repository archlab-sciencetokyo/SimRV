/**
 * @file DmaController.cpp
 * @brief Implementation of programmable interrupt-based MMIO DMA Controller.
 */
#include "simrv/device/DmaController.hpp"

#include <cstdint>
#include <vector>

#include "simrv/core/Machine.hpp"
#include "simrv/memory/MemorySubsystem.hpp"

namespace simrv::device {

DmaController::DmaController(core::Machine* machine, simrv::xlen::PhysAddr base_address,
                             simrv::xlen::Address size, uint32_t irq_num)
    : memory::MmioDevice(machine), base_address_(base_address), size_(size), irq_num_(irq_num) {}

auto DmaController::read32(simrv::memory::MmioOffset offset) -> uint32_t {
    switch (offset.raw()) {
        case kRegControl.raw():
            return (control_ & kControlIntEnable) | (busy_ ? kControlBusy : 0U);
        case kRegStatus.raw():
            return status_;
        case kRegSrcAddrLo.raw():
            return src_addr_lo_;
        case kRegSrcAddrHi.raw():
            return src_addr_hi_;
        case kRegDstAddrLo.raw():
            return dst_addr_lo_;
        case kRegDstAddrHi.raw():
            return dst_addr_hi_;
        case kRegByteCount.raw():
            return byte_count_;
        default:
            // Interrupt acknowledgement and unmapped registers read as zero.
            return 0U;
    }
}

void DmaController::write32(simrv::memory::MmioOffset offset, uint32_t val) {
    switch (offset.raw()) {
        case kRegControl.raw(): {
            const bool was_int_enabled = (control_ & kControlIntEnable) != 0;
            control_ = val & kControlIntEnable;
            if (was_int_enabled && (control_ & kControlIntEnable) == 0 && machine_ != nullptr) {
                machine_->set_platform_irq(irq_num_, false);
            }
            if ((val & kControlStart) != 0) {
                start_transfer();
            }
            break;
        }
        case kRegStatus.raw():
            status_ &= ~val;
            break;
        case kRegSrcAddrLo.raw():
            src_addr_lo_ = val;
            break;
        case kRegSrcAddrHi.raw():
            src_addr_hi_ = val;
            break;
        case kRegDstAddrLo.raw():
            dst_addr_lo_ = val;
            break;
        case kRegDstAddrHi.raw():
            dst_addr_hi_ = val;
            break;
        case kRegByteCount.raw():
            byte_count_ = val;
            break;
        case kRegInterruptAck.raw():
            status_ &= ~(val != 0 ? val : (kStatusDone | kStatusError));
            if (machine_ != nullptr) {
                machine_->set_platform_irq(irq_num_, false);
            }
            break;
        default:
            break;
    }
}

void DmaController::reset() noexcept {
    control_ = 0;
    status_ = 0;
    src_addr_lo_ = 0;
    src_addr_hi_ = 0;
    dst_addr_lo_ = 0;
    dst_addr_hi_ = 0;
    byte_count_ = 0;
    busy_ = false;
    if (machine_ != nullptr) {
        machine_->set_platform_irq(irq_num_, false);
    }
}

void DmaController::start_transfer() {
    if (busy_) return;

    const simrv::xlen::PhysAddr src = src_address();
    const simrv::xlen::PhysAddr dst = dst_address();
    const uint32_t count = byte_count_;
    status_ = 0;

    if (count == 0) {
        status_ |= kStatusError;
        if ((control_ & kControlIntEnable) != 0 && machine_ != nullptr) {
            machine_->set_platform_irq(irq_num_, true);
        }
        return;
    }

    const bool is_ca = (machine_ != nullptr) && !machine_->runtime_profile.is_instruction_mode() &&
                       machine_->dma_engine().is_enabled();
    if (is_ca) {
        busy_ = true;
        const auto current_cycle = machine_->memory().system_bus().cycle();
        machine_->dma_engine().schedule_transfer(
            count, current_cycle, [this, src, dst, count]() { execute_transfer(src, dst, count); },
            "dma-controller");
    } else {
        execute_transfer(src, dst, count);
    }
}

void DmaController::execute_transfer(simrv::xlen::PhysAddr src, simrv::xlen::PhysAddr dst,
                                     uint32_t count) {
    busy_ = false;
    if (machine_ == nullptr || count == 0) {
        status_ |= kStatusError;
        if ((control_ & kControlIntEnable) != 0 && machine_ != nullptr) {
            machine_->set_platform_irq(irq_num_, true);
        }
        return;
    }

    std::vector<uint8_t> buffer(count);
    const bool read_ok = dma_read(src, buffer);
    const bool write_ok = read_ok && dma_write(dst, buffer);

    if (write_ok) {
        status_ |= kStatusDone;
    } else {
        status_ |= kStatusError;
    }

    if ((control_ & kControlIntEnable) != 0 && machine_ != nullptr) {
        machine_->set_platform_irq(irq_num_, true);
    }
}

}  // namespace simrv::device
