/**
 * @file VirtioPciBlock.cpp
 * @brief Implementation of VirtIO-PCI Block Storage Endpoint.
 */
#include "simrv/device/pci/VirtioPciBlock.hpp"

#include <vector>

#include "simrv/core/Machine.hpp"
#include "simrv/device/pci/PcieRootComplex.hpp"
#include "simrv/device/virtio/VirtioBlockQueue.hpp"
#include "simrv/memory/MemorySubsystem.hpp"

namespace simrv::device {

VirtioPciBlock::VirtioPciBlock(const std::string& disk_path)
    : VirtioPciDevice(virtio::kDevIdBlock, 0x010000, 1), backend_(disk_path) {}

auto VirtioPciBlock::get_device_features(uint32_t select) -> uint32_t {
    if (select == 0) {
        return (1U << 9) | (1U << 13) | (1U << 14);  // FLUSH, DISCARD, WRITE_ZEROES
    }
    if (select == 1) {
        return virtio::kVirtioFVersion1FeatureWord;
    }
    return 0;
}

auto VirtioPciBlock::read_device_config(simrv::xlen::Address offset, uint8_t size) -> uint32_t {
    (void)size;
    const uint64_t capacity = backend_.capacity_sectors();
    if (offset == 0) return static_cast<uint32_t>(capacity & 0xFFFFFFFFULL);
    if (offset == 4) return static_cast<uint32_t>((capacity >> 32) & 0xFFFFFFFFULL);
    if (offset == 36 || offset == 48) return virtio::BlockBackend::kMaxRangeSectors;
    if (offset == 40 || offset == 52) return 1;  // One range per request
    if (offset == 56) return 1;                  // write_zeroes_may_unmap
    return 0;
}

void VirtioPciBlock::on_queue_notify(uint16_t queue_index) {
    if (queue_index >= queues_.size()) return;
    auto& q = queues_[queue_index];
    core::Machine* machine = root_complex_ != nullptr ? root_complex_->machine() : nullptr;
    const bool defer_completion = machine != nullptr && machine->runtime_profile.is_cycle_mode() &&
                                  machine->dma_engine().is_enabled();

    auto dma_read_cb = [this](simrv::xlen::Address addr, void* buf, size_t size) {
        return dma_read(addr, buf, size);
    };
    auto dma_write_cb = [this](simrv::xlen::Address addr, const void* buf, size_t size) {
        return dma_write(addr, buf, size);
    };
    auto schedule_deferred = [this, machine, queue_index](size_t transfer_bytes,
                                                          uint16_t head_desc_idx,
                                                          uint32_t total_written) {
        const auto current_cycle = machine->memory().system_bus().cycle();
        machine->dma_engine().schedule_transfer(
            transfer_bytes, current_cycle,
            [this, queue_index, head_desc_idx, total_written]() {
                auto& q = queues_[queue_index];
                uint16_t used_idx = 0;
                dma_read(q.device_addr + 2, &used_idx, 2);
                virtio::VirtqUsedElem elem{head_desc_idx, total_written};
                dma_write(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem), &elem,
                          sizeof(elem));
                used_idx++;
                dma_write(q.device_addr + 2, &used_idx, 2);
                isr_status_ |= 0x1;
                trigger_irq();
            },
            "virtio-pci-block");
    };
    auto notify_complete = [this]() {
        isr_status_ |= 0x1;
        trigger_irq();
    };

    virtio::process_block_queue(q, backend_, io_buffer_, defer_completion, dma_read_cb,
                                dma_write_cb, schedule_deferred, notify_complete);
}

}  // namespace simrv::device
