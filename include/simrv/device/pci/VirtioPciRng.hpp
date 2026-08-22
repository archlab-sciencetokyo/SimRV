/**
 * @file VirtioPciRng.hpp
 * @brief Modern VirtIO-PCI Random Number Generator Endpoint.
 */
#pragma once

#include "simrv/device/pci/VirtioPciDevice.hpp"
#include "simrv/device/virtio/VirtioCore.hpp"

namespace simrv::device {

/**
 * @class VirtioPciRng
 * @brief VirtIO-PCI RNG Endpoint (Device ID 0x1044 / Subsystem ID 4).
 */
class VirtioPciRng : public VirtioPciDevice {
   public:
    VirtioPciRng();

   protected:
    auto get_device_features(uint32_t select) -> uint32_t override;
    void on_queue_notify(uint16_t queue_index) override;

   private:
    virtio::RngBackend backend_;
};

}  // namespace simrv::device
