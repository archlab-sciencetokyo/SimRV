/**
 * @file TileLinkNode.hpp
 * @brief TileLink-style device node interface.
 */
#pragma once

#include <concepts>

#include "simrv/memory/Bus.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::memory {

class TileLinkNode {
   public:
    virtual ~TileLinkNode() = default;
    [[nodiscard]] virtual auto name() const -> const char* = 0;
    [[nodiscard]] virtual auto base_address() const -> simrv::xlen::PhysAddr = 0;
    [[nodiscard]] virtual auto size() const -> simrv::xlen::Address = 0;
    [[nodiscard]] virtual auto contains(simrv::xlen::PhysAddr address) const -> bool {
        const auto addr = address.raw();
        const auto base = base_address().raw();
        return addr >= base && (addr - base) < size();
    }
    template <std::integral I>
    auto contains(I) const -> bool = delete;
    [[nodiscard]] virtual auto alignment() const -> simrv::xlen::Address { return 1; }
    [[nodiscard]] virtual auto is_read_only() const -> bool { return false; }
    virtual void reset() {}
    virtual auto handle_request(const TlChannelA& req, TlChannelD& resp) -> bool = 0;
};

}  // namespace simrv::memory
