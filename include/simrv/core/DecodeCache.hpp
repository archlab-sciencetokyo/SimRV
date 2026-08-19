/**
 * @file DecodeCache.hpp
 * @brief Fast direct-mapped instruction decode cache for simulator acceleration.
 */
#pragma once

#include <array>

#include "simrv/Define.hpp"
#include "simrv/pipeline/DecodedInstruction.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

/**
 * @struct CachedOp
 * @brief Decoded instruction augmented with cache entry validity.
 */
struct alignas(64) CachedOp : public simrv::pipeline::DecodedInstruction {
    bool valid = false;  ///< Validity flag for direct-mapped cache line
    uint8_t len = 4;     ///< Instruction length in bytes (2 for RVC, 4 for 32-bit)
};

/**
 * @class DecodeCache
 * @brief Direct-mapped cache for pre-decoded instructions to bypass fetch/decode stages.
 *
 * Indexed by XOR-hash of PC (`(pc >> 1) ^ (pc >> 13)`), caching 8192 decoded instructions
 * with 64-byte alignment for cache locality.
 */
class DecodeCache {
   public:
    static constexpr size_t kCacheSize =
        4096;  ///< Number of entries in direct-mapped cache (256KB fits in L2)
    static constexpr size_t kCacheMask = kCacheSize - 1;  ///< Bitmask for index calculation

    /**
     * @brief Invalidate all entries in the decode cache.
     */
    void flush() {
        for (auto& entry : cache_) {
            entry.valid = false;
            entry.cpc = ~Register{0};
        }
    }

    /**
     * @brief Calculate the cache entry index for a given program counter.
     * @param pc Program counter address.
     * @return Cache set index in range [0, kCacheSize - 1].
     */
    [[nodiscard]] static constexpr inline auto calc_index(Register pc) noexcept -> size_t {
        return (pc >> 1) & kCacheMask;
    }

    /**
     * @brief Lookup pre-decoded operation for given program counter.
     * @param pc Program counter address.
     * @return Pointer to CachedOp if hit, or nullptr on miss.
     */
    [[nodiscard]] inline auto lookup(Register pc) noexcept -> CachedOp* {
        size_t index = calc_index(pc);
        auto* entry = &cache_[index];
        if (simrv::compiler::likely(entry->valid && entry->cpc == pc)) {
            return entry;
        }
        return nullptr;
    }

    /**
     * @brief Insert pre-decoded operation into the decode cache.
     * @param pc Program counter address.
     * @param op Decoded instruction payload.
     */
    inline void insert(Register pc, const CachedOp& op) {
        size_t index = calc_index(pc);
        cache_[index] = op;
        cache_[index].cpc = pc;
        cache_[index].len = op.cinsn ? 2 : 4;
        cache_[index].valid = true;
    }

   private:
    alignas(64) std::array<CachedOp, kCacheSize> cache_{};
};

}  // namespace simrv::core
