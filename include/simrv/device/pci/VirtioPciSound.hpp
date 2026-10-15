/**
 * @file VirtioPciSound.hpp
 * @brief Modern VirtIO-PCI Audio/Sound Endpoint.
 */
#pragma once

#include "simrv/device/pci/VirtioPciDevice.hpp"
#include "simrv/device/virtio/VirtioSoundBackend.hpp"

namespace simrv::device {

/**
 * @class VirtioPciSound
 * @brief VirtIO-PCI Sound Endpoint (Device ID 0x1059 / Subsystem ID 25).
 */
class VirtioPciSound : public VirtioPciDevice {
   public:
    VirtioPciSound();

    [[nodiscard]] auto backend() noexcept -> virtio::VirtioSoundBackend& { return backend_; }
    [[nodiscard]] auto backend() const noexcept -> const virtio::VirtioSoundBackend& {
        return backend_;
    }

   protected:
    auto read_device_config(PciBarOffset offset, PciAccessBytes size) -> PciMmioData override;
    void on_queue_notify(virtio::VirtioQueueIndex queue_index) override;

   private:
    virtio::VirtioSoundBackend backend_{};
};

}  // namespace simrv::device
