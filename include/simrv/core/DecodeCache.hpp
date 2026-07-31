#pragma once

#include <array>
#include <optional>

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

    [[nodiscard]] auto lookup(Register pc) -> CachedOp* {
        size_t index = (pc >> 1) & kCacheMask;
        auto* entry = &cache_[index];
        if (entry->valid && entry->cpc == pc) {
            return entry;
        }
        return nullptr;
    }

    void insert(Register pc, const CachedOp& op) {
        size_t index = (pc >> 1) & kCacheMask;
        cache_[index] = op;
        cache_[index].cpc = pc;
        cache_[index].valid = true;
    }

   private:
    std::array<CachedOp, kCacheSize> cache_{};
};

}  // namespace simrv::core
