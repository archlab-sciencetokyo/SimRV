/**
 * @file BaremetalMachine.cpp
 * @brief BaremetalMachine implementation unit.
 */
#include "simrv/core/BaremetalMachine.hpp"

#include <chrono>
#include <limits>

#include "simrv/core/Logger.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

using namespace simrv::isa;

void BaremetalMachine::run() {
    cpu.evaluate_timer_interrupt();

    // Start background stdin input thread for non-TUI mode
    if (uart && !s_tuimode) {
        uart->start_input_thread();
    }

    // Execution batch size to balance timer update checks with throughput
    constexpr uint32_t kBatchSize = 65536;
    uint32_t cycle_count = 0;

    // Fast-path execution branching between TUI interactive mode and baremetal mode

    if (s_tuimode && tui) {
        // ---- TUI / debug path (ebreak-aware, single-threaded) ----
        while (is_running() &&
               execution_state_.load(std::memory_order_relaxed) != ExecutionState::Stopped) {
            if (tui->is_tui_paused()) {
                tui->set_sim_thread_sleeping(true);
                execution_state_.wait(ExecutionState::Paused, std::memory_order_relaxed);
                if (execution_state_.load(std::memory_order_relaxed) == ExecutionState::Paused) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                continue;
            }
            tui->set_sim_thread_sleeping(false);
            if (s_cycle_accurate) {
                cpu.run_cycle(*this);
            } else {
                cpu.run_cycle_baremetal(*this);
            }
            tui->on_cycle_completed();

            if (is_stepping()) {
                execution_state_.store(ExecutionState::Paused, std::memory_order_release);
                tui->set_paused(true);
            }

            if (simrv::compiler::unlikely(tracer.fp_trace.is_open())) {
                tracer.write_trace_snapshot();
            }

            // Ebreak pause hook
            const bool hit_ebreak =
                (cpu.pipeline_context.opcode == Opcode::System) &&
                (cpu.pipeline_context.funct12 == static_cast<Word>(Funct12Priv::Ebreak));
            if (simrv::compiler::unlikely(hit_ebreak)) {
                tui->pause_loop();
            }

            if (simrv::compiler::unlikely(tohost != 0)) {
                finalize_cycle_tohost();
            }

            if (simrv::compiler::unlikely(s_fincnt != std::numeric_limits<Counter>::max() &&
                                          cpu.e_icount >= s_fincnt)) {
                simrv::log::info("finished by -e option");
                stop();
            }

            cycle_count++;
            if (simrv::compiler::unlikely(cycle_count >= kBatchSize)) {
                cycle_count = 0;

                if (!s_multithreaded && tui) {
                    if (simrv::tui::g_resized) {
                        tui->render();
                    }
                    static uint64_t last_tui_check_cycles = 0;
                    if (cpu.e_icount - last_tui_check_cycles >= 1000 ||
                        tui->step_delay_us_.load(std::memory_order_relaxed) > 0) {
                        last_tui_check_cycles = cpu.e_icount;
                        static auto last_tui_update = std::chrono::steady_clock::now();
                        auto tui_now = std::chrono::steady_clock::now();
                        if (tui_now - last_tui_update >= std::chrono::milliseconds(33)) {
                            tui->update();
                            tui->render();
                            last_tui_update = tui_now;
                        }
                    }
                }
            }
        }
    } else {
        // Fast path: GUI / ISA-test / non-TUI mode
        while (simrv::compiler::likely(is_running_.load(std::memory_order_relaxed)) &&
               execution_state_.load(std::memory_order_relaxed) != ExecutionState::Stopped) {
            if (simrv::compiler::likely(s_high_performance && !s_tuimode && !s_lockstep_mode &&
                                        !s_gdb_mode && !s_bp_trace && s_strace == 0 &&
                                        !breakpoints.has_any() && !s_rollback_enabled)) {
                cpu.run_fast_baremetal_batch(*this, kBatchSize);
            } else {
                cpu.run_cycle_baremetal(*this);
            }

            if (simrv::compiler::unlikely(tracer.fp_trace.is_open())) {
                tracer.write_trace_snapshot();
            }

            if (simrv::compiler::unlikely(tohost != 0)) {
                finalize_cycle_tohost();
            }

            if (simrv::compiler::unlikely(s_fincnt != std::numeric_limits<Counter>::max() &&
                                          cpu.e_icount >= s_fincnt)) {
                simrv::log::info("finished by -e option");
                is_running_ = false;
            }

            if (uart) {
                uart->non_tui_poll_input();
            }

            if (!s_multithreaded && sdl_display) {
                sdl_display->update(cpu.e_icount);
            }
        }
    }

    // Clean up background input thread
    if (uart && !s_tuimode) {
        uart->stop_input_thread();
    }
}

}  // namespace simrv::core
