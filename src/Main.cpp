/**
 * @file Main.cpp
 * @brief SimRV entry point and command-line option handling.
 *
 * SimCore/RISC-V functional simulator (ArchLab, Science Tokyo (former TokyoTech)).
 */
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <format>
#include <limits>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#include "simrv/Define.hpp"
#include "simrv/core/Boot.hpp"
#include "simrv/core/BuildInfo.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/util/FormatUtil.hpp"
#include "simrv/xlen/Types.hpp"

static auto parse_scaled_u64(std::string_view num, uint64_t& out) -> bool;
static auto parse_u32_base0(std::string_view num, uint32_t& out) -> bool;

namespace {

enum class CliAction : uint8_t { Run, ShowHelp, ShowVersion };

struct RuntimeOptions {
    std::string fn_memimg;
    std::string fn_dskimg;
    std::string fn_dvtree;
    std::string fn_traplog;

    Address start_pc = simrv::boot::kStartPc;
    Counter fincnt = std::numeric_limits<Counter>::max();
    Counter memimg = 0;
    Counter strace = 0;
    Counter trace_begin = std::numeric_limits<Counter>::max();
    Counter trace_end = std::numeric_limits<Counter>::max();
    Counter enabletimer = 70000000UL;
    Address isatest_tohost = 0x80001000;
    MisaProfile misa_profile = MisaProfile::GC;
    bool misa_override = false;
    unsigned int misa_xlen = 0;

    bool appmode = false;
    bool tuimode = false;
    bool debugmode = false;
    bool dlog_mode = false;
    bool traplog_mode = false;
    bool use_disk = false;
    bool use_mix = false;
    bool bp_trace = false;
    bool isatest = false;
    bool trace_enabled = false;
    bool use_opensbi = false;
    bool cycle_accurate = false;
    bool high_performance = true;
    bool high_contrast = false;

    // Debug / co-simulation options
    bool gdb_mode = false;
    uint16_t gdb_port = 1234;
    bool lockstep_mode = false;
    std::string spike_bin = "spike";
    std::string spike_elf;

    std::string fn_cpuconfig;
    bool debug_mode = false;
};

struct ParseResult {
    CliAction action = CliAction::Run;
    RuntimeOptions options{};
};

class TerminalModeGuard {
   public:
    ~TerminalModeGuard() {
        if (active_) {
            tcsetattr(0, TCSANOW, &saved_);
        }
    }

    auto enable_raw_mode() -> bool {
        struct termios tty{};
        if (tcgetattr(0, &tty) != 0) {
            return false;
        }
        saved_ = tty;

        tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
        tty.c_oflag |= OPOST;
        tty.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN);
        tty.c_cflag &= ~(CSIZE | PARENB);
        tty.c_cflag |= CS8;
        tty.c_cc[VMIN] = 1;
        tty.c_cc[VTIME] = 0;
        if (tcsetattr(0, TCSANOW, &tty) != 0) {
            return false;
        }

        active_ = true;
        return true;
    }

   private:
    bool active_ = false;
    struct termios saved_{};
};

[[noreturn]] void option_error(std::string_view message, int code = 1) {
    simrv::log::error("{}", message);
    std::exit(code);
}

auto next_argument(std::span<char* const> args, std::size_t& index, std::string_view option_name)
    -> std::expected<std::string_view, std::string> {
    if (index + 1 >= args.size()) {
        return std::unexpected(std::format("missing value for {}", option_name));
    }
    ++index;
    return std::string_view(
        args[index]);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
}

auto parse_scaled_required(std::span<char* const> args, std::size_t& index,
                           std::string_view option_name) -> std::expected<uint64_t, std::string> {
    return next_argument(args, index, option_name)
        .and_then([&](std::string_view value_text) -> std::expected<uint64_t, std::string> {
            uint64_t parsed_value = 0;
            if (!parse_scaled_u64(value_text, parsed_value)) {
                return std::unexpected(std::format("invalid numeric value for {}", option_name));
            }
            return parsed_value;
        });
}

auto parse_u32_required(std::span<char* const> args, std::size_t& index,
                        std::string_view option_name) -> std::expected<uint32_t, std::string> {
    return next_argument(args, index, option_name)
        .and_then([&](std::string_view value_text) -> std::expected<uint32_t, std::string> {
            uint32_t parsed_value = 0;
            if (!parse_u32_base0(value_text, parsed_value)) {
                return std::unexpected(std::format("invalid address value for {}", option_name));
            }
            return parsed_value;
        });
}

auto parse_trace_window(RuntimeOptions& options, std::span<char* const> args, std::size_t& index)
    -> std::expected<void, std::string> {
    return parse_scaled_required(args, index, "-t")
        .and_then([&](uint64_t begin) -> std::expected<void, std::string> {
            return parse_scaled_required(args, index, "-t")
                .and_then([&](uint64_t end) -> std::expected<void, std::string> {
                    if (begin > end) {
                        return std::unexpected("-t begin must be <= end");
                    }
                    options.trace_enabled = true;
                    options.trace_begin = begin;
                    options.trace_end = end;
                    return {};
                });
        });
}

inline auto iequals(std::string_view a, std::string_view b) -> bool {
    return std::ranges::equal(a, b, [](char c1, char c2) -> bool {
        return std::tolower(static_cast<unsigned char>(c1)) ==
               std::tolower(static_cast<unsigned char>(c2));
    });
}

struct ParsedMisa {
    MisaProfile profile;
    unsigned int xlen;
};

auto parse_misa_profile(std::string_view value) -> std::expected<ParsedMisa, std::string> {
    // 1. Accept XLEN-agnostic profiles without warning
    if (iequals(value, "i")) {
        return ParsedMisa{MisaProfile::I, 0};
    }
    if (iequals(value, "imac")) {
        return ParsedMisa{MisaProfile::IMAC, 0};
    }
    if (iequals(value, "gc")) {
        return ParsedMisa{MisaProfile::GC, 0};
    }

    // 2. Accept rv32/rv64 prefixed options and validate constraints
    unsigned int parsed_xlen = 0;
    MisaProfile profile = MisaProfile::GC;
    bool valid = false;

    if (iequals(value, "rv32i")) {
        parsed_xlen = 32;
        profile = MisaProfile::I;
        valid = true;
    } else if (iequals(value, "rv64i")) {
        parsed_xlen = 64;
        profile = MisaProfile::I;
        valid = true;
    } else if (iequals(value, "rv32imac")) {
        parsed_xlen = 32;
        profile = MisaProfile::IMAC;
        valid = true;
    } else if (iequals(value, "rv64imac")) {
        parsed_xlen = 64;
        profile = MisaProfile::IMAC;
        valid = true;
    } else if (iequals(value, "rv32gc")) {
        parsed_xlen = 32;
        profile = MisaProfile::GC;
        valid = true;
    } else if (iequals(value, "rv64gc")) {
        parsed_xlen = 64;
        profile = MisaProfile::GC;
        valid = true;
    }

    if (valid) {
        if (parsed_xlen == 64 && simrv::xlen::kXLenBits == 32) {
            return std::unexpected(std::format(
                "cannot run a 64-bit MISA profile ({}) on a 32-bit simulator build", value));
        }
        return ParsedMisa{profile, parsed_xlen};
    }

    const auto xlen_suffix = simrv::xlen::kIsXLen64 ? "64" : "32";
    auto supported =
        std::format("i, imac, gc, rv{}i, rv{}imac, rv{}gc", xlen_suffix, xlen_suffix, xlen_suffix);
    if constexpr (simrv::xlen::kIsXLen64) {
        supported += ", rv32i, rv32imac, rv32gc";
    }
    return std::unexpected(
        std::format("unsupported MISA profile '{}' (supported: {})", value, supported));
}

auto effective_misa_profile(const RuntimeOptions& options) -> MisaProfile {
    if (options.misa_override) {
        return options.misa_profile;
    }
    return MisaProfile::GC;
}

auto parse_command_line(std::span<char* const> args) -> std::expected<ParseResult, std::string> {
    ParseResult result{};

    for (std::size_t i = 1; i < args.size(); ++i) {
        std::string_view const arg =
            args[i];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        if (arg == "-h" || arg == "--help") {
            result.action = CliAction::ShowHelp;
            return result;
        }
        if (arg == "--version") {
            result.action = CliAction::ShowVersion;
            return result;
        }

        // Memory Image
        if (arg == "-m" || arg == "-k" || arg == "-i" || arg == "--image" || arg == "--kernel") {
            auto value = next_argument(args, i, arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            result.options.fn_memimg = std::string(*value);
            continue;
        }

        // Disk Image
        if (arg == "-D" || arg == "--disk") {
            auto value = next_argument(args, i, arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            result.options.fn_dskimg = std::string(*value);
            result.options.use_disk = true;
            continue;
        }

        // FDT/DTB
        if (arg == "-f" || arg == "--fdt" || arg == "--dtb" || arg == "-c") {
            auto value = next_argument(args, i, arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            result.options.fn_dvtree = std::string(*value);
            continue;
        }

        // Steps limit
        if (arg == "-s" || arg == "--steps" || arg == "-e") {
            auto value = parse_scaled_required(args, i, arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            result.options.fincnt = *value;
            continue;
        }

        // Timer limit
        if (arg == "-t" || arg == "--timer" || arg == "-l") {
            auto value = parse_scaled_required(args, i, arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            result.options.enabletimer = *value;
            continue;
        }

        // Custom tohost address (HTIF)
        if (arg == "-H" || arg == "--tohost-addr") {
            auto value = parse_u32_required(args, i, arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            result.options.isatest_tohost = *value;
            continue;
        }

        // Trace Window / Range
        if (arg == "--trace-range" || arg == "-r") {
            auto trace = parse_trace_window(result.options, args, i);
            if (!trace) {
                return std::unexpected(trace.error());
            }
            continue;
        }

        // MISA profile
        if (arg == "--misa") {
            auto value = next_argument(args, i, "--misa");
            if (!value) {
                return std::unexpected(value.error());
            }
            auto parsed_misa = parse_misa_profile(*value);
            if (!parsed_misa) {
                return std::unexpected(parsed_misa.error());
            }
            result.options.misa_profile = parsed_misa->profile;
            result.options.misa_xlen = parsed_misa->xlen;
            result.options.misa_override = true;
            continue;
        }

        // OpenSBI flag - ignored with deprecation warning
        if (arg == "-B" || arg == "--opensbi") {
            simrv::log::warn(
                "Option '{}' is deprecated. OpenSBI is automatically enabled when a device tree is "
                "loaded.",
                arg);
            continue;
        }

        // Branch trace
        if (arg == "--trace-bpred" || arg == "-w") {
            result.options.bp_trace = true;
            continue;
        }

        // HTIF monitor
        if (arg == "-T" || arg == "--tohost-monitor") {
            result.options.isatest = true;
            continue;
        }

        // Debug mode
        if (arg == "-g" || arg == "-v" || arg == "--debug" || arg == "--verbose") {
            result.options.debugmode = true;
            continue;
        }

        // TUI Debug diagnostics mode
        if (arg == "-d" || arg == "--debug-mode") {
            result.options.debug_mode = true;
            continue;
        }

        // MMIO logging
        if (arg == "--log-mmio" || arg == "-M") {
            result.options.dlog_mode = true;
            continue;
        }

        // Trap logging file
        if (arg == "--trap-log" || arg == "-P") {
            auto value = next_argument(args, i, arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            result.options.fn_traplog = std::string(*value);
            result.options.traplog_mode = true;
            continue;
        }

        // Instruction Mix report
        if (arg == "--instmix" || arg == "-x") {
            result.options.use_mix = true;
            continue;
        }

        // Baremetal mode
        if (arg == "-b" || arg == "--baremetal" || arg == "-a") {
            result.options.start_pc = 0;
            result.options.appmode = true;
            continue;
        }

        // Trace period PC
        if (arg == "--trace-pc-period" || arg == "-q") {
            auto value = parse_scaled_required(args, i, arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            result.options.strace = *value;
            continue;
        }

        // Dump initial state
        if (arg == "--dump-init" || arg == "-I") {
            auto value = parse_scaled_required(args, i, arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            result.options.memimg = *value;
            continue;
        }

        // TUI dashboard
        if (arg == "--tui" || arg == "-u") {
            result.options.tuimode = true;
            continue;
        }

        // High-contrast TUI mode
        if (arg == "--high-contrast" || arg == "--contrast") {
            result.options.high_contrast = true;
            continue;
        }

        // Shorthands for cycle-accurate
        if (arg == "--ca") {
            result.options.cycle_accurate = true;
            result.options.high_performance = false;
            continue;
        }

        // Shorthands for instruction-accurate / high-performance
        if (arg == "--ia") {
            result.options.cycle_accurate = false;
            result.options.high_performance = true;
            continue;
        }

        // Cycle-accurate simulation mode
        if (arg == "--cycle-accurate" || arg == "-C") {
            result.options.cycle_accurate = true;
            result.options.high_performance = false;
            continue;
        }

        // High-accuracy mode
        if (arg == "--high-accuracy" || arg == "--accuracy-mode") {
            result.options.cycle_accurate = true;
            result.options.high_performance = false;
            continue;
        }

        // High-performance mode
        if (arg == "--high-performance" || arg == "--perf-mode") {
            result.options.cycle_accurate = false;
            result.options.high_performance = true;
            continue;
        }

        // ---- Debug / co-simulation ----
        if (arg == "--gdb" || arg == "-G") {
            result.options.gdb_mode = true;
            continue;
        }
        if (arg == "--gdb-port" || arg == "--port" || arg == "-p") {
            auto value = next_argument(args, i, arg);
            if (!value) return std::unexpected(value.error());
            uint64_t port_val = 0;
            if (!parse_scaled_u64(*value, port_val) || port_val == 0 || port_val > 65535) {
                return std::unexpected(std::format("invalid GDB port value for {}", arg));
            }
            result.options.gdb_port = static_cast<uint16_t>(port_val);
            continue;
        }
        if (arg == "--lockstep") {
            result.options.lockstep_mode = true;
            continue;
        }
        if (arg == "--spike-bin") {
            auto value = next_argument(args, i, "--spike-bin");
            if (!value) return std::unexpected(value.error());
            result.options.spike_bin = std::string(*value);
            continue;
        }
        if (arg == "--spike-elf") {
            auto value = next_argument(args, i, "--spike-elf");
            if (!value) return std::unexpected(value.error());
            result.options.spike_elf = std::string(*value);
            continue;
        }

        // CPU configuration file
        if (arg == "--cpu-config") {
            auto value = next_argument(args, i, arg);
            if (!value) {
                return std::unexpected(value.error());
            }
            result.options.fn_cpuconfig = std::string(*value);
            continue;
        }

        return std::unexpected(std::format("unknown option : {}", arg));
    }

    if (result.options.fn_memimg.empty() && !result.options.tuimode) {
        return std::unexpected("-m/--image <FILE> is required to load a memory image");
    }

    return result;
}

auto apply_runtime_options(simrv::core::Machine* machine, const RuntimeOptions& options)
    -> std::expected<void, std::string> {
    machine->s_fn_memimg = options.fn_memimg;
    machine->s_fn_dskimg = options.fn_dskimg;
    machine->s_fn_dvtree = options.fn_dvtree;
    machine->s_fn_traplog = options.fn_traplog;

    machine->s_start_pc = options.start_pc;
    machine->s_fincnt = options.fincnt;
    machine->s_memimg = options.memimg;
    machine->s_strace = options.strace;
    machine->s_trace_begin = options.trace_begin;
    machine->s_trace_end = options.trace_end;
    machine->s_enabletimer = options.enabletimer;
    machine->s_isatest_tohost = options.isatest_tohost;
    machine->s_misa_profile = misa_profile_bits(effective_misa_profile(options));
    machine->s_misa_override = options.misa_override;
    machine->s_misa_xlen = options.misa_xlen;

    machine->s_appmode = options.appmode;
    simrv::memory::g_appmode = options.appmode;
    machine->s_tuimode = options.tuimode;
    machine->s_high_contrast = options.high_contrast;
    machine->s_debugmode = options.debugmode;
    machine->s_debug_mode = options.debug_mode;
    machine->s_dlog_mode = options.dlog_mode;
    machine->s_traplog_mode = options.traplog_mode;
    machine->s_use_disk = options.use_disk;
    machine->s_use_mix = options.use_mix;
    machine->s_bp_trace = options.bp_trace;
    machine->s_isatest = options.isatest;

    machine->s_cycle_accurate = options.cycle_accurate;
    machine->s_high_performance = options.high_performance;
    machine->s_fn_cpuconfig = options.fn_cpuconfig;

    machine->tracer.init_trace(options.trace_enabled);
    machine->tracer.init_dlog(options.dlog_mode);

    machine->cpu.trap_log_stream = nullptr;
    if (options.traplog_mode) {
        machine->tracer.init_trap_log(options.traplog_mode, options.fn_traplog);
        if (!machine->tracer.fp_traplog.is_open()) {
            return std::unexpected("cannot open trap/SBI log file: " + options.fn_traplog);
        }
        machine->cpu.trap_log_stream = &machine->tracer.fp_traplog;
    }

    machine->cpu.use_opensbi = options.use_opensbi || !options.fn_dvtree.empty();

    // Debug / co-simulation flags
    machine->s_gdb_mode = options.gdb_mode;
    machine->s_gdb_port = options.gdb_port;
    machine->s_lockstep_mode = options.lockstep_mode;
    machine->s_spike_bin = options.spike_bin;
    machine->s_spike_elf = options.spike_elf;

    return {};
}

void set_start_time(simrv::core::Machine& machine) {
    machine.s_start_time = std::chrono::steady_clock::now();
}

}  // namespace

[[noreturn]] static void usage(const char* program_name, int exit_code = 0) {
    const auto xlen_suffix = simrv::xlen::kIsXLen64 ? "64" : "32";
    const bool use_color = simrv::util::is_terminal(STDOUT_FILENO);

    auto style = [use_color](std::string_view ansi_code) -> std::string_view {
        return use_color ? ansi_code : "";
    };

    using namespace simrv::util::ansi;

    // Banner
    std::print(stdout, "\n{}{}{} SimRV - High-Performance RISC-V Functional Simulator {}\n\n",
               style(kBold), style(kBrightWhite), style(kReset), style(kReset));

    // Usage
    std::print(stdout, "{}Usage:{} {}{} [options]{}\n\n", style(kBold), style(kReset),
               style(kBrightGreen), program_name, style(kReset));

    // Required
    std::print(stdout, "{}{}:{}{}\n", style(kBoldFgBrightBlue), "Required", style(kReset),
               style(kReset));
    std::print(stdout,
               "  {}-m, -k, -i, --image, --kernel {}{}<FILE>{} Memory image file to load\n\n",
               style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));

    // Devices & Files
    std::print(stdout, "{}{}:{}{}\n", style(kBoldFgBrightBlue), "Devices and Files", style(kReset),
               style(kReset));
    std::print(
        stdout,
        "  {}-D, --disk {}{}<FILE>{}      Disk image file (enables block storage virtio-disk)\n",
        style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(stdout,
               "  {}-f, -c, --fdt, --dtb {}{}<FILE>{} Device-tree binary file (FDT / DTB "
               "configuration)\n\n",
               style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));

    // Execution Control
    std::print(stdout, "{}{}:{}{}\n", style(kBoldFgBrightBlue),
               "Execution Control (runs on high-performance simrv::pipeline engine)", style(kReset),
               style(kReset));
    std::print(stdout, "  {}-s, -e, --steps {}{}<N>{}    Stop execution after N instructions\n",
               style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(
        stdout,
        "  {}-t, -l, --timer {}{}<N>{}    Enable timer after N cycles (CLINT ticker threshold)\n",
        style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(
        stdout,
        "  {}-b, -a, --baremetal{}         Binary mode (raw baremetal execution, start_pc=0)\n",
        style(kBrightGreen), style(kReset));
    std::print(
        stdout,
        "  {}-u, --tui{}                  Enable interactive TUI split-screen monitor dashboard\n",
        style(kBrightGreen), style(kReset));
    std::print(stdout,
               "  {}--high-contrast, --contrast{} Toggle TUI colors to high-contrast palette\n",
               style(kBrightGreen), style(kReset));

    std::print(stdout,
               "  {}-C, --cycle-accurate, --ca{} Enable structural cycle-accurate performance "
               "simulation mode\n",
               style(kBrightGreen), style(kReset));
    std::print(
        stdout,
        "  {}--high-accuracy, --accuracy-mode{} Alias for --cycle-accurate (High-Accuracy Mode)\n",
        style(kBrightGreen), style(kReset));
    std::print(stdout,
               "  {}--high-performance, --perf-mode, --ia{} Enable optimized simulation mode "
               "bypassing caches/coroutines (default)\n",
               style(kBrightGreen), style(kReset));
    std::print(stdout,
               "  {}--cpu-config {}{}<FILE>{}    Load CPU latency configuration from a text file\n",
               style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(
        stdout,
        "  {}--misa {}{}<PROFILE>{}      Select CPU MISA profile: rv{}i | rv{}imac | rv{}gc\n\n",
        style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset), xlen_suffix,
        xlen_suffix, xlen_suffix);

    // Tracing and Debug
    std::print(stdout, "{}{}:{}{}\n", style(kBoldFgBrightBlue),
               "Tracing and Debug (outputs generated inside trace/ subdirectory)", style(kReset),
               style(kReset));
    std::print(stdout,
               "  {}-r, --trace-range {}{}<BG> <EN>{} Write trace snapshot to trace/trace.txt for "
               "instruction range\n",
               style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(stdout,
               "  {}--trace-pc-period {}{}<N>{}   Generate periodic trace/tracepc.txt every N "
               "instructions\n",
               style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(stdout,
               "  {}--trace-bpred{}           Generate trace/bpred.txt branch prediction trace\n",
               style(kBrightGreen), style(kReset));
    std::print(stdout,
               "  {}-I, --dump-init {}{}<N>{}     Dump initial architectural and TLB state "
               "artifacts at cycle N\n",
               style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(stdout, "  {}-g, -v, --debug, --verbose{} Enable verbose debug logging output\n",
               style(kBrightGreen), style(kReset));
    std::print(stdout,
               "  {}-M, --log-mmio{}              Enable interactive disk/console MMIO transaction "
               "logging\n",
               style(kBrightGreen), style(kReset));
    std::print(
        stdout,
        "  {}--trap-log {}{}<FILE>{}     Write comprehensive trap/SBI diagnostic logs to file\n",
        style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(stdout,
               "  {}--instmix{}               Write instruction mix metrics report to "
               "trace/instmix.txt on exit\n\n",
               style(kBrightGreen), style(kReset));

    // ISA Test Mode
    std::print(stdout, "{}{}:{}{}\n", style(kBoldFgBrightBlue), "ISA Test Mode", style(kReset),
               style(kReset));
    std::print(
        stdout,
        "  {}-T, --tohost-monitor{}    Enable riscv-isa-tests tohost termination monitoring\n",
        style(kBrightGreen), style(kReset));
    std::print(
        stdout,
        "  {}-H, --tohost-addr {}{}<AD>{} Custom custom tohost device MMIO address for -T\n\n",
        style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));

    // Debug / co-simulation
    std::print(stdout, "{}{}:{}{}\n", style(kBoldFgBrightBlue), "Debug / co-simulation",
               style(kReset), style(kReset));
    std::print(stdout, "  {}-g, --debug, -v{}            Enable debug logging in MMIO paths\n",
               style(kBrightGreen), style(kReset));
    std::print(stdout,
               "  {}-d, --debug-mode{}            Enable TUI debug diagnostics panel/symbol view\n",
               style(kBrightGreen), style(kReset));
    std::print(stdout, "  {}--version{}         Show compiler-injected version details and exit\n",
               style(kBrightGreen), style(kReset));
    std::print(
        stdout,
        "  {}-G, --gdb{}                  Enable GDB RSP stub (waits for client before running)\n",
        style(kBrightGreen), style(kReset));
    std::print(
        stdout,
        "  {}-p, --port, --gdb-port {}{}<PORT>{} Override GDB stub listen port (default: 1234)\n",
        style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(
        stdout,
        "  {}--lockstep{}         Enable Spike lockstep instruction-by-instruction verification\n",
        style(kBrightGreen), style(kReset));
    std::print(
        stdout,
        "  {}--spike-bin {}{}<PATH>{} Path to spike binary for lockstep (default: spike in PATH)\n",
        style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(
        stdout,
        "  {}--spike-elf {}{}<PATH>{} Path to spike ELF image for lockstep co-simulation\n\n",
        style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));

    // scaled suffixes
    std::print(
        stdout,
        "{}Numeric values accept standard scaled suffixes:{} k/K (1e3), m/M (1e6), g/G (1e9)\n\n",
        style(kBold), style(kReset));

    // Examples
    std::print(stdout, "{}{}:{}{}\n", style(kBoldFgBrightBlue), "Examples", style(kReset),
               style(kReset));
    std::print(stdout, "  {}{}{} -m img/hello.bin -b\n", style(kBrightBlack), program_name,
               style(kReset));
    std::print(stdout,
               "  {}{}{} --image linux-images/rv{}/fw_payload.bin --disk "
               "linux-images/rv{}/root.bin --fdt linux-images/rv{}/devicetree.dtb\n",
               style(kBrightBlack), program_name, style(kReset), xlen_suffix, xlen_suffix,
               xlen_suffix);
    std::print(stdout, "  {}{}{} --image linux-images/rv{}/fw_payload.bin -u\n",
               style(kBrightBlack), program_name, style(kReset), xlen_suffix);

    std::exit(exit_code);
}

auto parse_scaled_u64(std::string_view num, uint64_t& out) -> bool {
    if (num.empty()) {
        return false;
    }

    uint64_t multiplier = 1;
    const char last = static_cast<char>(std::tolower(static_cast<unsigned char>(num.back())));
    if (last == 'k') {
        multiplier = 1000ULL;
        num.remove_suffix(1);
    } else if (last == 'm') {
        multiplier = 1000000ULL;
        num.remove_suffix(1);
    } else if (last == 'g') {
        multiplier = 1000000000ULL;
        num.remove_suffix(1);
    }

    if (num.empty()) {
        return false;
    }

    uint64_t value = 0;
    const char* begin = num.data();
    const char* end = num.data() + num.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }
    out = value * multiplier;
    return true;
}

auto parse_u32_base0(std::string_view num, uint32_t& out) -> bool {
    if (num.empty()) {
        return false;
    }

    uint64_t value = 0;
    int base = 10;
    if (num.starts_with("0x") || num.starts_with("0X")) {
        num.remove_prefix(2);
        base = 16;
    } else if (num.starts_with('0') && num.size() > 1) {
        num.remove_prefix(1);
        base = 8;
    }

    const char* begin = num.data();
    const char* end = num.data() + num.size();
    const auto result = std::from_chars(begin, end, value, base);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }
    out = static_cast<uint32_t>(value);
    return true;
}

void set_options(simrv::core::Machine* m, int argc, char* const* argv) {
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
            usage(args[0],
                  0);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        case CliAction::ShowVersion:
            std::println("{} (RV{})", simrv::buildinfo::kVersion, simrv::xlen::kXLenBits);
            std::exit(0);
        case CliAction::Run:
            break;
    }

    auto applied = apply_runtime_options(m, parsed->options);
    if (!applied) {
        option_error(applied.error(), 0);
    }
}

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
        simrv::core::Machine sim_machine;

        const int init_result = sim_machine.initialize(argc, argv);
        if (init_result != 0) {
            return init_result;
        }

        set_start_time(sim_machine);

        // Initialize terminal in raw mode for simulator I/O.
        TerminalModeGuard terminal_mode;
        if (!terminal_mode.enable_raw_mode()) {
            if (!is_tui) {
                simrv::log::warn("Terminal raw mode setup failed; continuing in current mode");
            }
        }
        sim_machine.run();

        if (sim_machine.reboot_requested) {
            simrv::log::info("Rebooting guest system...");
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        } else {
            keep_running = false;
            final_exit_code = sim_machine.exit_code;
            if (!sim_machine.s_tuimode) {
                sim_machine.tracer.print_summary();
            }
        }
    }
    return final_exit_code;
}
