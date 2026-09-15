/**
 * @file Protocol.hpp
 * @brief Wire protocol framing, packet layouts, and channel definitions for SimRV IPC.
 */
#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace simrv::net {

constexpr uint16_t kProtocolMagic = 0x5352;  // 'S', 'R'

enum class ChannelId : uint8_t {
    Control = 0x00,
    Console = 0x01,
    Telemetry = 0x02,
    Hello = 0x03,
    Reply = 0x04,
    Terminal = 0x05,
    Lifecycle = 0x06,
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

// Version 2 uses bounded little-endian fields, never host object layouts.
namespace simrv::net {
inline constexpr uint64_t kProtocolVersion = 2;
inline constexpr size_t kMaxPayload = 1024 * 1024;
inline void put_u64(std::vector<uint8_t>& out, uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) out.push_back(static_cast<uint8_t>(value >> (i * 8)));
}
inline auto get_u64(std::span<const uint8_t>& input) -> uint64_t {
    if (input.size() < 8) throw std::runtime_error("truncated protocol field");
    uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= uint64_t(input[i]) << (i * 8);
    input = input.subspan(8);
    return value;
}
inline auto make_frame(ChannelId channel, std::span<const uint8_t> payload)
    -> std::vector<uint8_t> {
    if (payload.size() > kMaxPayload) throw std::runtime_error("oversized frame");
    std::vector<uint8_t> out{0x52, 0x53, static_cast<uint8_t>(channel), 0};
    for (unsigned i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(payload.size() >> (i * 8)));
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}
inline bool take_frame(std::vector<uint8_t>& input, ChannelId& channel,
                       std::vector<uint8_t>& payload) {
    if (input.size() < 8) return false;
    if (input[0] != 0x52 || input[1] != 0x53 || input[3] != 0)
        throw std::runtime_error("invalid frame header");
    size_t size = 0;
    for (unsigned i = 0; i < 4; ++i) size |= size_t(input[4 + i]) << (i * 8);
    if (size > kMaxPayload) throw std::runtime_error("oversized frame");
    if (input.size() < size + 8) return false;
    channel = static_cast<ChannelId>(input[2]);
    payload.assign(input.begin() + 8, input.begin() + 8 + size);
    input.erase(input.begin(), input.begin() + 8 + size);
    return true;
}
}  // namespace simrv::net
