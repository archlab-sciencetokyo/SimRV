/**
 * @file Rng.cpp
 * @brief VirtIO Hardware Random Number Generator (RNG) device implementation.
 */
#include "simrv/device/Rng.hpp"

#include <algorithm>
#include <random>

#include "simrv/core/Machine.hpp"
#include "simrv/device/Virtio.hpp"
#include "simrv/device/VirtioUtil.hpp"

namespace simrv::device {

Rng::Rng(simrv::core::Machine& machine)
    : VirtioDevice(machine, virtio::kRngIrq, virtio::kRngMaxQueueNum) {}

void Rng::process_queue(Word q_idx) {
    if (q_idx >= virtio::kRngMaxQueueNum) return;
    virtio::QueueState& qs = Queue[q_idx];
    if (qs.Ready == 0) return;
    const auto avail_idx = virtio_detail::read_struct_from_ram<uint16_t>(qs.AvailLow + 2, mmem);
    bool serviced = false;
    while (static_cast<uint16_t>(qs.last_avail_idx) != avail_idx) {
        const auto desc_idx = virtio_detail::next_avail_desc_idx(&qs, QueueNum, mmem);
        const auto desc_addr = virtio_detail::get_desc_addr(desc_idx, &qs);
        auto desc = virtio_detail::read_struct_from_ram<virtio::Descriptor>(desc_addr, mmem);

        std::independent_bits_engine<std::default_random_engine, 8, uint8_t> engine(rd_());
        for (Word i = 0; i < desc.len; ++i) {
            const uint8_t byte = engine();
            virtio_detail::store_to_ram(desc.adr + i, static_cast<Word>(byte), 1, mmem);
        }

        serviced = true;
        virtio_detail::update_descriptor(desc_idx, desc.len, static_cast<int>(QueueNum), &qs, mmem);
        qs.last_avail_idx++;
    }
    if (serviced) {
        trigger_interrupt();
    }
}

}  // namespace simrv::device
