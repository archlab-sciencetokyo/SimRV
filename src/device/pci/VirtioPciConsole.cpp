/**
 * @file VirtioPciConsole.cpp
 * @brief Implementation of VirtIO-PCI Console Endpoint.
 */
#include "simrv/device/pci/VirtioPciConsole.hpp"

#include <vector>

#include "simrv/device/virtio/VirtioConsoleQueue.hpp"

namespace simrv::device {

VirtioPciConsole::VirtioPciConsole() : VirtioPciDevice(virtio::kDevIdConsole, 0x070000, 2) {}

auto VirtioPciConsole::read_device_config(PciBarOffset offset, PciAccessBytes size) -> PciMmioData {
    (void)size;
    if (offset == 0) {
        return 80 | (24 << 16);  // cols=80, rows=24
    }
    return 0;
}

void VirtioPciConsole::send_input(uint8_t ch) {
    backend_.push_rx(ch);
    on_queue_notify(0);  // Trigger receive queue processing
}

void VirtioPciConsole::on_queue_notify(virtio::VirtioQueueIndex queue_index) {
    if (queue_index >= queues_.size()) return;
    virtio::process_console_queue(
        queue_index, queues_[queue_index], backend_,
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
