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

    /**
     * @brief Attempt to read a 32-bit instruction word from cache.
     * @param addr Physical address to read from.
     * @param[out] data Output parameter filled with instruction word on cache hit.
     * @return true on cache hit, false on cache miss.
     */
    [[nodiscard]] auto read(Address addr, uint32_t& data) -> bool;

    /**
     * @brief Attempt to read a 16-bit instruction halfword from cache.
     * @param addr Physical address to read from.
     * @param[out] data Output parameter filled with compressed instruction halfword on hit.
     * @return true on cache hit, false on cache miss.
     */
    [[nodiscard]] auto read16(Address addr, uint16_t& data) -> bool;
};

}  // namespace simrv::cache
