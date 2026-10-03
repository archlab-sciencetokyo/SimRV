/**
 * @file VirtioPciSound.cpp
 * @brief Implementation of VirtIO-PCI Audio/Sound Endpoint.
 */
#include "simrv/device/pci/VirtioPciSound.hpp"

namespace simrv::device {

VirtioPciSound::VirtioPciSound() : VirtioPciDevice(virtio::kDevIdSound, 0x040100, 4) {
    init_bar(4, sizeof(virtio::VirtioSndConfig), false, false);
}

auto VirtioPciSound::get_device_features(uint32_t select) -> uint32_t {
    if (select == 1) {
        return (1U << 0);  // VIRTIO_F_VERSION_1
    }
    return 0;
}

auto VirtioPciSound::read_device_config(Address offset, uint8_t size) -> uint32_t {
    return backend_.read_config(offset, size);
}

void VirtioPciSound::on_queue_notify(uint16_t queue_index) {
    if (queue_index >= queues_.size()) return;
    auto& q = queues_[queue_index];

    auto dma_read_fn = [this](uint64_t addr, void* dst, std::size_t len) -> bool {
        return dma_read(addr, dst, len);
    };
    auto dma_write_fn = [this](uint64_t addr, const void* src, std::size_t len) -> bool {
        return dma_write(addr, src, len);
    };
    auto trigger_irq_fn = [this]() { trigger_irq(); };

    if (queue_index == virtio::kVirtioSndVqControl) {
        backend_.process_controlq(q, dma_read_fn, dma_write_fn, trigger_irq_fn);
    } else if (queue_index == virtio::kVirtioSndVqTx) {
        backend_.process_txq(q, dma_read_fn, dma_write_fn, trigger_irq_fn);
    }
}

}  // namespace simrv::device
