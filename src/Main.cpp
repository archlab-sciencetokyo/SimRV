/**
 * @file Main.cpp
 * @brief SimRV entry point and command-line option handling.
 *
 * SimCore/RISC-V functional simulator (ArchLab, Science Tokyo (former TokyoTech)).
 */
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

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
#include "simrv/util/SoCManifest.hpp"
#include "simrv/xlen/Types.hpp"

using namespace simrv::util;

namespace {

class UartTranscriptSink final : public simrv::core::IConsoleSink {
   public:
    UartTranscriptSink(const std::string& path,
                       std::shared_ptr<simrv::core::IConsoleSink> downstream)
        : stream_(path, std::ios::binary | std::ios::trunc), downstream_(std::move(downstream)) {}

    [[nodiscard]] auto is_open() const -> bool { return stream_.is_open(); }

    void handle_char_write(char ch) override {
        if (downstream_) {
            downstream_->handle_char_write(ch);
        } else {
            std::print("{}", ch);
            std::fflush(stdout);
        }
        stream_.put(ch);
    }

   private:
    std::ofstream stream_;
    std::shared_ptr<simrv::core::IConsoleSink> downstream_;
};

auto terminal_style(int fd, std::string_view code) -> std::string_view {
    return simrv::util::terminal_color_enabled(fd) ? code : std::string_view{};
}

auto lifecycle_event_name(simrv::core::LifecycleEventKind kind) -> std::string_view {
    switch (kind) {
        case simrv::core::LifecycleEventKind::Initialized:
            return "initialized";
        case simrv::core::LifecycleEventKind::Started:
            return "started";
        case simrv::core::LifecycleEventKind::Running:
            return "running";
        case simrv::core::LifecycleEventKind::Stopped:
            return "stopped";
        case simrv::core::LifecycleEventKind::Completed:
            return "completed";
        case simrv::core::LifecycleEventKind::Failed:
            return "failed";
        case simrv::core::LifecycleEventKind::RebootRequested:
            return "reboot_requested";
        case simrv::core::LifecycleEventKind::ExitRequested:
            return "exit_requested";
    }
    return "unknown";
}

auto lifecycle_timestamp() -> std::string {
    const auto now = std::chrono::system_clock::now();
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_r(&time, &utc);
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z", utc.tm_year + 1900,
                       utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec,
                       millis.count());
}

auto event_name(simrv::core::MachineEventKind kind) -> std::string_view {
    switch (kind) {
        case simrv::core::MachineEventKind::InterruptAsserted:
            return "interrupt_asserted";
        case simrv::core::MachineEventKind::InterruptDeasserted:
            return "interrupt_deasserted";
        case simrv::core::MachineEventKind::DmaStarted:
            return "dma_started";
        case simrv::core::MachineEventKind::DmaCompleted:
            return "dma_completed";
        case simrv::core::MachineEventKind::DmaCancelled:
            return "dma_cancelled";
    }
    return "unknown";
}

auto json_escape(std::string_view value) -> std::string {
    std::string escaped;
    escaped.reserve(value.size());
    for (const unsigned char ch : value) {
        if (ch == '"' || ch == '\\') {
            escaped.push_back('\\');
            escaped.push_back(static_cast<char>(ch));
        } else if (ch < 0x20) {
            escaped += std::format("\\u{:04x}", static_cast<unsigned>(ch));
        } else {
            escaped.push_back(static_cast<char>(ch));
        }
    }
    return escaped;
}

auto write_machine_event(std::ofstream& out, const simrv::core::MachineEvent& event) -> void {
    if (!out) return;
    out << "{\"schema_version\":2,\"event\":\"" << event_name(event.kind) << "\",\"timestamp\":\""
        << lifecycle_timestamp() << "\",\"cycle\":" << event.cycle;
    if (event.hart != UINT32_MAX) out << ",\"hart\":" << event.hart;
    out << ",\"component\":\"" << json_escape(event.component) << "\",\"payload\":{";
    if (event.kind == simrv::core::MachineEventKind::InterruptAsserted ||
        event.kind == simrv::core::MachineEventKind::InterruptDeasserted) {
        if (event.hart == UINT32_MAX) {
            out << "\"source_id\":" << event.interrupt_source;
        } else {
            out << "\"cause\":" << event.interrupt_cause;
        }
    } else {
        out << "\"transfer_id\":" << event.transfer_id << ",\"byte_count\":" << event.byte_count
            << ",\"request_cycle\":" << event.request_cycle
            << ",\"start_cycle\":" << event.start_cycle
            << ",\"completion_cycle\":" << event.completion_cycle;
    }
    out << "}}\n";
    out.flush();
}

auto privilege_mode(simrv::PrivilegeLevel privilege) -> std::string_view {
    switch (privilege) {
        case simrv::PrivilegeLevel::User:
            return "U";
        case simrv::PrivilegeLevel::Supervisor:
            return "S";
        case simrv::PrivilegeLevel::Machine:
            return "M";
    }
    return "unknown";
}

auto write_lifecycle_event(std::ofstream& out, const simrv::core::Machine& machine,
                           simrv::core::LifecycleEventKind kind, int status = 0) -> void {
    if (!out) return;
    const auto& hart = machine.primary_hart();
    out << "{\"schema_version\":2,\"event\":\"" << lifecycle_event_name(kind)
        << "\",\"timestamp\":\"" << lifecycle_timestamp()
        << "\",\"cycle\":" << hart.clint_mmio.mcycle << ",\"hart\":0,\"pc\":\"0x" << std::hex
        << hart.state().pc << std::dec << "\",\"mode\":\"" << privilege_mode(hart.state().priv)
        << "\",\"payload\":{\"status\":\"" << (status == 0 ? "ok" : "failed")
        << "\",\"stop_reason\":\"" << simrv::core::Machine::stop_reason_name(machine.stop_reason())
        << "\",\"retired_instructions\":" << hart.e_icount
        << ",\"cycles\":" << hart.clint_mmio.mcycle;
    if (status != 0) out << ",\"exit_status\":" << status;
    out << "}}\n";
    out.flush();
}

auto print_isa_info() -> void {
    std::println("SimRV {} capabilities", simrv::buildinfo::kVersion);
    std::println("  host build       : RV{}", simrv::xlen::kXLenBits);
    std::println("  public profiles  : RV32GCBV, RV64GCBV");
    std::println("  selectable       : rv{}i, rv{}imac, rv{}gc, rv{}gcbv", simrv::xlen::kXLenBits,
                 simrv::xlen::kXLenBits, simrv::xlen::kXLenBits, simrv::xlen::kXLenBits);
    std::println("  vector           : RVV 1.0 implemented subset; VLEN 128..1024 bits");
    std::println("  privilege         : M/S/U, Sv32 (RV32), Sv39/Sv48 (RV64)");
    std::println("  debug frontends   : TUI, attachable Unix socket, GDB RSP");
    std::println("  qualified caveats : RMM arithmetic and complete RVV are not claimed");
    std::println("  see               : docs/architecture/compliance.md");
}

auto print_doctor() -> int {
    const char* term = std::getenv("TERM");
    const char* colorterm = std::getenv("COLORTERM");
    const char* term_program = std::getenv("TERM_PROGRAM");
    const bool tty = ::isatty(STDIN_FILENO) != 0 && ::isatty(STDOUT_FILENO) != 0;
    const std::string_view term_value = term != nullptr ? term : "";
    const std::string_view color_value = colorterm != nullptr ? colorterm : "";
    const std::string_view program_value = term_program != nullptr ? term_program : "";
    const bool sixel = term_value.contains("sixel") || color_value.contains("sixel") ||
                       program_value.contains("foot") || program_value.contains("contour") ||
                       program_value.contains("WezTerm");

    std::println("SimRV environment diagnostics");
    std::println("  version          : {}", simrv::buildinfo::kVersion);
    std::println("  host             : Linux x86-64 expected for release binaries");
    std::println("  interactive TTY  : {}", tty ? "yes" : "no (use --cli or --attach for pipes)");
    std::println("  TERM             : {}", term_value.empty() ? "(unset)" : term_value);
    std::println("  Sixel hint       : {}",
                 sixel ? "detected" : "not detected (text TUI remains available)");
    std::println("  mouse protocol   : SGR-pixel mode requested by interactive TUI");
    std::println("  attach transport : local framed Unix socket, bounded and versioned");
    std::println(
        "  package runtime  : use the static musl archive outside the tested native package "
        "matrix");
    return 0;
}

}  // namespace

auto main(int argc, char* argv[]) -> int {  // NOLINT(bugprone-exception-escape)
    bool is_tui = simrv::util::interactive_tui_available();
    bool skip_banner = false;
    for (int i = 1; i < argc; ++i) {
        std::string_view const arg(argv[i]);
        if (arg == "--cli" || arg == "-c") {
            is_tui = false;
        } else if (arg == "--gdb") {
            is_tui = false;
        } else if (arg == "--tui" || arg == "-u") {
            is_tui = true;
        } else if (arg == "-h" || arg == "--help" || arg == "--version" || arg == "--license" ||
                   arg == "--isa-info" || arg == "--capabilities" || arg == "--doctor" ||
                   arg == "--dump-soc-manifest" || arg == "--export-soc-manifest") {
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
        } else if (arg == "--log-format" && i + 1 < argc) {
            if (const auto format = simrv::log::parse_format(argv[i + 1]))
                simrv::log::set_format(*format);
        }
    }

    simrv::log::set_tui_mode(is_tui);

    if (!is_tui && !skip_banner) {
        simrv::log::info("{} v{} ({}@{})\nRunning in headless CLI mode.\n",
                         simrv::buildinfo::kProjectDescription, simrv::buildinfo::kVersion,
                         simrv::buildinfo::kGitBranch, simrv::buildinfo::kGitSha);
    }

    // Write startup entry to MMU debug log
    std::signal(SIGINT, SIG_IGN);  // ignore control+'C'

    std::optional<simrv::core::MachineConfig> staged_configuration;
    std::optional<simrv::core::RuntimeProfile> staged_runtime_profile;

    std::shared_ptr<simrv::net::SimRvServer> ipc_server;
    std::ofstream event_stream;
    bool event_stream_opened = false;
    std::mutex event_stream_mutex;
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
            case CliAction::ShowIsaInfo:
                print_isa_info();
                std::exit(0);
            case CliAction::Doctor:
                std::exit(print_doctor());
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
                std::string target = parsed->options.dump_cpu_model_preset;
                if (target.empty()) target = "balanced";
                if (auto resolved = simrv::core::resolve_cpu_model_path(target)) {
                    (void)simrv::core::load_cpu_config(*resolved, cfg);
                } else if (auto p = simrv::pipeline::parse_cpu_model_preset(target)) {
                    cfg = simrv::pipeline::make_cpu_model_preset(*p);
                } else {
                    cfg = simrv::pipeline::make_cpu_model_preset(
                        simrv::pipeline::CpuModelPreset::Balanced);
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
            case CliAction::ExportSoCManifest: {
                const auto target = parsed->options.soc_manifest_preset;
                auto config = simrv::core::SoCConfig::preset(target);
                if (!config.has_value()) {
                    simrv::log::error("Unknown SoC preset: {}", target);
                    std::exit(1);
                }
                if (const auto resolved = simrv::core::resolve_cpu_model_path(target)) {
                    (void)simrv::core::parse_soc_config(*resolved, *config);
                }
                if (const auto valid = config->validate(); !valid) {
                    simrv::log::error("Invalid SoC preset '{}': {}", target, valid.error());
                    std::exit(1);
                }
                if (parsed->options.soc_manifest_output.empty()) {
                    if (!simrv::util::serialize_soc_manifest(*config, std::cout)) {
                        simrv::log::error("Failed to write SoC manifest");
                        std::exit(1);
                    }
                } else if (!simrv::util::save_soc_manifest(parsed->options.soc_manifest_output,
                                                           *config)) {
                    simrv::log::error("Failed to write SoC manifest to {}",
                                      parsed->options.soc_manifest_output);
                    std::exit(1);
                } else {
                    std::println("Wrote SoC manifest to {}", parsed->options.soc_manifest_output);
                }
                std::exit(0);
            }
            case CliAction::ValidateCpuModel: {
                std::string target = parsed->options.dump_cpu_model_preset;
                if (target.empty()) {
                    simrv::log::error("No CPU model path specified to validate.");
                    std::exit(1);
                }
                auto resolved = simrv::core::resolve_cpu_model_path(target);
                std::string path_str = resolved.value_or(target);
                if (!std::filesystem::exists(path_str)) {
                    std::println(std::cerr, "{}[ERROR]{} CPU config file not found: '{}'",
                                 terminal_style(STDERR_FILENO, "\033[1;31m"),
                                 terminal_style(STDERR_FILENO, "\033[0m"), target);
                    std::exit(1);
                }
                simrv::pipeline::CpuModelConfig cfg{};
                if (!simrv::core::parse_cpu_config(path_str, cfg)) {
                    std::println(std::cerr, "{}[INVALID]{} Syntax or parse error reading '{}'",
                                 terminal_style(STDERR_FILENO, "\033[1;31m"),
                                 terminal_style(STDERR_FILENO, "\033[0m"),
                                 simrv::util::terminal_path_label(path_str, STDERR_FILENO));
                    std::exit(1);
                }
                std::println("{}=== Validating CPU Model Configuration: {} ==={}",
                             terminal_style(STDOUT_FILENO, "\033[1;34m"),
                             simrv::util::terminal_path_label(path_str, STDOUT_FILENO),
                             terminal_style(STDOUT_FILENO, "\033[0m"));
                std::println("  Model Name      : {}", cfg.name.empty() ? "(unnamed)" : cfg.name);
                std::println("  Description     : {}",
                             cfg.description.empty() ? "(none)" : cfg.description);
                std::println("  Required XLEN   : {}",
                             cfg.supported_xlen == 0 ? "Any (RV32 or RV64)"
                                                     : std::format("RV{}", cfg.supported_xlen));
                std::println("  Simulator XLEN  : RV{}", simrv::xlen::kXLenBits);
                std::println("  MISA Profile    : {}",
                             simrv::core::detail::isa_preset_name(cfg.isa_preset));
                std::println("  Pipeline Type   : {}",
                             simrv::pipeline::pipeline_type_name(cfg.pipeline.pipeline_type));
                std::println("  Branch Predictor: {}",
                             simrv::pipeline::to_string(cfg.pipeline.branch_predictor.type));
                std::println(
                    "  ICache Geometry : {} KiB, {} B line, {}-way (hit: {} cyc, miss: {} cyc)",
                    cfg.instruction_cache.capacity_bytes / 1024, cfg.instruction_cache.line_bytes,
                    cfg.instruction_cache.associativity, cfg.instruction_cache.hit_latency,
                    cfg.instruction_cache.miss_latency);
                std::println(
                    "  DCache Geometry : {} KiB, {} B line, {}-way (hit: {} cyc, miss: {} cyc)",
                    cfg.data_cache.capacity_bytes / 1024, cfg.data_cache.line_bytes,
                    cfg.data_cache.associativity, cfg.data_cache.hit_latency,
                    cfg.data_cache.miss_latency);

                auto res = cfg.validate();
                if (!res.has_value()) {
                    std::println(std::cerr, "{}[INVALID]{} {}",
                                 terminal_style(STDERR_FILENO, "\033[1;31m"),
                                 terminal_style(STDERR_FILENO, "\033[0m"), res.error());
                    std::exit(1);
                }
                std::println(
                    "{}[VALID]{} CPU model configuration is fully valid and compatible "
                    "with this simulator.",
                    terminal_style(STDOUT_FILENO, "\033[1;32m"),
                    terminal_style(STDOUT_FILENO, "\033[0m"));
                std::exit(0);
            }
            case CliAction::Run:
                break;
        }

        if (parsed->options.log_level.has_value()) {
            simrv::log::set_level(*parsed->options.log_level);
        }
        simrv::log::set_format(parsed->options.log_format);

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
        std::vector<std::string> command_line;
        command_line.reserve(static_cast<size_t>(argc));
        for (int arg = 0; arg < argc; ++arg) command_line.emplace_back(argv[arg]);
        sim_machine->trace().set_command_line(std::move(command_line));

        if (staged_runtime_profile.has_value()) {
            sim_machine->runtime_profile = *staged_runtime_profile;
        } else {
            auto applied = apply_runtime_options(sim_machine.get(), parsed->options);
            if (!applied) {
                option_error(applied.error(), 0);
            }
        }

        sim_machine->set_persistent_control(parsed->options.server_mode);
        std::string lifecycle_path = parsed->options.fn_events;
        if (lifecycle_path.empty() && !parsed->options.fn_archtrace.empty()) {
            const std::filesystem::path retire_path(parsed->options.fn_archtrace);
            const auto directory = retire_path.has_parent_path() ? retire_path.parent_path()
                                                                 : std::filesystem::path(".");
            lifecycle_path = (directory / "events.jsonl").string();
        }
        if (!lifecycle_path.empty() && !event_stream_opened) {
            event_stream.open(lifecycle_path, std::ios::trunc);
            event_stream_opened = true;
            if (!event_stream) {
                simrv::log::error("Cannot open lifecycle event stream {}", lifecycle_path);
                return 1;
            }
        }

        if (event_stream) {
            (void)sim_machine->add_lifecycle_observer(
                [&event_stream, &event_stream_mutex,
                 machine = sim_machine.get()](const simrv::core::LifecycleEvent& event) {
                    const std::scoped_lock lock(event_stream_mutex);
                    write_lifecycle_event(event_stream, *machine, event.kind, event.exit_status);
                });
            (void)sim_machine->add_event_observer(
                [&event_stream, &event_stream_mutex](const simrv::core::MachineEvent& event) {
                    const std::scoped_lock lock(event_stream_mutex);
                    write_machine_event(event_stream, event);
                });
        }

        const auto init_result = sim_machine->initialize();
        if (!init_result) {
            simrv::log::error("Machine initialization failed: {}", init_result.error());
            if (event_stream) {
                event_stream << "{\"schema_version\":2,\"event\":\"failed\",\"timestamp\":\""
                             << lifecycle_timestamp()
                             << "\",\"cycle\":0,\"payload\":{\"status\":\"failed\",\"error\":"
                                "\"initialization failed\"}}\n";
                event_stream.flush();
            }
            return 1;
        }
        sim_machine->trace().refresh_runtime_trace_metadata();
        if (!parsed->options.fn_load_checkpoint.empty()) {
            const auto restored = sim_machine->load_checkpoint(parsed->options.fn_load_checkpoint);
            if (!restored) {
                simrv::log::error("Checkpoint restore failed: {}", restored.error());
                return 1;
            }
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

        std::shared_ptr<UartTranscriptSink> uart_transcript;
        if (!parsed->options.fn_uart_transcript.empty()) {
            uart_transcript = std::make_shared<UartTranscriptSink>(
                parsed->options.fn_uart_transcript, sim_machine->console_sink());
            if (!uart_transcript->is_open()) {
                simrv::log::error("Cannot open UART transcript {}",
                                  parsed->options.fn_uart_transcript);
                return 1;
            }
            sim_machine->set_console_sink(uart_transcript);
        }

        // Change terminal attributes only when this process owns the foreground terminal.
        // The TUI manages its own terminal lifecycle; batch/redirected CLI runs must not
        // attempt raw mode at all.
        TerminalModeGuard terminal_mode;
        const bool owns_terminal = simrv::util::owns_interactive_terminal();
        if (!parsed->options.server_mode && !sim_machine->tui_enabled() && owns_terminal &&
            !terminal_mode.enable_raw_mode()) {
            simrv::log::warn("Terminal raw mode setup failed; continuing in current mode");
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
            if (!parsed->options.fn_summary.empty() &&
                !sim_machine->trace().write_summary_json(parsed->options.fn_summary)) {
                simrv::log::error("Cannot write execution summary to {}",
                                  parsed->options.fn_summary);
                if (final_exit_code == 0) final_exit_code = 1;
            }
            if (!parsed->options.fn_save_checkpoint.empty()) {
                const auto saved = sim_machine->save_checkpoint(parsed->options.fn_save_checkpoint);
                if (!saved) {
                    simrv::log::error("Checkpoint save failed: {}", saved.error());
                    if (final_exit_code == 0) final_exit_code = 1;
                }
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
