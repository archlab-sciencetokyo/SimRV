#pragma once

#include <cstdint>

namespace simrv::device::virtio {

inline constexpr uint16_t kVirtioSndVqControl = 0;
inline constexpr uint16_t kVirtioSndVqTx = 1;

inline constexpr uint32_t kVirtioSndRJackInfo = 0x0100;
inline constexpr uint32_t kVirtioSndRPcmInfo = 0x0101;
inline constexpr uint32_t kVirtioSndRChmapInfo = 0x0102;
inline constexpr uint32_t kVirtioSndRPcmSetParams = 0x0120;
inline constexpr uint32_t kVirtioSndRPcmPrepare = 0x0121;
inline constexpr uint32_t kVirtioSndRPcmStart = 0x0122;
inline constexpr uint32_t kVirtioSndRPcmStop = 0x0123;
inline constexpr uint32_t kVirtioSndRPcmRelease = 0x0124;
inline constexpr uint32_t kVirtioSndSOk = 0;
inline constexpr uint32_t kVirtioSndSNotSupp = 0x8001;
inline constexpr uint32_t kVirtioSndDOutput = 0;
inline constexpr uint32_t kVirtioSndPcmFmtS16 = 0;
inline constexpr uint32_t kVirtioSndPcmRate44100 = 7;
inline constexpr uint32_t kVirtioSndPcmRate48000 = 10;
inline constexpr uint8_t kVirtioSndChmapFl = 3;
inline constexpr uint8_t kVirtioSndChmapFr = 4;

struct VirtioSndHdr {
    uint32_t code;
};

struct VirtioSndPcmHdr {
    VirtioSndHdr hdr;
    uint32_t stream_id;
};

struct VirtioSndQueryInfo {
    VirtioSndHdr hdr;
    uint32_t start_id;
    uint32_t count;
    uint32_t size;
};

struct VirtioSndJackInfo {
    uint32_t hda_fn_nid;
    uint32_t features;
    uint32_t hda_reg_base;
    uint8_t connected;
    uint8_t padding[7];
    char name[64];
};

struct VirtioSndPcmInfo {
    uint32_t hda_fn_nid;
    uint32_t features;
    uint8_t direction;
    uint8_t channels_min;
    uint8_t channels_max;
    uint8_t padding[5];
    uint64_t formats;
    uint64_t rates;
    char name[64];
};

struct VirtioSndChmapInfo {
    uint32_t direction;
    uint8_t channels;
    uint8_t positions[18];
};

struct VirtioSndPcmSetParams {
    VirtioSndPcmHdr hdr;
    uint32_t buffer_bytes;
    uint32_t period_bytes;
    uint32_t features;
    uint8_t channels;
    uint8_t format;
    uint8_t rate;
    uint8_t padding;
};

struct VirtioSndPcmXfer {
    uint32_t stream_id;
};

struct VirtioSndPcmStatus {
    uint32_t status;
    uint32_t latency_bytes;
};

struct VirtioSndConfig {
    uint32_t jacks;
    uint32_t streams;
    uint32_t chmaps;
    uint32_t controls;
};

}  // namespace simrv::device::virtio
