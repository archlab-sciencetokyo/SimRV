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

auto VirtioMmioSound::read_device_config(Address offset, std::size_t size) -> uint64_t {
    return backend_.read_config(offset, size);
}

void VirtioMmioSound::on_queue_notify(uint32_t q_idx) {
    if (q_idx >= queues_.size()) return;
    auto& q = queues_[q_idx];

    auto dma_read_fn = [this](uint64_t addr, void* dst, std::size_t len) -> bool {
        return dma_read_bytes(addr, reinterpret_cast<std::byte*>(dst), len);
    };
    auto dma_write_fn = [this](uint64_t addr, const void* src, std::size_t len) -> bool {
        return dma_write_bytes(addr, reinterpret_cast<const std::byte*>(src), len);
    };
    auto trigger_irq_fn = [this]() { trigger_irq(); };

    if (q_idx == virtio::kVirtioSndVqControl) {
        backend_.process_controlq(q, dma_read_fn, dma_write_fn, trigger_irq_fn);
    } else if (q_idx == virtio::kVirtioSndVqTx) {
        backend_.process_txq(q, dma_read_fn, dma_write_fn, trigger_irq_fn);
    }
}

}  // namespace simrv::device
