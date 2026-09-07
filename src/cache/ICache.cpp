/**
 * @file ICache.cpp
 * @brief Level-1 Instruction Cache implementation.
 */
#include "simrv/cache/ICache.hpp"

#include <cstring>
#include <utility>

#include "simrv/Define.hpp"

namespace simrv::cache {

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
