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
        for (auto& line : set) {
            if (!line.valid) {
                victim = &line;
                break;
            }
            if (line.last_used < victim->last_used) {
                victim = &line;
            }
        }
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
    }

    [[nodiscard]] auto hit_count() const -> uint64_t { return hits_; }
    [[nodiscard]] auto miss_count() const -> uint64_t { return misses_; }

   protected:
    std::array<std::array<CacheLine, kWays>, kNumSets> sets_{};
    uint64_t access_tick_ = 0;
    uint64_t hits_ = 0;
    uint64_t misses_ = 0;

    [[nodiscard]] constexpr auto get_set_index(Address addr) const -> uint32_t {
        return (addr >> kLineShift) & kSetMask;
    }

    [[nodiscard]] constexpr auto get_tag(Address addr) const -> Address {
        return addr & ~static_cast<Address>((1u << kLineShift) - 1u);
    }
};

}  // namespace simrv::cache
