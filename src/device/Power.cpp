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
        if (offset == 0 && req.size >= 2) {
            const Word wdata = req.data;
            const auto cmd = static_cast<PowerCommand>(wdata & 0xffffU);
            if (cmd == PowerCommand::Poweroff) {
                const int status = static_cast<int>(wdata >> 16);
                simrv::log::info("[Power] SiFive Test Finisher: System Poweroff requested (status: {}).", status);
                machine_.exit_code = status;
                machine_.stop();
                if (machine_.s_tuimode && machine_.tui) {
                    machine_.tui->pause_loop();
                }
            } else if (cmd == PowerCommand::Crash) {
                const int status = static_cast<int>(wdata >> 16);
                simrv::log::info("[Power] SiFive Test Finisher: System Fail/Crash requested (status: {}).", status);
                machine_.exit_code = (status != 0) ? status : 1;
                machine_.stop();
                if (machine_.s_tuimode && machine_.tui) {
                    machine_.tui->pause_loop();
                }
            } else if (cmd == PowerCommand::Reboot) {
                simrv::log::info("[Power] SiFive Test Finisher: System Reboot requested.");
                machine_.request_reboot();
            } else {
                simrv::log::warn("[Power] SiFive Test Finisher: Write offset 0, unknown value 0x{:08x}", wdata);
            }
        } else {
            simrv::log::warn("[Power] SiFive Test Finisher: Out-of-bounds or misaligned write to offset 0x{:x}", offset);
            resp.error = true;
        }
    } else if (req.opcode == memory::TlOpcodeA::Get) {
        const Address offset = req.address - kBaseAddress;
        if (offset == 0) {
            // Read from test register always returns 0
            resp.data = 0;
        } else {
            simrv::log::warn("[Power] SiFive Test Finisher: Out-of-bounds read to offset 0x{:x}", offset);
            resp.error = true;
        }
    }

    return true;
}

}  // namespace simrv::device
