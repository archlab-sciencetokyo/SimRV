/**
 * @file ICache.cpp
 * @brief Level-1 Instruction Cache implementation.
 */
#include "simrv/cache/ICache.hpp"

#include <cstring>

#include "simrv/Define.hpp"

namespace simrv::cache {

auto ICache::read(Address addr, uint32_t& data) -> bool {
    ++access_tick_;
    const uint32_t set_idx = get_set_index(addr);
    const Address tag = get_tag(addr);
    last_accessed_set_ = set_idx;

    for (uint32_t w = 0; w < kWays; ++w) {
        auto& line = sets_.at(set_idx).at(w);
        if (line.valid && line.tag == tag) {
            const uint32_t byte_offset = addr & (kLineBytes - 1u);
            if (simrv::compiler::unlikely(byte_offset + sizeof(uint32_t) > kLineBytes)) {
                ++misses_;
                last_access_was_hit_ = false;
                last_hit_way_ = 0xFFFFFFFF;
                return false;
            }
            std::memcpy(&data, line.data.data() + byte_offset, sizeof(uint32_t));
            line.last_used = access_tick_;
            ++hits_;
            last_access_was_hit_ = true;
            last_hit_way_ = w;
            return true;
        }
    }

    ++misses_;
    last_access_was_hit_ = false;
    last_hit_way_ = 0xFFFFFFFF;
    return false;
}

auto ICache::read16(Address addr, uint16_t& data) -> bool {
    ++access_tick_;
    const uint32_t set_idx = get_set_index(addr);
    const Address tag = get_tag(addr);
    last_accessed_set_ = set_idx;

    for (uint32_t w = 0; w < kWays; ++w) {
        auto& line = sets_.at(set_idx).at(w);
        if (line.valid && line.tag == tag) {
            const uint32_t byte_offset = addr & (kLineBytes - 1u);
            if (simrv::compiler::unlikely(byte_offset + sizeof(uint16_t) > kLineBytes)) {
                ++misses_;
                last_access_was_hit_ = false;
                last_hit_way_ = 0xFFFFFFFF;
                return false;
            }
            std::memcpy(&data, line.data.data() + byte_offset, sizeof(uint16_t));
            line.last_used = access_tick_;
            ++hits_;
            last_access_was_hit_ = true;
            last_hit_way_ = w;
            return true;
        }
    }

    ++misses_;
    last_access_was_hit_ = false;
    last_hit_way_ = 0xFFFFFFFF;
    return false;
}

}  // namespace simrv::cache