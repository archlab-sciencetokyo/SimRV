/**
 * @file ICache.hpp
 * @brief Level-1 Instruction Cache interface.
 */
#pragma once

#include "simrv/cache/BaseCache.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::cache {

/**
 * @brief L1 instruction cache with direct-mapped organization.
 *
 * Simple direct-mapped cache: for a given address, index = (addr >> line_shift) & (num_lines - 1)
 * Supports configurable line size and number of lines.
 */
class ICache : public BaseCache<64, 32, 4> {
   public:
    /// Initialize cache to empty state
    ICache() = default;

    /// Attempt to read a word from cache
    /// Returns true if hit, false if miss
    [[nodiscard]] auto read(Address addr, uint32_t& data) -> bool;

    /// Attempt to read a halfword from cache
    /// Returns true if hit, false if miss
    [[nodiscard]] auto read16(Address addr, uint16_t& data) -> bool;
};

}  // namespace simrv::cache
