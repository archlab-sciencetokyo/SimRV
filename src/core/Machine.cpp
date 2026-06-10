/**
 * @file Machine.cpp
 * @brief SimRV implementation unit.
 */
#include "simrv/core/Machine.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/device/Power.hpp"
#include "simrv/device/Uart.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <print>

#include "simrv/xlen/Types.hpp"

namespace simrv::memory {
bool g_appmode = false;
}

namespace simrv::core {
Machine::Machine() : memory_(*this) {
    cpu.machine_ = this;
}

/**
 * @brief Run the simulation loop until a termination condition is reached.
 */
void Machine::run() {
    const bool runs_gdb = gdb_stub && gdb_stub->is_connected();
    const bool runs_lockstep = spike_lockstep && spike_lockstep->is_running();
    if (s_appmode && !runs_gdb && !runs_lockstep) {
        run_baremetal();
        return;
    }

    if (s_tuimode && uart) {
        uart->tui_pause_loop();
    }
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
            spike_lockstep->compare_and_report(cpu.state(), cpu.e_icount);
            if (spike_lockstep->should_halt()) {
                simrv::log::error("Lockstep: halting on divergence");
                is_running_ = false;
            }
        }
    }
}

void Machine::run_baremetal() {
    if (s_tuimode && uart) {
        uart->tui_pause_loop();
    }

    uint32_t cycle_count = 0;

    while (is_running_) {
        cpu.run_cycle_baremetal(*this);

        if (simrv::compiler::unlikely(tohost != 0)) {
            finalize_cycle_tohost();
        }

        cycle_count++;
        if (simrv::compiler::unlikely(cycle_count >= 1024)) {
            cycle_count = 0;

            // Tick the CLINT timer: 1024 CPU cycles is approximately 102 CLINT timer ticks
            cpu.clint_mmio.mtime += 102;

            if (uart) {
                uart->non_tui_poll_input();
            }

            if (cpu.clint_mmio.mtime >= s_fincnt - 1) {
                simrv::log::info("finished by -e option");
                is_running_ = false;
            }
        }
    }
}

/**
 * @brief Perform per-cycle side effects before CPU stage execution.
 *
 * This includes optional artifact dump generation, synthetic console input/timer handling,
 * and reset of pending trap bookkeeping fields.
 */
void Machine::prepare_cycle() {
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
        } else {
            cpu.evaluate_timer_interrupt(); /* Timer */
        }
    } else {
        cpu.evaluate_timer_interrupt(); /* Timer always evaluated */
    }

    cpu.pipeline_context.pending_exception = std::nullopt; /* initialize regs */
    cpu.pipeline_context.pending_tval = 0;                 /* initialize regs */
}


/**
 * @brief Apply end-of-cycle termination checks and optional trace outputs.
 */
void Machine::finalize_cycle() {
    if (s_tuimode) {
        if (simrv::tui::g_resized) {
            uart->refresh_tui();
        }
        if ((cpu.e_icount % 20000) == 0) {
            uart->tui_update();
            static auto last_tui_render = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            if (now - last_tui_render >= std::chrono::milliseconds(33)) {
                uart->refresh_tui();
                last_tui_render = now;
            }
        }
    } else if (uart) {
        if ((cpu.clint_mmio.mtime % 10000) == 0) {
            uart->non_tui_poll_input();
        }
    }

    if (s_strace != 0 && cpu.clint_mmio.mtime >= s_strace) {
        tracer.emit_periodic_pc_trace(cpu.clint_mmio.mtime, cpu.pipeline_context.cpc);
    }
    if (cpu.clint_mmio.mtime >= s_trace_begin && cpu.clint_mmio.mtime <= s_trace_end) {
        tracer.write_trace_snapshot();
    }
    if (cpu.clint_mmio.mtime >= s_fincnt - 1) {
        simrv::log::info("finished by -e option");
        if (s_tuimode && uart && uart->tui()) {
            uart->tui_pause_loop();
        }
        is_running_ = false;
    }
    if (s_bp_trace) {
        tracer.emit_branch_prediction_trace(cpu.clint_mmio.mtime, cpu.pipeline_context.cpc,
                                            cpu.pipeline_context.jmp_pc,
                                            cpu.pipeline_context.opcode, cpu.pipeline_context.tkn);
    }

    finalize_cycle_tohost();
}

void Machine::finalize_cycle_tohost() {
    if (tohost == 0) {
        return;
    }

    // Standard 64-bit HTIF handling
    const auto dev = static_cast<uint8_t>(tohost >> 56);
    const auto cmd = static_cast<uint8_t>(tohost >> 48);
    const uint64_t payload = tohost & 0x0000FFFFFFFFFFFFULL;

    if (dev == 1 && cmd == 1) {
        // HTIF Console Print
        if (s_tuimode && uart && uart->tui()) {
            uart->tui()->handle_char_write(static_cast<char>(payload & 0xff));
        } else {
            std::print("{}", static_cast<char>(payload & 0xff));
            fflush(stdout);
        }
        tohost = 0;
        return;
    }

    // Compatibility for older 32-bit SimRV HTIF protocol:
    // writes of ((CMD_PRINT_CHAR << 16) | c) or (CMD_POWER_OFF << 16)
    if (dev == 0 && cmd == 0) {
        const auto old_cmd = static_cast<uint16_t>(tohost >> 16);
        const auto old_payload = static_cast<uint16_t>(tohost & 0xffffULL);
        if (old_cmd == 1) { // CMD_PRINT_CHAR
            const char ch = static_cast<char>(old_payload & 0xff);
            if (s_tuimode && uart && uart->tui()) {
                uart->tui()->handle_char_write(ch);
            } else {
                std::print("{}", ch);
                fflush(stdout);
            }
            tohost = 0;
            return;
        } else if (old_cmd == 2) { // CMD_POWER_OFF
            simrv::log::info("[Power] Compatibility: guest requested poweroff via tohost (old protocol).");
            exit_code = 0;
            is_running_ = false;
            tohost = 0;
            return;
        }
    }

    // Universal tohost halting check (e.g. exit code via tohost)
    if (tohost == 1) {
        if (s_isatest) {
            simrv::log::info("ISA TEST PASS");
        } else {
            simrv::log::info("Program Halted (SUCCESS / PASS)");
        }
        exit_code = 0;
        if (s_tuimode && uart && uart->tui()) {
            uart->tui_pause_loop();
        }
        is_running_ = false;
        tohost = 0;
        return;
    } else if ((tohost & 1) != 0u) {
        const int code = static_cast<int>(tohost >> 1);
        if (s_isatest) {
            simrv::log::error("ISA TEST FAIL code={} (tohost=0x{:016x})", code, tohost);
        } else {
            simrv::log::error("Program Halted (FAIL / EXIT code={})", code);
        }
        exit_code = code == 0 ? 1 : code;
        if (s_tuimode && uart && uart->tui()) {
            uart->tui_pause_loop();
        }
        is_running_ = false;
        tohost = 0;
        return;
    }


}



Machine::~Machine() = default;

}  // namespace simrv::core
