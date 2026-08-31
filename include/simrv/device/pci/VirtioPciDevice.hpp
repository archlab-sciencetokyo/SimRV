/**
 * @file VirtioPciDevice.hpp
 * @brief Base class for modern OASIS VirtIO 1.2 PCIe transport endpoints.
 */
#pragma once

#include <cstdint>
#include <vector>

#include "simrv/device/pci/PciDevice.hpp"
#include "simrv/device/virtio/VirtioCore.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::device {

/**
 * @class VirtioPciDevice
 * @brief Modern VirtIO 1.2 PCIe transport endpoint base class.
 */
class VirtioPciDevice : public PciDevice {
   public:
    static constexpr PciVendorId kVirtioPciVendorId = 0x1AF4;
    static constexpr PciDeviceId kVirtioPciModernDeviceBase = 0x1040;

    VirtioPciDevice(uint16_t subsystem_device_id, PciClassCode class_code, size_t max_queues = 2);
    ~VirtioPciDevice() override = default;

    void reset() override;

    [[nodiscard]] auto bar_read(BarIndex bar_idx, Address offset, uint8_t size)
        -> uint32_t override;
    void bar_write(BarIndex bar_idx, Address offset, uint32_t val, uint8_t size) override;

    [[nodiscard]] auto device_status() const -> uint8_t { return device_status_; }
    [[nodiscard]] auto isr_status() const -> uint8_t { return isr_status_; }
    [[nodiscard]] auto num_queues() const -> size_t { return queues_.size(); }

    [[nodiscard]] auto get_queue_state(size_t idx) const -> const virtio::QueueState* {
        if (idx < queues_.size()) {
            return &queues_[idx];
        }
        return nullptr;
    }

   protected:
    virtual auto get_device_features(uint32_t select) -> uint32_t = 0;
    virtual void on_driver_features(uint32_t select, uint32_t val) {
        (void)select;
        (void)val;
    }
    virtual void on_queue_notify(virtio::VirtioQueueIndex queue_index) = 0;
    virtual auto read_device_config(Address offset, uint8_t size) -> uint32_t {
        (void)offset;
        (void)size;
        return 0;
    }
    virtual void write_device_config(Address offset, uint32_t val, uint8_t size) {
        (void)offset;
        (void)val;
        (void)size;
    }

    auto dma_read(Address paddr, void* dst, size_t len) -> bool;
    auto dma_write(Address paddr, const void* src, size_t len) -> bool;

    void add_pci_cap(uint8_t cap_type, uint8_t bar, uint32_t offset, uint32_t length,
                     uint32_t notify_mult = 0);

    std::vector<virtio::QueueState> queues_;
    uint16_t queue_select_{0};
    uint8_t device_status_{0};
    uint8_t isr_status_{0};
    uint32_t driver_features_sel_{0};
    uint32_t device_features_sel_{0};
    uint32_t driver_features_{0};
    uint8_t next_cap_offset_{0x40};
};

}  // namespace simrv::device
