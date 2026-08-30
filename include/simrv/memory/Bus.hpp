/**
 * @file Bus.hpp
 * @brief TileLink-style shared bus abstraction for memory/device access.
 */
#pragma once

#include <cstdint>

#include "simrv/memory/TileLinkProtocol.hpp"

namespace simrv::memory {

class Bus {
   public:
    Bus() = default;
    virtual ~Bus() = default;

    virtual auto send_request(const TlChannelA& req) -> bool = 0;
    virtual auto get_response(TlSourceId source_id, TlChannelD& resp) -> bool = 0;

    [[nodiscard]] virtual auto read_count() const -> uint64_t = 0;
    [[nodiscard]] virtual auto write_count() const -> uint64_t = 0;
};

}  // namespace simrv::memory
