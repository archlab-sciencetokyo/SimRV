/**
 * @file OSMachine.cpp
 * @brief OSMachine implementation unit.
 */
#include "simrv/core/OSMachine.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/device/Power.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/xlen/Types.hpp"
#include <array>
#include <chrono>
#include <limits>

namespace simrv::core {

void OSMachine::run() {
    if (s_tuimode && uart) {
        uart->tui_pause_loop();
    }

    cpu.evaluate_timer_interrupt();

    while (is_running_) {
        prepare_cycle();
        cpu.run_cycle(*this);
        finalize_cycle();

        // ---- GDB stub post-cycle hook ----
        if (gdb_stub && gdb_stub->is_connected()) {
            const bool hit_ebreak =
                (cpu.pipeline_context.opcode == Opcode::System) &&
                (cpu.pipeline_context.funct12 ==
                    static_cast<Word>(Funct12Priv::Ebreak)) &&
                !cpu.pipeline_context.pending_exception.has_value();

            if (gdb_stub->single_step() || hit_ebreak) {
                gdb_stub->notify_breakpoint(*this);
            } else {
                gdb_stub->poll(*this);
            }
        }

        // ---- Spike lockstep post-cycle hook ----
        if (spike_lockstep && spike_lockstep->is_running()) {
            spike_lockstep->compare_and_report(cpu.state(), cpu.pipeline_context.cpc, cpu.e_icount);
            if (spike_lockstep->should_halt()) {
                simrv::log::error("Lockstep: halting on divergence");
                is_running_ = false;
            }
        }

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
        console->fifo_en = static_cast<Byte>(1);
        console->cons_fifo = kSyntheticInput.at(adr % static_cast<int>(kSyntheticInput.size()));

        if ((cpu.clint_mmio.mtime & static_cast<Counter>(0xfffff)) == 0 &&
            console->fifo_en != static_cast<Byte>(0)) {       // 2019-08-30
            int const ret = console->MC_receive_input(*this); /* Keyboard */
            if (ret > 0) {
                cpu.plic_set_irq(simrv::virtio::kConsoleIrq, 1);
            }
            if (ret == -1) {
                is_running_ = false; /* break by Ctrl+q */
            }
            adr++;
        }
    }

    cpu.pipeline_context.pending_exception = std::nullopt; /* initialize regs */
    cpu.pipeline_context.pending_tval = 0;                 /* initialize regs */
}

void OSMachine::finalize_cycle() {
    const bool in_trace_window = (cpu.clint_mmio.mtime >= s_trace_begin && cpu.clint_mmio.mtime <= s_trace_end);
    if (simrv::compiler::likely(s_high_performance && !s_tuimode && s_strace == 0 && 
                                  !in_trace_window && !s_bp_trace)) {
        if (simrv::compiler::unlikely(tohost != 0)) {
            finalize_cycle_tohost();
        }
        if (simrv::compiler::unlikely(s_fincnt != std::numeric_limits<Counter>::max() && cpu.e_icount >= s_fincnt)) {
            simrv::log::info("finished by -e option");
            is_shutdown_ = true;
            is_running_ = false;
        }
        if (uart && simrv::compiler::unlikely((cpu.clint_mmio.mtime & 8191) == 0)) {
            uart->non_tui_poll_input();
        }
        return;
    }

    if (simrv::compiler::unlikely(s_tuimode)) {
        if (simrv::tui::g_resized) {
            uart->refresh_tui();
        }
        if ((cpu.e_icount % 20000) == 0) {
            static auto last_tui_update = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            if (now - last_tui_update >= std::chrono::milliseconds(33)) {
                uart->tui_update();
                uart->refresh_tui();
                last_tui_update = now;
            }
        }
    } else if (uart) {
        if (simrv::compiler::unlikely((cpu.clint_mmio.mtime & 8191) == 0)) {
            uart->non_tui_poll_input();
        }
    }

    if (simrv::compiler::unlikely(s_strace != 0 && cpu.clint_mmio.mtime >= s_strace)) {
        tracer.emit_periodic_pc_trace(cpu.clint_mmio.mtime, cpu.pipeline_context.cpc);
    }
    if (simrv::compiler::unlikely(cpu.clint_mmio.mtime >= s_trace_begin && cpu.clint_mmio.mtime <= s_trace_end)) {
        tracer.write_trace_snapshot();
    }
    if (simrv::compiler::unlikely(s_fincnt != std::numeric_limits<Counter>::max() && cpu.e_icount >= s_fincnt)) {
        simrv::log::info("finished by -e option");
        is_shutdown_ = true;
        if (s_tuimode && uart && uart->tui()) {
            uart->tui_pause_loop();
        }
        is_running_ = false;
    }
    if (simrv::compiler::unlikely(s_bp_trace)) {
        tracer.emit_branch_prediction_trace(cpu.clint_mmio.mtime, cpu.pipeline_context.cpc,
                                             cpu.pipeline_context.jmp_pc,
                                             cpu.pipeline_context.opcode, cpu.pipeline_context.tkn);
    }

    finalize_cycle_tohost();
}

} // namespace simrv::core
