/**
 * @file Main.cpp
 * @brief SimRV entry point and command-line option handling.
 *
 * SimCore/RISC-V functional simulator (ArchLab, Science Tokyo (former TokyoTech)).
 */
#include "simrv/core/Logger.hpp"
#include "simrv/util/FormatUtil.hpp"
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

#include "simrv/Define.hpp"
#include "simrv/core/Boot.hpp"
#include "simrv/core/BuildInfo.hpp"
#include "simrv/core/Machine.hpp"
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

    bool appmode = false;
    bool tuimode = false;
    bool debugmode = false;
    bool dlog_mode = false;
    bool traplog_mode = false;
    bool use_disk = false;
    bool use_mix = false;
    bool bp_trace = false;
    bool isatest = false;
    bool gen_binfile = false;
    bool trace_enabled = false;
    bool use_opensbi = false;

    // Debug / co-simulation options
    bool     gdb_mode      = false;
    uint16_t gdb_port      = 1234;
    bool     lockstep_mode = false;
    std::string spike_bin  = "spike";
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
    return std::string_view(args[index]);
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
        .and_then([&](uint64_t begin) {
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
    return std::ranges::equal(a, b, [](char c1, char c2) {
        return std::tolower(static_cast<unsigned char>(c1)) ==
               std::tolower(static_cast<unsigned char>(c2));
    });
}

auto parse_misa_profile(std::string_view value) -> std::expected<MisaProfile, std::string> {
    // 1. Accept XLEN-agnostic profiles without warning
    if (iequals(value, "i")) {
        return MisaProfile::I;
    }
    if (iequals(value, "imac")) {
        return MisaProfile::IMAC;
    }
    if (iequals(value, "gc")) {
        return MisaProfile::GC;
    }

    // 2. Accept rv32/rv64 prefixed options and warn if they don't match the current simulator XLEN
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
        if (parsed_xlen != simrv::xlen::kXLenBits) {
            simrv::log::warn("Specified MISA profile '{}' has XLEN={} which differs from simulator architecture (RV{}). Operating under RV{} mode.",
                         value, parsed_xlen, simrv::xlen::kXLenBits, simrv::xlen::kXLenBits);
        }
        return profile;
    }

    const auto xlen_suffix = simrv::xlen::kIsXLen64 ? "64" : "32";
    auto supported = std::format("i, imac, gc, rv{}i, rv{}imac, rv{}gc", xlen_suffix, xlen_suffix, xlen_suffix);
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
        std::string_view const arg = args[i];
        if (arg == "-h" || arg == "--help") {
            result.action = CliAction::ShowHelp;
            return result;
        }
        if (arg == "--version") {
            result.action = CliAction::ShowVersion;
            return result;
        }

        if (arg == "-m") {
            auto value = next_argument(args, i, "-m");
            if (!value) {
                return std::unexpected(value.error());
            }
            result.options.fn_memimg = std::string(*value);
            continue;
        }
        if (arg == "-d") {
            auto value = next_argument(args, i, "-d");
            if (!value) {
                return std::unexpected(value.error());
            }
            result.options.fn_dskimg = std::string(*value);
            result.options.use_disk = true;
            continue;
        }
        if (arg == "-c") {
            auto value = next_argument(args, i, "-c");
            if (!value) {
                return std::unexpected(value.error());
            }
            result.options.fn_dvtree = std::string(*value);
            continue;
        }

        if (arg == "-e") {
            auto value = parse_scaled_required(args, i, "-e");
            if (!value) {
                return std::unexpected(value.error());
            }
            result.options.fincnt = *value;
            continue;
        }
        if (arg == "-i") {
            auto value = parse_scaled_required(args, i, "-i");
            if (!value) {
                return std::unexpected(value.error());
            }
            result.options.memimg = *value;
            continue;
        }
        if (arg == "-q") {
            auto value = parse_scaled_required(args, i, "-q");
            if (!value) {
                return std::unexpected(value.error());
            }
            result.options.strace = *value;
            continue;
        }
        if (arg == "-l") {
            auto value = parse_scaled_required(args, i, "-l");
            if (!value) {
                return std::unexpected(value.error());
            }
            result.options.enabletimer = *value;
            continue;
        }
        if (arg == "-H") {
            auto value = parse_u32_required(args, i, "-H");
            if (!value) {
                return std::unexpected(value.error());
            }
            result.options.isatest_tohost = *value;
            continue;
        }
        if (arg == "-t") {
            auto trace = parse_trace_window(result.options, args, i);
            if (!trace) {
                return std::unexpected(trace.error());
            }
            continue;
        }
        if (arg == "--misa") {
            auto value = next_argument(args, i, "--misa");
            if (!value) {
                return std::unexpected(value.error());
            }
            auto profile = parse_misa_profile(*value);
            if (!profile) {
                return std::unexpected(profile.error());
            }
            result.options.misa_profile = *profile;
            result.options.misa_override = true;
            continue;
        }

        if (arg == "-B" || arg == "--opensbi") {
            result.options.use_opensbi = true;
            continue;
        }

        if (arg == "-w") {
            result.options.bp_trace = true;
            continue;
        }
        if (arg == "-T") {
            result.options.isatest = true;
            continue;
        }
        if (arg == "-b") {
            result.options.gen_binfile = true;
            continue;
        }
        if (arg == "-g") {
            result.options.debugmode = true;
            continue;
        }
        if (arg == "-p") {
            result.options.dlog_mode = true;
            continue;
        }
        if (arg == "-P" || arg == "--trap-log") {
            auto value = next_argument(args, i, "-P/--trap-log");
            if (!value) {
                return std::unexpected(value.error());
            }
            result.options.fn_traplog = std::string(*value);
            result.options.traplog_mode = true;
            continue;
        }
        if (arg == "-x") {
            result.options.use_mix = true;
            continue;
        }
        if (arg == "-a") {
            result.options.start_pc = 0;
            result.options.appmode = true;
            continue;
        }
        if (arg == "--tui") {
            result.options.tuimode = true;
            continue;
        }

        // ---- Debug / co-simulation ----
        if (arg == "--gdb") {
            result.options.gdb_mode = true;
            continue;
        }
        if (arg == "--gdb-port") {
            auto value = next_argument(args, i, "--gdb-port");
            if (!value) return std::unexpected(value.error());
            uint64_t port_val = 0;
            if (!parse_scaled_u64(*value, port_val) || port_val == 0 || port_val > 65535) {
                return std::unexpected("invalid GDB port value for --gdb-port");
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

        return std::unexpected(std::format("unknown option : {}", arg));
    }

    if (result.options.fn_memimg.empty()) {
        return std::unexpected("-m <FILE> is required to load a memory image");
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

    machine->s_appmode = options.appmode;
    machine->s_tuimode = options.tuimode;
    machine->s_debugmode = options.debugmode;
    machine->s_dlog_mode = options.dlog_mode;
    machine->s_traplog_mode = options.traplog_mode;
    machine->s_use_disk = options.use_disk;
    machine->s_use_mix = options.use_mix;
    machine->s_bp_trace = options.bp_trace;
    machine->s_isatest = options.isatest;
    machine->s_gen_binfile = options.gen_binfile;

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

    machine->cpu.use_opensbi = options.use_opensbi;

    // Debug / co-simulation flags
    machine->s_gdb_mode      = options.gdb_mode;
    machine->s_gdb_port      = options.gdb_port;
    machine->s_lockstep_mode = options.lockstep_mode;
    machine->s_spike_bin     = options.spike_bin;

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
    std::print(stdout, "{}Usage:{} {}{} [options]{}\n\n",
                 style(kBold), style(kReset),
                 style(kBrightGreen), program_name, style(kReset));

    // Required
    std::print(stdout, "{}{}:{}{}\n", style(kBoldFgBrightBlue), "Required", style(kReset), style(kReset));
    std::print(stdout, "  {}-m {}{}<FILE>{}        Memory image file (required to boot)\n\n",
                 style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));

    // Images and Devices
    std::print(stdout, "{}{}:{}{}\n", style(kBoldFgBrightBlue), "Images and Devices", style(kReset), style(kReset));
    std::print(stdout, "  {}-d {}{}<FILE>{}        Disk image file (enables block storage virtio-disk)\n",
                 style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(stdout, "  {}-c {}{}<FILE>{}        Device-tree binary file (FDT / DTB configuration)\n\n",
                 style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));

    // Execution Control
    std::print(stdout, "{}{}:{}{}\n", style(kBoldFgBrightBlue), "Execution Control (runs on high-performance simrv::pipeline engine)", style(kReset), style(kReset));
    std::print(stdout, "  {}-e {}{}<N>{}           Stop execution after N instructions (zero-overhead count)\n",
                 style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(stdout, "  {}-l {}{}<N>{}           Enable timer after N cycles (CLINT ticker threshold)\n",
                 style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(stdout, "  {}-B, --opensbi{}    Bypass legacy C++ SBI and boot native OpenSBI kernel payload\n",
                 style(kBrightGreen), style(kReset));
    std::print(stdout, "  {}-a{}                Binary mode (raw baremetal execution, start_pc=0, no OS / SBI)\n",
                 style(kBrightGreen), style(kReset));
    std::print(stdout, "  {}--tui{}             Enable interactive TUI split-screen monitor dashboard\n",
                 style(kBrightGreen), style(kReset));
    std::print(stdout, "  {}--misa {}{}<PROFILE>{} Select CPU MISA profile: rv{}i | rv{}imac | rv{}gc\n\n",
                 style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset), xlen_suffix, xlen_suffix, xlen_suffix);

    // Tracing and Debug
    std::print(stdout, "{}{}:{}{}\n", style(kBoldFgBrightBlue), "Tracing and Debug (outputs generated inside trace/ subdirectory)", style(kReset), style(kReset));
    std::print(stdout, "  {}-t {}{}<BEGIN> <END>{} Write trace/trace.txt for instruction range [BEGIN, END]\n",
                 style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(stdout, "  {}-q {}{}<N>{}           Generate periodic trace/tracepc.txt every 1,000 instructions after N\n",
                 style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(stdout, "  {}-w{}                Generate trace/bpred.txt branch prediction trace\n",
                 style(kBrightGreen), style(kReset));
    std::print(stdout, "  {}-i {}{}<N>{}           Dump init architectural and TLB state artifacts at cycle N\n",
                 style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(stdout, "  {}-g{}                Enable verbose debug logging output\n",
                 style(kBrightGreen), style(kReset));
    std::print(stdout, "  {}-p{}                Enable interactive disk/console MMIO transaction logging\n",
                 style(kBrightGreen), style(kReset));
    std::print(stdout, "  {}-P {}{}<FILE>{}        Write comprehensive trap/SBI diagnostic logs to file\n",
                 style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(stdout, "  {}-x{}                Write instruction mix metrics report to trace/instmix.txt on exit\n",
                 style(kBrightGreen), style(kReset));
    std::print(stdout, "  {}-b{}                Generate hardware initialization trace/inits.bin and exit\n\n",
                 style(kBrightGreen), style(kReset));

    // ISA Test Mode
    std::print(stdout, "{}{}:{}{}\n", style(kBoldFgBrightBlue), "ISA Test Mode", style(kReset), style(kReset));
    std::print(stdout, "  {}-T{}                Enable riscv-isa-tests tohost termination monitoring\n",
                 style(kBrightGreen), style(kReset));
    std::print(stdout, "  {}-H {}{}<ADDR>{}        Set custom tohost device MMIO address for -T (default: 0x80001000)\n\n",
                 style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));

    // Miscellaneous
    std::print(stdout, "{}{}:{}{}\n", style(kBoldFgBrightBlue), "Miscellaneous", style(kReset), style(kReset));
    std::print(stdout, "  {}-h, --help{}        Show this colorized dashboard menu and exit\n",
                 style(kBrightGreen), style(kReset));
    std::print(stdout, "  {}--version{}         Show compiler-injected version details and exit\n\n",
                 style(kBrightGreen), style(kReset));

    // Debug and Co-Simulation
    std::print(stdout, "{}{}:{}{}\n", style(kBoldFgBrightBlue), "Debug and Co-Simulation", style(kReset), style(kReset));
    std::print(stdout, "  {}--gdb{}              Enable GDB RSP stub (waits for client before running)\n",
                 style(kBrightGreen), style(kReset));
    std::print(stdout, "  {}--gdb-port {}{}<PORT>{}  Override GDB stub listen port (default: 1234)\n",
                 style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(stdout, "  {}--lockstep{}         Enable Spike lockstep instruction-by-instruction verification\n",
                 style(kBrightGreen), style(kReset));
    std::print(stdout, "  {}--spike-bin {}{}<PATH>{} Path to spike binary for lockstep (default: spike in PATH)\n\n",
                 style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));

    // scaled suffixes
    std::print(stdout, "{}Numeric values accept standard scaled suffixes:{} k/K (1e3), m/M (1e6), g/G (1e9)\n\n",
                 style(kBold), style(kReset));

    // Examples
    std::print(stdout, "{}{}:{}{}\n", style(kBoldFgBrightBlue), "Examples", style(kReset), style(kReset));
    std::print(stdout, "  {}{}{} -m img/hello.bin -a\n",
                 style(kBrightBlack), program_name, style(kReset));
    std::print(stdout, "  {}{}{} -m linux-images/rv{}/fw_payload.bin -d linux-images/rv{}/root.bin -c linux-images/rv{}/devicetree.dtb\n",
                 style(kBrightBlack), program_name, style(kReset), xlen_suffix, xlen_suffix, xlen_suffix);
    std::print(stdout, "  {}{}{} -m linux-images/rv{}/fw_payload.bin --tui\n",
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
    if (argc == 1) { usage(argv[0], 1);
}

    std::span<char* const> const args(argv, static_cast<std::size_t>(argc));
    auto parsed = parse_command_line(args);
    if (!parsed) {
        option_error(parsed.error());
    }

    switch (parsed->action) {
        case CliAction::ShowHelp:
            usage(args[0], 0);
        case CliAction::ShowVersion:
            std::println("{}", simrv::buildinfo::kVersion);
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
        if (arg == "--tui") {
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

    if (!sim_machine.s_tuimode) {
        sim_machine.tracer.print_summary();
    }
    return 0;
}
