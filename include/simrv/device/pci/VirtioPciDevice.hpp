/**
 * @file VirtioPciDevice.hpp
 * @brief Base class for modern OASIS VirtIO 1.2 PCIe transport endpoints.
 */
#pragma once

#include <cstdint>
#include <vector>

#include "simrv/device/pci/PciDevice.hpp"
#include "simrv/device/virtio/VirtioCore.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::device {

/**
 * @class VirtioPciDevice
 * @brief Modern VirtIO 1.2 PCIe transport endpoint base class.
 */
class VirtioPciDevice : public PciDevice {
   public:
    static constexpr PciVendorId kVirtioPciVendorId = 0x1AF4;
    static constexpr PciDeviceId kVirtioPciModernDeviceBase = 0x1040;

    VirtioPciDevice(PciSubsystemId subsystem_device_id, PciClassCode class_code,
                    size_t max_queues = 2);
    ~VirtioPciDevice() override = default;

    void reset() override;

    [[nodiscard]] auto bar_read(PciBarIndex bar_idx, PciBarOffset offset, PciAccessBytes size)
        -> PciMmioData override;
    void bar_write(PciBarIndex bar_idx, PciBarOffset offset, PciMmioData val,
                   PciAccessBytes size) override;

    [[nodiscard]] auto device_status() const -> uint8_t { return device_status_; }
    [[nodiscard]] auto isr_status() const -> virtio::VirtioIsrStatus { return isr_status_; }
    [[nodiscard]] auto num_queues() const -> size_t { return queues_.size(); }

    [[nodiscard]] auto get_queue_state(size_t idx) const -> const virtio::QueueState* {
        if (idx < queues_.size()) {
            return &queues_[idx];
        }
        return nullptr;
    }

   protected:
    virtual auto get_device_features(virtio::VirtioFeatureWord select)
        -> virtio::VirtioFeatureWord {
        return select == 1 ? virtio::kVirtioFVersion1FeatureWord : 0;
    }
    virtual void on_driver_features(virtio::VirtioFeatureWord select,
                                    virtio::VirtioFeatureWord val) {
        (void)select;
        (void)val;
    }
    virtual void on_queue_notify(virtio::VirtioQueueIndex queue_index) = 0;
    virtual auto read_device_config(PciBarOffset offset, PciAccessBytes size) -> PciMmioData {
        (void)offset;
        (void)size;
        return 0;
    }
    virtual void write_device_config(PciBarOffset offset, PciMmioData val, PciAccessBytes size) {
        (void)offset;
        (void)val;
        (void)size;
    }

    auto dma_read(simrv::xlen::Address paddr, void* dst, size_t len) -> bool;
    auto dma_write(simrv::xlen::Address paddr, const void* src, size_t len) -> bool;

    void add_pci_cap(uint8_t cap_type, PciBarIndex bar, PciBarOffset offset, PciBarSize length,
                     uint32_t notify_mult = 0);

    std::vector<virtio::QueueState> queues_;
    virtio::VirtioQueueIndex queue_select_{0};
    uint8_t device_status_{0};
    virtio::VirtioIsrStatus isr_status_{0};
    virtio::VirtioFeatureWord driver_features_sel_{0};
    virtio::VirtioFeatureWord device_features_sel_{0};
    virtio::VirtioFeatureWord driver_features_{0};
    PciConfigOffset next_cap_offset_{0x40};
};

}  // namespace simrv::device
