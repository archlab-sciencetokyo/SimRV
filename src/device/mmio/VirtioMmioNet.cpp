/**
 * @file VirtioMmioNet.cpp
 * @brief Implementation of VirtIO-MMIO v2 Network Adapter Endpoint.
 */
#include "simrv/device/mmio/VirtioMmioNet.hpp"

#include <cstring>
#include <vector>

#include "simrv/core/Machine.hpp"
#include "simrv/device/virtio/VirtioNetQueue.hpp"
#include "simrv/memory/MemorySubsystem.hpp"

namespace simrv::device {

VirtioMmioNet::VirtioMmioNet(simrv::xlen::Address base_address, uint32_t irq_num,
                             core::Machine* machine, virtio::NetBackend::Mode mode)
    : VirtioMmioDevice("virtio-net-mmio", base_address, 0x1000, virtio::kDevIdNet, irq_num,
                       machine),
      backend_(mode) {
    add_queue(64);  // rx
    add_queue(64);  // tx
}

void VirtioMmioNet::poll_backend() {
    if (backend_.poll_host_rx() != 0) on_queue_notify(0);
}

auto VirtioMmioNet::read_device_config(simrv::xlen::Address offset, std::size_t size) -> uint64_t {
    const auto& mac = backend_.get_mac();
    uint64_t value = 0;
    for (std::size_t byte = 0; byte < size && byte < sizeof(value); ++byte) {
        const auto config_offset = offset + byte;
        uint8_t value_byte = 0;
        if (config_offset < mac.size()) {
            value_byte = mac[config_offset];
        } else if (config_offset == 6) {
            value_byte = 1;  // VIRTIO_NET_S_LINK_UP
        }
        value |= static_cast<uint64_t>(value_byte) << (byte * 8);
    }
    return value;
}

void VirtioMmioNet::on_queue_notify(uint32_t q_idx) {
    if (q_idx >= queues_.size()) return;
    auto& q = queues_[q_idx];
    const bool defer_completion = machine_ != nullptr &&
                                  machine_->runtime_profile.is_cycle_mode() &&
                                  machine_->dma_engine().is_enabled();

    auto dma_read = [this](simrv::xlen::Address addr, void* buf, size_t size) {
        return dma_read_bytes(addr, reinterpret_cast<std::byte*>(buf), size);
    };
    auto dma_write = [this](simrv::xlen::Address addr, const void* buf, size_t size) {
        return dma_write_bytes(addr, reinterpret_cast<const std::byte*>(buf), size);
    };
    auto schedule_deferred = [this, q_idx](size_t transfer_bytes, uint16_t head_desc_idx,
                                           uint32_t len) {
        const auto current_cycle = machine_->memory().system_bus().cycle();
        machine_->dma_engine().schedule_transfer(
            transfer_bytes, current_cycle,
            [this, q_idx, head_desc_idx, len]() {
                auto& q = queues_[q_idx];
                uint16_t used_idx = 0;
                dma_read_bytes(q.device_addr + 2, reinterpret_cast<std::byte*>(&used_idx), 2);
                virtio::VirtqUsedElem elem{head_desc_idx, len};
                dma_write_bytes(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem),
                                reinterpret_cast<std::byte*>(&elem), sizeof(elem));
                used_idx++;
                dma_write_bytes(q.device_addr + 2, reinterpret_cast<std::byte*>(&used_idx), 2);
                trigger_irq();
            },
            "virtio-mmio-net");
    };
    auto notify_complete = [this]() { trigger_irq(); };

    virtio::process_net_queue(q_idx, q, backend_, defer_completion, dma_read, dma_write,
                              schedule_deferred, notify_complete);
}

}  // namespace simrv::device
