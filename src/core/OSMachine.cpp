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

void OSMachine::start_smp_threads() {
    if (!s_smp_multithreaded || secondary_harts_.empty() ||
        !runtime_profile.is_instruction_mode()) {
        return;
    }
    stop_smp_threads();
    smp_threads_running_.store(true, std::memory_order_release);
    for (size_t i = 0; i < secondary_harts_.size(); ++i) {
        smp_worker_threads_.emplace_back([this, i](const std::stop_token& stop_token) {
            auto& sec_hart = *secondary_harts_[i];
            constexpr uint32_t kWorkerBatch = 2048;
            while (!stop_token.stop_requested() && is_running() &&
                   smp_threads_running_.load(std::memory_order_relaxed)) {
                if (sec_hart.hart_status.load(std::memory_order_relaxed) != HartStatus::Started) {
                    sec_hart.hart_status.wait(HartStatus::Stopped, std::memory_order_relaxed);
                    continue;
                }

                if (execution_state_.load(std::memory_order_relaxed) == ExecutionState::Paused) {
                    execution_state_.wait(ExecutionState::Paused, std::memory_order_relaxed);
                    continue;
                }

                for (uint32_t step = 0;
                     step < kWorkerBatch && is_running() &&
                     sec_hart.hart_status.load(std::memory_order_relaxed) == HartStatus::Started;
                     ++step) {
                    sec_hart.run_cycle(*this);
                }
            }
        });
    }
}

void OSMachine::stop_smp_threads() {
    smp_threads_running_.store(false, std::memory_order_release);
    for (auto& sec_hart : secondary_harts_) {
        sec_hart->hart_status.notify_all();
    }
    execution_state_.notify_all();
    for (auto& thread : smp_worker_threads_) {
        if (thread.joinable()) {
            thread.request_stop();
            thread.join();
        }
    }
    smp_worker_threads_.clear();
}

void OSMachine::execute_cycle() {
    if (runtime_profile.is_cycle_mode()) {
        advance_ca_global_cycle();
        return;
    }
    cpu.run_cycle(*this);
    for (auto& sec_hart : secondary_harts_) {
        if (sec_hart->hart_status.load(std::memory_order_relaxed) == HartStatus::Started) {
            sec_hart->run_cycle(*this);
        }
    }
}

auto OSMachine::execute_fast_batch(uint32_t batch_size) -> bool {
    if (simrv::compiler::likely(can_execute_fast_batch())) {
        if (s_fincnt != std::numeric_limits<Counter>::max()) {
            if (cpu.e_icount >= s_fincnt) {
                stop(StopReason::InstructionLimit);
                return true;
            }
            batch_size =
                static_cast<uint32_t>(std::min<Counter>(batch_size, s_fincnt - cpu.e_icount));
        }

        if (s_smp_multithreaded && !secondary_harts_.empty()) {
            // Hart 0 executes bursts while secondary harts run concurrently on their worker threads
            const uint32_t burst = std::min(batch_size, 4096u);
            for (uint32_t i = 0; i < burst && is_running(); ++i) {
                cpu.run_cycle(*this);
            }
            return true;
        }

        // Cooperative fallback: check if any secondary hart is running
        bool any_sec_running = false;
        for (const auto& sec_hart : secondary_harts_) {
            if (sec_hart->hart_status.load(std::memory_order_relaxed) == HartStatus::Started) {
                any_sec_running = true;
                break;
            }
        }

        if (!any_sec_running) {
            // Fast single-hart burst
            const uint32_t burst = std::min(batch_size, 4096u);
            for (uint32_t i = 0; i < burst && is_running(); ++i) {
                cpu.run_cycle(*this);
            }
            return true;
        }

        // Multi-hart round-robin quantum batching
        constexpr uint32_t kHartQuantum = 2048;
        const uint32_t quantum = std::min(batch_size, kHartQuantum);

        for (uint32_t i = 0; i < quantum && is_running(); ++i) {
            cpu.run_cycle(*this);
        }
        for (auto& sec_hart : secondary_harts_) {
            if (sec_hart->hart_status.load(std::memory_order_relaxed) == HartStatus::Started) {
                for (uint32_t i = 0; i < quantum && is_running(); ++i) {
                    sec_hart->run_cycle(*this);
                }
            }
        }
        return true;
    }
    return false;
}

void OSMachine::prepare_cycle() {
    for (auto& sec_hart : secondary_harts_) {
        sec_hart->pipeline_context.pending_exception = std::nullopt;
        sec_hart->pipeline_context.pending_tval = 0;
    }

    if (simrv::compiler::likely(runtime_profile.is_instruction_fast() &&
                                cpu.clint_mmio.mtime <= s_enabletimer)) {
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

    cpu.pipeline_context.pending_exception = std::nullopt; /* initialize regs */
    cpu.pipeline_context.pending_tval = 0;                 /* initialize regs */
}

void OSMachine::finalize_cycle() {
    const bool in_trace_window =
        (cpu.clint_mmio.mtime >= s_trace_begin && cpu.clint_mmio.mtime <= s_trace_end);
    if (simrv::compiler::likely(runtime_profile.is_instruction_fast() &&
                                (!s_tuimode || s_multithreaded) && s_strace == 0 &&
                                !in_trace_window && !s_bp_trace)) {
        if (simrv::compiler::unlikely(tohost != 0)) {
            finalize_cycle_tohost();
        }
        if (simrv::compiler::unlikely(s_fincnt != std::numeric_limits<Counter>::max() &&
                                      cpu.e_icount >= s_fincnt)) {
            simrv::log::info("finished by -e option");
            stop(StopReason::InstructionLimit);
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
        stop(StopReason::InstructionLimit);
    }
    if (simrv::compiler::unlikely(s_bp_trace)) {
        tracer.emit_branch_prediction_trace(cpu.clint_mmio.mtime, cpu.pipeline_context.cpc,
                                            cpu.pipeline_context.jmp_pc,
                                            cpu.pipeline_context.opcode, cpu.pipeline_context.tkn);
    }

    finalize_cycle_tohost();
}

}  // namespace simrv::core
