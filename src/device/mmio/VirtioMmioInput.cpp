/**
 * @file VirtioMmioInput.cpp
 * @brief Implementation of VirtIO-MMIO v2 Keyboard/Mouse Input Endpoint.
 */
#include "simrv/device/mmio/VirtioMmioInput.hpp"

#include "simrv/device/virtio/VirtioInputQueue.hpp"

namespace simrv::device {

VirtioMmioInput::VirtioMmioInput(simrv::xlen::Address base_address, uint32_t irq_num,
                                 core::Machine* machine)
    : VirtioMmioDevice("virtio-input-mmio", base_address, 0x1000, virtio::kDevIdInput, irq_num,
                       machine) {
    add_queue(64);  // eventq
    add_queue(64);  // statusq
}

void VirtioMmioInput::on_queue_notify(uint32_t q_idx) {
    if (q_idx == 0) {
        process_eventq();
    }
}

void VirtioMmioInput::process_eventq() {
    if (queues_.empty()) return;
    virtio::process_input_event_queue(
        queues_[0], backend_,
        [this](uint64_t address, void* data, std::size_t size) {
            return dma_read_bytes(address, reinterpret_cast<std::byte*>(data), size);
        },
        [this](uint64_t address, const void* data, std::size_t size) {
            return dma_write_bytes(address, reinterpret_cast<const std::byte*>(data), size);
        },
        [this]() { trigger_irq(); });
}

void VirtioMmioInput::push_event(uint16_t type, uint16_t code, uint32_t value) {
    {
        std::scoped_lock lock(backend_.event_mutex);
        backend_.event_queue.push_back({.type = type, .code = code, .value = value});
    }
    process_eventq();
}

void VirtioMmioInput::push_key_event(uint16_t code, bool pressed) {
    push_event(0x01 /* EV_KEY */, code, pressed ? 1 : 0);
    push_event(0x00 /* EV_SYN */, 0 /* SYN_REPORT */, 0);
}

void VirtioMmioInput::push_mouse_motion(int32_t x, int32_t y) {
    push_event(0x03 /* EV_ABS */, 0 /* ABS_X */, static_cast<uint32_t>(std::clamp(x, 0, 639)));
    push_event(0x03 /* EV_ABS */, 1 /* ABS_Y */, static_cast<uint32_t>(std::clamp(y, 0, 479)));
    push_event(0x00 /* EV_SYN */, 0 /* SYN_REPORT */, 0);
}

void VirtioMmioInput::push_mouse_button(uint16_t button, bool pressed) {
    push_event(0x01 /* EV_KEY */, button, pressed ? 1 : 0);
    push_event(0x00 /* EV_SYN */, 0 /* SYN_REPORT */, 0);
}

void VirtioMmioInput::push_mouse_wheel(int32_t delta) {
    push_event(0x02 /* EV_REL */, 8 /* REL_WHEEL */, static_cast<uint32_t>(delta));
    push_event(0x00 /* EV_SYN */, 0 /* SYN_REPORT */, 0);
}

auto VirtioMmioInput::read_device_config(simrv::xlen::Address offset, std::size_t size)
    -> uint64_t {
    return backend_.read_config(offset, static_cast<uint8_t>(size));
}

void VirtioMmioInput::write_device_config(simrv::xlen::Address offset, uint64_t val,
                                          std::size_t size) {
    backend_.write_config(offset, static_cast<uint32_t>(val), static_cast<uint8_t>(size));
}

}  // namespace simrv::device
