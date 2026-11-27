/**
 * @file VirtioGpuQueue.hpp
 * @brief Software implementation of the VirtIO GPU 2D control queue.
 */
#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "simrv/core/Machine.hpp"
#include "simrv/device/virtio/VirtioCore.hpp"

namespace simrv::device::virtio {

inline constexpr uint32_t kGpuCmdGetDisplayInfo = 0x0100;
inline constexpr uint32_t kGpuCmdResourceCreate2d = 0x0101;
inline constexpr uint32_t kGpuCmdResourceUnref = 0x0102;
inline constexpr uint32_t kGpuCmdSetScanout = 0x0103;
inline constexpr uint32_t kGpuCmdResourceFlush = 0x0104;
inline constexpr uint32_t kGpuCmdTransferToHost2d = 0x0105;
inline constexpr uint32_t kGpuCmdResourceAttachBacking = 0x0106;
inline constexpr uint32_t kGpuCmdResourceDetachBacking = 0x0107;
inline constexpr uint32_t kGpuRespOkNoData = 0x1100;
inline constexpr uint32_t kGpuRespOkDisplayInfo = 0x1101;
inline constexpr uint32_t kGpuRespErrUnspec = 0x1200;
inline constexpr uint32_t kGpuRespErrInvalidResourceId = 0x1203;
inline constexpr uint32_t kGpuFormatB8G8R8A8Unorm = 1;
inline constexpr uint32_t kGpuFormatB8G8R8X8Unorm = 2;

#pragma pack(push, 1)
struct VirtioGpuCtrlHeader {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint8_t padding[4];
};
struct VirtioGpuRect {
    uint32_t x, y, width, height;
};
struct VirtioGpuDisplayOne {
    VirtioGpuRect rect;
    uint32_t enabled;
    uint32_t flags;
};
struct VirtioGpuDisplayInfo {
    VirtioGpuCtrlHeader hdr;
    VirtioGpuDisplayOne pmodes[16];
};
struct VirtioGpuResourceCreate2d {
    VirtioGpuCtrlHeader hdr;
    uint32_t resource_id, format, width, height;
};
struct VirtioGpuResourceId {
    VirtioGpuCtrlHeader hdr;
    uint32_t resource_id;
};
struct VirtioGpuSetScanout {
    VirtioGpuCtrlHeader hdr;
    VirtioGpuRect rect;
    uint32_t scanout_id, resource_id;
};
struct VirtioGpuTransferToHost2d {
    VirtioGpuCtrlHeader hdr;
    VirtioGpuRect rect;
    uint64_t offset;
    uint32_t resource_id, padding;
};
struct VirtioGpuMemEntry {
    uint64_t addr;
    uint32_t length, padding;
};
struct VirtioGpuAttachBacking {
    VirtioGpuCtrlHeader hdr;
    uint32_t resource_id, nr_entries;
};
struct VirtioGpuResp {
    uint32_t type;
    uint32_t flags;
    uint64_t fence_id;
    uint32_t ctx_id;
    uint8_t padding[4];
};
#pragma pack(pop)

struct VirtioGpuResource {
    uint32_t width{0}, height{0}, format{0};
    std::vector<VirtioGpuMemEntry> backing;
};
struct VirtioGpuState {
    std::unordered_map<uint32_t, VirtioGpuResource> resources;
    uint32_t scanout_resource{0};
    VirtioGpuRect scanout{};
};

inline auto gpu_read_backing(const VirtioGpuResource& resource, uint64_t offset, std::byte* output,
                             size_t length, auto&& dma_read) -> bool {
    size_t copied = 0;
    uint64_t skip = offset;
    for (const auto& entry : resource.backing) {
        if (skip >= entry.length) {
            skip -= entry.length;
            continue;
        }
        const size_t count = std::min<size_t>(length - copied, entry.length - skip);
        if (!dma_read(entry.addr + skip, output + copied, count)) return false;
        copied += count;
        skip = 0;
        if (copied == length) return true;
    }
    return copied == length;
}

template <typename DmaRead, typename DmaWrite, typename NotifyComplete>
void process_gpu_queue(QueueState& q, VirtioGpuState& state, core::Machine* machine,
                       DmaRead&& dma_read, DmaWrite&& dma_write, NotifyComplete&& notify_complete) {
    if (!q.usable()) return;
    uint16_t avail_idx = 0;
    if (!dma_read(q.driver_addr + 2, &avail_idx, sizeof(avail_idx))) return;
    bool processed_any = false;
    while (q.last_avail_idx != avail_idx) {
        const uint16_t ring_slot = q.last_avail_idx % q.num;
        uint16_t desc_idx = 0;
        if (!dma_read(q.driver_addr + 4 + ring_slot * 2, &desc_idx, sizeof(desc_idx)) ||
            desc_idx >= q.num)
            break;
        const uint16_t head_desc_idx = desc_idx;
        std::vector<std::byte> request;
        std::vector<VirtqDesc> writable;
        uint32_t total_written = 0;
        bool malformed = false;
        for (uint16_t n = 0; n < q.num; ++n) {
            if (desc_idx >= q.num) {
                malformed = true;
                break;
            }
            VirtqDesc desc{};
            if (!dma_read(q.desc_addr + desc_idx * sizeof(desc), &desc, sizeof(desc))) {
                malformed = true;
                break;
            }
            if ((desc.flags & kVirtqDescFWrite) != 0)
                writable.push_back(desc);
            else {
                if (desc.len > 1024 * 1024 || request.size() > 1024 * 1024 - desc.len) {
                    malformed = true;
                    break;
                }
                const auto old = request.size();
                request.resize(old + desc.len);
                if (desc.len && !dma_read(desc.addr, request.data() + old, desc.len)) {
                    malformed = true;
                    break;
                }
            }
            if ((desc.flags & kVirtqDescFNext) == 0) break;
            desc_idx = desc.next;
            if (n + 1 == q.num) malformed = true;
        }
        VirtioGpuResp resp{kGpuRespErrUnspec, 0, 0, 0, {0, 0, 0, 0}};
        if (!malformed && request.size() >= sizeof(VirtioGpuCtrlHeader)) {
            VirtioGpuCtrlHeader hdr{};
            std::memcpy(&hdr, request.data(), sizeof(hdr));
            resp.fence_id = hdr.fence_id;
            auto has = [&](size_t n) { return request.size() >= n; };
            switch (hdr.type) {
                case kGpuCmdGetDisplayInfo: {
                    VirtioGpuDisplayInfo info{};
                    info.hdr = {kGpuRespOkDisplayInfo, 0, hdr.fence_id, 0, {0, 0, 0, 0}};
                    if (machine) {
                        const auto geom = machine->framebuffer_geometry();
                        info.pmodes[0] = {{0, 0, geom.width, geom.height}, 1, 0};
                    }
                    std::memcpy(&resp, &info, sizeof(resp));
                    if (!writable.empty()) {
                        auto& out = writable.front();
                        const size_t len = std::min<size_t>(out.len, sizeof(info));
                        if (len && dma_write(out.addr, &info, len))
                            total_written = static_cast<uint32_t>(len);
                    }
                    goto response_written;
                }
                case kGpuCmdResourceCreate2d: {
                    if (!has(sizeof(VirtioGpuResourceCreate2d))) break;
                    VirtioGpuResourceCreate2d cmd{};
                    std::memcpy(&cmd, request.data(), sizeof(cmd));
                    if (!cmd.resource_id || !cmd.width || !cmd.height ||
                        (cmd.format != kGpuFormatB8G8R8A8Unorm &&
                         cmd.format != kGpuFormatB8G8R8X8Unorm) ||
                        uint64_t{cmd.width} * cmd.height * 4 > 256ULL * 1024 * 1024)
                        break;
                    state.resources[cmd.resource_id] = {cmd.width, cmd.height, cmd.format, {}};
                    resp.type = kGpuRespOkNoData;
                    break;
                }
                case kGpuCmdResourceUnref:
                case kGpuCmdResourceDetachBacking: {
                    if (!has(sizeof(VirtioGpuResourceId))) break;
                    VirtioGpuResourceId cmd{};
                    std::memcpy(&cmd, request.data(), sizeof(cmd));
                    auto it = state.resources.find(cmd.resource_id);
                    if (it == state.resources.end()) {
                        resp.type = kGpuRespErrInvalidResourceId;
                        break;
                    }
                    if (hdr.type == kGpuCmdResourceUnref)
                        state.resources.erase(it);
                    else
                        it->second.backing.clear();
                    if (state.scanout_resource == cmd.resource_id &&
                        hdr.type == kGpuCmdResourceUnref)
                        state.scanout_resource = 0;
                    resp.type = kGpuRespOkNoData;
                    break;
                }
                case kGpuCmdResourceAttachBacking: {
                    if (!has(sizeof(VirtioGpuAttachBacking))) break;
                    VirtioGpuAttachBacking cmd{};
                    std::memcpy(&cmd, request.data(), sizeof(cmd));
                    auto it = state.resources.find(cmd.resource_id);
                    const size_t bytes =
                        sizeof(cmd) + size_t{cmd.nr_entries} * sizeof(VirtioGpuMemEntry);
                    if (it == state.resources.end()) {
                        resp.type = kGpuRespErrInvalidResourceId;
                        break;
                    }
                    if (!cmd.nr_entries || cmd.nr_entries > 4096 || !has(bytes)) break;
                    it->second.backing.resize(cmd.nr_entries);
                    std::memcpy(it->second.backing.data(), request.data() + sizeof(cmd),
                                cmd.nr_entries * sizeof(VirtioGpuMemEntry));
                    resp.type = kGpuRespOkNoData;
                    break;
                }
                case kGpuCmdSetScanout: {
                    if (!has(sizeof(VirtioGpuSetScanout))) break;
                    VirtioGpuSetScanout cmd{};
                    std::memcpy(&cmd, request.data(), sizeof(cmd));
                    if (cmd.scanout_id != 0) break;
                    if (cmd.resource_id && !state.resources.contains(cmd.resource_id)) {
                        resp.type = kGpuRespErrInvalidResourceId;
                        break;
                    }
                    state.scanout_resource = cmd.resource_id;
                    state.scanout = cmd.rect;
                    resp.type = kGpuRespOkNoData;
                    break;
                }
                case kGpuCmdTransferToHost2d: {
                    if (!has(sizeof(VirtioGpuTransferToHost2d))) break;
                    VirtioGpuTransferToHost2d cmd{};
                    std::memcpy(&cmd, request.data(), sizeof(cmd));
                    auto it = state.resources.find(cmd.resource_id);
                    if (it == state.resources.end()) {
                        resp.type = kGpuRespErrInvalidResourceId;
                        break;
                    }
                    auto& res = it->second;
                    const uint64_t needed =
                        cmd.offset + (uint64_t{cmd.rect.y + cmd.rect.height} * res.width +
                                      cmd.rect.x + cmd.rect.width) *
                                         4;
                    if (!cmd.rect.width || !cmd.rect.height ||
                        cmd.rect.x + uint64_t{cmd.rect.width} > res.width ||
                        cmd.rect.y + uint64_t{cmd.rect.height} > res.height ||
                        needed > 256ULL * 1024 * 1024)
                        break;
                    const size_t row_size = size_t{cmd.rect.width} * 4;
                    std::vector<std::byte> row(row_size);
                    auto fb = machine ? machine->framebuffer_view() : memory::RamView{};
                    const auto geom =
                        machine ? machine->framebuffer_geometry() : core::FramebufferConfig{};
                    if (cmd.resource_id == state.scanout_resource && fb.data() && geom.bpp == 4) {
                        for (uint32_t y = 0; y < cmd.rect.height; ++y) {
                            const uint64_t src_off =
                                cmd.offset +
                                (uint64_t{cmd.rect.y + y} * res.width + cmd.rect.x) * 4;
                            if (!gpu_read_backing(res, src_off, row.data(), row_size, dma_read)) {
                                malformed = true;
                                break;
                            }
                            const uint32_t dx = state.scanout.x + cmd.rect.x,
                                           dy = state.scanout.y + cmd.rect.y + y;
                            if (dx >= geom.width || dy >= geom.height) continue;
                            const size_t count =
                                std::min<size_t>(row_size, size_t{geom.width - dx} * 4);
                            std::memcpy(
                                fb.data() + size_t{dy} * geom.stride_bytes() + size_t{dx} * 4,
                                row.data(), count);
                        }
                    }
                    resp.type = kGpuRespOkNoData;
                    break;
                }
                case kGpuCmdResourceFlush:
                    resp.type = kGpuRespOkNoData;
                    break;
                default:
                    break;
            }
        }
        if (!writable.empty()) {
            const auto& out = writable.front();
            if (out.len >= sizeof(resp) && dma_write(out.addr, &resp, sizeof(resp)))
                total_written = sizeof(resp);
        }
    response_written:
        uint16_t used = 0;
        (void)dma_read(q.device_addr + 2, &used, sizeof(used));
        const VirtqUsedElem elem{.id = static_cast<uint32_t>(head_desc_idx), .len = total_written};
        (void)dma_write(q.device_addr + 4 + (used % q.num) * sizeof(elem), &elem, sizeof(elem));
        ++used;
        (void)dma_write(q.device_addr + 2, &used, sizeof(used));
        ++q.last_avail_idx;
        processed_any = true;
    }
    if (processed_any) notify_complete();
}

}  // namespace simrv::device::virtio
