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

    for (auto& line : sets_.at(set_idx)) {
        if (line.valid && line.tag == tag) {
            const uint32_t byte_offset = addr & (kLineBytes - 1u);
            if (simrv::compiler::unlikely(byte_offset + size_bytes > kLineBytes)) {
                ++misses_;
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
            return true;
        }
    }
    ++misses_;
    return false;
}

void DCache::write(Address addr, Word data, Instruction funct3) {
    const unsigned size_bytes = 1u << (funct3 & 0x3u);
    const uint32_t byte_offset = addr & (kLineBytes - 1u);

    if (simrv::compiler::likely(byte_offset + size_bytes <= kLineBytes)) {
        const uint32_t set_idx = get_set_index(addr);
        const Address tag = get_tag(addr);
        for (auto& line : sets_.at(set_idx)) {
            if (line.valid && line.tag == tag) {
                std::memcpy(line.data.data() + byte_offset, &data, size_bytes);
                break;
            }
        }
    } else {
        const Address tag1 = get_tag(addr);
        const uint32_t set_idx1 = get_set_index(addr);
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