/**
 * @file VirtioPciDevice.cpp
 * @brief Implementation of modern VirtIO 1.2 PCIe transport endpoint base class.
 */
#include "simrv/device/pci/VirtioPciDevice.hpp"

#include <cstring>

#include "simrv/core/Machine.hpp"
#include "simrv/device/pci/PcieRootComplex.hpp"

namespace simrv::device {

VirtioPciDevice::VirtioPciDevice(uint16_t subsystem_device_id, uint32_t class_code,
                                 size_t max_queues)
    : PciDevice(kVirtioPciVendorId,
                static_cast<uint16_t>(kVirtioPciModernDeviceBase + subsystem_device_id), 0x01,
                class_code),
      queues_(max_queues) {
    config_space_[0x2C] = static_cast<uint8_t>(kVirtioPciVendorId & 0xff);
    config_space_[0x2D] = static_cast<uint8_t>((kVirtioPciVendorId >> 8) & 0xff);
    config_space_[0x2E] = static_cast<uint8_t>(subsystem_device_id & 0xff);
    config_space_[0x2F] = static_cast<uint8_t>((subsystem_device_id >> 8) & 0xff);

    init_bar(0, 0x1000, false, false);
    config_space_[0x06] = 0x10;  // Capabilities List present

    add_pci_cap(1, 0, 0x000, 0x100);     // Common cfg
    add_pci_cap(2, 0, 0x100, 0x100, 4);  // Notify cfg
    add_pci_cap(3, 0, 0x200, 0x100);     // ISR cfg
    add_pci_cap(4, 0, 0x300, 0x100);     // Device cfg
}

void VirtioPciDevice::reset() {
    device_status_ = 0;
    isr_status_ = 0;
    driver_features_ = 0;
    driver_features_sel_ = 0;
    device_features_sel_ = 0;
    for (auto& q : queues_) {
        q.ready = 0;
        q.desc_addr = 0;
        q.driver_addr = 0;
        q.device_addr = 0;
        q.last_avail_idx = 0;
    }
}

void VirtioPciDevice::add_pci_cap(uint8_t cap_type, uint8_t bar, uint32_t offset, uint32_t length,
                                  uint32_t notify_mult) {
    uint8_t cap_offset = next_cap_offset_;
    uint8_t cap_len = (cap_type == 2) ? 20 : 16;
    if (cap_offset + cap_len > 0x100) return;

    if (config_space_[0x34] == 0) {
        config_space_[0x34] = cap_offset;
    } else {
        uint8_t prev = config_space_[0x34];
        while (config_space_[prev + 1] != 0) {
            prev = config_space_[prev + 1];
        }
        config_space_[prev + 1] = cap_offset;
    }

    config_space_[cap_offset + 0] = 0x09;  // PCI_CAP_ID_VNDR
    config_space_[cap_offset + 1] = 0x00;  // Next cap
    config_space_[cap_offset + 2] = cap_len;
    config_space_[cap_offset + 3] = cap_type;
    config_space_[cap_offset + 4] = bar;
    std::memcpy(&config_space_[cap_offset + 8], &offset, 4);
    std::memcpy(&config_space_[cap_offset + 12], &length, 4);
    if (cap_type == 2) {
        std::memcpy(&config_space_[cap_offset + 16], &notify_mult, 4);
    }
    next_cap_offset_ += cap_len;
}

auto VirtioPciDevice::dma_read(Address paddr, void* dst, size_t len) -> bool {
    if (!root_complex_ || !root_complex_->machine() || !root_complex_->machine()->mmem)
        return false;
    auto* mem = root_complex_->machine()->mmem;
    if (paddr < 0x80000000ULL) return false;
    Address offset = paddr - 0x80000000ULL;
    std::memcpy(dst, mem + offset, len);
    return true;
}

auto VirtioPciDevice::dma_write(Address paddr, const void* src, size_t len) -> bool {
    if (!root_complex_ || !root_complex_->machine() || !root_complex_->machine()->mmem)
        return false;
    auto* mem = root_complex_->machine()->mmem;
    if (paddr < 0x80000000ULL) return false;
    Address offset = paddr - 0x80000000ULL;
    std::memcpy(mem + offset, src, len);
    return true;
}

auto VirtioPciDevice::bar_read(int bar_idx, Address offset, uint8_t size) -> uint32_t {
    if (bar_idx != 0) return 0;

    // Common Config (0x000 - 0x0FF)
    if (offset < 0x100) {
        switch (offset) {
            case 0x00:
                return device_features_sel_;
            case 0x04:
                return get_device_features(device_features_sel_);
            case 0x08:
                return driver_features_sel_;
            case 0x0C:
                return driver_features_;
            case 0x10:
                return 0xFFFF;  // config_msix_vector (VIRTIO_MSI_NO_VECTOR)
            case 0x12:
                return static_cast<uint16_t>(queues_.size());  // num_queues
            case 0x14:
                return device_status_;  // device_status
            case 0x15:
                return 0;  // config_generation
            case 0x16:
                return queue_select_;  // queue_select
            case 0x18: {               // queue_size
                if (queue_select_ < queues_.size()) return queues_[queue_select_].num_max;
                return 0;
            }
            case 0x1A:
                return 0xFFFF;  // queue_msix_vector (VIRTIO_MSI_NO_VECTOR)
            case 0x1C: {        // queue_enable
                if (queue_select_ < queues_.size()) return queues_[queue_select_].ready;
                return 0;
            }
            case 0x1E:
                return queue_select_;  // queue_notify_off
            case 0x20: {               // queue_desc low
                if (queue_select_ < queues_.size())
                    return static_cast<uint32_t>(queues_[queue_select_].desc_addr & 0xFFFFFFFFULL);
                return 0;
            }
            case 0x24: {  // queue_desc high
                if (queue_select_ < queues_.size())
                    return static_cast<uint32_t>(queues_[queue_select_].desc_addr >> 32);
                return 0;
            }
            case 0x28: {  // queue_driver low
                if (queue_select_ < queues_.size())
                    return static_cast<uint32_t>(queues_[queue_select_].driver_addr &
                                                 0xFFFFFFFFULL);
                return 0;
            }
            case 0x2C: {  // queue_driver high
                if (queue_select_ < queues_.size())
                    return static_cast<uint32_t>(queues_[queue_select_].driver_addr >> 32);
                return 0;
            }
            case 0x30: {  // queue_device low
                if (queue_select_ < queues_.size())
                    return static_cast<uint32_t>(queues_[queue_select_].device_addr &
                                                 0xFFFFFFFFULL);
                return 0;
            }
            case 0x34: {  // queue_device high
                if (queue_select_ < queues_.size())
                    return static_cast<uint32_t>(queues_[queue_select_].device_addr >> 32);
                return 0;
            }
            default:
                return 0;
        }
    }

    // Notify Config (0x100 - 0x1FF)
    if (offset >= 0x100 && offset < 0x200) {
        return 0;
    }

    // ISR Config (0x200 - 0x2FF)
    if (offset >= 0x200 && offset < 0x300) {
        uint32_t isr = isr_status_;
        isr_status_ = 0;  // Clear on read
        return isr;
    }

    // Device Specific Config (0x300 - 0x3FF)
    if (offset >= 0x300 && offset < 0x400) {
        return read_device_config(offset - 0x300, size);
    }

    return 0;
}

void VirtioPciDevice::bar_write(int bar_idx, Address offset, uint32_t val, uint8_t size) {
    if (bar_idx != 0) return;

    // Common Config
    if (offset < 0x100) {
        switch (offset) {
            case 0x00:
                device_features_sel_ = val;
                break;
            case 0x08:
                driver_features_sel_ = val;
                break;
            case 0x0C:
                driver_features_ = val;
                on_driver_features(driver_features_sel_, val);
                break;
            case 0x14:
                device_status_ = static_cast<uint8_t>(val);
                if (device_status_ == 0) reset();
                break;
            case 0x16:
                queue_select_ = static_cast<uint16_t>(val);
                break;
            case 0x18:
                if (queue_select_ < queues_.size()) {
                    queues_[queue_select_].num = static_cast<uint16_t>(val);
                }
                break;
            case 0x1C:
                if (queue_select_ < queues_.size()) {
                    queues_[queue_select_].ready = static_cast<uint16_t>(val);
                }
                break;
            case 0x20:
                if (queue_select_ < queues_.size()) {
                    queues_[queue_select_].desc_addr =
                        (queues_[queue_select_].desc_addr & 0xFFFFFFFF00000000ULL) |
                        (val & 0xFFFFFFFFULL);
                }
                break;
            case 0x24:
                if (queue_select_ < queues_.size()) {
                    queues_[queue_select_].desc_addr =
                        (queues_[queue_select_].desc_addr & 0x00000000FFFFFFFFULL) |
                        (static_cast<uint64_t>(val) << 32);
                }
                break;
            case 0x28:
                if (queue_select_ < queues_.size()) {
                    queues_[queue_select_].driver_addr =
                        (queues_[queue_select_].driver_addr & 0xFFFFFFFF00000000ULL) |
                        (val & 0xFFFFFFFFULL);
                }
                break;
            case 0x2C:
                if (queue_select_ < queues_.size()) {
                    queues_[queue_select_].driver_addr =
                        (queues_[queue_select_].driver_addr & 0x00000000FFFFFFFFULL) |
                        (static_cast<uint64_t>(val) << 32);
                }
                break;
            case 0x30:
                if (queue_select_ < queues_.size()) {
                    queues_[queue_select_].device_addr =
                        (queues_[queue_select_].device_addr & 0xFFFFFFFF00000000ULL) |
                        (val & 0xFFFFFFFFULL);
                }
                break;
            case 0x34:
                if (queue_select_ < queues_.size()) {
                    queues_[queue_select_].device_addr =
                        (queues_[queue_select_].device_addr & 0x00000000FFFFFFFFULL) |
                        (static_cast<uint64_t>(val) << 32);
                }
                break;
            default:
                break;
        }
        return;
    }

    // Notify Config
    if (offset >= 0x100 && offset < 0x200) {
        uint16_t queue_idx = static_cast<uint16_t>(val);
        on_queue_notify(queue_idx);
        return;
    }

    // Device Specific Config
    if (offset >= 0x300 && offset < 0x400) {
        write_device_config(offset - 0x300, val, size);
    }
}

}  // namespace simrv::device
