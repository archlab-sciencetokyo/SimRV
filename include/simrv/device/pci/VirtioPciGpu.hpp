/**
 * @file VirtioPciGpu.hpp
 * @brief Modern VirtIO-PCI GPU Display Endpoint.
 */
#pragma once

#include "simrv/device/pci/VirtioPciDevice.hpp"
#include "simrv/device/virtio/VirtioCore.hpp"

namespace simrv::device {

/**
 * @class VirtioPciGpu
 * @brief VirtIO-PCI GPU Endpoint (Device ID 0x1050 / Subsystem ID 16).
 */
class VirtioPciGpu : public VirtioPciDevice {
   public:
    VirtioPciGpu();

   protected:
    void on_queue_notify(virtio::VirtioQueueIndex queue_index) override;
    auto read_device_config(PciBarOffset offset, PciAccessBytes size) -> PciMmioData override;
};

}  // namespace simrv::device
