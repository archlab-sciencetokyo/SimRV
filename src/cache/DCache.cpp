/**
 * @file DCache.cpp
 * @brief Level-1 Data Cache implementation.
 */
#include "simrv/cache/DCache.hpp"

#include <cstring>

#include "simrv/Define.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/Math.hpp"

namespace simrv::cache {

auto DCache::read(Address addr, Word& data, Instruction funct3) -> bool {
    ++access_tick_;
    const uint32_t set_idx = get_set_index(addr);
    const Address tag = get_tag(addr);
    const unsigned size_bytes = 1u << (funct3 & 0x3u);
    last_accessed_set_ = set_idx;

    for (uint32_t w = 0; w < associativity(); ++w) {
        auto& line = sets_[set_idx][w];
        if (line.valid && line.tag == tag && line.state != simrv::memory::MesiState::Invalid) {
            const uint32_t byte_offset = addr & (kLineBytes - 1u);
            if (simrv::compiler::unlikely(byte_offset + size_bytes > kLineBytes)) {
                ++misses_;
                last_access_was_hit_ = false;
                last_hit_way_ = 0xFFFFFFFF;
                return false;
            }

            Word raw = 0;
            std::memcpy(&raw, line.data.data() + byte_offset, size_bytes);
            data = simrv::memory::extend_loaded_value(raw, static_cast<uint8_t>(funct3));

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

auto DCache::write(Address addr, Word data, Instruction funct3) -> bool {
    const unsigned size_bytes = 1u << (funct3 & 0x3u);
    const uint32_t byte_offset = addr & (kLineBytes - 1u);

    if (simrv::compiler::likely(byte_offset + size_bytes <= kLineBytes)) {
        const uint32_t set_idx = get_set_index(addr);
        const Address tag = get_tag(addr);
        last_accessed_set_ = set_idx;
        for (uint32_t w = 0; w < associativity(); ++w) {
            auto& line = sets_[set_idx][w];
            if (line.valid && line.tag == tag) {
                // Exclusive and Modified both carry TileLink Trunk permission.
                if (line.state == simrv::memory::MesiState::Exclusive ||
                    line.state == simrv::memory::MesiState::Modified) {
                    std::memcpy(line.data.data() + byte_offset, &data, size_bytes);
                    line.state = simrv::memory::MesiState::Modified;
                    line.last_used = ++access_tick_;
                    ++hits_;
                    last_access_was_hit_ = true;
                    last_hit_way_ = w;
                    return true;
                }
                // A Shared line requires a BtoT AcquirePerm upgrade.
                break;
            }
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

auto DCache::handle_probe(const simrv::memory::TlChannelB& req, simrv::memory::TlChannelC& resp,
                          std::array<Byte, kLineBytes>& dirty_data) -> bool {
    const auto target_state = simrv::memory::mesi_for(req.cap);
    const Address line_base = (req.address & ~(static_cast<Address>(kLineBytes - 1u))).raw();
    const uint32_t set_idx = get_set_index(line_base);
    const Address tag = get_tag(line_base);

    for (uint32_t w = 0; w < associativity(); ++w) {
        auto& line = sets_[set_idx][w];
        if (line.valid && line.tag == tag) {
            const bool was_dirty = line.state == simrv::memory::MesiState::Modified;
            if (was_dirty) {
                std::memcpy(dirty_data.data(), line.data.data(), kLineBytes);
                resp.opcode = simrv::memory::TlOpcodeC::ProbeAckData;
            } else {
                resp.opcode = simrv::memory::TlOpcodeC::ProbeAck;
            }
            resp.address = line_base;
            resp.report = simrv::memory::report_for(line.state, req.cap);

            line.state = target_state;
            if (target_state == simrv::memory::MesiState::Invalid) {
                line.valid = false;
            }
            return true;
        }
    }

    // Line was not present in cache
    resp.opcode = simrv::memory::TlOpcodeC::ProbeAck;
    resp.address = line_base;
    resp.report = simrv::memory::TlReport::NtoN;
    return false;
}

}  // namespace simrv::cache
