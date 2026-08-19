/**
 * @file MmioRouter.hpp
 * @brief Memory-mapped I/O address router and interconnect management.
 */
#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "simrv/memory/Bus.hpp"
#include "simrv/memory/TileLinkNode.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::memory {

/**
 * @class MmioRouter
 * @brief Centralized MMIO device router for address decoding, range validation,
 * and dispatching TileLink bus transactions to registered memory-mapped devices.
 */
class MmioRouter {
   public:
    MmioRouter() = default;
    ~MmioRouter() = default;

    /**
     * @brief Register a device node with the MMIO router.
     * @param node Non-null pointer to TileLinkNode instance.
     * @return true if successfully registered without address collision, false otherwise.
     */
    auto register_device(TileLinkNode* node) -> bool;

    /**
     * @brief Unregister a device node from the MMIO router.
     * @param node Non-null pointer to TileLinkNode instance.
     * @return true if device was found and removed.
     */
    auto unregister_device(TileLinkNode* node) -> bool;

    /**
     * @brief Remove all registered device nodes.
     */
    void clear();

    /**
     * @brief Resolve which registered MMIO device handles the target address.
     * @param addr Target byte address.
     * @return Pointer to target TileLinkNode if found, nullptr otherwise.
     */
    [[nodiscard]] auto resolve_device(Address addr) const -> TileLinkNode*;

    /**
     * @brief Search for a device node by string name.
     * @param name Device node identifier string.
     * @return Pointer to TileLinkNode if found, nullptr otherwise.
     */
    [[nodiscard]] auto find_by_name(std::string_view name) const -> TileLinkNode*;

    /**
     * @brief Dispatch a TileLink Channel A request to the appropriate MMIO device.
     * @param req TileLink Channel A request message.
     * @param resp TileLink Channel D response message (populated on completion).
     * @return true if address fell within MMIO space and was handled, false otherwise.
     */
    auto route_request(const TlChannelA& req, TlChannelD& resp) -> bool;

    /**
     * @brief Access list of all currently registered MMIO device nodes.
     */
    [[nodiscard]] auto devices() const -> const std::vector<TileLinkNode*>& { return nodes_; }

    [[nodiscard]] auto mmio_read_count() const -> uint64_t { return mmio_read_count_; }
    [[nodiscard]] auto mmio_write_count() const -> uint64_t { return mmio_write_count_; }
    [[nodiscard]] auto bus_error_count() const -> uint64_t { return bus_error_count_; }

    void reset_stats() {
        mmio_read_count_ = 0;
        mmio_write_count_ = 0;
        bus_error_count_ = 0;
    }

   private:
    std::vector<TileLinkNode*> nodes_;
    uint64_t mmio_read_count_ = 0;
    uint64_t mmio_write_count_ = 0;
    uint64_t bus_error_count_ = 0;
};

}  // namespace simrv::memory
