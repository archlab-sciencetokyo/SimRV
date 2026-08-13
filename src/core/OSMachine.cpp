/**
 * @file OSMachine.cpp
 * @brief OSMachine implementation unit.
 */
#include "simrv/core/OSMachine.hpp"

#include <array>
#include <chrono>
#include <limits>

#include "simrv/core/Logger.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

using namespace simrv::isa;

void OSMachine::reset_synthetic_input() { synthetic_input_idx_ = 0; }

void OSMachine::execute_cycle() {
    cpu.run_cycle(*this);
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

    if (cpu.clint_mmio.mtime > s_enabletimer) { /* enable timer after linux boot */
        if (console) {
            // Fast path: drain one TUI-pushed interactive byte every ~1024 ticks (~0.1ms).
            // This ensures Ctrl-A terminal input is responsive for interactive Linux login.
            if (s_tuimode && ((cpu.clint_mmio.mtime & static_cast<Counter>(0x3ff)) == 0)) {
                if (console->pop_pending_input()) {
                    int const ret = console->MC_receive_input(*this);
                    if (ret > 0) {
                        cpu.plic_set_irq(simrv::virtio::kConsoleIrq, 1);
                    }
                    if (ret == -1) {
                        stop();
                    }
                }
            }

            // Slow path (~100ms): CLI stdin poll + synthetic login injection for non-TUI mode.
            if ((cpu.clint_mmio.mtime & static_cast<Counter>(0xfffff)) == 0) {
                if (!s_tuimode) {
                    if (synthetic_input_idx_ < kSyntheticInput.size()) {
                        console->fifo_en = static_cast<Byte>(1);
                        console->cons_fifo = kSyntheticInput.at(synthetic_input_idx_);
                        if (uart) {
                            uart->push_rx_byte(
                                static_cast<uint8_t>(kSyntheticInput.at(synthetic_input_idx_)));
                        }
                        synthetic_input_idx_++;
                    } else {
                        console->fifo_en = static_cast<Byte>(0);
                    }
                    int const ret = console->MC_receive_input(*this); /* Keyboard / VirtIO RX */
                    if (ret > 0) {
                        cpu.plic_set_irq(simrv::virtio::kConsoleIrq, 1);
                    }
                    if (ret == -1) {
                        stop(); /* break by Ctrl+q */
                    }
                }
            }
        }
    }

    cpu.pipeline_context.pending_exception = std::nullopt; /* initialize regs */
    cpu.pipeline_context.pending_tval = 0;                 /* initialize regs */
}

void OSMachine::finalize_cycle() {
    const bool in_trace_window =
        (cpu.clint_mmio.mtime >= s_trace_begin && cpu.clint_mmio.mtime <= s_trace_end);
    if (simrv::compiler::likely(s_high_performance && (!s_tuimode || s_multithreaded) &&
                                s_strace == 0 && !in_trace_window && !s_bp_trace)) {
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
        if (uart && !uart->is_input_thread_running() &&
            simrv::compiler::unlikely((cpu.clint_mmio.mtime & 8191) == 0)) {
            uart->non_tui_poll_input();
        }
        return;
    }

    if (simrv::compiler::unlikely(s_tuimode && !s_multithreaded && tui)) {
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
