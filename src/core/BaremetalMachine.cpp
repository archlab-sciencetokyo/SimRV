/**
 * @file BaremetalMachine.cpp
 * @brief BaremetalMachine implementation unit.
 */
#include "simrv/core/BaremetalMachine.hpp"

#include <algorithm>
#include <limits>

#include "simrv/core/Logger.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

void BaremetalMachine::execute_cycle() {
    if (runtime_profile.is_cycle_mode()) {
        advance_ca_global_cycle();
    } else if (lockstep() && lockstep()->is_running()) {
        primary_hart().run_cycle(*this);
    } else {
        primary_hart().run_cycle_baremetal(*this);
    }
}

auto BaremetalMachine::execute_fast_batch(uint32_t batch_size) -> bool {
    if (simrv::compiler::likely(can_execute_fast_batch())) {
        if (s_fincnt != std::numeric_limits<Counter>::max()) {
            if (primary_hart().e_icount >= s_fincnt) {
                stop(StopReason::InstructionLimit);
                return true;
            }
            batch_size =
                static_cast<uint32_t>(
                    std::min<Counter>(batch_size, s_fincnt - primary_hart().e_icount));
        }
        primary_hart().run_fast_baremetal_batch(*this, batch_size);
        return true;
    }
    return false;
}

void BaremetalMachine::finalize_cycle() {
    if (simrv::compiler::unlikely(trace().fp_trace.is_open())) {
        trace().write_trace_snapshot();
    }

    if (simrv::compiler::unlikely(tohost != 0)) {
        finalize_cycle_tohost();
    }

    if (simrv::compiler::unlikely(s_fincnt != std::numeric_limits<Counter>::max() &&
                                  primary_hart().e_icount >= s_fincnt)) {
        simrv::log::info("finished by -e option");
        stop(StopReason::InstructionLimit);
    }

    // TUI and PTY readers enqueue bytes asynchronously. Service their UART IRQ immediately on
    // the simulation thread so an interactive guest does not wait for the low-rate host poll.
    if (uart_device() && s_tuimode) {
        uart_device()->service_interrupts();
    } else if (uart_device() && !uart_device()->is_input_thread_running()) {
        if (simrv::compiler::unlikely((primary_hart().clint_mmio.mtime & 8191) == 0)) {
            uart_device()->service_interrupts();
        }
    }
}

}  // namespace simrv::core
