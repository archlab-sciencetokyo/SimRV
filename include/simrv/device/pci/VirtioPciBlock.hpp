/**
 * @file VirtioPciBlock.hpp
 * @brief Modern VirtIO-PCI Block Storage Endpoint.
 */
#pragma once

#include <string>

#include "simrv/device/pci/VirtioPciDevice.hpp"
#include "simrv/device/virtio/VirtioCore.hpp"

namespace simrv::device {

/**
 * @class VirtioPciBlock
 * @brief VirtIO-PCI Block Device Endpoint (Device ID 0x1042 / Subsystem ID 2).
 */
class VirtioPciBlock : public VirtioPciDevice {
   public:
    explicit VirtioPciBlock(const std::string& disk_path = "");

    auto load_disk(const std::string& path) -> bool { return backend_.load_disk(path); }
    [[nodiscard]] auto is_disk_loaded() const -> bool { return backend_.is_loaded(); }
    [[nodiscard]] auto capacity_sectors() const -> uint64_t { return backend_.capacity_sectors(); }

   protected:
    auto get_device_features(uint32_t select) -> uint32_t override;
    void on_queue_notify(uint16_t queue_index) override;
    auto read_device_config(Address offset, uint8_t size) -> uint32_t override;

   private:
    virtio::BlockBackend backend_;
};

}  // namespace simrv::device
