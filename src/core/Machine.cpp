/**
 * @file Machine.cpp
 * @brief Machine top-level orchestration and cycle-loop implementation.
 */
#include "simrv/core/Machine.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <print>

#include "simrv/core/Logger.hpp"
#include "simrv/device/AIA.hpp"
#include "simrv/device/Aclint.hpp"
#include "simrv/device/Power.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/device/pci/PcieRootComplex.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::memory {
bool g_appmode = true;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
Address g_dram_base =
    kDramBaseAddress;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
}  // namespace simrv::memory

namespace simrv::core {
Machine::Machine() : memory_(*this) { cpu.machine_ = this; }

auto Machine::can_execute_fast_batch() const -> bool {
    if (!runtime_profile.allows_fast_batch() || s_lockstep_mode || s_gdb_mode || s_bp_trace ||
        s_strace != 0 || breakpoints.has_any() || s_rollback_enabled) {
        return false;
    }
    return true;
}

void Machine::reset_state() {
    tohost = 0;
    reboot_requested = false;
    exit_code = 0;
    is_shutdown_ = false;
    is_running_ = true;
    stop_reason_ = StopReason::Running;
    last_tui_check_cycles_ = 0;
    last_tui_update_ = {};
    execution_state_.store(ExecutionState::Running, std::memory_order_release);
    execution_state_.notify_all();
    cpu.reset();
}

void Machine::set_pending_reboot(const std::string& binary_path, std::optional<bool> appmode,
                                 std::optional<std::string> disk_path) {
    std::lock_guard<std::mutex> lock(pending_reboot_mutex_);
    pending_binary_path_ = binary_path;
    pending_appmode_ = appmode;
    pending_disk_path_ = disk_path;
}

auto Machine::get_pending_reboot() const -> PendingRebootState {
    std::lock_guard<std::mutex> lock(pending_reboot_mutex_);
    return PendingRebootState{
        .binary_path = pending_binary_path_,
        .appmode = pending_appmode_,
        .disk_path = pending_disk_path_,
    };
}

void Machine::clear_pending_reboot() {
    std::lock_guard<std::mutex> lock(pending_reboot_mutex_);
    pending_binary_path_.clear();
    pending_appmode_.reset();
    pending_disk_path_.reset();
}

auto Machine::is_paused() const -> bool {
    return execution_state_.load(std::memory_order_relaxed) == ExecutionState::Paused ||
           (s_tuimode && tui && tui->is_paused());
}

void Machine::pause() {
    execution_state_.store(ExecutionState::Paused, std::memory_order_release);
    execution_state_.notify_all();
    if (s_tuimode && tui) {
        tui->pause_loop();
    }
}

void Machine::resume() {
    if (is_shutdown_) {
        return;
    }
    execution_state_.store(ExecutionState::Running, std::memory_order_release);
    execution_state_.notify_all();
    if (s_tuimode && tui) {
        tui->unpause_loop();
    }
}

void Machine::step() {
    if (is_shutdown_) {
        return;
    }
    execution_state_.store(ExecutionState::Stepping, std::memory_order_release);
    execution_state_.notify_all();
}

auto Machine::stop_reason_name(StopReason reason) noexcept -> std::string_view {
    switch (reason) {
        case StopReason::Running:
            return "running";
        case StopReason::InstructionLimit:
            return "instruction limit reached";
        case StopReason::TohostPass:
            return "guest tohost pass";
        case StopReason::TohostFail:
            return "guest tohost failure";
        case StopReason::GuestPoweroff:
            return "guest poweroff";
        case StopReason::GuestCrash:
            return "guest crash";
        case StopReason::GuestReboot:
            return "guest reboot";
        case StopReason::GuestExit:
            return "guest exit";
        case StopReason::LockstepDivergence:
            return "lockstep divergence";
        case StopReason::UnhandledTrap:
            return "unhandled trap";
        case StopReason::ExternalStop:
            return "external/user stop";
    }
    return "unknown";
}

void Machine::stop(StopReason reason) {
    stop_reason_ = reason;
    is_shutdown_ = true;
    execution_state_.store(ExecutionState::Stopped, std::memory_order_release);
    execution_state_.notify_all();
    if (!s_tuimode) {
        is_running_ = false;
    }
    if (s_tuimode && tui) {
        tui->pause_loop();
    }
}

void Machine::request_reboot() {
    stop_reason_ = StopReason::GuestReboot;
    reboot_requested = true;
    is_running_ = false;
    execution_state_.store(ExecutionState::Stopped, std::memory_order_release);
    execution_state_.notify_all();
}

void Machine::request_exit(int status) {
    stop_reason_ = StopReason::GuestExit;
    exit_code = status;
    is_shutdown_ = true;
    is_running_ = false;
    execution_state_.store(ExecutionState::Stopped, std::memory_order_release);
    execution_state_.notify_all();
}

void Machine::run() {
    cpu.evaluate_timer_interrupt();

    // Start background stdin input thread for non-TUI mode
    if (uart && !s_tuimode) {
        uart->start_input_thread();
    }

    // In TUI mode expose the UART through a PTY for optional external terminals.
    if (uart && s_tuimode) {
        if (uart->start_pty()) {
            simrv::log::info("[UART] PTY slave: {}", uart->pty_slave_path());
        } else {
            simrv::log::warn("[UART] openpty() failed – falling back to direct push_rx_byte");
        }
    }

    constexpr uint32_t kBatchSize = 65536;

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

        if (execute_fast_batch(kBatchSize)) {
            if (simrv::compiler::unlikely(tracer.fp_trace.is_open())) {
                tracer.write_trace_snapshot();
            }
            if (simrv::compiler::unlikely(tohost != 0)) {
                finalize_cycle_tohost();
            }
            if (simrv::compiler::unlikely(s_fincnt != std::numeric_limits<Counter>::max() &&
                                          cpu.e_icount >= s_fincnt)) {
                simrv::log::info("finished by -e option");
                stop_reason_ = StopReason::InstructionLimit;
                is_running_ = false;
            }
            if (s_tuimode && tui) {
                if (simrv::tui::g_resized) {
                    tui->render();
                }
                auto now = std::chrono::steady_clock::now();
                if (now - last_tui_update_ >= std::chrono::milliseconds(33)) {
                    tui->update();
                    tui->update_cache();
                    tui->render();
                    last_tui_update_ = now;
                }
            } else if (uart && !uart->is_input_thread_running()) {
                uart->non_tui_poll_input();
            }
            continue;
        }

        prepare_cycle();
        execute_cycle();
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
            const bool hit_ebreak = (cpu.pipeline_context.opcode == simrv::isa::Opcode::System) &&
                                    (cpu.pipeline_context.funct12 ==
                                     static_cast<Word>(simrv::isa::Funct12Priv::Ebreak)) &&
                                    !cpu.pipeline_context.pending_exception.has_value();
            if (gdb_stub->single_step() || hit_ebreak) {
                gdb_stub->notify_breakpoint(*this);
            } else {
                gdb_stub->poll(*this);
            }
        }

        if (!s_appmode && spike_lockstep && spike_lockstep->is_running()) {
            spike_lockstep->compare_and_report(cpu.state(), cpu.pipeline_context.cpc, cpu.e_icount);
            if (spike_lockstep->should_halt()) {
                simrv::log::error("Lockstep: halting on divergence");
                stop(StopReason::LockstepDivergence);
            }
        }

        if (s_tuimode && tui) {
            const bool hit_ebreak = (cpu.pipeline_context.opcode == simrv::isa::Opcode::System) &&
                                    (cpu.pipeline_context.funct12 ==
                                     static_cast<Word>(simrv::isa::Funct12Priv::Ebreak));
            if (simrv::compiler::unlikely(hit_ebreak)) {
                tui->pause_loop();
            }
        }
    }

    // Clean up background input thread
    if (uart && !s_tuimode) {
        uart->stop_input_thread();
    }
    // Clean up PTY
    if (uart && s_tuimode) {
        uart->stop_pty();
    }
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
        if (s_tuimode && tui) {
            tui->handle_char_write(static_cast<char>(payload & 0xff));
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
        if (old_cmd == 1) {  // CMD_PRINT_CHAR
            const char ch = static_cast<char>(old_payload & 0xff);
            if (s_tuimode && tui) {
                tui->handle_char_write(ch);
            } else {
                std::print("{}", ch);
                fflush(stdout);
            }
            tohost = 0;
            return;
        } else if (old_cmd == 2) {  // CMD_POWER_OFF
            simrv::log::info(
                "[Power] Compatibility: guest requested poweroff via tohost (old protocol).");
            exit_code = 0;
            stop(StopReason::GuestPoweroff);
            tohost = 0;
            return;
        } else {
            // HTIF Syscall handling: payload is a pointer to the syscall block in guest DRAM
            if (simrv::memory::is_dram_addr(payload)) {
                const Address masked_payload = payload & simrv::memory::kDramMask;
                uint64_t syscall_num = 0;
                uint64_t arg0 = 0;
                uint64_t arg1 = 0;
                uint64_t arg2 = 0;

                std::memcpy(&syscall_num, mmem + masked_payload + 0, 8);
                std::memcpy(&arg0, mmem + masked_payload + 8, 8);
                std::memcpy(&arg1, mmem + masked_payload + 16, 8);
                std::memcpy(&arg2, mmem + masked_payload + 24, 8);

                if (syscall_num == 64) {  // SYS_write
                    const Address buf_masked = arg1 & simrv::memory::kDramMask;
                    for (uint64_t i = 0; i < arg2; ++i) {
                        char ch = static_cast<char>(mmem[buf_masked + i]);
                        if (s_tuimode && tui) {
                            tui->handle_char_write(ch);
                        } else {
                            std::print("{}", ch);
                        }
                    }
                    if (!s_tuimode) {
                        std::fflush(stdout);
                    }

                    // Write success response (bytes written) to fromhost
                    const Address fromhost_addr =
                        (s_isatest_tohost != 0 ? s_isatest_tohost : 0x80001000) + 8;
                    const Address fromhost_masked = fromhost_addr & simrv::memory::kDramMask;
                    uint64_t resp = arg2;
                    std::memcpy(mmem + fromhost_masked, &resp, 8);
                    tohost = 0;
                    return;
                } else if (syscall_num == 93) {  // SYS_exit
                    const int code = static_cast<int>(arg0);
                    if (s_appmode) {
                        if (code == 0) {
                            simrv::log::info("ISA TEST PASS");
                        } else {
                            simrv::log::error("ISA TEST FAIL code={}", code);
                        }
                    } else {
                        if (code == 0) {
                            simrv::log::info("Program Halted (SUCCESS / PASS)");
                        } else {
                            simrv::log::error("Program Halted (FAIL / EXIT code={})", code);
                        }
                    }
                    exit_code = code;
                    stop(code == 0 ? StopReason::TohostPass : StopReason::TohostFail);
                    tohost = 0;
                    return;
                }
                tohost = 0;
                return;
            }
        }
    }

    // Universal tohost halting check (e.g. exit code via tohost)
    if (tohost == 1) {
        if (s_appmode) {
            simrv::log::info("ISA TEST PASS");
        } else {
            simrv::log::info("Program Halted (SUCCESS / PASS)");
        }
        exit_code = 0;
        stop(StopReason::TohostPass);
        tohost = 0;
        return;
    } else if ((tohost & 1) != 0u) {
        const int code = static_cast<int>(tohost >> 1);
        if (s_appmode) {
            simrv::log::error("ISA TEST FAIL code={} (tohost=0x{:016x})", code, tohost.load());
        } else {
            simrv::log::error("Program Halted (FAIL / EXIT code={})", code);
        }
        exit_code = code == 0 ? 1 : code;
        stop(StopReason::TohostFail);
        tohost = 0;
        return;
    }
}

Machine::~Machine() = default;

}  // namespace simrv::core
