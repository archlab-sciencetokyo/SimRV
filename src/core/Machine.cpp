/**
 * @file Machine.cpp
 * @brief SimRV implementation unit.
 */
#include "simrv/core/Machine.hpp"
#include "simrv/device/Tui.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <print>

#include "simrv/xlen/Types.hpp"

namespace simrv::core {
Machine::Machine() : memory_(*this) {
    cpu.machine_ = this;
}

/**
 * @brief Run the simulation loop until a termination condition is reached.
 */
void Machine::run() {
    if (s_gen_binfile) {
        generate_binfile();
    }
    if (s_tuimode && uart) {
        uart->tui_pause_loop();
    }
    while (is_running_) {
        prepare_cycle();
        cpu.run_cycle(*this);
        finalize_cycle();
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

constexpr Word CMD_PRINT_CHAR = 1; /* command for application mode using tohost */
constexpr Word CMD_POWER_OFF = 2;  /* command for application mode using tohost */
/**
 * @brief Apply end-of-cycle termination checks and optional trace outputs.
 */
void Machine::finalize_cycle() {
    if (s_tuimode) {
        if (simrv::device::g_resized) {
            uart->refresh_tui();
        }
        if ((cpu.clint_mmio.mtime % 5000) == 0) {
            uart->tui_update();
            static auto last_tui_render = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            if (now - last_tui_render >= std::chrono::milliseconds(33)) {
                uart->refresh_tui();
                last_tui_render = now;
            }
        }
    }

    if (s_strace != 0 && cpu.clint_mmio.mtime >= s_strace) {
        tracer.emit_periodic_pc_trace(cpu.clint_mmio.mtime, cpu.pipeline_context.cpc);
    }
    if (cpu.clint_mmio.mtime >= s_trace_begin && cpu.clint_mmio.mtime <= s_trace_end) {
        tracer.write_trace_snapshot();
    }
    if (cpu.clint_mmio.mtime >= s_fincnt - 1) {
        if (s_tuimode && uart && uart->tui()) {
            uart->tui()->print_log("\n__ finished by -e option\n");
            uart->tui_pause_loop();
        } else {
            std::println("\n__finished by -e option");
        }
        is_running_ = false;
    }
    if (s_bp_trace) {
        tracer.emit_branch_prediction_trace(cpu.clint_mmio.mtime, cpu.pipeline_context.cpc,
                                            cpu.pipeline_context.jmp_pc,
                                            cpu.pipeline_context.opcode, cpu.pipeline_context.tkn);
    }

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

    // TUI Halting check
    if (s_tuimode && uart && uart->tui()) {
        if (tohost == 1) {
            uart->tui()->print_log("\n__ Program Halted (SUCCESS / PASS)\n");
            uart->tui_pause_loop();
            is_running_ = false;
        } else if ((tohost & 1) != 0u || (tohost >> 16) == CMD_POWER_OFF) {
            uart->tui()->print_log(std::format("\n__ Program Halted (FAIL / EXIT code={})\n", tohost >> 1));
            uart->tui_pause_loop();
            is_running_ = false;
        }
        tohost = 0;
        return;
    }

    if (s_isatest) {
        if (tohost == 1) {
            std::println("\n__ ISA TEST PASS");
            is_running_ = false;
            return;
        } else if ((tohost & 1) != 0u) {
            std::println("\n__ ISA TEST FAIL code={} (tohost=0x{:016x})", tohost >> 1, tohost);
            is_running_ = false;
            return;
        }
    }

    // Legacy 32-bit SimRV application mode magic handling
    if ((tohost >> 16) == CMD_POWER_OFF) {
        if (s_tuimode && uart && uart->tui()) {
            uart->tui()->print_log("\n__ Power off\n");
            uart->tui_pause_loop();
        } else {
            std::println("\n__ Power off");
        }
        is_running_ = false;
    } else if ((tohost >> 16) == CMD_PRINT_CHAR) {
        if (s_tuimode && uart && uart->tui()) {
            uart->tui()->handle_char_write(static_cast<char>(tohost & 0xff));
        } else {
            std::print("{}", static_cast<char>(tohost & 0xff));
            fflush(stdout);
        }
        tohost = 0;
    }
}

Machine::~Machine() = default;

}  // namespace simrv::core
