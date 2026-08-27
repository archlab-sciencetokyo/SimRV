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
#include "simrv/core/BuildInfo.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
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

    simrv::log::set_tui_mode(is_tui);

    if (!is_tui && !skip_banner) {
        simrv::log::info("{} v{} ({}@{})\nPlease type Control+'q' to quit the simulation\n",
                         simrv::buildinfo::kProjectDescription, simrv::buildinfo::kVersion,
                         simrv::buildinfo::kGitBranch, simrv::buildinfo::kGitSha);
    }

    // Write startup entry to MMU debug log
    std::signal(SIGINT, SIG_IGN);  // ignore control+'C'

    std::optional<RuntimeOptions> runtime_overrides;

    bool keep_running = true;
    int final_exit_code = 0;
    while (keep_running) {
        std::span<char* const> const args(argv, static_cast<std::size_t>(argc));
        auto parsed = parse_command_line(args);
        if (!parsed) {
            option_error(parsed.error());
        }

        if (runtime_overrides.has_value()) {
            parsed->options = *runtime_overrides;
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

        if (!parsed->options.fn_log.empty() && !simrv::log::set_log_file(parsed->options.fn_log)) {
            option_error("cannot open log file: " + parsed->options.fn_log, 0);
        }

        auto sim_machine = std::make_unique<simrv::core::Machine>(
            parsed->options.appmode ? simrv::core::MachineMode::Baremetal
                                    : simrv::core::MachineMode::OperatingSystem);

        auto applied = apply_runtime_options(sim_machine.get(), parsed->options);
        if (!applied) {
            option_error(applied.error(), 0);
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
        if (sim_machine->s_tuimode) {
            auto* machine_ptr = sim_machine.get();
            std::thread sim_thread([machine_ptr]() -> void { machine_ptr->run(); });

            if (sim_thread.joinable()) {
                sim_thread.join();
            }
        } else {
            sim_machine->run();
        }

        if (sim_machine->reboot_requested) {
            simrv::log::info("Rebooting guest system...");
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            auto next_opt = parsed->options;
            next_opt.cycle_mode_requested = sim_machine->runtime_profile.is_cycle_mode();
            next_opt.instruction_mode_requested = !sim_machine->runtime_profile.is_cycle_mode();
            next_opt.debug_mode = sim_machine->s_debug_mode;
            next_opt.high_contrast = sim_machine->s_high_contrast;
            next_opt.class_mode = sim_machine->s_class_mode;
            next_opt.use_mix = sim_machine->s_use_mix;
            next_opt.bp_trace = sim_machine->s_bp_trace;
            next_opt.traplog_mode = sim_machine->s_traplog_mode;
            next_opt.dlog_mode = sim_machine->s_dlog_mode;
            next_opt.lockstep_mode = sim_machine->s_lockstep_mode;
            next_opt.gdb_mode = sim_machine->s_gdb_mode;
            next_opt.num_harts = sim_machine->s_num_harts;
            next_opt.smp_quantum = sim_machine->s_smp_quantum;
            next_opt.smp_multithreaded = sim_machine->s_smp_multithreaded;
            next_opt.dram_size = sim_machine->s_dram_size;

            if (sim_machine->s_misa_override) {
                next_opt.misa_override = true;
                next_opt.misa_xlen = sim_machine->s_misa_xlen;
            }

            auto pending = sim_machine->get_pending_reboot();
            if (!pending.binary_path.empty()) {
                next_opt.fn_memimg = pending.binary_path;
                if (pending.appmode.has_value()) {
                    next_opt.appmode = *pending.appmode;
                }
                if (pending.disk_path.has_value()) {
                    next_opt.fn_dskimg = *pending.disk_path;
                    next_opt.use_disk = !pending.disk_path->empty();
                }
            }
            runtime_overrides = next_opt;
            continue;
        } else {
            keep_running = false;
            final_exit_code = sim_machine->exit_code.load();
            if (!sim_machine->s_tuimode) {
                sim_machine->trace().print_summary();
            }
        }
    }
    return final_exit_code;
}
