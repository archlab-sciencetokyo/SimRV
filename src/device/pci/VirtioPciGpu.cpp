/**
 * @file VirtioPciGpu.cpp
 * @brief Implementation of VirtIO-PCI GPU Display Endpoint.
 */
#include "simrv/device/pci/VirtioPciGpu.hpp"

#include "simrv/device/virtio/VirtioGpuQueue.hpp"

namespace simrv::device {

VirtioPciGpu::VirtioPciGpu() : VirtioPciDevice(virtio::kDevIdGpu, 0x030000, 2) {}

auto VirtioPciGpu::read_device_config(PciBarOffset offset, PciAccessBytes size) -> PciMmioData {
    (void)size;
    if (offset == 0) return 0;  // events_read
    if (offset == 4) return 0;  // events_clear
    if (offset == 8) return 1;  // num_scanouts = 1
    return 0;
}

void VirtioPciGpu::on_queue_notify(virtio::VirtioQueueIndex queue_index) {
    if (queue_index >= queues_.size()) return;
    auto& q = queues_[queue_index];

    auto dma_read_cb = [this](simrv::xlen::Address addr, void* buf, size_t size) {
        return dma_read(addr, buf, size);
    };
    auto dma_write_cb = [this](simrv::xlen::Address addr, const void* buf, size_t size) {
        return dma_write(addr, buf, size);
    };
    auto notify_complete = [this]() {
        isr_status_ |= 0x1;
        trigger_irq();
    };

    virtio::process_gpu_queue(q, dma_read_cb, dma_write_cb, notify_complete);
}

}  // namespace simrv::device
