/**
 * @file MmioRouter.hpp
 * @brief Memory-mapped I/O address router and interconnect management.
 */
#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

#include "simrv/memory/Bus.hpp"
#include "simrv/memory/TileLinkNode.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::core {
class Tracer;
}

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

    void set_tracer(simrv::core::Tracer* tracer) noexcept { tracer_ = tracer; }
    [[nodiscard]] auto tracer() const noexcept -> simrv::core::Tracer* { return tracer_; }

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

    struct DeviceEntry {
        simrv::xlen::Address base = 0;
        simrv::xlen::Address end = 0;
        TileLinkNode* node = nullptr;
        simrv::xlen::Address alignment = 1;
        bool is_read_only = false;
    };

    /**
     * @brief Resolve which registered MMIO device handles the target address.
     * @param addr Target byte address.
     * @return Pointer to target TileLinkNode if found, nullptr otherwise.
     */
    [[nodiscard]] auto resolve_device(simrv::xlen::Address addr) const -> TileLinkNode*;
    [[nodiscard]] auto resolve_entry(simrv::xlen::Address addr) const -> const DeviceEntry*;
    [[nodiscard]] auto resolve_device(simrv::xlen::PhysAddr paddr) const -> TileLinkNode* {
        return resolve_device(paddr.raw());
    }
    [[nodiscard]] auto resolve_entry(simrv::xlen::PhysAddr paddr) const -> const DeviceEntry* {
        return resolve_entry(paddr.raw());
    }

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

    [[nodiscard]] auto mmio_read_count() const -> simrv::xlen::Counter { return mmio_read_count_; }
    [[nodiscard]] auto mmio_write_count() const -> simrv::xlen::Counter {
        return mmio_write_count_;
    }
    [[nodiscard]] auto bus_error_count() const -> simrv::xlen::Counter { return bus_error_count_; }

    void reset_stats() {
        mmio_read_count_ = 0;
        mmio_write_count_ = 0;
        bus_error_count_ = 0;
    }

   private:
    static constexpr size_t kMruCapacity = 4;

    std::vector<TileLinkNode*> nodes_;
    std::vector<DeviceEntry> entries_;
    simrv::xlen::Address min_base_ = std::numeric_limits<simrv::xlen::Address>::max();
    simrv::xlen::Address max_end_ = 0;
    mutable std::array<DeviceEntry, kMruCapacity> mru_entries_{};
    mutable size_t mru_count_ = 0;
    simrv::xlen::Counter mmio_read_count_ = 0;
    simrv::xlen::Counter mmio_write_count_ = 0;
    simrv::xlen::Counter bus_error_count_ = 0;
    simrv::core::Tracer* tracer_ = nullptr;

    void rebuild_entries();
};

}  // namespace simrv::memory
