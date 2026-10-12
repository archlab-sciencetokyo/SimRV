/**
 * @file VirtioMmioConsole.cpp
 * @brief Implementation of VirtIO-MMIO v2 Console Endpoint.
 */
#include "simrv/device/mmio/VirtioMmioConsole.hpp"

#include <vector>

#include "simrv/device/virtio/VirtioConsoleQueue.hpp"

namespace simrv::device {

VirtioMmioConsole::VirtioMmioConsole(simrv::xlen::Address base_address, uint32_t irq_num,
                                     core::Machine* machine)
    : VirtioMmioDevice("virtio-console-mmio", base_address, 0x1000, virtio::kDevIdConsole, irq_num,
                       machine) {
    add_queue(64);  // rx
    add_queue(64);  // tx
}

auto VirtioMmioConsole::read_device_config(simrv::xlen::Address offset, std::size_t size)
    -> uint64_t {
    (void)size;
    if (offset == 0) return 80 | (24 << 16);  // cols=80, rows=24
    return 0;
}

void VirtioMmioConsole::send_input(uint8_t ch) {
    backend_.push_rx(ch);
    on_queue_notify(0);
}

void VirtioMmioConsole::on_queue_notify(uint32_t q_idx) {
    if (q_idx >= queues_.size()) return;
    virtio::process_console_queue(
        q_idx, queues_[q_idx], backend_,
        [this](simrv::xlen::Address addr, void* buf, size_t len) {
            return dma_read_bytes(addr, reinterpret_cast<std::byte*>(buf), len);
        },
        [this](simrv::xlen::Address addr, const void* buf, size_t len) {
            return dma_write_bytes(addr, reinterpret_cast<const std::byte*>(buf), len);
        },
        [this]() { trigger_irq(); });
}

}  // namespace simrv::device
