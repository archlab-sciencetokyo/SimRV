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
    void on_queue_notify(virtio::VirtioQueueIndex queue_index) override;

   private:
    virtio::RngBackend backend_;
};

}  // namespace simrv::device
