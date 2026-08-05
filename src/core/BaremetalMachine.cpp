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

    // Wall-clock time reference for mtime advancement (10 MHz RTC = 10 ticks/µs = 1 tick/100ns)
    auto last_mtime_update = std::chrono::high_resolution_clock::now();

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
        while (is_running_) {
            if (tui->is_tui_paused()) {
                tui->set_sim_thread_sleeping(true);
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                continue;
            }
            tui->set_sim_thread_sleeping(false);
            if (s_cycle_accurate) {
                cpu.run_cycle(*this);
            } else {
                cpu.run_cycle_baremetal(*this);
            }
            tui->on_cycle_completed();

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

                auto now = std::chrono::high_resolution_clock::now();
                uint64_t elapsed_ns = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_mtime_update)
                        .count());
                last_mtime_update = now;
                cpu.clint_mmio.mtime += elapsed_ns / 100;
                cpu.evaluate_timer_interrupt();

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
        // ---- Fast path: GUI / ISA-test / no-TUI ----
        // No ebreak check, no TUI branches, no uart poll in hot path.
        // SDL rendering stays entirely in the main thread (optimisation 2).
        while (is_running_) {
            cpu.run_cycle_baremetal(*this);

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

            cycle_count++;
            if (simrv::compiler::unlikely(cycle_count >= kBatchSize)) {
                cycle_count = 0;

                // Advance CLINT mtime from wall clock
                auto now = std::chrono::high_resolution_clock::now();
                uint64_t elapsed_ns = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::nanoseconds>(now - last_mtime_update)
                        .count());
                last_mtime_update = now;
                cpu.clint_mmio.mtime += elapsed_ns / 100;
                cpu.evaluate_timer_interrupt();

                if (uart) {
                    uart->non_tui_poll_input();
                }

                // --- Optimisation 2: SDL update only from main thread in MT mode ---
                // In multithreaded mode the main thread calls update_gui_only() at
                // 60 fps; we must not touch SDL from the sim thread.
                // In single-threaded mode (e.g. ISA tests with -g) we still need it.
                if (!s_multithreaded && sdl_display) {
                    sdl_display->update(cpu.e_icount);
                }
            }
        }
    }

    // Clean up background input thread
    if (uart && !s_tuimode) {
        uart->stop_input_thread();
    }
}

}  // namespace simrv::core
