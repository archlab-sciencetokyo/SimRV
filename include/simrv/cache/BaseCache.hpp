/**
 * @file BaseCache.hpp
 * @brief Base Level-1 Cache template class.
 */
#pragma once

#include <algorithm>
#include <array>
#include <bit>
#include <concepts>
#include <cstdint>
#include <cstring>
#include <optional>
#include <utility>

#if __has_include(<mdspan>)
#include <mdspan>
#define SIMRV_HAS_STD_MDSPAN 1
#else
#include "simrv/compat/Mdspan.hpp"
#define SIMRV_HAS_STD_MDSPAN 0
#endif

#include "simrv/CompilerHints.hpp"
#include "simrv/memory/Bus.hpp"
#include "simrv/xlen/AddressTypes.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::cache {

template <uint32_t NumLines = 32, uint32_t LineBytes = 64, uint32_t Ways = 4>
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
        simrv::memory::CacheTag tag =
            ~simrv::memory::CacheTag{0};  ///< Tag bits for address matching (8 bytes, offset 0)
        uint64_t last_used = 0;           ///< Timestamp tick for LRU eviction (8 bytes, offset 8)
        bool valid = false;               ///< Cache line validity bit (1 byte, offset 16)
        simrv::memory::MesiState state =
            simrv::memory::MesiState::Invalid;  ///< MESI state (offset 17)
        bool is_prefetched = false;             ///< Prefetched line indicator (1 byte, offset 18)
        std::array<uint8_t, 13> padding{};      ///< Explicit padding (offset 19..32)
        alignas(16)
            std::array<simrv::xlen::Byte, kLineBytes> data{};  ///< 16-byte aligned payload buffer
                                                               ///< Payload starts at offset 32.
    };

    struct EvictedLine {
        simrv::xlen::PhysAddr address{};
        simrv::memory::MesiState state = simrv::memory::MesiState::Invalid;
        std::array<simrv::xlen::Byte, kLineBytes> data{};
    };

#if SIMRV_HAS_STD_MDSPAN
    using CacheView = std::mdspan<CacheLine, std::dextents<uint32_t, 2>>;
    using ConstCacheView = std::mdspan<const CacheLine, std::dextents<uint32_t, 2>>;
#else
    using CacheView = simrv::compat::Mdspan2D<CacheLine>;
    using ConstCacheView = simrv::compat::Mdspan2D<const CacheLine>;
#endif

#undef SIMRV_HAS_STD_MDSPAN

    [[nodiscard]] constexpr auto cache_view() noexcept -> CacheView {
        return CacheView(lines_.data(), active_sets_, active_ways_);
    }
    [[nodiscard]] constexpr auto cache_view() const noexcept -> ConstCacheView {
        return ConstCacheView(lines_.data(), active_sets_, active_ways_);
    }
    [[nodiscard]] constexpr auto line(uint32_t set_idx, uint32_t way) noexcept -> CacheLine& {
        return lines_[set_idx * active_ways_ + way];
    }
    [[nodiscard]] constexpr auto line(uint32_t set_idx, uint32_t way) const noexcept
        -> const CacheLine& {
        return lines_[set_idx * active_ways_ + way];
    }

    BaseCache() = default;

    /// Configure the active BRAM-visible portion of the fixed maximum backing store.  Keeping
    /// storage bounded gives FPGA-style models deterministic resource limits while allowing
    /// profile changes to alter set/way/line geometry at runtime.
    [[nodiscard]] auto configure(uint32_t capacity_bytes, uint32_t associativity,
                                 uint32_t line_bytes = LineBytes) -> bool {
        if (capacity_bytes == 0 || associativity == 0 || line_bytes < 16 ||
            line_bytes > kLineBytes || !std::has_single_bit(line_bytes) ||
            !std::has_single_bit(capacity_bytes) || !std::has_single_bit(associativity) ||
            associativity > kWays || capacity_bytes > kNumLines * line_bytes ||
            capacity_bytes < associativity * line_bytes) {
            return false;
        }
        const uint32_t lines = capacity_bytes / line_bytes;
        if (!std::has_single_bit(lines) || lines > kNumLines || lines % associativity != 0 ||
            !std::has_single_bit(lines / associativity)) {
            return false;
        }
        active_lines_ = lines;
        active_ways_ = associativity;
        active_sets_ = lines / associativity;
        active_line_bytes_ = line_bytes;
        active_line_shift_ = static_cast<uint32_t>(std::bit_width(line_bytes) - 1u);
        active_line_mask_ = line_bytes - 1u;
        flush(true);
        return true;
    }
    [[nodiscard]] auto capacity_bytes() const -> uint32_t {
        return active_lines_ * active_line_bytes_;
    }
    [[nodiscard]] auto line_bytes() const noexcept -> uint32_t { return active_line_bytes_; }
    [[nodiscard]] auto line_shift() const noexcept -> uint32_t { return active_line_shift_; }
    [[nodiscard]] auto line_mask() const noexcept -> uint32_t { return active_line_mask_; }
    [[nodiscard]] auto associativity() const -> uint32_t { return active_ways_; }
    [[nodiscard]] auto set_count() const -> uint32_t { return active_sets_; }

    void insert(simrv::xlen::PhysAddr address, const simrv::xlen::Byte* line_data,
                simrv::memory::MesiState init_state = simrv::memory::MesiState::Exclusive) {
        const auto base_addr = address;
        ++access_tick_;
        const uint32_t set_idx = get_set_index(base_addr);
        const auto tag = get_tag(base_addr);
        CacheLine* const set_base = &lines_[set_idx * active_ways_];
        CacheLine* victim = nullptr;
        uint32_t victim_way = 0;
        uint64_t oldest_tick = UINT64_MAX;
        uint32_t first_invalid_way = 0xFFFFFFFF;

        for (uint32_t w = 0; w < active_ways_; ++w) {
            CacheLine& l = set_base[w];
            if (l.valid && l.tag == tag) {
                victim = &l;
                victim_way = w;
                break;
            }
            if (!l.valid && first_invalid_way == 0xFFFFFFFF) {
                first_invalid_way = w;
            } else if (l.valid && l.last_used < oldest_tick) {
                oldest_tick = l.last_used;
                victim_way = w;
            }
        }

        if (victim == nullptr) {
            if (first_invalid_way != 0xFFFFFFFF) {
                victim_way = first_invalid_way;
            }
            victim = &set_base[victim_way];
        }

        last_eviction_.reset();
        if (victim->valid) {
            ++replacements_;
            last_evicted_tag_ = victim->tag;
            last_eviction_ =
                EvictedLine{.address = victim->tag, .state = victim->state, .data = victim->data};
        } else {
            last_evicted_tag_ = simrv::xlen::PhysAddr{~simrv::xlen::Address{0}};
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
        std::memcpy(victim->data.data(), line_data, active_line_bytes_);
    }

    void flush(bool clear_stats = false) {
        std::ranges::fill(lines_, CacheLine{});
        last_replaced_set_ = 0xFFFFFFFF;
        last_replaced_way_ = 0xFFFFFFFF;
        last_evicted_tag_ = simrv::xlen::PhysAddr{~simrv::xlen::Address{0}};
        last_inserted_tag_ = simrv::xlen::PhysAddr{~simrv::xlen::Address{0}};
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
    [[nodiscard]] auto last_evicted_tag() const -> simrv::xlen::PhysAddr {
        return last_evicted_tag_;
    }
    [[nodiscard]] auto last_inserted_tag() const -> simrv::xlen::PhysAddr {
        return last_inserted_tag_;
    }
    auto take_last_eviction() -> std::optional<EvictedLine> {
        return std::exchange(last_eviction_, std::nullopt);
    }

    [[nodiscard]] auto is_line_valid(uint32_t set_idx, uint32_t way_idx) const -> bool {
        if (set_idx < active_sets_ && way_idx < active_ways_) {
            return line(set_idx, way_idx).valid;
        }
        return false;
    }
    [[nodiscard]] auto get_line_tag(uint32_t set_idx, uint32_t way_idx) const
        -> simrv::xlen::PhysAddr {
        if (set_idx < active_sets_ && way_idx < active_ways_) {
            return line(set_idx, way_idx).tag;
        }
        return ~simrv::xlen::PhysAddr{0};
    }
    [[nodiscard]] auto get_line_state(uint32_t set_idx, uint32_t way_idx) const
        -> simrv::memory::MesiState {
        if (set_idx < active_sets_ && way_idx < active_ways_) {
            return line(set_idx, way_idx).state;
        }
        return simrv::memory::MesiState::Invalid;
    }
    [[nodiscard]] auto is_line_dirty(uint32_t set_idx, uint32_t way_idx) const -> bool {
        if (set_idx < active_sets_ && way_idx < active_ways_) {
            return line(set_idx, way_idx).state == simrv::memory::MesiState::Modified;
        }
        return false;
    }
    [[nodiscard]] auto is_line_prefetched(uint32_t set_idx, uint32_t way_idx) const -> bool {
        if (set_idx < active_sets_ && way_idx < active_ways_) {
            return line(set_idx, way_idx).is_prefetched;
        }
        return false;
    }
    [[nodiscard]] auto get_line_last_used(uint32_t set_idx, uint32_t way_idx) const -> uint64_t {
        if (set_idx < active_sets_ && way_idx < active_ways_) {
            return line(set_idx, way_idx).last_used;
        }
        return 0;
    }
    [[nodiscard]] auto get_line_data(uint32_t set_idx, uint32_t way_idx) const
        -> const std::array<simrv::xlen::Byte, kLineBytes>* {
        if (set_idx < active_sets_ && way_idx < active_ways_) {
            return &line(set_idx, way_idx).data;
        }
        return nullptr;
    }

    template <uint32_t ActiveWays>
    [[nodiscard]] constexpr auto find_matching_line_impl(this auto& self, uint32_t set_idx,
                                                         simrv::memory::CacheTag tag) noexcept {
        using LinePtr = decltype(&self.lines_[0]);
        if (simrv::compiler::unlikely(set_idx >= self.active_sets_)) {
            return std::pair<uint32_t, LinePtr>{0xFFFFFFFF, nullptr};
        }
        if constexpr (ActiveWays == 1) {
            auto& l = self.lines_[set_idx];
            if (simrv::compiler::likely(l.valid && l.tag == tag &&
                                        l.state != simrv::memory::MesiState::Invalid)) {
                return std::pair<uint32_t, LinePtr>{0u, &l};
            }
            return std::pair<uint32_t, LinePtr>{0xFFFFFFFF, nullptr};
        } else {
            auto* line_ptr = &self.lines_[set_idx * ActiveWays];
            // Direct MRU fast path: check last hit way first when accessing the same set.
            if (self.last_access_was_hit_ && set_idx == self.last_accessed_set_ &&
                self.last_hit_way_ < ActiveWays) {
                auto& l = line_ptr[self.last_hit_way_];
                if (simrv::compiler::likely(l.valid && l.tag == tag &&
                                            l.state != simrv::memory::MesiState::Invalid)) {
                    return std::pair<uint32_t, LinePtr>{self.last_hit_way_, &l};
                }
            }

            for (uint32_t w = 0; w < ActiveWays; ++w) {
                auto& l = line_ptr[w];
                if (l.valid && l.tag == tag && l.state != simrv::memory::MesiState::Invalid) {
                    return std::pair<uint32_t, LinePtr>{w, &l};
                }
            }
            return std::pair<uint32_t, LinePtr>{0xFFFFFFFF, nullptr};
        }
    }

    [[nodiscard]] constexpr auto find_matching_line(this auto& self, uint32_t set_idx,
                                                    simrv::memory::CacheTag tag) noexcept {
        switch (self.active_ways_) {
            case 1:
                if constexpr (1 <= kWays)
                    return self.template find_matching_line_impl<1>(set_idx, tag);
                break;
            case 2:
                if constexpr (2 <= kWays)
                    return self.template find_matching_line_impl<2>(set_idx, tag);
                break;
            case 4:
                if constexpr (4 <= kWays)
                    return self.template find_matching_line_impl<4>(set_idx, tag);
                break;
            case 8:
                if constexpr (8 <= kWays)
                    return self.template find_matching_line_impl<8>(set_idx, tag);
                break;
            default:
                break;
        }
        using LinePtr = decltype(&self.lines_[0]);
        if (simrv::compiler::unlikely(set_idx >= self.active_sets_)) {
            return std::pair<uint32_t, LinePtr>{0xFFFFFFFF, nullptr};
        }
        auto* line_ptr = &self.lines_[set_idx * self.active_ways_];
        for (uint32_t w = 0; w < self.active_ways_; ++w) {
            auto& l = line_ptr[w];
            if (l.valid && l.tag == tag && l.state != simrv::memory::MesiState::Invalid) {
                return std::pair<uint32_t, LinePtr>{w, &l};
            }
        }
        return std::pair<uint32_t, LinePtr>{0xFFFFFFFF, nullptr};
    }

    [[nodiscard]] constexpr auto find_way(uint32_t set_idx,
                                          simrv::memory::CacheTag tag) const noexcept
        -> std::optional<uint32_t> {
        const auto [w, line_ptr] = find_matching_line(set_idx, tag);
        if (line_ptr != nullptr) return w;
        return std::nullopt;
    }

    [[nodiscard]] auto line_state(simrv::xlen::PhysAddr address) const -> simrv::memory::MesiState {
        const auto base_addr = address;
        const uint32_t set_idx = get_set_index(base_addr);
        const auto tag = get_tag(base_addr);
        const auto [w, line_ptr] = find_matching_line(set_idx, tag);
        if (line_ptr != nullptr) {
            return line_ptr->state;
        }
        return simrv::memory::MesiState::Invalid;
    }

    auto upgrade_line_state(simrv::xlen::PhysAddr address, simrv::memory::MesiState new_state)
        -> bool {
        const auto base_addr = address;
        const uint32_t set_idx = get_set_index(base_addr);
        const auto tag = get_tag(base_addr);
        const auto [w, line_ptr] = find_matching_line(set_idx, tag);
        if (line_ptr != nullptr) {
            line_ptr->state = new_state;
            line_ptr->last_used = ++access_tick_;
            return true;
        }
        return false;
    }

    auto probe_line(simrv::xlen::PhysAddr address, simrv::memory::MesiState target_state,
                    std::array<simrv::xlen::Byte, kLineBytes>* out_dirty_data = nullptr) -> bool {
        const auto base_addr = address;
        const uint32_t set_idx = get_set_index(base_addr);
        const auto tag = get_tag(base_addr);
        auto* line_ptr = &lines_[set_idx * active_ways_];
        for (uint32_t w = 0; w < active_ways_; ++w) {
            auto& l = line_ptr[w];
            if (l.valid && l.tag == tag) {
                if (l.state == simrv::memory::MesiState::Modified && out_dirty_data != nullptr) {
                    std::memcpy(out_dirty_data->data(), l.data.data(), active_line_bytes_);
                }
                l.state = target_state;
                if (target_state == simrv::memory::MesiState::Invalid) {
                    l.valid = false;
                }
                return true;
            }
        }
        return false;
    }

    template <std::integral I, typename... Args>
    void insert(I, Args&&...) = delete;
    template <std::integral I>
    auto line_state(I) const = delete;
    template <std::integral I, typename... Args>
    auto upgrade_line_state(I, Args&&...) = delete;
    template <std::integral I, typename... Args>
    auto probe_line(I, Args&&...) = delete;

    [[nodiscard]] auto last_accessed_set() const -> uint32_t { return last_accessed_set_; }
    [[nodiscard]] auto last_access_was_hit() const -> bool { return last_access_was_hit_; }
    [[nodiscard]] auto last_hit_way() const -> uint32_t { return last_hit_way_; }

   protected:
    alignas(64) std::array<CacheLine, kNumLines> lines_{};
    uint64_t access_tick_ = 0;
    uint64_t hits_ = 0;
    uint64_t misses_ = 0;
    uint64_t replacements_ = 0;
    uint32_t last_replaced_set_ = 0xFFFFFFFF;
    uint32_t last_replaced_way_ = 0xFFFFFFFF;
    simrv::memory::CacheTag last_evicted_tag_ = ~simrv::memory::CacheTag{0};
    simrv::memory::CacheTag last_inserted_tag_ = ~simrv::memory::CacheTag{0};
    std::optional<EvictedLine> last_eviction_;
    mutable uint32_t last_accessed_set_ = 0xFFFFFFFF;
    mutable bool last_access_was_hit_ = false;
    mutable uint32_t last_hit_way_ = 0xFFFFFFFF;
    uint32_t active_lines_ = kNumLines;
    uint32_t active_ways_ = kWays;
    uint32_t active_sets_ = kNumSets;
    uint32_t active_line_bytes_ = LineBytes;
    uint32_t active_line_shift_ = kLineShift;
    uint32_t active_line_mask_ = LineBytes - 1u;

    [[nodiscard]] auto get_set_index(simrv::xlen::PhysAddr addr) const
        -> simrv::memory::CacheSetIndex {
        return static_cast<simrv::memory::CacheSetIndex>((addr.raw() >> active_line_shift_) &
                                                         (active_sets_ - 1u));
    }

    [[nodiscard]] constexpr auto get_tag(simrv::xlen::PhysAddr addr) const
        -> simrv::memory::CacheTag {
        return simrv::xlen::PhysAddr{addr.raw() &
                                     ~static_cast<simrv::xlen::Address>(active_line_mask_)};
    }
};

}  // namespace simrv::cache
