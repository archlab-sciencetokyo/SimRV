/**
 * @file Power.cpp
 * @brief SiFive Test Finisher syscon power device implementation.
 */
#include "simrv/device/Power.hpp"

#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/tui/Tui.hpp"

namespace simrv::device {

PowerMmio::PowerMmio(simrv::core::Machine& machine) : machine_(machine) {}

auto PowerMmio::handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool {
    resp.error = false;
    resp.data = 0;

    const bool is_write = (req.opcode == memory::TlOpcodeA::PutFullData ||
                           req.opcode == memory::TlOpcodeA::PutPartialData);

    if (is_write) {
        const Address offset = req.address - kBaseAddress;
        // The finisher has a single 32-bit register at offset 0
        if (offset == 0 && req.size == 2) {
            const Word wdata = req.data;
            const auto cmd = static_cast<PowerCommand>(wdata & 0xffffU);
            switch (cmd) {
                case PowerCommand::Poweroff: {
                    const int status = static_cast<int>(wdata >> 16);
                    simrv::log::info(
                        "[Power] SiFive Test Finisher: System Poweroff requested (status: {}).",
                        status);
                    machine_.exit_code = status;
                    machine_.stop(simrv::core::Machine::StopReason::GuestPoweroff);
                    break;
                }
                case PowerCommand::Crash: {
                    const int status = static_cast<int>(wdata >> 16);
                    simrv::log::info(
                        "[Power] SiFive Test Finisher: System Fail/Crash requested (status: {}).",
                        status);
                    machine_.exit_code = (status != 0) ? status : 1;
                    machine_.stop(simrv::core::Machine::StopReason::GuestCrash);
                    break;
                }
                case PowerCommand::Reboot:
                    simrv::log::info("[Power] SiFive Test Finisher: System Reboot requested.");
                    machine_.request_reboot();
                    break;
                case PowerCommand::Exit: {
                    const int status = static_cast<int>(wdata >> 16);
                    simrv::log::info("[Power] SimRV: Simulator exit requested (status: {}).",
                                     status);
                    machine_.request_exit(status);
                    break;
                }
                default:
                    simrv::log::warn(
                        "[Power] SiFive Test Finisher: Write offset 0, unknown value 0x{:08x}",
                        wdata);
                    break;
            }
        } else {
            simrv::log::warn(
                "[Power] SiFive Test Finisher: Expected a 32-bit write at offset 0; got offset "
                "0x{:x}, size 2^{}",
                offset, req.size);
            resp.error = true;
        }
    } else if (req.opcode == memory::TlOpcodeA::Get) {
        const Address offset = req.address - kBaseAddress;
        if (offset == 0 && req.size == 2) {
            // Read from test register always returns 0
            resp.data = 0;
        } else {
            simrv::log::warn("[Power] SiFive Test Finisher: Out-of-bounds read to offset 0x{:x}",
                             offset);
            resp.error = true;
        }
    }

    return true;
}

}  // namespace simrv::device
