/**
 * @file VirtioMmioGpu.cpp
 * @brief Implementation of VirtIO-MMIO v2 GPU Display Endpoint.
 */
#include "simrv/device/mmio/VirtioMmioGpu.hpp"

#include <vector>

#include "simrv/device/virtio/VirtioGpuQueue.hpp"

namespace simrv::device {

VirtioMmioGpu::VirtioMmioGpu(simrv::xlen::Address base_address, uint32_t irq_num,
                             core::Machine* machine)
    : VirtioMmioDevice("virtio-gpu-mmio", base_address, 0x1000, virtio::kDevIdGpu, irq_num,
                       machine) {
    add_queue(64);  // controlq
    add_queue(64);  // cursorq
}

auto VirtioMmioGpu::read_device_config(simrv::xlen::Address offset, std::size_t size) -> uint64_t {
    (void)size;
    if (offset == 0) return 0;  // events_read
    if (offset == 4) return 0;  // events_clear
    if (offset == 8) return 1;  // num_scanouts = 1
    return 0;
}

void VirtioMmioGpu::on_queue_notify(uint32_t q_idx) {
    if (q_idx >= queues_.size()) return;
    auto& q = queues_[q_idx];

    auto dma_read = [this](simrv::xlen::Address addr, void* buf, size_t size) {
        return dma_read_bytes(addr, reinterpret_cast<std::byte*>(buf), size);
    };
    auto dma_write = [this](simrv::xlen::Address addr, const void* buf, size_t size) {
        return dma_write_bytes(addr, reinterpret_cast<const std::byte*>(buf), size);
    };
    auto notify_complete = [this]() { trigger_irq(); };

    virtio::process_gpu_queue(q, dma_read, dma_write, notify_complete);
}

}  // namespace simrv::device
