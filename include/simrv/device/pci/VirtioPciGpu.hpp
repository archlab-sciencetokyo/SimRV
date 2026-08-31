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
    auto get_device_features(uint32_t select) -> uint32_t override;
    void on_queue_notify(uint16_t queue_index) override;
    auto read_device_config(Address offset, uint8_t size) -> uint32_t override;
};

}  // namespace simrv::device
