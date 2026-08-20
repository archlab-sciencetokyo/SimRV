/**
 * @file Main.cpp
 * @brief SimRV entry point and command-line option handling.
 *
 * SimCore/RISC-V functional simulator (ArchLab, Science Tokyo (former TokyoTech)).
 */
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string_view>
#include <thread>

#include "simrv/Define.hpp"
#include "simrv/core/BaremetalMachine.hpp"
#include "simrv/core/BuildInfo.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/OSMachine.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/util/CliParser.hpp"
#include "simrv/util/FormatUtil.hpp"
#include "simrv/util/InstructionExplainer.hpp"
#include "simrv/xlen/Types.hpp"

using namespace simrv::util;

auto main(int argc, char* argv[]) -> int {  // NOLINT(bugprone-exception-escape)
    bool is_tui = (::isatty(STDIN_FILENO) != 0);
    bool skip_banner = false;
    for (int i = 1; i < argc; ++i) {
        std::string_view const arg(argv[i]);
        if (arg == "--cli" || arg == "-c") {
            is_tui = false;
        } else if (arg == "--tui" || arg == "-u") {
            is_tui = true;
        } else if (arg == "-h" || arg == "--help" || arg == "--version") {
            skip_banner = true;
        }
    }

    if (!is_tui && !skip_banner) {
        simrv::log::info("{} v{} ({}@{})\nPlease type Control+'q' to quit the simulation\n",
                         simrv::buildinfo::kProjectDescription, simrv::buildinfo::kVersion,
                         simrv::buildinfo::kGitBranch, simrv::buildinfo::kGitSha);
    }

    // Write startup entry to MMU debug log
    std::signal(SIGINT, SIG_IGN);  // ignore control+'C'

    std::optional<bool> override_appmode;
    std::optional<std::string> override_binary;
    std::optional<std::string> override_disk;
    std::optional<bool> override_cycle_accurate;
    std::optional<bool> override_debug_mode;
    std::optional<bool> override_high_performance;
    std::optional<bool> override_rollback;
    std::optional<bool> override_high_contrast;
    std::optional<bool> override_use_mix;
    std::optional<bool> override_bp_trace;
    std::optional<bool> override_traplog_mode;
    std::optional<bool> override_dlog_mode;
    std::optional<bool> override_lockstep_mode;
    std::optional<bool> override_gdb_mode;
    std::optional<bool> override_misa_override;
    std::optional<uint64_t> override_misa_profile;
    std::optional<unsigned int> override_misa_xlen;
    std::optional<uint32_t> override_num_harts;
    std::optional<uint32_t> override_smp_quantum;
    std::optional<bool> override_smp_multithreaded;

    bool keep_running = true;
    int final_exit_code = 0;
    while (keep_running) {
        std::span<char* const> const args(argv, static_cast<std::size_t>(argc));
        auto parsed = parse_command_line(args);
        if (!parsed) {
            option_error(parsed.error());
        }

        if (override_appmode.has_value()) {
            parsed->options.appmode = *override_appmode;
        }
        if (override_binary.has_value()) {
            parsed->options.fn_memimg = *override_binary;
        }
        if (override_disk.has_value()) {
            parsed->options.fn_dskimg = *override_disk;
            parsed->options.use_disk = !override_disk->empty();
        }
        if (override_cycle_accurate.has_value()) {
            parsed->options.cycle_accurate = *override_cycle_accurate;
        }
        if (override_debug_mode.has_value()) {
            parsed->options.debug_mode = *override_debug_mode;
        }
        if (override_high_performance.has_value()) {
            parsed->options.high_performance = *override_high_performance;
        }
        if (override_rollback.has_value()) {
            parsed->options.rollback = *override_rollback;
        }
        if (override_high_contrast.has_value()) {
            parsed->options.high_contrast = *override_high_contrast;
        }
        if (override_use_mix.has_value()) {
            parsed->options.use_mix = *override_use_mix;
        }
        if (override_bp_trace.has_value()) {
            parsed->options.bp_trace = *override_bp_trace;
        }
        if (override_traplog_mode.has_value()) {
            parsed->options.traplog_mode = *override_traplog_mode;
        }
        if (override_dlog_mode.has_value()) {
            parsed->options.dlog_mode = *override_dlog_mode;
        }
        if (override_lockstep_mode.has_value()) {
            parsed->options.lockstep_mode = *override_lockstep_mode;
        }
        if (override_gdb_mode.has_value()) {
            parsed->options.gdb_mode = *override_gdb_mode;
        }
        if (override_num_harts.has_value()) {
            parsed->options.num_harts = *override_num_harts;
        }
        if (override_smp_quantum.has_value()) {
            parsed->options.smp_quantum = *override_smp_quantum;
        }
        if (override_smp_multithreaded.has_value()) {
            parsed->options.smp_multithreaded = *override_smp_multithreaded;
        }

        switch (parsed->action) {
            case CliAction::ShowHelp:
                usage(args.front(), 0);
            case CliAction::ShowVersion:
                std::println("{} (RV{})", simrv::buildinfo::kVersion, simrv::xlen::kXLenBits);
                std::exit(0);
            case CliAction::ExplainInstruction:
                simrv::util::explain_instruction(parsed->options.explain_inst_val);
                std::exit(0);
            case CliAction::Run:
                break;
        }

        std::unique_ptr<simrv::core::Machine> sim_machine;
        if (parsed->options.appmode) {
            sim_machine = std::make_unique<simrv::core::BaremetalMachine>();
        } else {
            sim_machine = std::make_unique<simrv::core::OSMachine>();
        }

        auto applied = apply_runtime_options(sim_machine.get(), parsed->options);
        if (!applied) {
            option_error(applied.error(), 0);
        }

        if (override_num_harts.has_value()) {
            sim_machine->s_num_harts = *override_num_harts;
        }
        if (override_smp_quantum.has_value()) {
            sim_machine->s_smp_quantum = *override_smp_quantum;
        }
        if (override_smp_multithreaded.has_value()) {
            sim_machine->s_smp_multithreaded = *override_smp_multithreaded;
        }

        if (override_misa_override.has_value() && *override_misa_override) {
            sim_machine->s_misa_override = true;
            if (override_misa_profile.has_value()) {
                sim_machine->s_misa_profile = *override_misa_profile;
                sim_machine->cpu.state().misa = *override_misa_profile;
            }
            if (override_misa_xlen.has_value()) {
                sim_machine->s_misa_xlen = *override_misa_xlen;
            }
        }

        const int init_result = sim_machine->initialize();
        if (init_result != 0) {
            return init_result;
        }

        sim_machine->s_start_time = std::chrono::steady_clock::now();

        // Initialize terminal in raw mode for simulator I/O.
        TerminalModeGuard terminal_mode;
        if (!terminal_mode.enable_raw_mode()) {
            if (!is_tui) {
                simrv::log::warn("Terminal raw mode setup failed; continuing in current mode");
            }
        }
        if ((sim_machine->s_gui_mode && sim_machine->framebuffer) || sim_machine->s_tuimode) {
            sim_machine->s_multithreaded = true;
            if (sim_machine->s_gui_mode && sim_machine->framebuffer) {
                sim_machine->framebuffer->set_multithreaded(true);
            }
            auto* machine_ptr = sim_machine.get();
            std::thread sim_thread([machine_ptr]() -> void { machine_ptr->run(); });

            while (machine_ptr->is_running()) {
                if (machine_ptr->s_tuimode) {
                    if (simrv::tui::g_resized) {
                        machine_ptr->tui->render(true);
                    }
                    machine_ptr->tui->update();
                    machine_ptr->tui->render(false);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(33));  // ~30 FPS
            }

            if (sim_thread.joinable()) {
                sim_thread.join();
            }
        } else {
            sim_machine->run();
        }

        if (sim_machine->reboot_requested) {
            simrv::log::info("Rebooting guest system...");
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            override_cycle_accurate = sim_machine->s_cycle_accurate;
            override_high_performance = sim_machine->s_high_performance;
            override_debug_mode = sim_machine->s_debug_mode;
            override_rollback = sim_machine->s_rollback_enabled;
            override_high_contrast = sim_machine->s_high_contrast;
            override_use_mix = sim_machine->s_use_mix;
            override_bp_trace = sim_machine->s_bp_trace;
            override_traplog_mode = sim_machine->s_traplog_mode;
            override_dlog_mode = sim_machine->s_dlog_mode;
            override_lockstep_mode = sim_machine->s_lockstep_mode;
            override_gdb_mode = sim_machine->s_gdb_mode;
            override_num_harts = sim_machine->s_num_harts;
            override_smp_quantum = sim_machine->s_smp_quantum;
            override_smp_multithreaded = sim_machine->s_smp_multithreaded;

            if (sim_machine->s_misa_override) {
                override_misa_override = true;
                override_misa_profile = sim_machine->s_misa_profile;
                override_misa_xlen = sim_machine->s_misa_xlen;
            }

            auto pending = sim_machine->get_pending_reboot();
            if (!pending.binary_path.empty()) {
                override_binary = pending.binary_path;
                override_appmode = pending.appmode;
                override_disk = pending.disk_path;
            }
            continue;
        } else {
            keep_running = false;
            final_exit_code = sim_machine->exit_code.load();
            if (!sim_machine->s_tuimode) {
                sim_machine->tracer.print_summary();
            }
        }
    }
    return final_exit_code;
}
