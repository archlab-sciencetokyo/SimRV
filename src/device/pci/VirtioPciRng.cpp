#include "simrv/device/pci/VirtioPciRng.hpp"

#include "simrv/device/virtio/VirtioRngQueue.hpp"

namespace simrv::device {

VirtioPciRng::VirtioPciRng() : VirtioPciDevice(virtio::kDevIdRng, 0x00FF00, 1) {}

void VirtioPciRng::on_queue_notify(uint16_t queue_index) {
    if (queue_index >= queues_.size()) return;
    virtio::process_rng_queue(
        queues_[queue_index], backend_,
        [this](simrv::xlen::Address addr, void* buf, size_t len) {
            return dma_read(addr, buf, len);
        },
        [this](simrv::xlen::Address addr, const void* buf, size_t len) {
            return dma_write(addr, buf, len);
        },
        [this]() {
            isr_status_ |= 0x1;
            trigger_irq();
        });
}

}  // namespace simrv::device
