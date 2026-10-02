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

    void push_event(uint16_t type, uint16_t code, uint32_t value);
    void push_key_event(uint16_t code, bool pressed);
    void push_mouse_motion(int32_t x, int32_t y);
    void push_mouse_button(uint16_t button, bool pressed);
    void push_mouse_wheel(int32_t delta);

   protected:
    void on_queue_notify(uint32_t q_idx) override;
    auto read_device_config(Address offset, std::size_t size) -> uint64_t override;
    void write_device_config(Address offset, uint64_t val, std::size_t size) override;

   private:
    void process_eventq();
    virtio::InputBackend backend_;
};

}  // namespace simrv::device
