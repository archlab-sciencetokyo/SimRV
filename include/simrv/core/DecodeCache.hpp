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
 * @brief 2-way set-associative cache for pre-decoded instructions to bypass fetch/decode stages.
 *
 * Indexed by XOR-hash of PC (`((pc >> 1) ^ (pc >> 13)) & kSetMask`), caching 4096 decoded
 * instructions (2048 2-way sets) with 64-byte alignment for cache locality and 1-bit pseudo-LRU
 * replacement.
 */
class DecodeCache {
   public:
    static constexpr size_t kNumSets = 2048;            ///< Number of 2-way associative sets
    static constexpr size_t kSetMask = kNumSets - 1;    ///< Bitmask for set index calculation
    static constexpr size_t kCacheSize = kNumSets * 2;  ///< Total cached instruction entries (4096)

    struct alignas(64) CacheSet {
        std::array<CachedOp, 2> ways{};
        uint32_t lru_way = 0;
    };

    /**
     * @brief Invalidate all entries in the decode cache.
     */
    void flush() {
        for (auto& set : sets_) {
            set.ways[0].valid = false;
            set.ways[0].cpc = VirtAddr{~Register{0}};
            set.ways[1].valid = false;
            set.ways[1].cpc = VirtAddr{~Register{0}};
            set.lru_way = 0;
        }
    }

    /**
     * @brief Calculate the cache set index for a given program counter with bit-mixed XOR hashing.
     * @param pc Program counter address.
     * @return Cache set index in range [0, kNumSets - 1].
     */
    [[nodiscard]] static constexpr inline auto calc_set(Register pc) noexcept -> size_t {
        return ((pc >> 1) ^ (pc >> 13)) & kSetMask;
    }
    [[nodiscard]] static constexpr inline auto calc_set(VirtAddr pc) noexcept -> size_t {
        return ((pc.raw() >> 1) ^ (pc.raw() >> 13)) & kSetMask;
    }

    /**
     * @brief Lookup pre-decoded operation for given program counter.
     * @param pc Program counter address.
     * @return Pointer to CachedOp if hit, or nullptr on miss.
     */
    [[nodiscard]] inline auto lookup(Register pc) noexcept -> CachedOp* {
        const size_t set_idx = calc_set(pc);
        auto& set = sets_[set_idx];
        if (simrv::compiler::likely(set.ways[0].valid && set.ways[0].cpc == pc)) {
            set.lru_way = 1;
            return &set.ways[0];
        }
        if (set.ways[1].valid && set.ways[1].cpc == pc) {
            set.lru_way = 0;
            return &set.ways[1];
        }
        return nullptr;
    }
    [[nodiscard]] inline auto lookup(VirtAddr pc) noexcept -> CachedOp* { return lookup(pc.raw()); }

    /**
     * @brief Insert pre-decoded operation into the decode cache using LRU replacement.
     * @param pc Program counter address.
     * @param op Decoded instruction payload.
     */
    inline void insert(Register pc, const CachedOp& op) {
        const size_t set_idx = calc_set(pc);
        auto& set = sets_[set_idx];
        const uint32_t way = set.lru_way;
        auto& entry = set.ways[way];
        entry = op;
        entry.cpc = VirtAddr{pc};
        entry.len = op.cinsn ? 2 : 4;
        entry.valid = true;
        set.lru_way = 1 - way;
    }
    inline void insert(VirtAddr pc, const CachedOp& op) { insert(pc.raw(), op); }

   private:
    std::array<CacheSet, kNumSets> sets_{};
};

}  // namespace simrv::core
