/**
 * @file VirtioMmioSound.hpp
 * @brief VirtIO-MMIO v2 Audio/Sound Endpoint.
 */
#pragma once

#include "simrv/device/mmio/VirtioMmioDevice.hpp"
#include "simrv/device/virtio/VirtioCore.hpp"

namespace simrv::device {

/**
 * @class VirtioMmioSound
 * @brief VirtIO-MMIO v2 Sound Endpoint.
 */
class VirtioMmioSound : public VirtioMmioDevice {
   public:
    VirtioMmioSound(Address base_address, uint32_t irq_num, core::Machine* machine);

   protected:
    void on_queue_notify(uint32_t q_idx) override;
};

}  // namespace simrv::device
