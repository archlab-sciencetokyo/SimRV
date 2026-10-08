/** @file VirtioMmioFs.cpp */
#include "simrv/device/mmio/VirtioMmioFs.hpp"

namespace simrv::device {

VirtioMmioFs::VirtioMmioFs(Address base, uint32_t irq, core::Machine* machine,
                           const std::string& shared_directory, std::string tag)
    : VirtioMmioDevice("virtio-fs-mmio", base, 0x1000, virtio::kDevIdFs, irq, machine),
      backend_(std::move(tag)) {
    (void)backend_.set_shared_directory(shared_directory);
    add_queue(8);    // HIPRIO
    add_queue(128);  // REQUEST
}

auto VirtioMmioFs::read_device_config(Address offset, std::size_t size) -> uint64_t {
    uint64_t value = 0;
    for (std::size_t i = 0; i < size && i < 8; ++i) {
        const auto index = static_cast<size_t>(offset + i);
        const uint8_t byte = index < backend_.tag().size() ? backend_.tag()[index]
                             : index == 36                 ? 1
                                                           : 0;
        value |= static_cast<uint64_t>(byte) << (i * 8);
    }
    return value;
}

void VirtioMmioFs::on_queue_notify(uint32_t queue_index) {
    if (queue_index >= queues_.size()) return;
    backend_.process_queue(
        queues_[queue_index],
        [this](uint64_t address, void* data, size_t size) {
            return dma_read_bytes(address, reinterpret_cast<std::byte*>(data), size);
        },
        [this](uint64_t address, const void* data, size_t size) {
            return dma_write_bytes(address, reinterpret_cast<const std::byte*>(data), size);
        },
        [this]() { trigger_irq(); });
}

}  // namespace simrv::device
