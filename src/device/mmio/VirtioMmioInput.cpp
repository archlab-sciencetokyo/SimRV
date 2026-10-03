/**
 * @file VirtioMmioInput.cpp
 * @brief Implementation of VirtIO-MMIO v2 Keyboard/Mouse Input Endpoint.
 */
#include "simrv/device/mmio/VirtioMmioInput.hpp"

namespace simrv::device {

VirtioMmioInput::VirtioMmioInput(Address base_address, uint32_t irq_num, core::Machine* machine)
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
    auto& q = queues_[0];
    if (q.ready == 0 || q.driver_addr == 0 || q.device_addr == 0 || q.desc_addr == 0) return;

    std::scoped_lock lock(backend_.event_mutex);
    if (backend_.event_queue.empty()) return;

    uint16_t avail_idx = 0;
    if (!dma_read_bytes(q.driver_addr + 2, reinterpret_cast<std::byte*>(&avail_idx), 2)) return;

    bool processed_any = false;
    while (q.last_avail_idx != avail_idx && !backend_.event_queue.empty()) {
        const uint16_t ring_idx = q.last_avail_idx % q.num;
        uint16_t head_desc_idx = 0;
        if (!dma_read_bytes(q.driver_addr + 4 + ring_idx * 2,
                            reinterpret_cast<std::byte*>(&head_desc_idx), 2))
            break;

        virtio::VirtqDesc desc{};
        if (!dma_read_bytes(q.desc_addr + head_desc_idx * sizeof(virtio::VirtqDesc),
                            reinterpret_cast<std::byte*>(&desc), sizeof(desc)))
            break;

        if (desc.len < sizeof(virtio::VirtioInputEvent)) break;

        const auto event = backend_.event_queue.front();
        backend_.event_queue.pop_front();

        dma_write_bytes(desc.addr, reinterpret_cast<const std::byte*>(&event), sizeof(event));

        uint16_t used_idx = 0;
        dma_read_bytes(q.device_addr + 2, reinterpret_cast<std::byte*>(&used_idx), 2);
        virtio::VirtqUsedElem elem{head_desc_idx, sizeof(event)};
        dma_write_bytes(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem),
                        reinterpret_cast<std::byte*>(&elem), sizeof(elem));
        used_idx++;
        dma_write_bytes(q.device_addr + 2, reinterpret_cast<std::byte*>(&used_idx), 2);

        q.last_avail_idx++;
        processed_any = true;
    }

    if (processed_any) {
        trigger_irq();
    }
}

void VirtioMmioInput::push_event(uint16_t type, uint16_t code, uint32_t value) {
    {
        std::scoped_lock lock(backend_.event_mutex);
        backend_.event_queue.push_back({type, code, value});
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

auto VirtioMmioInput::read_device_config(Address offset, std::size_t size) -> uint64_t {
    return backend_.read_config(offset, static_cast<uint8_t>(size));
}

void VirtioMmioInput::write_device_config(Address offset, uint64_t val, std::size_t size) {
    backend_.write_config(offset, static_cast<uint32_t>(val), static_cast<uint8_t>(size));
}

}  // namespace simrv::device
