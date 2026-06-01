/**
 * @file ICache.hpp
 * @brief Level-1 Instruction Cache interface.
 */
#pragma once

#include <array>
#include <cstdint>

#include "simrv/xlen/Types.hpp"

namespace simrv::cache {

/**
 * @brief L1 instruction cache with direct-mapped organization.
 *
 * Simple direct-mapped cache: for a given address, index = (addr >> line_shift) & (num_lines - 1)
 * Supports configurable line size and number of lines.
 */
class ICache {
   public:
    /// Cache configuration: number of lines (power of 2)
    static constexpr uint32_t kNumLines = 64;
    /// Cache configuration: bytes per line (must be power of 2)
    static constexpr uint32_t kLineBytes = 32;
    /// Derived: bits to shift for line indexing
    static constexpr uint32_t kLineShift = 5;  // log2(kLineBytes)
    /// Derived: line size in words
    static constexpr uint32_t kLineWords = kLineBytes / sizeof(Word);
    /// Associativity: 4 ways per set
    static constexpr uint32_t kWays = 4;
    /// Derived: number of cache sets
    static constexpr uint32_t kNumSets = kNumLines / kWays;
    /// Derived: mask for set indexing
    static constexpr uint32_t kSetMask = kNumSets - 1;

    /// Initialize cache to empty state
    ICache();

    /// Attempt to read a word from cache
    /// Returns true if hit, false if miss
    [[nodiscard]] auto read(Address addr, uint32_t& data) -> bool;

    /// Attempt to read a halfword from cache
    /// Returns true if hit, false if miss
    [[nodiscard]] auto read16(Address addr, uint16_t& data) -> bool;

    /// Insert a cache line at the given address (aligned to kLineBytes)
    void insert(Address base_addr, const Byte* line_data);

    /// Flush all cache entries
    void flush();

    /// Get current hit/miss statistics
    [[nodiscard]] auto hit_count() const -> uint64_t { return hits_; }
    [[nodiscard]] auto miss_count() const -> uint64_t { return misses_; }

   private:
    struct CacheLine {
        Address tag = ~Address{0};  // Invalid tag initially
        std::array<Byte, kLineBytes> data{};
        uint64_t last_used = 0;  // LRU access tick
        bool valid = false;
    };

    std::array<std::array<CacheLine, kWays>, kNumSets> sets_{};
    uint64_t access_tick_ = 0;
    uint64_t hits_ = 0;
    uint64_t misses_ = 0;

    [[nodiscard]] auto get_set_index(Address addr) const -> uint32_t {
        return (addr >> kLineShift) & kSetMask;
    }

    [[nodiscard]] auto get_tag(Address addr) const -> Address {
        return addr & ~static_cast<Address>((1u << kLineShift) - 1u);
    }
};

}  // namespace simrv::cache
