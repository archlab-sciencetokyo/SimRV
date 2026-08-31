/**
 * @file VirtioMmioGpu.hpp
 * @brief VirtIO-MMIO v2 GPU Display Endpoint.
 */
#pragma once

#include "simrv/device/mmio/VirtioMmioDevice.hpp"
#include "simrv/device/virtio/VirtioCore.hpp"

namespace simrv::device {

/**
 * @class VirtioMmioGpu
 * @brief VirtIO-MMIO v2 GPU Display Endpoint.
 */
class VirtioMmioGpu : public VirtioMmioDevice {
   public:
    VirtioMmioGpu(Address base_address, uint32_t irq_num, core::Machine* machine);

   protected:
    void on_queue_notify(uint32_t q_idx) override;
    auto read_device_config(Address offset, std::size_t size) -> uint64_t override;
};

}  // namespace simrv::device
