/**
 * @file Main.cpp
 * @brief SimRV entry point and command-line option handling.
 *
 * SimCore/RISC-V functional simulator (ArchLab, Science Tokyo (former TokyoTech)).
 */
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <print>
#include <span>
#include <string_view>
#include <thread>

#include "simrv/Define.hpp"
#include "simrv/core/Boot.hpp"
#include "simrv/core/BuildInfo.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/BaremetalMachine.hpp"
#include "simrv/core/OSMachine.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/util/FormatUtil.hpp"
#include "simrv/xlen/Types.hpp"
#include "simrv/util/InstructionExplainer.hpp"
#include "simrv/util/CliParser.hpp"

using namespace simrv::util;

auto main(int argc, char* argv[]) -> int {  // NOLINT(bugprone-exception-escape)
    bool is_tui = false;
    bool skip_banner = false;
    for (int i = 1; i < argc; ++i) {
        std::string_view const arg(argv[i]);
        if (arg == "--tui" || arg == "-u") {
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

    bool keep_running = true;
    int final_exit_code = 0;
    while (keep_running) {
        if (argc == 1) {
            usage(argv[0], 1);
        }

        std::span<char* const> const args(argv, static_cast<std::size_t>(argc));
        auto parsed = parse_command_line(args);
        if (!parsed) {
            option_error(parsed.error());
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
            std::thread sim_thread([machine_ptr]() {
                machine_ptr->run();
            });

            while (machine_ptr->is_running()) {
                if (machine_ptr->s_tuimode) {
                    if (simrv::tui::g_resized) {
                        machine_ptr->tui->render(true);
                    }
                    machine_ptr->tui->update();
                    machine_ptr->tui->render(false);
                    std::this_thread::sleep_for(std::chrono::milliseconds(33)); // ~30 FPS
                } else {
                    machine_ptr->sdl_display->update_gui_only();
                    std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
                }
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
        } else {
            keep_running = false;
            final_exit_code = sim_machine->exit_code;
            if (!sim_machine->s_tuimode) {
                sim_machine->tracer.print_summary();
            }
        }
    }
    return final_exit_code;
}
