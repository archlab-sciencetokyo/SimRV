/**
 * @file TileLinkNode.hpp
 * @brief TileLink-style device node interface.
 */
#pragma once

#include "simrv/memory/Bus.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::memory {

class TileLinkNode {
   public:
    virtual ~TileLinkNode() = default;
    [[nodiscard]] virtual auto name() const -> const char* = 0;
    [[nodiscard]] virtual auto base_address() const -> Address = 0;
    [[nodiscard]] virtual auto size() const -> Address = 0;
    [[nodiscard]] virtual auto contains(Address addr) const -> bool {
        return addr >= base_address() && (addr - base_address()) < size();
    }
    [[nodiscard]] virtual auto alignment() const -> Address { return 1; }
    [[nodiscard]] virtual auto is_read_only() const -> bool { return false; }
    virtual void reset() {}
    virtual auto handle_request(const TlChannelA& req, TlChannelD& resp) -> bool = 0;
};

}  // namespace simrv::memory
