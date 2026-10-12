#include "simrv/device/mmio/VirtioMmioRng.hpp"

#include "simrv/device/virtio/VirtioRngQueue.hpp"

namespace simrv::device {

VirtioMmioRng::VirtioMmioRng(simrv::xlen::Address base_address, uint32_t irq_num,
                             core::Machine* machine)
    : VirtioMmioDevice("virtio-rng-mmio", base_address, 0x1000, virtio::kDevIdRng, irq_num,
                       machine) {
    add_queue(64);
}

void VirtioMmioRng::on_queue_notify(uint32_t q_idx) {
    if (q_idx >= queues_.size()) return;
    virtio::process_rng_queue(
        queues_[q_idx], backend_,
        [this](simrv::xlen::Address addr, void* buf, size_t len) {
            return dma_read_bytes(addr, reinterpret_cast<std::byte*>(buf), len);
        },
        [this](simrv::xlen::Address addr, const void* buf, size_t len) {
            return dma_write_bytes(addr, reinterpret_cast<const std::byte*>(buf), len);
        },
        [this]() { trigger_irq(); });
}

}  // namespace simrv::device
