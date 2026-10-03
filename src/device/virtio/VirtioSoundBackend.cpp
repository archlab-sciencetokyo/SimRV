/**
 * @file VirtioSoundBackend.cpp
 * @brief Implementation of VirtIO 1.2 Sound control and PCM streams.
 */
#include "simrv/device/virtio/VirtioSoundBackend.hpp"

#include <algorithm>
#include <cstring>

namespace simrv::device::virtio {

void VirtioSoundBackend::process_controlq(QueueState& q, const DmaReadFn& dma_read,
                                          const DmaWriteFn& dma_write,
                                          const std::function<void()>& trigger_irq) {
    if (q.ready == 0 || q.driver_addr == 0 || q.device_addr == 0 || q.desc_addr == 0) return;

    uint16_t avail_idx = 0;
    if (!dma_read(q.driver_addr + 2, &avail_idx, 2)) return;

    bool processed_any = false;
    while (q.last_avail_idx != avail_idx) {
        const uint16_t ring_idx = q.last_avail_idx % q.num;
        uint16_t head_desc_idx = 0;
        if (!dma_read(q.driver_addr + 4 + ring_idx * 2, &head_desc_idx, 2)) break;

        VirtqDesc desc0{};
        if (!dma_read(q.desc_addr + head_desc_idx * sizeof(VirtqDesc), &desc0, sizeof(desc0)))
            break;

        VirtioSndHdr hdr{};
        if (desc0.len >= sizeof(hdr)) {
            dma_read(desc0.addr, &hdr, sizeof(hdr));
        }

        uint32_t written_len = 0;
        if ((desc0.flags & kVirtqDescFNext) != 0) {
            VirtqDesc desc1{};
            if (dma_read(q.desc_addr + desc0.next * sizeof(VirtqDesc), &desc1, sizeof(desc1))) {
                switch (hdr.code) {
                    case kVirtioSndRJackInfo: {
                        VirtioSndJackInfo info{};
                        info.connected = 1;
                        const uint32_t to_write = std::min<uint32_t>(desc1.len, sizeof(info));
                        dma_write(desc1.addr, &info, to_write);
                        written_len = to_write;
                        break;
                    }
                    case kVirtioSndRPcmInfo: {
                        VirtioSndPcmInfo info{};
                        info.direction = kVirtioSndDOutput;
                        info.channels_min = 1;
                        info.channels_max = 2;
                        info.formats = 1ULL << kVirtioSndPcmFmtS16;
                        info.rates =
                            (1ULL << kVirtioSndPcmRate44100) | (1ULL << kVirtioSndPcmRate48000);
                        const uint32_t to_write = std::min<uint32_t>(desc1.len, sizeof(info));
                        dma_write(desc1.addr, &info, to_write);
                        written_len = to_write;
                        break;
                    }
                    case kVirtioSndRChmapInfo: {
                        VirtioSndChmapInfo info{};
                        info.direction = kVirtioSndDOutput;
                        info.channels = 2;
                        info.positions[0] = kVirtioSndChmapFl;
                        info.positions[1] = kVirtioSndChmapFr;
                        const uint32_t to_write = std::min<uint32_t>(desc1.len, sizeof(info));
                        dma_write(desc1.addr, &info, to_write);
                        written_len = to_write;
                        break;
                    }
                    case kVirtioSndRPcmSetParams: {
                        VirtioSndPcmSetParams params{};
                        if (desc0.len >= sizeof(params)) {
                            dma_read(desc0.addr, &params, sizeof(params));
                        }
                        const uint32_t rate =
                            (params.rate == kVirtioSndPcmRate44100) ? 44100 : 48000;
                        const uint8_t ch =
                            std::max<uint8_t>(1, std::min<uint8_t>(2, params.channels));
                        HostAudioSink::instance().start(rate, ch);

                        VirtioSndHdr resp{.code = kVirtioSndSOk};
                        const uint32_t to_write = std::min<uint32_t>(desc1.len, sizeof(resp));
                        dma_write(desc1.addr, &resp, to_write);
                        written_len = to_write;
                        break;
                    }
                    case kVirtioSndRPcmPrepare:
                    case kVirtioSndRPcmStart: {
                        HostAudioSink::instance().start();
                        VirtioSndHdr resp{.code = kVirtioSndSOk};
                        const uint32_t to_write = std::min<uint32_t>(desc1.len, sizeof(resp));
                        dma_write(desc1.addr, &resp, to_write);
                        written_len = to_write;
                        break;
                    }
                    case kVirtioSndRPcmStop:
                    case kVirtioSndRPcmRelease: {
                        HostAudioSink::instance().stop();
                        VirtioSndHdr resp{.code = kVirtioSndSOk};
                        const uint32_t to_write = std::min<uint32_t>(desc1.len, sizeof(resp));
                        dma_write(desc1.addr, &resp, to_write);
                        written_len = to_write;
                        break;
                    }
                    default: {
                        VirtioSndHdr resp{.code = kVirtioSndSNotSupp};
                        const uint32_t to_write = std::min<uint32_t>(desc1.len, sizeof(resp));
                        dma_write(desc1.addr, &resp, to_write);
                        written_len = to_write;
                        break;
                    }
                }
            }
        }

        uint16_t used_idx = 0;
        dma_read(q.device_addr + 2, &used_idx, 2);
        VirtqUsedElem elem{head_desc_idx, written_len};
        dma_write(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem), &elem, sizeof(elem));
        used_idx++;
        dma_write(q.device_addr + 2, &used_idx, 2);

        q.last_avail_idx++;
        processed_any = true;
    }

    if (processed_any && trigger_irq) {
        trigger_irq();
    }
}

void VirtioSoundBackend::process_txq(QueueState& q, const DmaReadFn& dma_read,
                                     const DmaWriteFn& dma_write,
                                     const std::function<void()>& trigger_irq) {
    if (q.ready == 0 || q.driver_addr == 0 || q.device_addr == 0 || q.desc_addr == 0) return;

    uint16_t avail_idx = 0;
    if (!dma_read(q.driver_addr + 2, &avail_idx, 2)) return;

    bool processed_any = false;
    while (q.last_avail_idx != avail_idx) {
        const uint16_t ring_idx = q.last_avail_idx % q.num;
        uint16_t head_desc_idx = 0;
        if (!dma_read(q.driver_addr + 4 + ring_idx * 2, &head_desc_idx, 2)) break;

        uint16_t cur_desc_idx = head_desc_idx;
        VirtqDesc desc{};
        std::vector<uint8_t> pcm_data;
        uint64_t status_addr = 0;
        uint32_t status_len = 0;
        bool is_first = true;

        while (true) {
            if (!dma_read(q.desc_addr + cur_desc_idx * sizeof(VirtqDesc), &desc, sizeof(desc))) {
                break;
            }

            if ((desc.flags & kVirtqDescFWrite) != 0) {
                status_addr = desc.addr;
                status_len = desc.len;
            } else {
                if (is_first) {
                    is_first = false;
                    if (desc.len > sizeof(VirtioSndPcmXfer)) {
                        const std::size_t sample_bytes = desc.len - sizeof(VirtioSndPcmXfer);
                        const std::size_t offset = pcm_data.size();
                        pcm_data.resize(offset + sample_bytes);
                        dma_read(desc.addr + sizeof(VirtioSndPcmXfer), pcm_data.data() + offset,
                                 sample_bytes);
                    }
                } else {
                    const std::size_t offset = pcm_data.size();
                    pcm_data.resize(offset + desc.len);
                    dma_read(desc.addr, pcm_data.data() + offset, desc.len);
                }
            }

            if ((desc.flags & kVirtqDescFNext) == 0) break;
            cur_desc_idx = desc.next;
        }

        if (!pcm_data.empty()) {
            HostAudioSink::instance().write_samples(pcm_data.data(), pcm_data.size());
        }

        if (status_addr != 0 && status_len >= sizeof(VirtioSndPcmStatus)) {
            VirtioSndPcmStatus st{
                .status = kVirtioSndSOk,
                .latency_bytes = 0,
            };
            dma_write(status_addr, &st, sizeof(st));
        }

        uint16_t used_idx = 0;
        dma_read(q.device_addr + 2, &used_idx, 2);
        VirtqUsedElem elem{head_desc_idx, status_len};
        dma_write(q.device_addr + 4 + (used_idx % q.num) * sizeof(elem), &elem, sizeof(elem));
        used_idx++;
        dma_write(q.device_addr + 2, &used_idx, 2);

        q.last_avail_idx++;
        processed_any = true;
    }

    if (processed_any && trigger_irq) {
        trigger_irq();
    }
}

}  // namespace simrv::device::virtio
