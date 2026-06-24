/**
 * @file BaremetalMachine.cpp
 * @brief BaremetalMachine implementation unit.
 */
#include "simrv/core/BaremetalMachine.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/xlen/Types.hpp"
#include <limits>

namespace simrv::core {

using namespace simrv::isa;

void BaremetalMachine::run() {
    if (s_tuimode && uart) {
        uart->tui_pause_loop();
    }

    uint32_t cycle_count = 0;

    cpu.evaluate_timer_interrupt();

    while (is_running_) {
        cpu.run_cycle_baremetal(*this);

        // ---- TUI Breakpoint Pause hook ----
        if (s_tuimode && uart) {
            const bool hit_ebreak =
                (cpu.pipeline_context.opcode == Opcode::System) &&
                (cpu.pipeline_context.funct12 ==
                    static_cast<Word>(Funct12Priv::Ebreak));
            if (hit_ebreak) {
                uart->tui_pause_loop();
            }
        }

        if (simrv::compiler::unlikely(tohost != 0)) {
            finalize_cycle_tohost();
        }

        if (simrv::compiler::unlikely(s_fincnt != std::numeric_limits<Counter>::max() && cpu.e_icount >= s_fincnt)) {
            simrv::log::info("finished by -e option");
            is_running_ = false;
        }

        cycle_count++;
        if (simrv::compiler::unlikely(cycle_count >= 1024)) {
            cycle_count = 0;

            // Tick the CLINT timer: 1024 CPU cycles is approximately 102 CLINT timer ticks
            cpu.clint_mmio.mtime += 102;
            cpu.evaluate_timer_interrupt();

            if (uart) {
                uart->non_tui_poll_input();
            }
        }
    }
}

} // namespace simrv::core
