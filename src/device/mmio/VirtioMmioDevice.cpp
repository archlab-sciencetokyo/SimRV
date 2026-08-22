/**
 * @file VirtioMmioDevice.cpp
 * @brief Implementation of modern VirtIO-MMIO v2 transport base class.
 */
#include "simrv/device/mmio/VirtioMmioDevice.hpp"

#include <cstring>
#include <span>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"

namespace simrv::device {

VirtioMmioDevice::VirtioMmioDevice(const char* name, Address base_address, Address size,
                                   uint32_t device_id, uint32_t irq_num, core::Machine* machine)
    : memory::MmioDevice(machine),
      name_(name),
      base_address_(base_address),
      size_(size),
      device_id_(device_id),
      irq_num_(irq_num) {}

void VirtioMmioDevice::add_queue(uint16_t max_size) {
    virtio::QueueState q;
    q.num_max = max_size;
    q.num = max_size;
    queues_.push_back(q);
}

void VirtioMmioDevice::trigger_irq() {
    interrupt_status_ |= 0x1;
    if (machine_ != nullptr && irq_num_ != 0) {
        machine_->cpu.plic_set_irq(static_cast<int>(irq_num_), 1);
    }
}

auto VirtioMmioDevice::dma_read_bytes(uint64_t phys_addr, std::byte* dst, std::size_t len) -> bool {
    return dma_read(phys_addr, std::span<uint8_t>(reinterpret_cast<uint8_t*>(dst), len));
}

auto VirtioMmioDevice::dma_write_bytes(uint64_t phys_addr, const std::byte* src, std::size_t len)
    -> bool {
    return dma_write(phys_addr,
                     std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(src), len));
}

auto VirtioMmioDevice::read8(Address offset) -> uint8_t {
    return static_cast<uint8_t>(read32(offset & ~3ULL) >> ((offset & 3ULL) * 8));
}

auto VirtioMmioDevice::read16(Address offset) -> uint16_t {
    return static_cast<uint16_t>(read32(offset & ~3ULL) >> ((offset & 2ULL) * 8));
}

auto VirtioMmioDevice::read64(Address offset) -> uint64_t {
    const uint64_t lo = read32(offset);
    const uint64_t hi = read32(offset + 4);
    return lo | (hi << 32);
}

void VirtioMmioDevice::write8(Address offset, uint8_t val) {
    write32(offset & ~3ULL, static_cast<uint32_t>(val) << ((offset & 3ULL) * 8));
}

void VirtioMmioDevice::write16(Address offset, uint16_t val) {
    write32(offset & ~3ULL, static_cast<uint32_t>(val) << ((offset & 2ULL) * 8));
}

void VirtioMmioDevice::write64(Address offset, uint64_t val) {
    write32(offset, static_cast<uint32_t>(val & 0xFFFFFFFFULL));
    write32(offset + 4, static_cast<uint32_t>(val >> 32));
}

auto VirtioMmioDevice::read32(Address offset) -> uint32_t {
    if (offset >= 0x100) {
        return static_cast<uint32_t>(read_device_config(offset - 0x100, 4));
    }

    switch (offset) {
        case 0x00:
            return 0x74726976;  // Magic "virt"
        case 0x04:
            return 2;  // Version 2 (Modern)
        case 0x08:
            return device_id_;
        case 0x0C:
            return 0x554D4551;  // Vendor "QEMU"
        case 0x10: {            // DeviceFeatures
            if (device_features_sel_ == 0) {
                if (device_id_ == virtio::kDevIdBlock) return (1U << 1);  // SIZE_MAX
                if (device_id_ == virtio::kDevIdGpu) return (1U << 1);    // VIRGL
                return 0;
            }
            if (device_features_sel_ == 1) {
                return (1U << 0);  // VIRTIO_F_VERSION_1 (bit 32)
            }
            return 0;
        }
        case 0x34: {  // QueueNumMax
            if (queue_sel_ < queues_.size()) return queues_[queue_sel_].num_max;
            return 0;
        }
        case 0x44: {  // QueueReady
            if (queue_sel_ < queues_.size()) return queues_[queue_sel_].ready;
            return 0;
        }
        case 0x60:
            return interrupt_status_;
        case 0x70:
            return status_;
        case 0xFC:
            return 0;  // ConfigGeneration
        default:
            return 0;
    }
}

void VirtioMmioDevice::write32(Address offset, uint32_t value) {
    if (offset >= 0x100) {
        write_device_config(offset - 0x100, value, 4);
        return;
    }

    switch (offset) {
        case 0x14:
            device_features_sel_ = value;
            break;
        case 0x20:
            driver_features_ = value;
            break;
        case 0x24:
            driver_features_sel_ = value;
            break;
        case 0x30:
            queue_sel_ = value;
            break;
        case 0x38:
            if (queue_sel_ < queues_.size()) {
                queues_[queue_sel_].num = static_cast<uint16_t>(value);
            }
            break;
        case 0x44:
            if (queue_sel_ < queues_.size()) {
                queues_[queue_sel_].ready = static_cast<uint16_t>(value);
            }
            break;
        case 0x50:  // QueueNotify
            if (value < queues_.size()) {
                on_queue_notify(value);
            }
            break;
        case 0x64:  // InterruptACK
            interrupt_status_ &= ~value;
            if (interrupt_status_ == 0 && machine_ != nullptr && irq_num_ != 0) {
                machine_->cpu.plic_set_irq(static_cast<int>(irq_num_), 0);
            }
            break;
        case 0x70:  // Status
            status_ = value;
            if (status_ == 0) {
                for (auto& q : queues_) {
                    q.ready = 0;
                    q.desc_addr = 0;
                    q.driver_addr = 0;
                    q.device_addr = 0;
                    q.last_avail_idx = 0;
                }
            }
            break;
        case 0x80:  // QueueDescLow
            if (queue_sel_ < queues_.size()) {
                queues_[queue_sel_].desc_addr =
                    (queues_[queue_sel_].desc_addr & 0xFFFFFFFF00000000ULL) |
                    (value & 0xFFFFFFFFULL);
            }
            break;
        case 0x84:  // QueueDescHigh
            if (queue_sel_ < queues_.size()) {
                queues_[queue_sel_].desc_addr =
                    (queues_[queue_sel_].desc_addr & 0x00000000FFFFFFFFULL) |
                    (static_cast<uint64_t>(value) << 32);
            }
            break;
        case 0x90:  // QueueDriverLow
            if (queue_sel_ < queues_.size()) {
                queues_[queue_sel_].driver_addr =
                    (queues_[queue_sel_].driver_addr & 0xFFFFFFFF00000000ULL) |
                    (value & 0xFFFFFFFFULL);
            }
            break;
        case 0x94:  // QueueDriverHigh
            if (queue_sel_ < queues_.size()) {
                queues_[queue_sel_].driver_addr =
                    (queues_[queue_sel_].driver_addr & 0x00000000FFFFFFFFULL) |
                    (static_cast<uint64_t>(value) << 32);
            }
            break;
        case 0xA0:  // QueueDeviceLow
            if (queue_sel_ < queues_.size()) {
                queues_[queue_sel_].device_addr =
                    (queues_[queue_sel_].device_addr & 0xFFFFFFFF00000000ULL) |
                    (value & 0xFFFFFFFFULL);
            }
            break;
        case 0xA4:  // QueueDeviceHigh
            if (queue_sel_ < queues_.size()) {
                queues_[queue_sel_].device_addr =
                    (queues_[queue_sel_].device_addr & 0x00000000FFFFFFFFULL) |
                    (static_cast<uint64_t>(value) << 32);
            }
            break;
        default:
            break;
    }
}

}  // namespace simrv::device
