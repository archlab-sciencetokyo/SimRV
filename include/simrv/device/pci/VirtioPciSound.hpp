/**
 * @file VirtioPciSound.hpp
 * @brief Modern VirtIO-PCI Audio/Sound Endpoint.
 */
#pragma once

#include "simrv/device/pci/VirtioPciDevice.hpp"
#include "simrv/device/virtio/VirtioCore.hpp"

namespace simrv::device {

/**
 * @class VirtioPciSound
 * @brief VirtIO-PCI Sound Endpoint (Device ID 0x1059 / Subsystem ID 25).
 */
class VirtioPciSound : public VirtioPciDevice {
   public:
    VirtioPciSound();

   protected:
    auto get_device_features(uint32_t select) -> uint32_t override;
    void on_queue_notify(uint16_t queue_index) override;
};

}  // namespace simrv::device
