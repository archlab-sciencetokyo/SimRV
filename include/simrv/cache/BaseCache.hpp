/**
 * @file BaseCache.hpp
 * @brief Base Level-1 Cache template class.
 */
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <utility>

#include "simrv/Define.hpp"
#include "simrv/memory/Bus.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::cache {

template <uint32_t NumLines = 64, uint32_t LineBytes = 32, uint32_t Ways = 4>
class BaseCache {
   public:
    static constexpr uint32_t kNumLines = NumLines;
    static constexpr uint32_t kLineBytes = LineBytes;
    static constexpr uint32_t kLineShift = std::bit_width(LineBytes) - 1u;
    static constexpr uint32_t kWays = Ways;
    static constexpr uint32_t kNumSets = kNumLines / kWays;
    static constexpr uint32_t kSetMask = kNumSets - 1;

    /**
     * @struct CacheLine
     * @brief Aligned L1 cache line entry with TileLink-C CoherenceState.
     */
    struct alignas(64) CacheLine {
        Address tag = ~Address{0};          ///< Tag bits for address matching (8 bytes, offset 0)
        uint64_t last_used = 0;             ///< Timestamp tick for LRU eviction (8 bytes, offset 8)
        bool valid = false;                 ///< Cache line validity bit (1 byte, offset 16)
        simrv::memory::CoherenceState state = simrv::memory::CoherenceState::None;  ///< TL-C state (offset 17)
        bool dirty = false;                 ///< Modified dirty flag (offset 18)
        std::array<uint8_t, 13> padding{};  ///< Explicit padding (offset 19..32)
        alignas(16) std::array<Byte, kLineBytes> data{};  ///< 16-byte aligned payload buffer (32 bytes, offset 32..64)
    };

    BaseCache() = default;

    void insert(Address base_addr, const Byte* line_data,
                simrv::memory::CoherenceState init_state = simrv::memory::CoherenceState::Trunk,
                bool is_dirty = false) {
        ++access_tick_;
        const uint32_t set_idx = get_set_index(base_addr);
        const Address tag = get_tag(base_addr);
        auto& set = sets_[set_idx];
        CacheLine* victim = &set[0];
        uint32_t victim_way = 0;
        bool found_exact_tag = false;

        for (uint32_t w = 0; w < kWays; ++w) {
            auto& line = set[w];
            if (line.valid && line.tag == tag) {
                victim = &line;
                victim_way = w;
                found_exact_tag = true;
                break;
            }
        }

        if (!found_exact_tag) {
            for (uint32_t w = 0; w < kWays; ++w) {
                auto& line = set[w];
                if (!line.valid) {
                    victim = &line;
                    victim_way = w;
                    break;
                }
                if (line.last_used < victim->last_used) {
                    victim = &line;
                    victim_way = w;
                }
            }
        }

        if (victim->valid) {
            ++replacements_;
            last_evicted_tag_ = victim->tag;
        } else {
            last_evicted_tag_ = ~Address{0};
        }
        last_replaced_set_ = set_idx;
        last_replaced_way_ = victim_way;
        last_inserted_tag_ = tag;

        victim->tag = tag;
        victim->valid = (init_state != simrv::memory::CoherenceState::None);
        victim->state = init_state;
        victim->dirty = is_dirty;
        victim->last_used = access_tick_;
        std::memcpy(victim->data.data(), line_data, kLineBytes);
    }

    void flush(bool clear_stats = false) {
        for (auto& set : sets_) {
            std::ranges::fill(set, CacheLine{});
        }
        last_replaced_set_ = 0xFFFFFFFF;
        last_replaced_way_ = 0xFFFFFFFF;
        last_evicted_tag_ = ~Address{0};
        last_inserted_tag_ = ~Address{0};
        if (clear_stats) {
            reset_stats();
        }
    }

    void reset_stats() {
        hits_ = 0;
        misses_ = 0;
        access_tick_ = 0;
        replacements_ = 0;
    }

    [[nodiscard]] auto hit_count() const -> uint64_t { return hits_; }
    [[nodiscard]] auto miss_count() const -> uint64_t { return misses_; }
    [[nodiscard]] auto replacement_count() const -> uint64_t { return replacements_; }
    [[nodiscard]] auto last_replaced_set() const -> uint32_t { return last_replaced_set_; }
    [[nodiscard]] auto last_replaced_way() const -> uint32_t { return last_replaced_way_; }
    [[nodiscard]] auto last_evicted_tag() const -> Address { return last_evicted_tag_; }
    [[nodiscard]] auto last_inserted_tag() const -> Address { return last_inserted_tag_; }

    [[nodiscard]] auto is_line_valid(uint32_t set_idx, uint32_t way_idx) const -> bool {
        if (set_idx < kNumSets && way_idx < kWays) {
            return sets_[set_idx][way_idx].valid;
        }
        return false;
    }
    [[nodiscard]] auto get_line_tag(uint32_t set_idx, uint32_t way_idx) const -> Address {
        if (set_idx < kNumSets && way_idx < kWays) {
            return sets_[set_idx][way_idx].tag;
        }
        return ~Address{0};
    }
    [[nodiscard]] auto get_line_state(uint32_t set_idx, uint32_t way_idx) const
        -> simrv::memory::CoherenceState {
        if (set_idx < kNumSets && way_idx < kWays) {
            return sets_[set_idx][way_idx].state;
        }
        return simrv::memory::CoherenceState::None;
    }
    [[nodiscard]] auto is_line_dirty(uint32_t set_idx, uint32_t way_idx) const -> bool {
        if (set_idx < kNumSets && way_idx < kWays) {
            return sets_[set_idx][way_idx].dirty;
        }
        return false;
    }
    [[nodiscard]] auto get_line_last_used(uint32_t set_idx, uint32_t way_idx) const -> uint64_t {
        if (set_idx < kNumSets && way_idx < kWays) {
            return sets_[set_idx][way_idx].last_used;
        }
        return 0;
    }
    [[nodiscard]] auto get_line_data(uint32_t set_idx, uint32_t way_idx) const
        -> const std::array<Byte, kLineBytes>* {
        if (set_idx < kNumSets && way_idx < kWays) {
            return &sets_[set_idx][way_idx].data;
        }
        return nullptr;
    }

    auto probe_line(Address base_addr, simrv::memory::CoherenceState target_state,
                    std::array<Byte, kLineBytes>* out_dirty_data = nullptr) -> bool {
        const uint32_t set_idx = get_set_index(base_addr);
        const Address tag = get_tag(base_addr);
        for (uint32_t w = 0; w < kWays; ++w) {
            auto& line = sets_[set_idx][w];
            if (line.valid && line.tag == tag) {
                if (line.dirty && out_dirty_data != nullptr) {
                    std::memcpy(out_dirty_data->data(), line.data.data(), kLineBytes);
                }
                line.state = target_state;
                if (target_state == simrv::memory::CoherenceState::None) {
                    line.valid = false;
                    line.dirty = false;
                } else if (target_state == simrv::memory::CoherenceState::Branch) {
                    line.dirty = false;
                }
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] auto last_accessed_set() const -> uint32_t { return last_accessed_set_; }
    [[nodiscard]] auto last_access_was_hit() const -> bool { return last_access_was_hit_; }
    [[nodiscard]] auto last_hit_way() const -> uint32_t { return last_hit_way_; }

   protected:
    std::array<std::array<CacheLine, kWays>, kNumSets> sets_{};
    uint64_t access_tick_ = 0;
    uint64_t hits_ = 0;
    uint64_t misses_ = 0;
    uint64_t replacements_ = 0;
    uint32_t last_replaced_set_ = 0xFFFFFFFF;
    uint32_t last_replaced_way_ = 0xFFFFFFFF;
    Address last_evicted_tag_ = ~Address{0};
    Address last_inserted_tag_ = ~Address{0};
    mutable uint32_t last_accessed_set_ = 0xFFFFFFFF;
    mutable bool last_access_was_hit_ = false;
    mutable uint32_t last_hit_way_ = 0xFFFFFFFF;

    [[nodiscard]] constexpr auto get_set_index(Address addr) const -> uint32_t {
        return (addr >> kLineShift) & kSetMask;
    }

    [[nodiscard]] constexpr auto get_tag(Address addr) const -> Address {
        return addr & ~static_cast<Address>((1u << kLineShift) - 1u);
    }
};

}  // namespace simrv::cache
