/**
 * @file Rtc.cpp
 * @brief Real-Time Clock (RTC) MMIO device implementation.
 */
#include "simrv/device/Rtc.hpp"

#include "simrv/core/Machine.hpp"

namespace simrv {

Rtc::Rtc(simrv::core::Machine& machine) : machine_(machine) {}

auto Rtc::handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool {
    if (req.opcode == memory::TlOpcodeA::Get) {
        const Address offset = req.address - kBaseAddress;
        if (offset == kRtcOffset) {
            resp.data = static_cast<Word>(machine_.cpu.clint_mmio.mtime);
        } else if (offset == kRtcOffset + 4) {
            resp.data = static_cast<Word>(machine_.cpu.clint_mmio.mtime >> 32);
        } else {
            resp.data = 0;
        }
    }
    return true;
}

}  // namespace simrv
