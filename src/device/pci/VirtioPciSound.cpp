/**
 * @file VirtioPciSound.cpp
 * @brief Implementation of VirtIO-PCI Audio/Sound Endpoint.
 */
#include "simrv/device/pci/VirtioPciSound.hpp"

namespace simrv::device {

VirtioPciSound::VirtioPciSound() : VirtioPciDevice(virtio::kDevIdSound, 0x040100, 4) {}

auto VirtioPciSound::get_device_features(uint32_t select) -> uint32_t {
    if (select == 1) {
        return (1U << 0);  // VIRTIO_F_VERSION_1
    }
    return 0;
}

void VirtioPciSound::on_queue_notify(uint16_t queue_index) { (void)queue_index; }

}  // namespace simrv::device
