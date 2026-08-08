/**
 * @file Disk.hpp
 * @brief Virtio block-disk MMIO device interface.
 */
#pragma once

#include <vector>

#include "simrv/device/VirtioDevice.hpp"
#include "simrv/memory/Mmio.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::device {

/**
 * @class Disk
 * @brief Virtio block device model with queue-backed disk image access.
 */
class Disk : public VirtioDevice {
   public:
    /// Construct a disk device instance.
    explicit Disk(simrv::core::Machine& machine);
    static constexpr Address kBaseAddress = simrv::mmio::kDiskBaseAddress;
    static constexpr Address kSize = simrv::mmio::kDiskSize;

    [[nodiscard]] auto name() const -> const char* override { return "disk"; }
    [[nodiscard]] auto base_address() const -> Address override { return kBaseAddress; }
    [[nodiscard]] auto size() const -> Address override { return kSize; }

    Byte* mmem = nullptr;
    std::vector<Byte> sector_storage_;  // backing owner for disk image storage
    Byte* sector = nullptr;

   protected:
    [[nodiscard]] auto get_device_id() const -> Word override { return virtio::kDiskDeviceId; }
    [[nodiscard]] auto get_device_features() const -> Word override {
        return virtio::kDiskDeviceFeatures;
    }
    [[nodiscard]] auto get_queue_num_max() const -> Word override {
        return virtio::kDiskQueueNumMax;
    }
    [[nodiscard]] auto get_config_generation() const -> Word override {
        return virtio::kDiskConfigGeneration;
    }

    [[nodiscard]] auto read_config(Address offset) const -> Word override;
    void process_queue(Word q_idx) override;
};

}  // namespace simrv::device
