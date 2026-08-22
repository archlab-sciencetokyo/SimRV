/**
 * @file CoherenceHub.hpp
 * @brief TileLink-C Directory-Based Coherence Hub for multi-hart SMP.
 */
#pragma once

#include <array>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "simrv/Define.hpp"
#include "simrv/cache/BaseCache.hpp"
#include "simrv/memory/Bus.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::memory {

struct DirectoryEntry {
    CoherenceState state = CoherenceState::None;
    uint32_t sharers_mask = 0;
    uint32_t owner_hart = 0;
};

struct CoherenceStats {
    uint64_t acquire_count = 0;
    uint64_t probe_count = 0;
    uint64_t release_count = 0;
    uint64_t grant_count = 0;
    uint64_t invalidation_count = 0;
    uint64_t writeback_count = 0;
};

class CoherenceHub {
   public:
    static constexpr uint32_t kLineBytes = simrv::cache::BaseCache<>::kLineBytes;

    explicit CoherenceHub(simrv::core::Machine& machine);

    auto handle_acquire(const TlChannelA& req, TlChannelD& resp,
                        std::array<Byte, kLineBytes>& line_buffer) -> bool;

    auto handle_release(const TlChannelC& req, TlChannelD& resp,
                        const std::array<Byte, kLineBytes>* release_data = nullptr) -> bool;

    void process_grant_ack(const TlChannelE& ack);

    void invalidate_line_broadcast(Address line_base, uint32_t initiator_hart);

    /// Invalidate every cached copy before a coherent implicit/DMA memory write.
    void invalidate_line_external(Address line_base);

    [[nodiscard]] auto stats() const -> const CoherenceStats& { return stats_; }
    void reset_stats() { stats_ = {}; }

    [[nodiscard]] auto get_directory_state(Address line_base) const -> DirectoryEntry;

   private:
    struct FastCacheEntry {
        Address line_base = ~Address{0};
        DirectoryEntry entry{};
    };

    static constexpr size_t kFastCacheEntries = 1024;
    static constexpr size_t kFastCacheMask = kFastCacheEntries - 1;

    simrv::core::Machine& machine_;
    std::unordered_map<Address, DirectoryEntry> directory_;
    std::array<FastCacheEntry, kFastCacheEntries> fast_cache_{};
    CoherenceStats stats_{};

    void update_dir_entry(Address line_base, const DirectoryEntry& entry);
    void erase_dir_entry(Address line_base);
    auto lookup_dir_entry(Address line_base, DirectoryEntry& out_entry) -> bool;

    void probe_hart_dcache(uint32_t hart_id, const TlChannelB& probe_req, TlChannelC& probe_resp,
                           std::array<Byte, kLineBytes>& dirty_data);

    void probe_hart_icache(uint32_t hart_id, const TlChannelB& probe_req);
};

}  // namespace simrv::memory
