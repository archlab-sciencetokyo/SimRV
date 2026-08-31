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
#include <optional>
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
     * @brief Aligned L1 cache line entry with explicit Illinois MESI state.
     */
    struct alignas(64) CacheLine {
        Address tag = ~Address{0};  ///< Tag bits for address matching (8 bytes, offset 0)
        uint64_t last_used = 0;     ///< Timestamp tick for LRU eviction (8 bytes, offset 8)
        bool valid = false;         ///< Cache line validity bit (1 byte, offset 16)
        simrv::memory::MesiState state =
            simrv::memory::MesiState::Invalid;            ///< MESI state (offset 17)
        std::array<uint8_t, 14> padding{};                ///< Explicit padding (offset 18..32)
        alignas(16) std::array<Byte, kLineBytes> data{};  ///< 16-byte aligned payload buffer (32
                                                          ///< bytes, offset 32..64)
    };

    struct EvictedLine {
        Address address = 0;
        simrv::memory::MesiState state = simrv::memory::MesiState::Invalid;
        std::array<Byte, kLineBytes> data{};
    };

    BaseCache() = default;

    /// Configure the active BRAM-visible portion of the fixed maximum backing store.  Keeping
    /// storage bounded gives FPGA-style models deterministic resource limits while allowing
    /// profile changes to alter set/way behaviour at runtime.
    [[nodiscard]] auto configure(uint32_t capacity_bytes, uint32_t associativity) -> bool {
        if (capacity_bytes == 0 || associativity == 0 || !std::has_single_bit(capacity_bytes) ||
            !std::has_single_bit(associativity) || associativity > kWays ||
            capacity_bytes > kNumLines * kLineBytes ||
            capacity_bytes < associativity * kLineBytes) {
            return false;
        }
        const uint32_t lines = capacity_bytes / kLineBytes;
        if (!std::has_single_bit(lines) || lines > kNumLines || lines % associativity != 0 ||
            !std::has_single_bit(lines / associativity)) {
            return false;
        }
        active_lines_ = lines;
        active_ways_ = associativity;
        active_sets_ = lines / associativity;
        flush(true);
        return true;
    }
    [[nodiscard]] auto capacity_bytes() const -> uint32_t { return active_lines_ * kLineBytes; }
    [[nodiscard]] auto associativity() const -> uint32_t { return active_ways_; }
    [[nodiscard]] auto set_count() const -> uint32_t { return active_sets_; }

    void insert(Address base_addr, const Byte* line_data,
                simrv::memory::MesiState init_state = simrv::memory::MesiState::Exclusive) {
        ++access_tick_;
        const uint32_t set_idx = get_set_index(base_addr);
        const Address tag = get_tag(base_addr);
        auto& set = sets_[set_idx];
        CacheLine* victim = &set[0];
        uint32_t victim_way = 0;
        bool found_exact_tag = false;

        for (uint32_t w = 0; w < active_ways_; ++w) {
            auto& line = set[w];
            if (line.valid && line.tag == tag) {
                victim = &line;
                victim_way = w;
                found_exact_tag = true;
                break;
            }
        }

        if (!found_exact_tag) {
            for (uint32_t w = 0; w < active_ways_; ++w) {
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

        last_eviction_.reset();
        if (victim->valid) {
            ++replacements_;
            last_evicted_tag_ = victim->tag;
            last_eviction_ =
                EvictedLine{.address = victim->tag, .state = victim->state, .data = victim->data};
        } else {
            last_evicted_tag_ = ~Address{0};
        }
        last_replaced_set_ = set_idx;
        last_replaced_way_ = victim_way;
        last_inserted_tag_ = tag;
        last_accessed_set_ = set_idx;
        last_access_was_hit_ = false;
        last_hit_way_ = 0xFFFFFFFF;

        victim->tag = tag;
        victim->valid = (init_state != simrv::memory::MesiState::Invalid);
        victim->state = init_state;
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
        last_eviction_.reset();
        last_accessed_set_ = 0xFFFFFFFF;
        last_access_was_hit_ = false;
        last_hit_way_ = 0xFFFFFFFF;
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
    auto take_last_eviction() -> std::optional<EvictedLine> {
        return std::exchange(last_eviction_, std::nullopt);
    }

    [[nodiscard]] auto is_line_valid(uint32_t set_idx, uint32_t way_idx) const -> bool {
        if (set_idx < active_sets_ && way_idx < active_ways_) {
            return sets_[set_idx][way_idx].valid;
        }
        return false;
    }
    [[nodiscard]] auto get_line_tag(uint32_t set_idx, uint32_t way_idx) const -> Address {
        if (set_idx < active_sets_ && way_idx < active_ways_) {
            return sets_[set_idx][way_idx].tag;
        }
        return ~Address{0};
    }
    [[nodiscard]] auto get_line_state(uint32_t set_idx, uint32_t way_idx) const
        -> simrv::memory::MesiState {
        if (set_idx < active_sets_ && way_idx < active_ways_) {
            return sets_[set_idx][way_idx].state;
        }
        return simrv::memory::MesiState::Invalid;
    }
    [[nodiscard]] auto is_line_dirty(uint32_t set_idx, uint32_t way_idx) const -> bool {
        if (set_idx < active_sets_ && way_idx < active_ways_) {
            return sets_[set_idx][way_idx].state == simrv::memory::MesiState::Modified;
        }
        return false;
    }
    [[nodiscard]] auto get_line_last_used(uint32_t set_idx, uint32_t way_idx) const -> uint64_t {
        if (set_idx < active_sets_ && way_idx < active_ways_) {
            return sets_[set_idx][way_idx].last_used;
        }
        return 0;
    }
    [[nodiscard]] auto get_line_data(uint32_t set_idx, uint32_t way_idx) const
        -> const std::array<Byte, kLineBytes>* {
        if (set_idx < active_sets_ && way_idx < active_ways_) {
            return &sets_[set_idx][way_idx].data;
        }
        return nullptr;
    }

    [[nodiscard]] constexpr auto find_way(uint32_t set_idx, Address tag) const noexcept
        -> std::optional<uint32_t> {
        if (set_idx >= active_sets_) return std::nullopt;
        const auto& set = sets_[set_idx];
        for (uint32_t w = 0; w < active_ways_; ++w) {
            if (set[w].valid && set[w].tag == tag &&
                set[w].state != simrv::memory::MesiState::Invalid) {
                return w;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] auto line_state(Address base_addr) const -> simrv::memory::MesiState {
        const uint32_t set_idx = get_set_index(base_addr);
        const Address tag = get_tag(base_addr);
        const auto way_opt = find_way(set_idx, tag);
        if (way_opt.has_value()) {
            return sets_[set_idx][*way_opt].state;
        }
        return simrv::memory::MesiState::Invalid;
    }

    auto probe_line(Address base_addr, simrv::memory::MesiState target_state,
                    std::array<Byte, kLineBytes>* out_dirty_data = nullptr) -> bool {
        const uint32_t set_idx = get_set_index(base_addr);
        const Address tag = get_tag(base_addr);
        for (uint32_t w = 0; w < active_ways_; ++w) {
            auto& line = sets_[set_idx][w];
            if (line.valid && line.tag == tag) {
                if (line.state == simrv::memory::MesiState::Modified && out_dirty_data != nullptr) {
                    std::memcpy(out_dirty_data->data(), line.data.data(), kLineBytes);
                }
                line.state = target_state;
                if (target_state == simrv::memory::MesiState::Invalid) {
                    line.valid = false;
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
    std::optional<EvictedLine> last_eviction_;
    mutable uint32_t last_accessed_set_ = 0xFFFFFFFF;
    mutable bool last_access_was_hit_ = false;
    mutable uint32_t last_hit_way_ = 0xFFFFFFFF;
    uint32_t active_lines_ = kNumLines;
    uint32_t active_ways_ = kWays;
    uint32_t active_sets_ = kNumSets;

    [[nodiscard]] auto get_set_index(Address addr) const -> uint32_t {
        return static_cast<uint32_t>((addr >> kLineShift) & (active_sets_ - 1u));
    }

    [[nodiscard]] constexpr auto get_tag(Address addr) const -> Address {
        return addr & ~static_cast<Address>((1u << kLineShift) - 1u);
    }
};

}  // namespace simrv::cache
