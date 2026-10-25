/**
 * @file DCache.hpp
 * @brief Level-1 Data Cache interface.
 */
#pragma once

#include "simrv/CompilerHints.hpp"
#include "simrv/cache/BaseCache.hpp"
#include "simrv/cache/DataPrefetcher.hpp"
#include "simrv/isa/InstructionFields.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::cache {

class DCache : public BaseCache<1024, 32, 8> {
   public:
    DCache() = default;

    [[nodiscard]] auto read(simrv::xlen::Address addr, simrv::xlen::Word& data,
                            simrv::xlen::Instruction funct3) -> bool;
    [[nodiscard]] auto write(simrv::xlen::Address addr, simrv::xlen::Word data,
                             simrv::xlen::Instruction funct3, bool* out_first_write = nullptr)
        -> bool;

    [[nodiscard]] auto read(simrv::xlen::Address addr, simrv::xlen::Word& data, isa::Funct3 funct3)
        -> bool {
        return read(addr, data, static_cast<simrv::xlen::Instruction>(funct3));
    }
    [[nodiscard]] auto write(simrv::xlen::Address addr, simrv::xlen::Word data, isa::Funct3 funct3,
                             bool* out_first_write = nullptr) -> bool {
        return write(addr, data, static_cast<simrv::xlen::Instruction>(funct3), out_first_write);
    }

    [[nodiscard]] auto prefetcher() noexcept -> DataPrefetcher& { return prefetcher_; }
    [[nodiscard]] auto prefetcher() const noexcept -> const DataPrefetcher& { return prefetcher_; }

    auto handle_probe(const simrv::memory::TlChannelB& req, simrv::memory::TlChannelC& resp,
                      std::array<simrv::xlen::Byte, kLineBytes>& dirty_data) -> bool;

   private:
    DataPrefetcher prefetcher_{};
};

inline auto DCache::read(simrv::xlen::Address addr, simrv::xlen::Word& data,
                         simrv::xlen::Instruction funct3) -> bool {
    if (simrv::compiler::unlikely(prefetcher_.is_enabled())) {
        (void)prefetcher_.observe(addr, kLineBytes);
    }
    const uint32_t set_idx = get_set_index(addr);
    const simrv::xlen::Address tag = get_tag(addr);
    const unsigned size_bytes = 1u << (funct3 & 0x3u);
    last_accessed_set_ = set_idx;

    const auto [w, cache_line] = find_matching_line(set_idx, tag);
    if (simrv::compiler::likely(cache_line != nullptr)) {
        const uint32_t byte_offset = addr & (kLineBytes - 1u);
        if (simrv::compiler::unlikely(byte_offset + size_bytes > kLineBytes)) {
            ++misses_;
            last_access_was_hit_ = false;
            last_hit_way_ = 0xFFFFFFFF;
            return false;
        }

        simrv::xlen::Word raw = 0;
        std::memcpy(&raw, cache_line->data.data() + byte_offset, size_bytes);
        data = simrv::memory::extend_loaded_value(raw, static_cast<uint8_t>(funct3));

        if (simrv::compiler::unlikely(cache_line->is_prefetched)) {
            prefetcher_.record_prefetch_hit();
            cache_line->is_prefetched = false;
        }

        cache_line->last_used = ++access_tick_;
        ++hits_;
        last_access_was_hit_ = true;
        last_hit_way_ = w;
        return true;
    }
    ++misses_;
    last_access_was_hit_ = false;
    last_hit_way_ = 0xFFFFFFFF;
    return false;
}

inline auto DCache::write(simrv::xlen::Address addr, simrv::xlen::Word data,
                          simrv::xlen::Instruction funct3, bool* out_first_write) -> bool {
    if (simrv::compiler::unlikely(prefetcher_.is_enabled())) {
        (void)prefetcher_.observe(addr, kLineBytes);
    }
    const unsigned size_bytes = 1u << (funct3 & 0x3u);
    const uint32_t byte_offset = addr & (kLineBytes - 1u);

    if (simrv::compiler::likely(byte_offset + size_bytes <= kLineBytes)) {
        const uint32_t set_idx = get_set_index(addr);
        const simrv::xlen::Address tag = get_tag(addr);
        last_accessed_set_ = set_idx;
        const auto [w, cache_line] = find_matching_line(set_idx, tag);
        if (cache_line != nullptr) {
            // Exclusive and Modified both carry TileLink Trunk permission.
            if (cache_line->state == simrv::memory::MesiState::Exclusive ||
                cache_line->state == simrv::memory::MesiState::Modified) {
                if (out_first_write != nullptr) {
                    *out_first_write = (cache_line->state == simrv::memory::MesiState::Exclusive);
                }
                std::memcpy(cache_line->data.data() + byte_offset, &data, size_bytes);
                cache_line->state = simrv::memory::MesiState::Modified;
                cache_line->last_used = ++access_tick_;
                ++hits_;
                last_access_was_hit_ = true;
                last_hit_way_ = w;
                return true;
            }
            // A Shared line requires a BtoT AcquirePerm upgrade.
        }
        ++misses_;
        last_access_was_hit_ = false;
        last_hit_way_ = 0xFFFFFFFF;
        return false;
    }

    // Crosses cache line boundary
    ++misses_;
    last_access_was_hit_ = false;
    last_hit_way_ = 0xFFFFFFFF;
    return false;
}

}  // namespace simrv::cache
