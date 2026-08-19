/**
 * @file BaremetalMachine.cpp
 * @brief BaremetalMachine implementation unit.
 */
#include "simrv/core/BaremetalMachine.hpp"

#include <algorithm>
#include <chrono>
#include <limits>

#include "simrv/core/Logger.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

void BaremetalMachine::execute_cycle() {
    if (s_cycle_accurate || (spike_lockstep && spike_lockstep->is_running())) {
        cpu.run_cycle(*this);
    } else {
        cpu.run_cycle_baremetal(*this);
    }
}

auto BaremetalMachine::execute_fast_batch(uint32_t batch_size) -> bool {
    if (simrv::compiler::likely(s_high_performance &&
                                (!s_tuimode || (tui && !tui->is_trace_active())) &&
                                !s_lockstep_mode && !s_gdb_mode && !s_bp_trace && s_strace == 0 &&
                                !breakpoints.has_any() && !s_rollback_enabled)) {
        if (s_fincnt != std::numeric_limits<Counter>::max()) {
            if (cpu.e_icount >= s_fincnt) {
                stop();
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
        stop();
    }

    if (s_tuimode && !s_multithreaded && tui) {
        if (simrv::tui::g_resized) {
            tui->render();
        }
        if (cpu.e_icount - last_tui_check_cycles_ >= 1000 ||
            tui->step_delay_us_.load(std::memory_order_relaxed) > 0) {
            last_tui_check_cycles_ = cpu.e_icount;
            auto now = std::chrono::steady_clock::now();
            if (now - last_tui_update_ >= std::chrono::milliseconds(33)) {
                tui->update();
                tui->update_cache();
                tui->render();
                last_tui_update_ = now;
            }
        }
    } else if (uart && !uart->is_input_thread_running()) {
        if (simrv::compiler::unlikely((cpu.clint_mmio.mtime & 8191) == 0)) {
            uart->non_tui_poll_input();
        }
    }
}

}  // namespace simrv::core
