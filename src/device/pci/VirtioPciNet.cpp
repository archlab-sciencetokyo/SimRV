/**
 * @file VirtioPciNet.cpp
 * @brief Implementation of VirtIO-PCI Network Adapter Endpoint.
 */
#include "simrv/device/pci/VirtioPciNet.hpp"

#include <cstring>
#include <vector>

namespace simrv::device {

// VirtIO Net Header (10 or 12 bytes)
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

VirtioPciNet::VirtioPciNet(virtio::NetBackend::Mode mode)
    : VirtioPciDevice(virtio::kDevIdNet, 0x020000, 2), backend_(mode) {}

auto VirtioPciNet::get_device_features(uint32_t select) -> uint32_t {
    if (select == 0) {
        return static_cast<uint32_t>(virtio::kVirtioNetFMac | virtio::kVirtioNetFStatus);
    }
    if (select == 1) {
        return (1U << 0);  // VIRTIO_F_VERSION_1
    }
    return 0;
}

auto VirtioPciNet::read_device_config(Address offset, uint8_t size) -> uint32_t {
    (void)size;
    const auto& mac = backend_.get_mac();
    if (offset == 0) {
        return mac[0] | (static_cast<uint32_t>(mac[1]) << 8) |
               (static_cast<uint32_t>(mac[2]) << 16) | (static_cast<uint32_t>(mac[3]) << 24);
    }
    if (offset == 4) {
        return mac[4] | (static_cast<uint32_t>(mac[5]) << 8) | (1U << 16);  // Link up
    }
    return 0;
}

void VirtioPciNet::on_queue_notify(uint16_t queue_index) {
    if (queue_index >= queues_.size()) return;
    auto& q = queues_[queue_index];
    if (!q.ready || !q.driver_addr || !q.device_addr || !q.desc_addr) return;

    uint16_t avail_idx = 0;
    if (!dma_read(q.driver_addr + 2, &avail_idx, 2)) return;

    bool processed_any = false;
    if (queue_index == 0) {  // RX Queue (receive packets from backend)
        while (q.last_avail_idx != avail_idx && backend_.has_rx_packet()) {
            const uint16_t ring_idx = q.last_avail_idx % q.num;
            uint16_t head_desc_idx = 0;
            if (!dma_read(q.driver_addr + 4 + ring_idx * 2, &head_desc_idx, 2)) break;

            virtio::VirtqDesc desc{};
            if (!dma_read(q.desc_addr + head_desc_idx * sizeof(virtio::VirtqDesc), &desc,
                          sizeof(desc)))
                break;

            auto pkt = backend_.pop_rx_packet();
            VirtioNetHdr hdr{};
            uint32_t written = 0;

            if (desc.len >= sizeof(hdr)) {
                dma_write(desc.addr, &hdr, sizeof(hdr));
                written += sizeof(hdr);
                uint32_t payload_len = static_cast<uint32_t>(
                    std::min(static_cast<size_t>(desc.len - sizeof(hdr)), pkt.size()));
                if (payload_len > 0) {
                    dma_write(desc.addr + sizeof(hdr), pkt.data(), payload_len);
                    written += payload_len;
                }
            }

            uint16_t used_idx = 0;
            dma_read(q.device_addr + 2, &used_idx, 2);
            virtio::VirtqUsedElem elem{head_desc_idx, written};
            dma_write(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem), &elem, sizeof(elem));
            used_idx++;
            dma_write(q.device_addr + 2, &used_idx, 2);

            q.last_avail_idx++;
            processed_any = true;
        }
    } else if (queue_index == 1) {  // TX Queue (send packet out)
        while (q.last_avail_idx != avail_idx) {
            const uint16_t ring_idx = q.last_avail_idx % q.num;
            uint16_t head_desc_idx = 0;
            if (!dma_read(q.driver_addr + 4 + ring_idx * 2, &head_desc_idx, 2)) break;

            virtio::VirtqDesc desc{};
            if (!dma_read(q.desc_addr + head_desc_idx * sizeof(virtio::VirtqDesc), &desc,
                          sizeof(desc)))
                break;

            if (desc.len > sizeof(VirtioNetHdr)) {
                std::vector<uint8_t> pkt(desc.len - sizeof(VirtioNetHdr));
                dma_read(desc.addr + sizeof(VirtioNetHdr), pkt.data(), pkt.size());
                backend_.send_tx_packet(pkt.data(), pkt.size());
            }

            uint16_t used_idx = 0;
            dma_read(q.device_addr + 2, &used_idx, 2);
            virtio::VirtqUsedElem elem{head_desc_idx, desc.len};
            dma_write(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem), &elem, sizeof(elem));
            used_idx++;
            dma_write(q.device_addr + 2, &used_idx, 2);

            q.last_avail_idx++;
            processed_any = true;
        }
    }

    if (processed_any) {
        isr_status_ |= 0x1;
        trigger_irq();
    }
}

}  // namespace simrv::device
