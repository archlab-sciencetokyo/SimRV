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
    auto get_device_features(uint32_t select) -> uint32_t override;
    auto read_device_config(Address offset, uint8_t size) -> uint32_t override;
    void on_queue_notify(uint16_t queue_index) override;

   private:
    virtio::VirtioSoundBackend backend_{};
};

}  // namespace simrv::device
