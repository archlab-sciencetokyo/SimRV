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
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <print>
#include <span>
#include <string_view>
#include <thread>

#include "simrv/Define.hpp"
#include "simrv/core/BuildInfo.hpp"
#include "simrv/core/CpuConfigParser.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/net/Client.hpp"
#include "simrv/net/Server.hpp"
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
        } else if (arg == "--gdb") {
            is_tui = false;
        } else if (arg == "--tui" || arg == "-u") {
            is_tui = true;
        } else if (arg == "-h" || arg == "--help" || arg == "--version" || arg == "--license") {
            skip_banner = true;
        } else if (arg == "-q" || arg == "--quiet") {
            skip_banner = true;
            simrv::log::set_level(simrv::log::Level::Warn);
        } else if (arg == "-v" || arg == "--verbose") {
            simrv::log::set_level(simrv::log::Level::Debug);
        } else if (arg == "--log-level" && i + 1 < argc) {
            if (auto lvl = simrv::log::parse_level(argv[i + 1])) {
                simrv::log::set_level(*lvl);
                if (*lvl > simrv::log::Level::Info) {
                    skip_banner = true;
                }
            }
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

    std::optional<simrv::core::MachineConfig> staged_configuration;
    std::optional<simrv::core::RuntimeProfile> staged_runtime_profile;

    std::shared_ptr<simrv::net::SimRvServer> ipc_server;
    bool keep_running = true;
    int final_exit_code = 0;
    while (keep_running) {
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
            case CliAction::ShowLicense:
                std::println("SimRV {} (RV{})", simrv::buildinfo::kVersion, simrv::xlen::kXLenBits);
                std::println("Licensed under the MIT License.");
                std::println("Copyright (c) 2024-2026 Lennart Trunk and ArchLab @ ScienceTokyo");
                std::println("Full license: LICENSE; third-party notices: THIRD_PARTY_NOTICES.md");
                std::exit(0);
            case CliAction::ExplainInstruction:
                simrv::util::explain_instruction(parsed->options.explain_inst_val);
                std::exit(0);
            case CliAction::Attach:
                return simrv::net::run_client(parsed->options.attach_endpoint,
                                              !parsed->options.tuimode);
            case CliAction::DumpCpuModel: {
                simrv::pipeline::CpuModelConfig cfg{};
                std::string target = parsed->options.dump_cpu_model_profile;
                if (target.empty()) target = "balanced";
                if (auto resolved = simrv::core::resolve_cpu_model_path(target)) {
                    (void)simrv::core::load_cpu_config(*resolved, cfg);
                } else if (auto p = simrv::pipeline::parse_cpu_model_profile(target)) {
                    cfg = simrv::pipeline::make_cpu_model_profile(*p);
                } else {
                    cfg = simrv::pipeline::make_cpu_model_profile(
                        simrv::pipeline::CpuModelProfile::Balanced);
                }
                if (!parsed->options.dump_cpu_model_output.empty()) {
                    if (!simrv::core::save_cpu_config(parsed->options.dump_cpu_model_output, cfg,
                                                      target)) {
                        simrv::log::error("Failed to write CPU model to {}",
                                          parsed->options.dump_cpu_model_output);
                        std::exit(1);
                    }
                    std::println("Wrote CPU model configuration to {}",
                                 parsed->options.dump_cpu_model_output);
                } else {
                    simrv::core::serialize_cpu_config(cfg, std::cout, target);
                }
                std::exit(0);
            }
            case CliAction::Run:
                break;
        }

        if (parsed->options.log_level.has_value()) {
            simrv::log::set_level(*parsed->options.log_level);
        }

        if (!parsed->options.fn_log.empty() && !simrv::log::set_log_file(parsed->options.fn_log)) {
            option_error("cannot open log file: " + parsed->options.fn_log, 0);
        }

        if (parsed->options.server_mode) {
            parsed->options.tuimode = false;
            is_tui = false;
            simrv::log::set_tui_mode(false);
        }
        auto machine_config = staged_configuration.value_or(parsed->options.to_machine_config());
        if (const auto valid = machine_config.validate(); !valid) {
            option_error(valid.error(), 0);
        }
        auto sim_machine = std::make_unique<simrv::core::Machine>(std::move(machine_config));

        if (staged_runtime_profile.has_value()) {
            sim_machine->runtime_profile = *staged_runtime_profile;
        } else {
            auto applied = apply_runtime_options(sim_machine.get(), parsed->options);
            if (!applied) {
                option_error(applied.error(), 0);
            }
        }

        sim_machine->set_persistent_control(parsed->options.server_mode);
        const auto init_result = sim_machine->initialize();
        if (!init_result) {
            simrv::log::error("Machine initialization failed: {}", init_result.error());
            return 1;
        }

        sim_machine->set_start_time(std::chrono::steady_clock::now());

        std::shared_ptr<simrv::tui::Tui> tui;
        if (sim_machine->tui_enabled()) {
            tui = std::make_shared<simrv::tui::Tui>(*sim_machine);
            sim_machine->set_telemetry_sink(tui);
            sim_machine->set_console_sink(tui);
            tui->initialize();
        }

        if (parsed->options.server_mode) {
            if (ipc_server)
                ipc_server->bind(*sim_machine);
            else
                ipc_server = std::make_shared<simrv::net::SimRvServer>(
                    *sim_machine, parsed->options.server_endpoint);
            sim_machine->set_telemetry_sink(ipc_server);
            sim_machine->set_console_sink(ipc_server);
            if (!ipc_server->start()) {
                ipc_server->unbind();
                return 1;
            }
        }

        // Initialize terminal in raw mode for simulator I/O.
        TerminalModeGuard terminal_mode;
        if (!parsed->options.server_mode && !terminal_mode.enable_raw_mode()) {
            if (!is_tui) {
                simrv::log::warn("Terminal raw mode setup failed; continuing in current mode");
            }
        }
        if (sim_machine->tui_enabled()) {
            auto* machine_ptr = sim_machine.get();
            std::thread sim_thread([machine_ptr]() -> void { machine_ptr->run(); });

            if (sim_thread.joinable()) {
                sim_thread.join();
            }
        } else {
            sim_machine->run();
        }

        if (ipc_server) {
            ipc_server->unbind();
        }

        if (sim_machine->reboot_requested) {
            simrv::log::info("Rebooting guest system...");
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            auto next_config =
                sim_machine->take_staged_reconfiguration().value_or(sim_machine->configuration());

            staged_configuration = std::move(next_config);
            staged_runtime_profile = sim_machine->runtime_profile;
            continue;
        } else {
            keep_running = false;
            final_exit_code = sim_machine->exit_code.load();
            if (!sim_machine->tui_enabled()) {
                sim_machine->trace().print_summary();
            }
            if (!sim_machine->configuration().files.dump_dmem_path.empty()) {
                std::ofstream out(sim_machine->configuration().files.dump_dmem_path);
                if (out) {
                    const auto ram = sim_machine->ram_view();
                    constexpr ::Address kDmemBase = 0x10000000;
                    for (size_t i = 0; i < 512; ++i) {
                        const ::Address a = kDmemBase + i * 4;
                        if (ram.contains(a, 4)) {
                            const uint32_t val =
                                static_cast<uint32_t>(
                                    std::to_integer<uint8_t>(*ram.unchecked_ptr(a))) |
                                (static_cast<uint32_t>(
                                     std::to_integer<uint8_t>(*ram.unchecked_ptr(a + 1)))
                                 << 8) |
                                (static_cast<uint32_t>(
                                     std::to_integer<uint8_t>(*ram.unchecked_ptr(a + 2)))
                                 << 16) |
                                (static_cast<uint32_t>(
                                     std::to_integer<uint8_t>(*ram.unchecked_ptr(a + 3)))
                                 << 24);
                            std::println(out, "{:08x}", val);
                        } else {
                            std::println(out, "00000000");
                        }
                    }
                }
            }
        }
    }
    return final_exit_code;
}
