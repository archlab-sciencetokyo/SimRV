/**
 * @file OSMachine.cpp
 * @brief OSMachine implementation unit.
 */
#include "simrv/core/OSMachine.hpp"

#include <array>
#include <chrono>
#include <limits>
#include <utility>

#include "simrv/core/Logger.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

using namespace simrv::isa;

void OSMachine::run() {
    cpu.evaluate_timer_interrupt();

    // Start background stdin input thread (removes uart poll from hot path)
    if (uart && !s_tuimode) {
        uart->start_input_thread();
    }

    // Select debug/lockstep/TUI path or direct execution loop
    const bool has_debug = (gdb_stub && gdb_stub->is_connected()) ||
                           (spike_lockstep && spike_lockstep->is_running()) || (s_tuimode && tui);

    if (has_debug) {
        // ---- Debug path: GDB / lockstep / TUI ebreak ----
        while (is_running() &&
               execution_state_.load(std::memory_order_relaxed) != ExecutionState::Stopped) {
            if (s_tuimode && tui && tui->is_tui_paused()) {
                tui->set_sim_thread_sleeping(true);
                execution_state_.wait(ExecutionState::Paused, std::memory_order_relaxed);
                if (execution_state_.load(std::memory_order_relaxed) == ExecutionState::Paused) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
                continue;
            }
            if (s_tuimode && tui) {
                tui->set_sim_thread_sleeping(false);
            }
            prepare_cycle();
            cpu.run_cycle(*this);
            finalize_cycle();
            if (s_tuimode && tui) {
                tui->on_cycle_completed();
            }

            if (is_stepping()) {
                execution_state_.store(ExecutionState::Paused, std::memory_order_release);
                if (s_tuimode && tui) {
                    tui->set_paused(true);
                }
            }

            if (gdb_stub && gdb_stub->is_connected()) {
                const bool hit_ebreak =
                    (cpu.pipeline_context.opcode == Opcode::System) &&
                    (cpu.pipeline_context.funct12 == static_cast<Word>(Funct12Priv::Ebreak)) &&
                    !cpu.pipeline_context.pending_exception.has_value();
                if (gdb_stub->single_step() || hit_ebreak) {
                    gdb_stub->notify_breakpoint(*this);
                } else {
                    gdb_stub->poll(*this);
                }
            }

            if (spike_lockstep && spike_lockstep->is_running()) {
                spike_lockstep->compare_and_report(cpu.state(), cpu.pipeline_context.cpc,
                                                   cpu.e_icount);
                if (spike_lockstep->should_halt()) {
                    simrv::log::error("Lockstep: halting on divergence");
                    stop();
                }
            }

            if (s_tuimode && tui) {
                const bool hit_ebreak =
                    (cpu.pipeline_context.opcode == Opcode::System) &&
                    (cpu.pipeline_context.funct12 == static_cast<Word>(Funct12Priv::Ebreak));
                if (simrv::compiler::unlikely(hit_ebreak)) {
                    tui->pause_loop();
                }
            }
        }
    } else {
        // ---- Fast path: normal Linux/RTOS execution ----
        // No per-cycle GDB/lockstep/TUI branches.
        while (is_running_ &&
               execution_state_.load(std::memory_order_relaxed) != ExecutionState::Stopped) {
            prepare_cycle();
            cpu.run_cycle(*this);
            finalize_cycle();
        }
    }

    // Clean up background input thread
    if (uart && !s_tuimode) {
        uart->stop_input_thread();
    }
}

void OSMachine::prepare_cycle() {
    if (simrv::compiler::likely(s_high_performance && cpu.clint_mmio.mtime <= s_enabletimer)) {
        if (simrv::compiler::unlikely(cpu.clint_mmio.mtime == s_memimg)) {
            tracer.dump_init_artifacts();
        }
        cpu.pipeline_context.pending_exception = std::nullopt;
        cpu.pipeline_context.pending_tval = 0;
        return;
    }

    // Emit initialization artifacts at the configured cycle boundary.
    if (cpu.clint_mmio.mtime == s_memimg) {
        tracer.dump_init_artifacts();
    }

    static constexpr std::array<Byte, 9> kSyntheticInput = {
        static_cast<Byte>('r'), static_cast<Byte>('o'),  static_cast<Byte>('o'),
        static_cast<Byte>('t'), static_cast<Byte>('\n'), static_cast<Byte>('t'),
        static_cast<Byte>('o'), static_cast<Byte>('p'),  static_cast<Byte>('\n')};
    static int adr = 0;

    if (cpu.clint_mmio.mtime > s_enabletimer) { /* enable timer after linux boot */
        if (std::cmp_less(adr, kSyntheticInput.size())) {
            console->fifo_en = static_cast<Byte>(1);
            console->cons_fifo = kSyntheticInput.at(static_cast<std::size_t>(adr));
        } else {
            console->fifo_en = static_cast<Byte>(0);
        }

        if ((cpu.clint_mmio.mtime & static_cast<Counter>(0xfffff)) == 0) {
            int const ret = console->MC_receive_input(*this); /* Keyboard */
            if (ret > 0) {
                cpu.plic_set_irq(simrv::virtio::kConsoleIrq, 1);
            }
            if (ret == -1) {
                is_running_ = false; /* break by Ctrl+q */
            }
            if (std::cmp_less(adr, kSyntheticInput.size())) {
                adr++;
            }
        }
    }

    cpu.pipeline_context.pending_exception = std::nullopt; /* initialize regs */
    cpu.pipeline_context.pending_tval = 0;                 /* initialize regs */
}

void OSMachine::finalize_cycle() {
    if (simrv::compiler::likely(s_high_performance && (!s_tuimode || s_multithreaded) &&
                                s_strace == 0 && !s_bp_trace)) {
        const bool in_trace_window = simrv::compiler::unlikely(
            cpu.clint_mmio.mtime >= s_trace_begin && cpu.clint_mmio.mtime <= s_trace_end);
        if (simrv::compiler::unlikely(in_trace_window)) {
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
        // SDL update only from main thread in multithreaded mode
        if (!s_multithreaded && sdl_display &&
            simrv::compiler::unlikely((cpu.clint_mmio.mtime & 8191) == 0)) {
            sdl_display->update(cpu.e_icount);
        }
        if (uart && simrv::compiler::unlikely((cpu.clint_mmio.mtime & 8191) == 0)) {
            uart->non_tui_poll_input();
        }
        return;
    }

    if (simrv::compiler::unlikely(s_tuimode && !s_multithreaded && tui)) {
        if (simrv::tui::g_resized) {
            tui->render();
        }
        static uint64_t last_tui_check_cycles = 0;
        if (cpu.e_icount - last_tui_check_cycles >= 1000 ||
            tui->step_delay_us_.load(std::memory_order_relaxed) > 0) {
            last_tui_check_cycles = cpu.e_icount;
            static auto last_tui_update = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            if (now - last_tui_update >= std::chrono::milliseconds(33)) {
                tui->update();
                tui->render();
                last_tui_update = now;
            }
        }
    } else if (uart) {
        if (simrv::compiler::unlikely((cpu.clint_mmio.mtime & 8191) == 0)) {
            uart->non_tui_poll_input();
        }
    }
    // SDL update only from main thread in multithreaded mode
    if (!s_multithreaded && sdl_display) {
        if (simrv::compiler::unlikely((cpu.clint_mmio.mtime & 8191) == 0)) {
            sdl_display->update(cpu.e_icount);
        }
    }

    if (simrv::compiler::unlikely(s_strace != 0 && cpu.clint_mmio.mtime >= s_strace)) {
        tracer.emit_periodic_pc_trace(cpu.clint_mmio.mtime, cpu.pipeline_context.cpc);
    }
    if (simrv::compiler::unlikely(cpu.clint_mmio.mtime >= s_trace_begin &&
                                  cpu.clint_mmio.mtime <= s_trace_end)) {
        tracer.write_trace_snapshot();
    }
    if (simrv::compiler::unlikely(s_fincnt != std::numeric_limits<Counter>::max() &&
                                  cpu.e_icount >= s_fincnt)) {
        simrv::log::info("finished by -e option");
        stop();
    }
    if (simrv::compiler::unlikely(s_bp_trace)) {
        tracer.emit_branch_prediction_trace(cpu.clint_mmio.mtime, cpu.pipeline_context.cpc,
                                            cpu.pipeline_context.jmp_pc,
                                            cpu.pipeline_context.opcode, cpu.pipeline_context.tkn);
    }

    finalize_cycle_tohost();
}

}  // namespace simrv::core
