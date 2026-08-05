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

struct CachedOp : public simrv::pipeline::DecodedInstruction {
    bool valid = false;
};

class DecodeCache {
   public:
    static constexpr size_t kCacheSize = 8192;
    static constexpr size_t kCacheMask = kCacheSize - 1;

    void flush() {
        for (auto& entry : cache_) {
            entry.valid = false;
        }
    }

    [[nodiscard]] static constexpr inline auto calc_index(Register pc) noexcept -> size_t {
        return ((pc >> 1) ^ (pc >> 13)) & kCacheMask;
    }

    [[nodiscard]] inline auto lookup(Register pc) -> CachedOp* {
        size_t index = calc_index(pc);
        auto* entry = &cache_[index];
        if (entry->valid && entry->cpc == pc) {
            return entry;
        }
        return nullptr;
    }

    inline void insert(Register pc, const CachedOp& op) {
        size_t index = calc_index(pc);
        cache_[index] = op;
        cache_[index].cpc = pc;
        cache_[index].valid = true;
    }

   private:
    std::array<CachedOp, kCacheSize> cache_{};
};

}  // namespace simrv::core
