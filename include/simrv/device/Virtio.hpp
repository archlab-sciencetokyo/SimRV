/**
 * @file Virtio.hpp
 * @brief Virtio device queue configuration and constants.
 */
#pragma once

#include <cstdint>

#include "simrv/xlen/Types.hpp"

#ifndef SIMRV_DISK_SIZE_MB
#define SIMRV_DISK_SIZE_MB 128
#endif

namespace simrv::virtio {
inline constexpr uint32_t kConsoleMaxQueueNum = 2;
inline constexpr uint32_t kConsoleIrq = 1;
inline constexpr uint32_t kDiskSectorSize = 512;
inline constexpr uint32_t kDiskBufferSize = (512u * 512u);
inline constexpr uint32_t kDiskSize = (SIMRV_DISK_SIZE_MB * 1024u * 1024u);
inline constexpr uint32_t kDiskMaxQueueNum = 4;
inline constexpr uint32_t kDiskIrq = 2;

inline constexpr Word kMagicValue = 0x74726976;  // "virt"
inline constexpr Word kVersion = 2;
inline constexpr Word kVendorId = 0xffff;

inline constexpr Word kDiskDeviceId = 2;
inline constexpr Word kDiskDeviceFeatures = 1;
inline constexpr Word kDiskConfigGeneration = 0;
inline constexpr Word kDiskQueueNumMax = 4;

enum class MmioOffset : Address {
    MagicValue = 0x000,
    Version = 0x004,
    DeviceId = 0x008,
    VendorId = 0x00c,
    DeviceFeatures = 0x010,
    DeviceFeaturesSel = 0x014,
    DriverFeatures = 0x020,
    DriverFeaturesSel = 0x024,
    QueueSel = 0x030,
    QueueNumMax = 0x034,
    QueueNum = 0x038,
    QueueReady = 0x044,
    QueueNotify = 0x050,
    InterruptStatus = 0x060,
    InterruptACK = 0x064,
    Status = 0x070,
    QueueDescLow = 0x080,
    QueueDescHigh = 0x084,
    QueueAvailLow = 0x090,
    QueueAvailHigh = 0x094,
    QueueUsedLow = 0x0a0,
    QueueUsedHigh = 0x0a4,
    ConfigGeneration = 0x0fc,
    Config = 0x100
};

struct QueueState {
    Word Ready;
    Word Notify;
    Address DescLow;
    Address DescHigh;
    Address AvailLow;
    Address AvailHigh;
    Address UsedLow;
    Address UsedHigh;
    Word last_avail_idx;
};

struct Descriptor {
    Counter adr;
    Word len;
    uint16_t flags;
    uint16_t next;
};

struct BlockRequestHeader {
    Word type;
    Word ioprio;
    Counter sector_num;
};

struct VirtqUsedElem {
    uint32_t id;
    uint32_t len;
};

}  // namespace simrv::virtio
