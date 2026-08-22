/**
 * @file VirtioPciNet.hpp
 * @brief Modern VirtIO-PCI Network Adapter Endpoint.
 */
#pragma once

#include "simrv/device/pci/VirtioPciDevice.hpp"
#include "simrv/device/virtio/VirtioCore.hpp"

namespace simrv::device {

/**
 * @class VirtioPciNet
 * @brief VirtIO-PCI Network Adapter (Device ID 0x1041 / Subsystem ID 1).
 */
class VirtioPciNet : public VirtioPciDevice {
   public:
    explicit VirtioPciNet(virtio::NetBackend::Mode mode = virtio::NetBackend::Mode::User);

    auto backend() -> virtio::NetBackend& { return backend_; }
    [[nodiscard]] auto backend() const -> const virtio::NetBackend& { return backend_; }

   protected:
    auto get_device_features(uint32_t select) -> uint32_t override;
    void on_queue_notify(uint16_t queue_index) override;
    auto read_device_config(Address offset, uint8_t size) -> uint32_t override;

   private:
    virtio::NetBackend backend_;
};

}  // namespace simrv::device
