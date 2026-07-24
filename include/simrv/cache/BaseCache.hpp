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

#include "simrv/Define.hpp"
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

    struct CacheLine {
        Address tag = ~Address{0};  // Invalid tag initially
        std::array<Byte, kLineBytes> data{};
        uint64_t last_used = 0;  // LRU access tick
        bool valid = false;
    };

    BaseCache() = default;

    void insert(Address base_addr, const Byte* line_data) {
        ++access_tick_;
        const uint32_t set_idx = get_set_index(base_addr);
        const Address tag = get_tag(base_addr);
        auto& set = sets_.at(set_idx);
        CacheLine* victim = &set.at(0);
        uint32_t victim_way = 0;
        for (uint32_t w = 0; w < kWays; ++w) {
            auto& line = set.at(w);
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
        victim->valid = true;
        victim->last_used = access_tick_;
        std::memcpy(victim->data.data(), line_data, kLineBytes);
    }

    void flush() {
        for (auto& set : sets_) {
            std::ranges::fill(set, CacheLine{});
        }
        hits_ = 0;
        misses_ = 0;
        access_tick_ = 0;
        replacements_ = 0;
        last_replaced_set_ = 0xFFFFFFFF;
        last_replaced_way_ = 0xFFFFFFFF;
        last_evicted_tag_ = ~Address{0};
        last_inserted_tag_ = ~Address{0};
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
            return sets_.at(set_idx).at(way_idx).valid;
        }
        return false;
    }
    [[nodiscard]] auto get_line_tag(uint32_t set_idx, uint32_t way_idx) const -> Address {
        if (set_idx < kNumSets && way_idx < kWays) {
            return sets_.at(set_idx).at(way_idx).tag;
        }
        return ~Address{0};
    }
    [[nodiscard]] auto get_line_last_used(uint32_t set_idx, uint32_t way_idx) const -> uint64_t {
        if (set_idx < kNumSets && way_idx < kWays) {
            return sets_.at(set_idx).at(way_idx).last_used;
        }
        return 0;
    }
    [[nodiscard]] auto get_line_data(uint32_t set_idx, uint32_t way_idx) const -> const std::array<Byte, kLineBytes>* {
        if (set_idx < kNumSets && way_idx < kWays) {
            return &sets_.at(set_idx).at(way_idx).data;
        }
        return nullptr;
    }

    [[nodiscard]] auto last_accessed_set() const -> uint32_t { return last_accessed_set_; }
    [[nodiscard]] auto last_access_was_hit() const -> bool { return last_access_was_hit_; }

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

    [[nodiscard]] constexpr auto get_set_index(Address addr) const -> uint32_t {
        return (addr >> kLineShift) & kSetMask;
    }

    [[nodiscard]] constexpr auto get_tag(Address addr) const -> Address {
        return addr & ~static_cast<Address>((1u << kLineShift) - 1u);
    }
};

}  // namespace simrv::cache
