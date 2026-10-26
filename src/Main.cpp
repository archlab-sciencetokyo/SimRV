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
#include <format>
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

#include "simrv/core/BuildInfo.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/CpuConfigParser.hpp"
#include "simrv/core/ExitCode.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/TelemetrySink.hpp"
#include "simrv/core/Tracer.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/util/CliParser.hpp"
#include "simrv/util/FormatUtil.hpp"
#include "simrv/util/InstructionExplainer.hpp"
#include "simrv/util/SoCManifest.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

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

auto privilege_mode(simrv::xlen::PrivilegeLevel privilege) -> std::string_view {
    switch (privilege) {
        case simrv::xlen::PrivilegeLevel::User:
            return "U";
        case simrv::xlen::PrivilegeLevel::Supervisor:
            return "S";
        case simrv::xlen::PrivilegeLevel::Machine:
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
    std::println(
        "  public profiles  : RV32GCBV/RV32GCZBV, RV64GCBV/RV64GCZBV (canonical RV32GCV, RV64GCV "
        "with Zb)");
    std::println(
        "  selectable       : rv{}i, rv{}imac, rv{}gc, rv{}gczb, rv{}gcv, rv{}gczbv (plus "
        "_zba_zbb_zbc_zbs)",
        simrv::xlen::kXLenBits, simrv::xlen::kXLenBits, simrv::xlen::kXLenBits,
        simrv::xlen::kXLenBits, simrv::xlen::kXLenBits, simrv::xlen::kXLenBits);
    std::println("  vector           : RVV 1.0 implemented subset; VLEN 128..1024 bits");
    std::println("  privilege         : M/S/U, Sv32 (RV32), Sv39/Sv48 (RV64)");
    std::println("  debug frontends   : TUI, GDB RSP");
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
    std::println("  interactive TTY  : {}", tty ? "yes" : "no (use --cli for pipes)");
    std::println("  TERM             : {}", term_value.empty() ? "(unset)" : term_value);
    std::println("  Sixel hint       : {}",
                 sixel ? "detected" : "not detected (text TUI remains available)");
    std::println("  mouse protocol   : SGR-pixel mode requested by interactive TUI");
    std::println(
        "  package runtime  : use the static musl archive outside the tested native package "
        "matrix");
    return 0;
}

std::atomic<simrv::core::Machine*> g_active_cli_machine{nullptr};

void handle_cli_termination_signal(int sig) {
    simrv::util::restore_terminal_mode();
    if (auto* machine = g_active_cli_machine.load(std::memory_order_relaxed)) {
        machine->exit_code.store(128 + sig);
        machine->stop(simrv::core::Machine::StopReason::ExternalStop);
    }
    ::signal(sig, SIG_DFL);
    ::raise(sig);
}

void install_cli_signal_handlers() {
    struct sigaction sa{};
    sa.sa_handler = handle_cli_termination_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGHUP, &sa, nullptr);
    sigaction(SIGQUIT, &sa, nullptr);
    sigaction(SIGPIPE, &sa, nullptr);
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
                   arg == "--dump-soc-manifest") {
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
        } else if (arg == "--log-stderr") {
            simrv::log::set_log_to_stderr(true);
        }
    }

    simrv::log::set_tui_mode(is_tui);

    if (!is_tui) {
        install_cli_signal_handlers();
    }

    std::optional<simrv::core::MachineConfig> staged_configuration;
    std::optional<simrv::core::RuntimeProfile> staged_runtime_profile;

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
                usage(args.front(), simrv::core::HostExitCode::Success);
            case CliAction::ShowVersion:
                std::println("{} (RV{})", simrv::buildinfo::kVersion, simrv::xlen::kXLenBits);
                std::exit(simrv::core::to_exit_code(simrv::core::HostExitCode::Success));
            case CliAction::ShowIsaInfo:
                print_isa_info();
                std::exit(simrv::core::to_exit_code(simrv::core::HostExitCode::Success));
            case CliAction::Doctor:
                std::exit(print_doctor());
            case CliAction::ShowLicense:
                std::println("SimRV {} (RV{})", simrv::buildinfo::kVersion, simrv::xlen::kXLenBits);
                std::println("Licensed under the MIT License.");
                std::println("Copyright (c) 2024-2026 Lennart Trunk and ArchLab @ ScienceTokyo");
                std::println("Full license: LICENSE; third-party notices: THIRD_PARTY_NOTICES.md");
                std::exit(simrv::core::to_exit_code(simrv::core::HostExitCode::Success));
            case CliAction::ExplainInstruction:
                simrv::util::explain_instruction(parsed->options.explain_inst_val);
                std::exit(simrv::core::to_exit_code(simrv::core::HostExitCode::Success));
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
                if (!parsed->options.dump_cpu_model_output.empty() &&
                    parsed->options.dump_cpu_model_output != "-") {
                    if (!simrv::core::save_cpu_config(parsed->options.dump_cpu_model_output, cfg,
                                                      target)) {
                        simrv::log::error("Failed to write CPU model to {}",
                                          parsed->options.dump_cpu_model_output);
                        std::exit(
                            simrv::core::to_exit_code(simrv::core::HostExitCode::GeneralError));
                    }
                    std::println("Wrote CPU model configuration to {}",
                                 parsed->options.dump_cpu_model_output);
                } else {
                    simrv::core::serialize_cpu_config(cfg, std::cout, target);
                }
                std::exit(simrv::core::to_exit_code(simrv::core::HostExitCode::Success));
            }
            case CliAction::ExportSoCManifest: {
                const auto target = parsed->options.soc_manifest_preset;
                auto config = simrv::core::SoCConfig::preset(target);
                if (!config.has_value()) {
                    if (const auto resolved = simrv::core::resolve_cpu_model_path(target)) {
                        simrv::core::SoCConfig loaded{};
                        if (simrv::core::load_soc_config(*resolved, loaded)) {
                            config = loaded;
                        }
                    }
                }
                if (!config.has_value()) {
                    simrv::log::error("Unknown SoC preset or invalid config file: {}", target);
                    std::exit(
                        simrv::core::to_exit_code(simrv::core::HostExitCode::ConfigurationError));
                }
                if (const auto valid = config->validate(); !valid) {
                    simrv::log::error("Invalid SoC configuration '{}': {}", target, valid.error());
                    std::exit(
                        simrv::core::to_exit_code(simrv::core::HostExitCode::ConfigurationError));
                }
                if (parsed->options.soc_manifest_output.empty() ||
                    parsed->options.soc_manifest_output == "-") {
                    if (!simrv::util::serialize_soc_manifest(*config, std::cout)) {
                        simrv::log::error("Failed to write SoC manifest");
                        std::exit(
                            simrv::core::to_exit_code(simrv::core::HostExitCode::GeneralError));
                    }
                } else if (!simrv::util::save_soc_manifest(parsed->options.soc_manifest_output,
                                                           *config)) {
                    simrv::log::error("Failed to write SoC manifest to {}",
                                      parsed->options.soc_manifest_output);
                    std::exit(simrv::core::to_exit_code(simrv::core::HostExitCode::GeneralError));
                } else {
                    std::println("Wrote SoC manifest to {}", parsed->options.soc_manifest_output);
                }
                std::exit(simrv::core::to_exit_code(simrv::core::HostExitCode::Success));
            }
            case CliAction::ValidateCpuModel: {
                std::string target = parsed->options.dump_cpu_model_preset;
                if (target.empty()) {
                    simrv::log::error("No configuration path or preset specified to validate.");
                    std::exit(simrv::core::to_exit_code(simrv::core::HostExitCode::UsageError));
                }
                auto resolved = simrv::core::resolve_cpu_model_path(target);
                std::string path_str = resolved.value_or(target);
                simrv::pipeline::CpuModelConfig cfg{};
                simrv::core::SoCConfig soc{};
                bool has_cpu = false;
                bool has_soc = false;

                if (target == "-") {
                    std::string const content((std::istreambuf_iterator<char>(std::cin)),
                                              std::istreambuf_iterator<char>());
                    std::istringstream stream_cpu{content};
                    std::istringstream stream_soc{content};
                    has_cpu = simrv::core::parse_cpu_config_stream(stream_cpu, cfg);
                    has_soc = simrv::core::parse_soc_config_stream(stream_soc, soc);
                    path_str = "<stdin>";
                } else if (std::filesystem::exists(path_str)) {
                    has_cpu = simrv::core::parse_cpu_config(path_str, cfg);
                    has_soc = simrv::core::load_soc_config(path_str, soc);
                } else if (auto soc_preset = simrv::core::SoCConfig::preset(target)) {
                    soc = *soc_preset;
                    has_soc = true;
                    path_str = target + " (built-in SoC preset)";
                } else if (auto cpu_preset = simrv::pipeline::parse_cpu_model_preset(target)) {
                    cfg = simrv::pipeline::make_cpu_model_preset(*cpu_preset);
                    has_cpu = true;
                    path_str = target + " (built-in CPU preset)";
                } else {
                    std::println(std::cerr,
                                 "{}[ERROR]{} Configuration file or preset not found: '{}'",
                                 terminal_style(STDERR_FILENO, "\033[1;31m"),
                                 terminal_style(STDERR_FILENO, "\033[0m"), target);
                    std::exit(
                        simrv::core::to_exit_code(simrv::core::HostExitCode::ConfigurationError));
                }
                if (!has_cpu && !has_soc) {
                    std::println(std::cerr, "{}[INVALID]{} Syntax or parse error reading '{}'",
                                 terminal_style(STDERR_FILENO, "\033[1;31m"),
                                 terminal_style(STDERR_FILENO, "\033[0m"),
                                 simrv::util::terminal_path_label(path_str, STDERR_FILENO));
                    std::exit(
                        simrv::core::to_exit_code(simrv::core::HostExitCode::ConfigurationError));
                }
                std::println("{}=== Validating Configuration: {} ==={}",
                             terminal_style(STDOUT_FILENO, "\033[1;34m"),
                             simrv::util::terminal_path_label(path_str, STDOUT_FILENO),
                             terminal_style(STDOUT_FILENO, "\033[0m"));
                if (has_cpu) {
                    std::println("  Model Name      : {}",
                                 cfg.name.empty() ? "(unnamed)" : cfg.name);
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
                        cfg.instruction_cache.capacity_bytes / 1024,
                        cfg.instruction_cache.line_bytes, cfg.instruction_cache.associativity,
                        cfg.instruction_cache.hit_latency, cfg.instruction_cache.miss_latency);
                    std::println(
                        "  DCache Geometry : {} KiB, {} B line, {}-way (hit: {} cyc, miss: {} cyc)",
                        cfg.data_cache.capacity_bytes / 1024, cfg.data_cache.line_bytes,
                        cfg.data_cache.associativity, cfg.data_cache.hit_latency,
                        cfg.data_cache.miss_latency);

                    auto res = cfg.validate();
                    if (!res.has_value()) {
                        std::println(std::cerr, "{}[INVALID]{} CPU model: {}",
                                     terminal_style(STDERR_FILENO, "\033[1;31m"),
                                     terminal_style(STDERR_FILENO, "\033[0m"), res.error());
                        std::exit(simrv::core::to_exit_code(
                            simrv::core::HostExitCode::ConfigurationError));
                    }
                }
                if (has_soc) {
                    std::println("  SoC Name        : {}", soc.name);
                    std::println("  Device Policy   : {}",
                                 soc.disable_unlisted_devices ? "explicit" : "implicit defaults");
                    if (soc.dram_base.has_value() && soc.dram_size.has_value()) {
                        std::println("  SoC DRAM Base   : {:#x} (size: {:#x})", *soc.dram_base,
                                     *soc.dram_size);
                    }
                    std::println("  SoC Devices     : {} configured", soc.devices.size());

                    auto soc_res = soc.validate();
                    if (!soc_res.has_value()) {
                        std::println(std::cerr, "{}[INVALID]{} SoC configuration: {}",
                                     terminal_style(STDERR_FILENO, "\033[1;31m"),
                                     terminal_style(STDERR_FILENO, "\033[0m"), soc_res.error());
                        std::exit(simrv::core::to_exit_code(
                            simrv::core::HostExitCode::ConfigurationError));
                    }
                }
                std::println(
                    "{}[VALID]{} Configuration is fully valid and compatible with this simulator.",
                    terminal_style(STDOUT_FILENO, "\033[1;32m"),
                    terminal_style(STDOUT_FILENO, "\033[0m"));
                std::exit(simrv::core::to_exit_code(simrv::core::HostExitCode::Success));
            }
            case CliAction::Run:
                if (!parsed->options.tuimode && !skip_banner) {
                    simrv::log::info("{} v{} ({}@{})\nRunning in headless CLI mode.\n",
                                     simrv::buildinfo::kProjectDescription,
                                     simrv::buildinfo::kVersion, simrv::buildinfo::kGitBranch,
                                     simrv::buildinfo::kGitSha);
                }
                break;
        }

        if (parsed->options.log_level.has_value()) {
            simrv::log::set_level(*parsed->options.log_level);
        }
        simrv::log::set_format(parsed->options.log_format);
        simrv::log::set_log_to_stderr(parsed->options.log_stderr);

        if (!parsed->options.fn_log.empty() && !simrv::log::set_log_file(parsed->options.fn_log)) {
            option_error("cannot open log file: " + parsed->options.fn_log,
                         simrv::core::HostExitCode::ConfigurationError);
        }

        auto machine_config = staged_configuration.value_or(parsed->options.to_machine_config());
        if (const auto valid = machine_config.validate(); !valid) {
            option_error(valid.error(), simrv::core::HostExitCode::ConfigurationError);
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
                option_error(applied.error(), simrv::core::HostExitCode::ConfigurationError);
            }
        }

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
                return simrv::core::to_exit_code(simrv::core::HostExitCode::ConfigurationError);
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
            return simrv::core::to_exit_code(simrv::core::HostExitCode::InitializationError);
        }
        sim_machine->trace().refresh_runtime_trace_metadata();
        if (!parsed->options.fn_load_checkpoint.empty()) {
            const auto restored = sim_machine->load_checkpoint(parsed->options.fn_load_checkpoint);
            if (!restored) {
                simrv::log::error("Checkpoint restore failed: {}", restored.error());
                return simrv::core::to_exit_code(simrv::core::HostExitCode::CheckpointError);
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

        std::shared_ptr<UartTranscriptSink> uart_transcript;
        if (!parsed->options.fn_uart_transcript.empty()) {
            uart_transcript = std::make_shared<UartTranscriptSink>(
                parsed->options.fn_uart_transcript, sim_machine->console_sink());
            if (!uart_transcript->is_open()) {
                simrv::log::error("Cannot open UART transcript {}",
                                  parsed->options.fn_uart_transcript);
                return simrv::core::to_exit_code(simrv::core::HostExitCode::ConfigurationError);
            }
            sim_machine->set_console_sink(uart_transcript);
        }

        // Change terminal attributes only when this process owns the foreground terminal.
        // The TUI manages its own terminal lifecycle; batch/redirected CLI runs must not
        // attempt raw mode at all.
        TerminalModeGuard terminal_mode;
        const bool owns_terminal = simrv::util::owns_interactive_terminal();
        if (!sim_machine->tui_enabled() && owns_terminal && !terminal_mode.enable_raw_mode()) {
            simrv::log::warn("Terminal raw mode setup failed; continuing in current mode");
        }
        if (sim_machine->tui_enabled()) {
            auto* machine_ptr = sim_machine.get();
            std::thread sim_thread([machine_ptr]() -> void { machine_ptr->run(); });

            if (sim_thread.joinable()) {
                sim_thread.join();
            }
        } else {
            g_active_cli_machine.store(sim_machine.get(), std::memory_order_release);
            sim_machine->run();
            g_active_cli_machine.store(nullptr, std::memory_order_release);
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
            if (final_exit_code == 0) {
                switch (sim_machine->stop_reason()) {
                    case simrv::core::Machine::StopReason::UnhandledTrap:
                    case simrv::core::Machine::StopReason::LockstepDivergence:
                    case simrv::core::Machine::StopReason::GuestCrash:
                    case simrv::core::Machine::StopReason::TohostFail:
                        final_exit_code =
                            simrv::core::to_exit_code(simrv::core::HostExitCode::ExecutionError);
                        break;
                    case simrv::core::Machine::StopReason::ExternalStop:
                        final_exit_code = 130;
                        break;
                    default:
                        break;
                }
            }
            if (!sim_machine->tui_enabled()) {
                sim_machine->trace().print_summary();
            }
            if (!parsed->options.fn_summary.empty() &&
                !sim_machine->trace().write_summary_json(parsed->options.fn_summary)) {
                simrv::log::error("Cannot write execution summary to {}",
                                  parsed->options.fn_summary);
                if (final_exit_code == 0) {
                    final_exit_code =
                        simrv::core::to_exit_code(simrv::core::HostExitCode::GeneralError);
                }
            }
            if (!parsed->options.fn_signature.empty() &&
                !sim_machine->dump_signature(parsed->options.fn_signature)) {
                simrv::log::error("Cannot write architectural signature to {}",
                                  parsed->options.fn_signature);
                if (final_exit_code == 0) {
                    final_exit_code =
                        simrv::core::to_exit_code(simrv::core::HostExitCode::GeneralError);
                }
            }
            if (!parsed->options.fn_save_checkpoint.empty()) {
                const auto saved = sim_machine->save_checkpoint(parsed->options.fn_save_checkpoint);
                if (!saved) {
                    simrv::log::error("Checkpoint save failed: {}", saved.error());
                    if (final_exit_code == 0) {
                        final_exit_code =
                            simrv::core::to_exit_code(simrv::core::HostExitCode::CheckpointError);
                    }
                }
            }
        }
    }
    return final_exit_code;
}
