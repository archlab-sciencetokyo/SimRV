/**
 * @file ICache.cpp
 * @brief Level-1 Instruction Cache implementation.
 */
#include "simrv/cache/ICache.hpp"

#include <cstring>
#include <utility>

#include "simrv/Define.hpp"

namespace simrv::cache {

auto ICache::read(Address addr, uint32_t& data) -> bool {
    ++access_tick_;
    const uint32_t set_idx = get_set_index(addr);
    const Address tag = get_tag(addr);
    last_accessed_set_ = set_idx;

    for (uint32_t w = 0; w < associativity(); ++w) {
        auto& line = sets_[set_idx][w];
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

    for (uint32_t w = 0; w < associativity(); ++w) {
        auto& line = sets_[set_idx][w];
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

auto ICache::handle_probe(const simrv::memory::TlChannelB& req, simrv::memory::TlChannelC& resp)
    -> bool {
    const Address line_base = (req.address & ~(static_cast<Address>(kLineBytes - 1u))).raw();
    const uint32_t set_idx = get_set_index(line_base);
    const Address tag = get_tag(line_base);

    resp.opcode = simrv::memory::TlOpcodeC::ProbeAck;
    resp.address = line_base;
    resp.report = simrv::memory::TlReport::NtoN;

    for (uint32_t w = 0; w < associativity(); ++w) {
        auto& line = sets_[set_idx][w];
        if (line.valid && line.tag == tag) {
            resp.report = simrv::memory::report_for(line.state, req.cap);
            line.state = simrv::memory::mesi_for(req.cap);
            line.valid = line.state != simrv::memory::MesiState::Invalid;
            return true;
        }
    }
    return false;
}

}  // namespace simrv::cache
