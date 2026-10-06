/**
 * @file DmaController.cpp
 * @brief Implementation of programmable interrupt-based MMIO DMA Controller.
 */
#include "simrv/device/DmaController.hpp"

#include <cstdint>
#include <vector>

#include "simrv/core/Machine.hpp"

namespace simrv::device {

DmaController::DmaController(core::Machine* machine, Address base_address, Address size,
                             uint32_t irq_num)
    : memory::MmioDevice(machine), base_address_(base_address), size_(size), irq_num_(irq_num) {}

auto DmaController::read32(Address offset) -> uint32_t {
    switch (offset) {
        case kRegControl:
            return (control_ & kControlIntEnable) | (busy_ ? kControlBusy : 0U);
        case kRegStatus:
            return status_;
        case kRegSrcAddrLo:
            return src_addr_lo_;
        case kRegSrcAddrHi:
            return src_addr_hi_;
        case kRegDstAddrLo:
            return dst_addr_lo_;
        case kRegDstAddrHi:
            return dst_addr_hi_;
        case kRegByteCount:
            return byte_count_;
        case kRegInterruptAck:
            return 0U;
        default:
            return 0U;
    }
}

void DmaController::write32(Address offset, uint32_t val) {
    switch (offset) {
        case kRegControl: {
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
        case kRegStatus:
            status_ &= ~val;
            break;
        case kRegSrcAddrLo:
            src_addr_lo_ = val;
            break;
        case kRegSrcAddrHi:
            src_addr_hi_ = val;
            break;
        case kRegDstAddrLo:
            dst_addr_lo_ = val;
            break;
        case kRegDstAddrHi:
            dst_addr_hi_ = val;
            break;
        case kRegByteCount:
            byte_count_ = val;
            break;
        case kRegInterruptAck:
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

    const uint64_t src = src_address();
    const uint64_t dst = dst_address();
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

void DmaController::execute_transfer(uint64_t src, uint64_t dst, uint32_t count) {
    busy_ = false;
    if (machine_ == nullptr || count == 0) {
        status_ |= kStatusError;
        if ((control_ & kControlIntEnable) != 0 && machine_ != nullptr) {
            machine_->set_platform_irq(irq_num_, true);
        }
        return;
    }

    std::vector<uint8_t> buffer(count);
    const bool read_ok = dma_read(static_cast<Address>(src), buffer);
    const bool write_ok = read_ok && dma_write(static_cast<Address>(dst), buffer);

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
