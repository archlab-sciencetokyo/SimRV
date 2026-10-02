/**
 * @file VirtioPciInput.hpp
 * @brief Modern VirtIO-PCI Keyboard/Mouse Input Endpoint.
 */
#pragma once

#include "simrv/device/pci/VirtioPciDevice.hpp"
#include "simrv/device/virtio/VirtioCore.hpp"

namespace simrv::device {

/**
 * @class VirtioPciInput
 * @brief VirtIO-PCI Input Endpoint (Device ID 0x1052 / Subsystem ID 18).
 */
class VirtioPciInput : public VirtioPciDevice {
   public:
    VirtioPciInput();

    void push_event(uint16_t type, uint16_t code, uint32_t value);
    void push_key_event(uint16_t code, bool pressed);
    void push_mouse_motion(int32_t x, int32_t y);
    void push_mouse_button(uint16_t button, bool pressed);
    void push_mouse_wheel(int32_t delta);

   protected:
    auto get_device_features(uint32_t select) -> uint32_t override;
    void on_queue_notify(uint16_t queue_index) override;
    auto read_device_config(Address offset, uint8_t size) -> uint32_t override;
    void write_device_config(Address offset, uint32_t val, uint8_t size) override;

   private:
    void process_eventq();
    virtio::InputBackend backend_;
};

}  // namespace simrv::device
