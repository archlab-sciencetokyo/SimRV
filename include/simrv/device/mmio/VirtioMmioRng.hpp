/**
 * @file VirtioMmioRng.hpp
 * @brief VirtIO-MMIO v2 Random Number Generator Endpoint.
 */
#pragma once

#include "simrv/device/mmio/VirtioMmioDevice.hpp"
#include "simrv/device/virtio/VirtioCore.hpp"

namespace simrv::device {

/**
 * @class VirtioMmioRng
 * @brief VirtIO-MMIO v2 Random Number Generator Endpoint.
 */
class VirtioMmioRng : public VirtioMmioDevice {
   public:
    VirtioMmioRng(Address base_address, uint32_t irq_num, core::Machine* machine);

   protected:
    void on_queue_notify(uint32_t q_idx) override;

   private:
    virtio::RngBackend backend_;
};

}  // namespace simrv::device
