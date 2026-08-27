/**
 * @file VirtioMmioBlock.hpp
 * @brief VirtIO-MMIO v2 Block Storage Endpoint.
 */
#pragma once

#include <string>

#include "simrv/device/mmio/VirtioMmioDevice.hpp"
#include "simrv/device/virtio/VirtioCore.hpp"

namespace simrv::device {

/**
 * @class VirtioMmioBlock
 * @brief VirtIO-MMIO v2 Block Storage Endpoint.
 */
class VirtioMmioBlock : public VirtioMmioDevice {
   public:
    VirtioMmioBlock(Address base_address, uint32_t irq_num, core::Machine* machine,
                    const std::string& disk_path = "");

    auto load_disk(const std::string& path) -> bool { return backend_.load_disk(path); }
    [[nodiscard]] auto is_disk_loaded() const -> bool { return backend_.is_loaded(); }
    [[nodiscard]] auto capacity_sectors() const -> uint64_t { return backend_.capacity_sectors(); }

   protected:
    void on_queue_notify(uint32_t q_idx) override;
    auto read_device_config(Address offset, std::size_t size) -> uint64_t override;

   private:
    virtio::BlockBackend backend_;
    std::vector<std::byte> io_buffer_{};
};

}  // namespace simrv::device
