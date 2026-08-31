/**
 * @file VirtioMmioNet.hpp
 * @brief VirtIO-MMIO v2 Network Adapter Endpoint.
 */
#pragma once

#include "simrv/device/mmio/VirtioMmioDevice.hpp"
#include "simrv/device/virtio/VirtioCore.hpp"

namespace simrv::device {

/**
 * @class VirtioMmioNet
 * @brief VirtIO-MMIO v2 Network Adapter (Device ID 1).
 */
class VirtioMmioNet : public VirtioMmioDevice {
   public:
    VirtioMmioNet(Address base_address, uint32_t irq_num, core::Machine* machine,
                  virtio::NetBackend::Mode mode = virtio::NetBackend::Mode::User);

    auto backend() -> virtio::NetBackend& { return backend_; }
    [[nodiscard]] auto backend() const -> const virtio::NetBackend& { return backend_; }

   protected:
    void on_queue_notify(uint32_t q_idx) override;
    auto read_device_config(Address offset, std::size_t size) -> uint64_t override;

   private:
    virtio::NetBackend backend_;
};

}  // namespace simrv::device
