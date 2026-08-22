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

void VirtioMmioInput::on_queue_notify(uint32_t q_idx) { (void)q_idx; }

auto VirtioMmioInput::read_device_config(Address offset, std::size_t size) -> uint64_t {
    return backend_.read_config(offset, static_cast<uint8_t>(size));
}

void VirtioMmioInput::write_device_config(Address offset, uint64_t val, std::size_t size) {
    backend_.write_config(offset, static_cast<uint32_t>(val), static_cast<uint8_t>(size));
}

}  // namespace simrv::device
