/**
 * @file Protocol.hpp
 * @brief Wire protocol framing, packet layouts, and channel definitions for SimRV IPC.
 */
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <string_view>

namespace simrv::net {

constexpr uint16_t kProtocolMagic = 0x5352;  // 'S', 'R'

enum class ChannelId : uint8_t {
    Control = 0x00,
    Console = 0x01,
    Telemetry = 0x02,
};

namespace FrameFlags {
constexpr uint8_t None = 0x00;
constexpr uint8_t Urgent = 0x01;
constexpr uint8_t Compressed = 0x02;
}  // namespace FrameFlags

#pragma pack(push, 1)
struct FrameHeader {
    uint16_t magic{kProtocolMagic};
    uint8_t channel{0};
    uint8_t flags{0};
    uint32_t payload_len{0};
};

struct TelemetryPacket {
    uint64_t mcycle{0};
    uint64_t mtime{0};
    uint64_t retired_icount{0};
    uint64_t pc{0};
    std::array<uint64_t, 32> gpr{};
    std::array<uint64_t, 32> fpr{};
    std::array<uint64_t, 16> csrs{};
    std::array<uint32_t, 5> pipeline_stages{};
    uint32_t mips_rate{0};
    uint32_t hart_id{0};
    uint32_t sequence{0};
};
#pragma pack(pop)

static_assert(sizeof(FrameHeader) == 8, "FrameHeader must be exactly 8 bytes");

}  // namespace simrv::net
