/**
 * @file CoherenceHub.hpp
 * @brief TileLink-C Directory-Based Coherence Hub for multi-hart SMP.
 */
#pragma once

#include <array>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "simrv/cache/BaseCache.hpp"
#include "simrv/cache/L2Cache.hpp"
#include "simrv/cache/L3Cache.hpp"
#include "simrv/core/SoCConfig.hpp"
#include "simrv/memory/Bus.hpp"
#include "simrv/xlen/IdentifierTypes.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::memory {

struct DirectoryEntry {
    MesiState state = MesiState::Invalid;
    CoherenceSharersMask sharers_mask = 0;
    std::optional<simrv::xlen::HartId> owner_hart;
};

struct CoherenceStats {
    simrv::xlen::Counter acquire_count = 0;
    simrv::xlen::Counter probe_count = 0;
    simrv::xlen::Counter release_count = 0;
    simrv::xlen::Counter grant_count = 0;
    simrv::xlen::Counter invalidation_count = 0;
    simrv::xlen::Counter writeback_count = 0;
    simrv::xlen::Counter prefetch_count = 0;
};

class CoherenceHub {
   public:
    static constexpr uint32_t kLineBytes = simrv::cache::BaseCache<>::kLineBytes;

    explicit CoherenceHub(simrv::core::Machine& machine);

    auto configure_topology(const simrv::core::SmpTopologyConfig& topology)
        -> std::expected<void, std::string>;

    auto handle_acquire(const TlChannelA& req, TlChannelD& resp,
                        std::array<simrv::xlen::Byte, kLineBytes>& line_buffer,
                        uint32_t& service_latency) -> bool;

    auto handle_intent(const TlChannelA& req, TlChannelD& resp) -> bool;

    auto handle_release(const TlChannelC& req, TlChannelD& resp,
                        const std::array<simrv::xlen::Byte, kLineBytes>* release_data = nullptr)
        -> bool;

    void process_grant_ack(const TlChannelE& ack);

    /// Notify the directory when a Trunk holder performs its first write (E -> M).
    void mark_modified(LineAddress line_base, simrv::xlen::HartId hart);

    void invalidate_line_broadcast(LineAddress line_base, simrv::xlen::HartId initiator_hart);

    /// Invalidate every cached copy before a coherent implicit/DMA memory write.
    void invalidate_line_external(LineAddress line_base);

    /// Register a clean line populated directly during machine startup prewarming.
    void register_preloaded_line(LineAddress line_base, simrv::xlen::HartId hart);

    [[nodiscard]] auto stats() const -> const CoherenceStats& { return stats_; }
    void reset_stats() { stats_ = {}; }

    [[nodiscard]] auto l3_cache() noexcept -> simrv::cache::L3Cache& { return l3_cache_; }
    [[nodiscard]] auto l3_cache() const noexcept -> const simrv::cache::L3Cache& {
        return l3_cache_;
    }
    [[nodiscard]] auto shared_l2_for_hart(uint32_t hart) noexcept -> simrv::cache::L2Cache*;
    [[nodiscard]] auto shared_l3_enabled() const noexcept -> bool { return l3_enabled_; }

    [[nodiscard]] auto get_directory_state(LineAddress line_base) const -> DirectoryEntry;
    [[nodiscard]] auto validate_directory() const -> std::expected<void, std::string>;
    void clear();

   private:
    struct FastCacheEntry {
        LineAddress line_base = ~LineAddress{0};
        DirectoryEntry entry{};
    };

    static constexpr size_t kFastCacheEntries = 1024;
    static constexpr size_t kFastCacheMask = kFastCacheEntries - 1;

    simrv::core::Machine& machine_;
    simrv::cache::L3Cache l3_cache_{};
    struct ClusterCache {
        uint32_t id = 0;
        std::vector<uint32_t> harts;
        simrv::cache::L2Cache cache{};
        simrv::core::SharedCacheConfig config{};
    };
    std::vector<std::unique_ptr<ClusterCache>> l2_caches_;
    simrv::core::SmpTopologyConfig topology_{};
    bool l3_enabled_ = true;
    std::unordered_map<LineAddress, DirectoryEntry> directory_;
    std::array<FastCacheEntry, kFastCacheEntries> fast_cache_{};
    CoherenceStats stats_{};
    TlSinkId next_sink_ = 1;
    std::unordered_set<TlSinkId> pending_grants_;

    void update_dir_entry(LineAddress line_base, const DirectoryEntry& entry);
    void erase_dir_entry(LineAddress line_base);
    auto lookup_dir_entry(LineAddress line_base, DirectoryEntry& out_entry) -> bool;

    void probe_hart_dcache(simrv::xlen::HartId hart_id, const TlChannelB& probe_req,
                           TlChannelC& probe_resp,
                           std::array<simrv::xlen::Byte, kLineBytes>& dirty_data);

    void probe_hart_icache(simrv::xlen::HartId hart_id, const TlChannelB& probe_req);
    [[nodiscard]] auto l2_cache_for_hart(uint32_t hart) -> ClusterCache*;
    void invalidate_shared_caches(LineAddress line_base);
    [[nodiscard]] auto allocate_sink() -> TlSinkId;
};

}  // namespace simrv::memory
