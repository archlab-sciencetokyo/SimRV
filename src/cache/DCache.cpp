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
