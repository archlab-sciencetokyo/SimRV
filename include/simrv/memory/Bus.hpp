/**
 * @file Bus.hpp
 * @brief TileLink-style shared bus abstraction for memory/device access.
 */
#pragma once

#include <cstdint>

#include "simrv/xlen/Types.hpp"

namespace simrv::memory {

enum class TlOpcodeA : uint8_t {
    PutFullData = 0,
    PutPartialData = 1,
    Get = 4,
};

enum class TlOpcodeD : uint8_t {
    AccessAck = 0,
    AccessAckData = 1,
};

struct TlChannelA {
    TlOpcodeA opcode;
    uint8_t size;
    uint8_t source;
    Address address;
    Word mask;
    Word data;
};

struct TlChannelD {
    TlOpcodeD opcode;
    uint8_t size;
    uint8_t source;
    Word data;
    bool error;
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