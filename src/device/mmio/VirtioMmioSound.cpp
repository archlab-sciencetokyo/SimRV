/**
 * @file VirtioMmioSound.cpp
 * @brief Implementation of VirtIO-MMIO v2 Audio/Sound Endpoint.
 */
#include "simrv/device/mmio/VirtioMmioSound.hpp"

namespace simrv::device {

VirtioMmioSound::VirtioMmioSound(Address base_address, uint32_t irq_num, core::Machine* machine)
    : VirtioMmioDevice("virtio-sound-mmio", base_address, 0x1000, virtio::kDevIdSound, irq_num,
                       machine) {
    add_queue(64);  // controlq
    add_queue(64);  // eventq
    add_queue(64);  // txq
    add_queue(64);  // rxq
}

void VirtioMmioSound::on_queue_notify(uint32_t q_idx) { (void)q_idx; }

}  // namespace simrv::device
