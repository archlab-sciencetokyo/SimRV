/**
 * @file VirtioPciInput.cpp
 * @brief Implementation of VirtIO-PCI Keyboard/Mouse Input Endpoint.
 */
#include "simrv/device/pci/VirtioPciInput.hpp"

#include "simrv/device/virtio/VirtioInputQueue.hpp"

namespace simrv::device {

VirtioPciInput::VirtioPciInput() : VirtioPciDevice(virtio::kDevIdInput, 0x090000, 2) {}

void VirtioPciInput::on_queue_notify(uint16_t queue_index) {
    if (queue_index == 0) {
        process_eventq();
    }
}

void VirtioPciInput::process_eventq() {
    if (queues_.empty()) return;
    virtio::process_input_event_queue(
        queues_[0], backend_,
        [this](uint64_t address, void* data, std::size_t size) {
            return dma_read(address, data, size);
        },
        [this](uint64_t address, const void* data, std::size_t size) {
            return dma_write(address, data, size);
        },
        [this]() {
            isr_status_ |= 0x1;
            trigger_irq();
        });
}

void VirtioPciInput::push_event(uint16_t type, uint16_t code, uint32_t value) {
    {
        std::scoped_lock lock(backend_.event_mutex);
        backend_.event_queue.push_back({.type = type, .code = code, .value = value});
    }
    process_eventq();
}

void VirtioPciInput::push_key_event(uint16_t code, bool pressed) {
    push_event(0x01 /* EV_KEY */, code, pressed ? 1 : 0);
    push_event(0x00 /* EV_SYN */, 0 /* SYN_REPORT */, 0);
}

void VirtioPciInput::push_mouse_motion(int32_t x, int32_t y) {
    push_event(0x03 /* EV_ABS */, 0 /* ABS_X */, static_cast<uint32_t>(std::clamp(x, 0, 639)));
    push_event(0x03 /* EV_ABS */, 1 /* ABS_Y */, static_cast<uint32_t>(std::clamp(y, 0, 479)));
    push_event(0x00 /* EV_SYN */, 0 /* SYN_REPORT */, 0);
}

void VirtioPciInput::push_mouse_button(uint16_t button, bool pressed) {
    push_event(0x01 /* EV_KEY */, button, pressed ? 1 : 0);
    push_event(0x00 /* EV_SYN */, 0 /* SYN_REPORT */, 0);
}

void VirtioPciInput::push_mouse_wheel(int32_t delta) {
    push_event(0x02 /* EV_REL */, 8 /* REL_WHEEL */, static_cast<uint32_t>(delta));
    push_event(0x00 /* EV_SYN */, 0 /* SYN_REPORT */, 0);
}

auto VirtioPciInput::read_device_config(simrv::xlen::Address offset, uint8_t size) -> uint32_t {
    return backend_.read_config(offset, size);
}

void VirtioPciInput::write_device_config(simrv::xlen::Address offset, uint32_t val, uint8_t size) {
    backend_.write_config(offset, val, size);
}

}  // namespace simrv::device
