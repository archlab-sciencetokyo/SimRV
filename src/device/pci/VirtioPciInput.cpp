/**
 * @file VirtioPciInput.cpp
 * @brief Implementation of VirtIO-PCI Keyboard/Mouse Input Endpoint.
 */
#include "simrv/device/pci/VirtioPciInput.hpp"

namespace simrv::device {

VirtioPciInput::VirtioPciInput() : VirtioPciDevice(virtio::kDevIdInput, 0x090000, 2) {}

auto VirtioPciInput::get_device_features(uint32_t select) -> uint32_t {
    if (select == 1) {
        return (1U << 0);  // VIRTIO_F_VERSION_1
    }
    return 0;
}

void VirtioPciInput::on_queue_notify(uint16_t queue_index) {
    if (queue_index == 0) {
        process_eventq();
    }
}

void VirtioPciInput::process_eventq() {
    if (queues_.empty()) return;
    auto& q = queues_[0];
    if (!q.ready || !q.driver_addr || !q.device_addr || !q.desc_addr) return;

    std::scoped_lock lock(backend_.event_mutex);
    if (backend_.event_queue.empty()) return;

    uint16_t avail_idx = 0;
    if (!dma_read(q.driver_addr + 2, &avail_idx, 2)) return;

    bool processed_any = false;
    while (q.last_avail_idx != avail_idx && !backend_.event_queue.empty()) {
        const uint16_t ring_idx = q.last_avail_idx % q.num;
        uint16_t head_desc_idx = 0;
        if (!dma_read(q.driver_addr + 4 + ring_idx * 2, &head_desc_idx, 2)) break;

        virtio::VirtqDesc desc{};
        if (!dma_read(q.desc_addr + head_desc_idx * sizeof(virtio::VirtqDesc), &desc, sizeof(desc)))
            break;

        if (desc.len < sizeof(virtio::VirtioInputEvent)) break;

        const auto event = backend_.event_queue.front();
        backend_.event_queue.pop_front();

        dma_write(desc.addr, &event, sizeof(event));

        uint16_t used_idx = 0;
        dma_read(q.device_addr + 2, &used_idx, 2);
        virtio::VirtqUsedElem elem{head_desc_idx, sizeof(event)};
        dma_write(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem), &elem, sizeof(elem));
        used_idx++;
        dma_write(q.device_addr + 2, &used_idx, 2);

        q.last_avail_idx++;
        processed_any = true;
    }

    if (processed_any) {
        isr_status_ |= 0x1;
        trigger_irq();
    }
}

void VirtioPciInput::push_event(uint16_t type, uint16_t code, uint32_t value) {
    {
        std::scoped_lock lock(backend_.event_mutex);
        backend_.event_queue.push_back({type, code, value});
    }
    process_eventq();
}

void VirtioPciInput::push_key_event(uint16_t code, bool pressed) {
    push_event(0x01 /* EV_KEY */, code, pressed ? 1 : 0);
    push_event(0x00 /* EV_SYN */, 0 /* SYN_REPORT */, 0);
}

void VirtioPciInput::push_mouse_motion(int32_t x, int32_t y) {
    push_event(0x02 /* EV_REL */, 0 /* REL_X */, static_cast<uint32_t>(x));
    push_event(0x02 /* EV_REL */, 1 /* REL_Y */, static_cast<uint32_t>(y));
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

auto VirtioPciInput::read_device_config(Address offset, uint8_t size) -> uint32_t {
    return backend_.read_config(offset, size);
}

void VirtioPciInput::write_device_config(Address offset, uint32_t val, uint8_t size) {
    backend_.write_config(offset, val, size);
}

}  // namespace simrv::device
