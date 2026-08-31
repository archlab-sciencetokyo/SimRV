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

void VirtioPciInput::on_queue_notify(uint16_t queue_index) { (void)queue_index; }

auto VirtioPciInput::read_device_config(Address offset, uint8_t size) -> uint32_t {
    return backend_.read_config(offset, size);
}

void VirtioPciInput::write_device_config(Address offset, uint32_t val, uint8_t size) {
    backend_.write_config(offset, val, size);
}

}  // namespace simrv::device
