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
    } else if (spike_lockstep && spike_lockstep->is_running()) {
        cpu.run_cycle(*this);
    } else {
        cpu.run_cycle_baremetal(*this);
    }
}

auto BaremetalMachine::execute_fast_batch(uint32_t batch_size) -> bool {
    if (simrv::compiler::likely(can_execute_fast_batch())) {
        if (s_fincnt != std::numeric_limits<Counter>::max()) {
            if (cpu.e_icount >= s_fincnt) {
                stop(StopReason::InstructionLimit);
                return true;
            }
            batch_size =
                static_cast<uint32_t>(std::min<Counter>(batch_size, s_fincnt - cpu.e_icount));
        }
        cpu.run_fast_baremetal_batch(*this, batch_size);
        return true;
    }
    return false;
}

void BaremetalMachine::finalize_cycle() {
    if (simrv::compiler::unlikely(tracer.fp_trace.is_open())) {
        tracer.write_trace_snapshot();
    }

    if (simrv::compiler::unlikely(tohost != 0)) {
        finalize_cycle_tohost();
    }

    if (simrv::compiler::unlikely(s_fincnt != std::numeric_limits<Counter>::max() &&
                                  cpu.e_icount >= s_fincnt)) {
        simrv::log::info("finished by -e option");
        stop(StopReason::InstructionLimit);
    }

    // TUI and PTY readers enqueue bytes asynchronously. Service their UART IRQ immediately on
    // the simulation thread so an interactive guest does not wait for the low-rate host poll.
    if (uart && s_tuimode) {
        uart->service_interrupts();
    } else if (uart && !uart->is_input_thread_running()) {
        if (simrv::compiler::unlikely((cpu.clint_mmio.mtime & 8191) == 0)) {
            uart->service_interrupts();
        }
    }
}

}  // namespace simrv::core
