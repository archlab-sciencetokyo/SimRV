/**
 * @file DCache.hpp
 * @brief Level-1 Data Cache interface.
 */
#pragma once

#include "simrv/cache/BaseCache.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::cache {

class DCache : public BaseCache<1024, 32, 8> {
   public:
    DCache() = default;

    [[nodiscard]] auto read(Address addr, Word& data, Instruction funct3) -> bool;
    [[nodiscard]] auto write(Address addr, Word data, Instruction funct3,
                             bool* out_first_write = nullptr) -> bool;

    auto handle_probe(const simrv::memory::TlChannelB& req, simrv::memory::TlChannelC& resp,
                      std::array<Byte, kLineBytes>& dirty_data) -> bool;
};

inline auto DCache::read(Address addr, Word& data, Instruction funct3) -> bool {
    const uint32_t set_idx = get_set_index(addr);
    const Address tag = get_tag(addr);
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

        Word raw = 0;
        std::memcpy(&raw, cache_line->data.data() + byte_offset, size_bytes);
        data = simrv::memory::extend_loaded_value(raw, static_cast<uint8_t>(funct3));

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

inline auto DCache::write(Address addr, Word data, Instruction funct3, bool* out_first_write)
    -> bool {
    const unsigned size_bytes = 1u << (funct3 & 0x3u);
    const uint32_t byte_offset = addr & (kLineBytes - 1u);

    if (simrv::compiler::likely(byte_offset + size_bytes <= kLineBytes)) {
        const uint32_t set_idx = get_set_index(addr);
        const Address tag = get_tag(addr);
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
