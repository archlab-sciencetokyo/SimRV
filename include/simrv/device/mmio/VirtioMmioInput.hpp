/**
 * @file VirtioMmioInput.hpp
 * @brief VirtIO-MMIO v2 Keyboard/Mouse Input Endpoint.
 */
#pragma once

#include "simrv/device/mmio/VirtioMmioDevice.hpp"
#include "simrv/device/virtio/VirtioCore.hpp"

namespace simrv::device {

/**
 * @class VirtioMmioInput
 * @brief VirtIO-MMIO v2 Input Endpoint.
 */
class VirtioMmioInput : public VirtioMmioDevice {
   public:
    VirtioMmioInput(Address base_address, uint32_t irq_num, core::Machine* machine);

   protected:
    void on_queue_notify(uint32_t q_idx) override;
    auto read_device_config(Address offset, std::size_t size) -> uint64_t override;
    void write_device_config(Address offset, uint64_t val, std::size_t size) override;

   private:
    virtio::InputBackend backend_;
};

}  // namespace simrv::device
