/**
 * @file Bus.hpp
 * @brief TileLink-style shared bus abstraction for memory/device access.
 */
#pragma once

#include <cstdint>

#include "simrv/xlen/Types.hpp"

namespace simrv::memory {

enum class CoherenceState : uint8_t {
    None = 0,    ///< Invalid / No copy
    Branch = 1,  ///< Shared read-only copy
    Trunk = 2    ///< Exclusive read/write copy (dirty or clean)
};

enum class TlOpcodeA : uint8_t {
    PutFullData = 0,
    PutPartialData = 1,
    ArithmeticData = 2,
    LogicalData = 3,
    Get = 4,
    Intent = 5,
    AcquireBlock = 6,
    AcquirePerm = 7,
};

enum class TlOpcodeB : uint8_t {
    ProbeBlock = 0,
    ProbePerm = 1,
};

enum class TlOpcodeC : uint8_t {
    ProbeAck = 4,
    ProbeAckData = 5,
    Release = 6,
    ReleaseData = 7,
};

enum class TlOpcodeD : uint8_t {
    AccessAck = 0,
    AccessAckData = 1,
    HintAck = 2,
    Grant = 4,
    GrantData = 5,
    ReleaseAck = 6,
};

enum class TlOpcodeE : uint8_t {
    GrantAck = 0,
};

struct TlChannelA {
    TlOpcodeA opcode{TlOpcodeA::Get};
    uint8_t param{0};  // CoherenceState requested for Acquire
    uint8_t size{0};   // 2^size bytes (0=1B, 1=2B, 2=4B, 3=8B)
    uint8_t source{0};
    Address address{0};
    Word mask{0};
    Word data{0};

    [[nodiscard]] static constexpr auto compute_mask(uint8_t size, [[maybe_unused]] Address addr)
        -> Word {
        const auto size_bytes = static_cast<unsigned>(1u << (size & 0x3u));
        const Word base_mask = (size_bytes >= sizeof(Word))
                                   ? static_cast<Word>(~Word{0})
                                   : ((static_cast<Word>(1) << (size_bytes * 8u)) - 1u);
        return base_mask;
    }
};

struct TlChannelB {
    TlOpcodeB opcode{TlOpcodeB::ProbeBlock};
    uint8_t param{0};  // Target CoherenceState (Branch or None)
    uint8_t size{0};
    uint8_t source{0};  // Hub probe ID
    Address address{0};
    Word mask{0};
    Word data{0};
};

struct TlChannelC {
    TlOpcodeC opcode{TlOpcodeC::ProbeAck};
    uint8_t param{0};  // CoherenceState transition details
    uint8_t size{0};
    uint8_t source{0};
    Address address{0};
    Word data{0};
    bool error{false};
};

struct TlChannelD {
    TlOpcodeD opcode{TlOpcodeD::AccessAckData};
    uint8_t param{0};  // Granted CoherenceState (Branch or Trunk)
    uint8_t size{0};
    uint8_t source{0};
    uint8_t sink{0};
    Word data{0};
    bool error{false};
};

struct TlChannelE {
    TlOpcodeE opcode{TlOpcodeE::GrantAck};
    uint8_t sink{0};
};

class Bus {
   public:
    Bus() = default;
    virtual ~Bus() = default;

    virtual auto send_request(const TlChannelA& req) -> bool = 0;
    virtual auto get_response(uint8_t source_id, TlChannelD& resp) -> bool = 0;

    [[nodiscard]] virtual auto read_count() const -> uint64_t = 0;
    [[nodiscard]] virtual auto write_count() const -> uint64_t = 0;
};

}  // namespace simrv::memory