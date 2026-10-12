/**
 * @file VirtioMmioBlock.cpp
 * @brief Implementation of VirtIO-MMIO v2 Block Storage Endpoint.
 */
#include "simrv/device/mmio/VirtioMmioBlock.hpp"

#include <vector>

#include "simrv/core/Machine.hpp"
#include "simrv/device/virtio/VirtioBlockQueue.hpp"
#include "simrv/memory/MemorySubsystem.hpp"

namespace simrv::device {

VirtioMmioBlock::VirtioMmioBlock(simrv::xlen::Address base_address, uint32_t irq_num,
                                 core::Machine* machine, const std::string& disk_path)
    : VirtioMmioDevice("virtio-block-mmio", base_address, 0x1000, virtio::kDevIdBlock, irq_num,
                       machine),
      backend_(disk_path) {
    add_queue(64);
}

auto VirtioMmioBlock::read_device_config(simrv::xlen::Address offset, std::size_t size)
    -> uint64_t {
    (void)size;
    const uint64_t capacity = backend_.capacity_sectors();
    if (offset == 0) return capacity & 0xFFFFFFFFULL;
    if (offset == 4) return (capacity >> 32) & 0xFFFFFFFFULL;
    if (offset == 36 || offset == 48) return virtio::BlockBackend::kMaxRangeSectors;
    if (offset == 40 || offset == 52) return 1;  // One range per request
    if (offset == 56) return 1;                  // write_zeroes_may_unmap
    return 0;
}

void VirtioMmioBlock::on_queue_notify(uint32_t q_idx) {
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
                                           uint32_t total_written) {
        const auto current_cycle = machine_->memory().system_bus().cycle();
        machine_->dma_engine().schedule_transfer(
            transfer_bytes, current_cycle,
            [this, q_idx, head_desc_idx, total_written]() {
                auto& q = queues_[q_idx];
                uint16_t used_idx = 0;
                dma_read_bytes(q.device_addr + 2, reinterpret_cast<std::byte*>(&used_idx), 2);
                virtio::VirtqUsedElem elem{head_desc_idx, total_written};
                dma_write_bytes(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem),
                                reinterpret_cast<std::byte*>(&elem), sizeof(elem));
                used_idx++;
                dma_write_bytes(q.device_addr + 2, reinterpret_cast<std::byte*>(&used_idx), 2);
                trigger_irq();
            },
            "virtio-mmio-block");
    };
    auto notify_complete = [this]() { trigger_irq(); };

    virtio::process_block_queue(q, backend_, io_buffer_, defer_completion, dma_read, dma_write,
                                schedule_deferred, notify_complete);
}

}  // namespace simrv::device
