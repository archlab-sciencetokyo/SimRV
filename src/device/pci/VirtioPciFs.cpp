/** @file VirtioPciFs.cpp */
#include "simrv/device/pci/VirtioPciFs.hpp"

#include <algorithm>

namespace simrv::device {

VirtioPciFs::VirtioPciFs(const std::string& shared_directory, std::string tag)
    : VirtioPciDevice(virtio::kDevIdFs, 0x010000, 2), backend_(std::move(tag)) {
    (void)backend_.set_shared_directory(shared_directory);
    queues_[0].num_max = queues_[0].num = 8;    // HIPRIO
    queues_[1].num_max = queues_[1].num = 128;  // REQUEST
}

auto VirtioPciFs::get_device_features(uint32_t select) -> uint32_t {
    return select == 1 ? 1U : 0U;  // VIRTIO_F_VERSION_1
}

auto VirtioPciFs::read_device_config(Address offset, uint8_t size) -> uint32_t {
    uint32_t value = 0;
    for (uint8_t i = 0; i < size && i < 4; ++i) {
        const auto index = static_cast<size_t>(offset + i);
        const uint8_t byte = index < backend_.tag().size() ? backend_.tag()[index]
                             : index == 36                 ? 1
                                                           : 0;
        value |= static_cast<uint32_t>(byte) << (i * 8);
    }
    return value;
}

void VirtioPciFs::on_queue_notify(uint16_t queue_index) {
    if (queue_index >= queues_.size()) return;
    backend_.process_queue(
        queues_[queue_index],
        [this](uint64_t address, void* data, size_t size) { return dma_read(address, data, size); },
        [this](uint64_t address, const void* data, size_t size) {
            return dma_write(address, data, size);
        },
        [this]() {
            isr_status_ |= 1;
            trigger_irq();
        });
}

}  // namespace simrv::device
