/**
 * @file DCache.cpp
 * @brief Level-1 Data Cache implementation.
 */
#include "simrv/cache/DCache.hpp"

#include <cstring>

#include "simrv/Define.hpp"
#include "simrv/xlen/Math.hpp"

namespace simrv::cache {

auto DCache::read(Address addr, Word& data, Instruction funct3) -> bool {
    ++access_tick_;
    const uint32_t set_idx = get_set_index(addr);
    const Address tag = get_tag(addr);
    const unsigned size_bytes = 1u << (funct3 & 0x3u);
    last_accessed_set_ = set_idx;

    for (uint32_t w = 0; w < kWays; ++w) {
        auto& line = sets_.at(set_idx).at(w);
        if (line.valid && line.tag == tag) {
            const uint32_t byte_offset = addr & (kLineBytes - 1u);
            if (simrv::compiler::unlikely(byte_offset + size_bytes > kLineBytes)) {
                ++misses_;
                last_access_was_hit_ = false;
                last_hit_way_ = 0xFFFFFFFF;
                return false;
            }

            Word raw = 0;
            std::memcpy(&raw, line.data.data() + byte_offset, size_bytes);

            if ((funct3 & 0x4u) == 0) {  // Signed load
                data = sign_extend(raw, 8 * size_bytes);
            } else {
                data = raw;
            }

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

void DCache::write(Address addr, Word data, Instruction funct3) {
    const unsigned size_bytes = 1u << (funct3 & 0x3u);
    const uint32_t byte_offset = addr & (kLineBytes - 1u);

    if (simrv::compiler::likely(byte_offset + size_bytes <= kLineBytes)) {
        const uint32_t set_idx = get_set_index(addr);
        const Address tag = get_tag(addr);
        last_accessed_set_ = set_idx;
        bool found = false;
        uint32_t found_way = 0xFFFFFFFF;
        for (uint32_t w = 0; w < kWays; ++w) {
            auto& line = sets_.at(set_idx).at(w);
            if (line.valid && line.tag == tag) {
                std::memcpy(line.data.data() + byte_offset, &data, size_bytes);
                found = true;
                found_way = w;
                break;
            }
        }
        last_access_was_hit_ = found;
        last_hit_way_ = found ? found_way : 0xFFFFFFFF;
    } else {
        const Address tag1 = get_tag(addr);
        const uint32_t set_idx1 = get_set_index(addr);
        last_accessed_set_ = set_idx1;
        last_access_was_hit_ = false;
        for (auto& line : sets_.at(set_idx1)) {
            if (line.valid && line.tag == tag1) {
                line.valid = false;
                break;
            }
        }

        const Address addr2 = addr + (kLineBytes - byte_offset);
        const Address tag2 = get_tag(addr2);
        const uint32_t set_idx2 = get_set_index(addr2);
        for (auto& line : sets_.at(set_idx2)) {
            if (line.valid && line.tag == tag2) {
                line.valid = false;
                break;
            }
        }
    }
}

}  // namespace simrv::cache