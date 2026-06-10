/**
 * @file ICache.cpp
 * @brief Level-1 Instruction Cache implementation.
 */
#include "simrv/cache/ICache.hpp"

#include <algorithm>
#include <cstring>

#include "simrv/Define.hpp"

namespace simrv::cache {

ICache::ICache() = default;

auto ICache::read(Address addr, uint32_t& data) -> bool {
    ++access_tick_;
    const uint32_t set_idx = get_set_index(addr);
    const Address tag = get_tag(addr);

    for (auto& line : sets_.at(set_idx)) {
        if (line.valid && line.tag == tag) {
            const uint32_t byte_offset = addr & (kLineBytes - 1u);
            if (simrv::compiler::unlikely(byte_offset + sizeof(uint32_t) > kLineBytes)) {
                ++misses_;
                return false;
            }
            std::memcpy(&data, line.data.data() + byte_offset, sizeof(uint32_t));
            line.last_used = access_tick_;
            ++hits_;
            return true;
        }
    }

    ++misses_;
    return false;
}

auto ICache::read16(Address addr, uint16_t& data) -> bool {
    ++access_tick_;
    const uint32_t set_idx = get_set_index(addr);
    const Address tag = get_tag(addr);

    for (auto& line : sets_.at(set_idx)) {
        if (line.valid && line.tag == tag) {
            const uint32_t byte_offset = addr & (kLineBytes - 1u);
            if (simrv::compiler::unlikely(byte_offset + sizeof(uint16_t) > kLineBytes)) {
                ++misses_;
                return false;
            }
            std::memcpy(&data, line.data.data() + byte_offset, sizeof(uint16_t));
            line.last_used = access_tick_;
            ++hits_;
            return true;
        }
    }

    ++misses_;
    return false;
}

void ICache::insert(Address base_addr, const Byte* line_data) {
    ++access_tick_;
    const uint32_t set_idx = get_set_index(base_addr);
    const Address tag = get_tag(base_addr);
    auto& set = sets_.at(set_idx);
    CacheLine* victim = &set.at(0);
    for (auto& line : set) {
        if (!line.valid) {
            victim = &line;
            break;
        }
        if (line.last_used < victim->last_used) {
            victim = &line;
        }
    }
    victim->tag = tag;
    victim->valid = true;
    victim->last_used = access_tick_;
    std::memcpy(victim->data.data(), line_data, kLineBytes);
}

void ICache::flush() {
    for (auto& set : sets_) {
        std::ranges::fill(set, CacheLine{});
    }
    hits_ = 0;
    misses_ = 0;
    access_tick_ = 0;
}

}  // namespace simrv::cache