/**
 * @file VirtioMmioNet.cpp
 * @brief Implementation of VirtIO-MMIO v2 Network Adapter Endpoint.
 */
#include "simrv/device/mmio/VirtioMmioNet.hpp"

#include <cstring>
#include <vector>

#include "simrv/core/Machine.hpp"

namespace simrv::device {

// VirtIO Net Header
#pragma pack(push, 1)
struct VirtioNetHdr {
    uint8_t flags{0};
    uint8_t gso_type{0};
    uint16_t hdr_len{0};
    uint16_t gso_size{0};
    uint16_t csum_start{0};
    uint16_t csum_offset{0};
    uint16_t num_buffers{1};
};
#pragma pack(pop)

VirtioMmioNet::VirtioMmioNet(Address base_address, uint32_t irq_num, core::Machine* machine,
                             virtio::NetBackend::Mode mode)
    : VirtioMmioDevice("virtio-net-mmio", base_address, 0x1000, virtio::kDevIdNet, irq_num,
                       machine),
      backend_(mode) {
    add_queue(64);  // rx
    add_queue(64);  // tx
}

void VirtioMmioNet::poll_backend() {
    if (backend_.poll_host_rx() != 0) on_queue_notify(0);
}

auto VirtioMmioNet::read_device_config(Address offset, std::size_t size) -> uint64_t {
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
    if (q.ready == 0 || q.driver_addr == 0 || q.device_addr == 0 || q.desc_addr == 0) return;

    uint16_t avail_idx = 0;
    if (!dma_read_bytes(q.driver_addr + 2, reinterpret_cast<std::byte*>(&avail_idx), 2)) return;

    const bool defer_completion = machine_ != nullptr &&
                                  machine_->runtime_profile.is_cycle_mode() &&
                                  machine_->dma_engine().is_enabled();

    bool processed_any = false;
    if (q_idx == 0) {  // RX
        while (q.last_avail_idx != avail_idx && backend_.has_rx_packet()) {
            const uint16_t ring_idx = q.last_avail_idx % q.num;
            uint16_t head_desc_idx = 0;
            if (!dma_read_bytes(q.driver_addr + 4 + ring_idx * 2,
                                reinterpret_cast<std::byte*>(&head_desc_idx), 2))
                break;

            virtio::VirtqDesc desc{};
            if (!dma_read_bytes(q.desc_addr + head_desc_idx * sizeof(virtio::VirtqDesc),
                                reinterpret_cast<std::byte*>(&desc), sizeof(desc)))
                break;

            auto pkt = backend_.pop_rx_packet();
            VirtioNetHdr hdr{};
            uint32_t written = 0;

            if (desc.len >= sizeof(hdr)) {
                dma_write_bytes(desc.addr, reinterpret_cast<std::byte*>(&hdr), sizeof(hdr));
                written += sizeof(hdr);
                uint32_t payload_len = static_cast<uint32_t>(
                    std::min(static_cast<size_t>(desc.len - sizeof(hdr)), pkt.size()));
                if (payload_len > 0) {
                    dma_write_bytes(desc.addr + sizeof(hdr),
                                    reinterpret_cast<const std::byte*>(pkt.data()), payload_len);
                    written += payload_len;
                }
            }

            if (defer_completion) {
                const auto current_cycle = machine_->memory().system_bus().cycle();
                machine_->dma_engine().schedule_transfer(
                    written, current_cycle, [this, q_idx, head_desc_idx, written]() {
                        auto& q = queues_[q_idx];
                        uint16_t used_idx = 0;
                        dma_read_bytes(q.device_addr + 2, reinterpret_cast<std::byte*>(&used_idx),
                                       2);
                        virtio::VirtqUsedElem elem{head_desc_idx, written};
                        dma_write_bytes(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem),
                                        reinterpret_cast<std::byte*>(&elem), sizeof(elem));
                        used_idx++;
                        dma_write_bytes(q.device_addr + 2, reinterpret_cast<std::byte*>(&used_idx),
                                        2);
                        trigger_irq();
                    });
            } else {
                uint16_t used_idx = 0;
                dma_read_bytes(q.device_addr + 2, reinterpret_cast<std::byte*>(&used_idx), 2);
                virtio::VirtqUsedElem elem{head_desc_idx, written};
                dma_write_bytes(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem),
                                reinterpret_cast<std::byte*>(&elem), sizeof(elem));
                used_idx++;
                dma_write_bytes(q.device_addr + 2, reinterpret_cast<std::byte*>(&used_idx), 2);
                processed_any = true;
            }

            q.last_avail_idx++;
        }
    } else if (q_idx == 1) {  // TX
        while (q.last_avail_idx != avail_idx) {
            const uint16_t ring_idx = q.last_avail_idx % q.num;
            uint16_t head_desc_idx = 0;
            if (!dma_read_bytes(q.driver_addr + 4 + ring_idx * 2,
                                reinterpret_cast<std::byte*>(&head_desc_idx), 2))
                break;

            virtio::VirtqDesc desc{};
            if (!dma_read_bytes(q.desc_addr + head_desc_idx * sizeof(virtio::VirtqDesc),
                                reinterpret_cast<std::byte*>(&desc), sizeof(desc)))
                break;

            if (desc.len > sizeof(VirtioNetHdr)) {
                std::vector<uint8_t> pkt(desc.len - sizeof(VirtioNetHdr));
                dma_read_bytes(desc.addr + sizeof(VirtioNetHdr),
                               reinterpret_cast<std::byte*>(pkt.data()), pkt.size());
                backend_.send_tx_packet(pkt.data(), pkt.size());
            }

            if (defer_completion) {
                const auto current_cycle = machine_->memory().system_bus().cycle();
                const auto len = desc.len;
                machine_->dma_engine().schedule_transfer(
                    len, current_cycle, [this, q_idx, head_desc_idx, len]() {
                        auto& q = queues_[q_idx];
                        uint16_t used_idx = 0;
                        dma_read_bytes(q.device_addr + 2, reinterpret_cast<std::byte*>(&used_idx),
                                       2);
                        virtio::VirtqUsedElem elem{head_desc_idx, len};
                        dma_write_bytes(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem),
                                        reinterpret_cast<std::byte*>(&elem), sizeof(elem));
                        used_idx++;
                        dma_write_bytes(q.device_addr + 2, reinterpret_cast<std::byte*>(&used_idx),
                                        2);
                        trigger_irq();
                    });
            } else {
                uint16_t used_idx = 0;
                dma_read_bytes(q.device_addr + 2, reinterpret_cast<std::byte*>(&used_idx), 2);
                virtio::VirtqUsedElem elem{head_desc_idx, desc.len};
                dma_write_bytes(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem),
                                reinterpret_cast<std::byte*>(&elem), sizeof(elem));
                used_idx++;
                dma_write_bytes(q.device_addr + 2, reinterpret_cast<std::byte*>(&used_idx), 2);
                processed_any = true;
            }

            q.last_avail_idx++;
        }
    }

    if (!defer_completion && processed_any) {
        trigger_irq();
    }
}

}  // namespace simrv::device
