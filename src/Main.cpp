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
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include "simrv/DebugLog.hpp"
#include <format>
#include <initializer_list>
#include <limits>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

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
    std::string fn_asm;
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
    bool rtosmode = false;
    bool debugmode = false;
    bool dlog_mode = false;
    bool traplog_mode = false;
    bool use_disk = false;
    bool use_mix = false;
    bool bp_trace = false;
    bool isatest = false;
    bool gen_binfile = false;
    bool trace_enabled = false;
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
    std::println(stderr, "__ Error: {}", message);
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
    auto value_text = next_argument(args, index, option_name);
    if (!value_text) {
        return std::unexpected(value_text.error());
    }

    uint64_t parsed_value = 0;
    if (!parse_scaled_u64(*value_text, parsed_value)) {
        return std::unexpected(std::format("invalid numeric value for {}", option_name));
    }
    return parsed_value;
}

auto parse_u32_required(std::span<char* const> args, std::size_t& index,
                        std::string_view option_name) -> std::expected<uint32_t, std::string> {
    auto value_text = next_argument(args, index, option_name);
    if (!value_text) {
        return std::unexpected(value_text.error());
    }

    uint32_t parsed_value = 0;
    if (!parse_u32_base0(*value_text, parsed_value)) {
        return std::unexpected(std::format("invalid address value for {}", option_name));
    }
    return parsed_value;
}

auto parse_trace_window(RuntimeOptions& options, std::span<char* const> args, std::size_t& index)
    -> std::expected<void, std::string> {
    auto begin = parse_scaled_required(args, index, "-t");
    if (!begin) {
        return std::unexpected(begin.error());
    }
    auto end = parse_scaled_required(args, index, "-t");
    if (!end) {
        return std::unexpected(end.error());
    }
    if (*begin > *end) {
        return std::unexpected("-t begin must be <= end");
    }

    options.trace_enabled = true;
    options.trace_begin = *begin;
    options.trace_end = *end;
    return {};
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
            std::println(stderr, "__ Warning: Specified MISA profile '{}' has XLEN={} which differs from simulator architecture (RV{}). Operating under RV{} mode.",
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
    if (options.rtosmode) {
        return MisaProfile::I;
    }
    return MisaProfile::GC;
}

auto shell_quote(std::string_view arg) -> std::string {
    std::string quoted = "'";
    for (char const c : arg) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
}

auto shell_command_success(const std::string& command) -> bool {
    return std::system(command.c_str()) == 0;
}

auto command_exists(std::string_view command) -> bool {
    return shell_command_success(std::format("command -v {} >/dev/null 2>&1", shell_quote(command)));
}

auto resolve_tool(const char* env_name, std::initializer_list<std::string_view> candidates,
                  std::string_view display_name) -> std::expected<std::string, std::string> {
    if (const char* env_value = std::getenv(env_name); env_value != nullptr && *env_value != '\0') {
        return std::string{env_value};
    }
    for (const auto candidate : candidates) {
        if (command_exists(candidate)) {
            return std::string(candidate);
        }
    }
    return std::unexpected(std::format("cannot find {}; set environment variable {}", display_name, env_name));
}

// ToolchainInfo aggregates architecture and ABI strings.
struct ToolchainInfo {
    std::string arch; // e.g., "rv64gc"
    std::string abi;  // e.g., "lp64d"
};

auto misa_toolchain_flags(MisaProfile profile) -> ToolchainInfo {
    const auto xlen_suffix = simrv::xlen::kIsXLen64 ? "64" : "32";
    const auto abi_prefix = simrv::xlen::kIsXLen64 ? "lp64" : "ilp32";

    switch (profile) {
        case MisaProfile::I:
            return {std::format("rv{}i", xlen_suffix), abi_prefix};
        case MisaProfile::IMAC:
            return {std::format("rv{}imac", xlen_suffix), abi_prefix};
        case MisaProfile::GC:
        default:
            return {std::format("rv{}gc", xlen_suffix), std::format("{}d", abi_prefix)};
    }
}

auto assemble_to_binary(const RuntimeOptions& options) -> std::expected<std::string, std::string> {
    namespace fs = std::filesystem;

    if (options.fn_asm.empty()) {
        return std::unexpected("-A/--asm requires an assembly source path");
    }

    std::error_code ec;
    const fs::path source_path = fs::path(options.fn_asm);
    if (!fs::exists(source_path, ec)) {
        return std::unexpected("assembly source not found: " + options.fn_asm);
    }

    const MisaProfile profile = effective_misa_profile(options);
    const auto [march, mabi] = misa_toolchain_flags(profile);

    auto cc_res = resolve_tool(
        "RISCV_CC", {"riscv64-unknown-elf-gcc", "riscv32-unknown-elf-gcc"}, "RISC-V C compiler");
    if (!cc_res) {
        option_error(cc_res.error());
    }
    const std::string cc = *cc_res;
    auto objcopy_res = resolve_tool(
        "RISCV_OBJCOPY", {"riscv64-unknown-elf-objcopy", "riscv32-unknown-elf-objcopy", "objcopy"}, "RISC-V objcopy");
    if (!objcopy_res) {
        option_error(objcopy_res.error());
    }
    const std::string objcopy = *objcopy_res;

    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path base = fs::temp_directory_path(ec) /
                          std::format("simrv-asm-{}-{}", static_cast<long long>(::getpid()), nonce);
    if (ec) {
        return std::unexpected("failed to resolve temporary directory");
    }

    const fs::path elf_path = base.string() + ".elf";
    const fs::path bin_path = base.string() + ".bin";

    const std::string compile_cmd = std::format(
        "{} -x assembler-with-cpp -march={} -mabi={} -nostdlib -nostartfiles -Wl,-N "
        "-Wl,--build-id=none -Ttext=0 -o {} {}",
        shell_quote(cc), march, mabi, shell_quote(elf_path.string()),
        shell_quote(source_path.string()));
    if (!shell_command_success(compile_cmd)) {
        return std::unexpected("failed to assemble source: " + options.fn_asm);
    }

    const std::string objcopy_cmd =
        std::format("{} -O binary {} {}", shell_quote(objcopy), shell_quote(elf_path.string()),
                    shell_quote(bin_path.string()));
    if (!shell_command_success(objcopy_cmd)) {
        return std::unexpected("failed to objcopy assembled ELF to binary image");
    }

    return bin_path.string();
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
        if (arg == "-A" || arg == "--asm") {
            auto value = next_argument(args, i, "-A/--asm");
            if (!value) {
                return std::unexpected(value.error());
            }
            result.options.fn_asm = std::string(*value);
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
        if (arg == "-r") {
            result.options.start_pc = 0;
            result.options.enabletimer = 0;
            result.options.rtosmode = true;
            continue;
        }

        return std::unexpected(std::format("unknown option : {}", arg));
    }

    if (!result.options.fn_memimg.empty() && !result.options.fn_asm.empty()) {
        return std::unexpected("choose either -m <FILE> or -A/--asm <FILE>, not both");
    }
    if (result.options.fn_memimg.empty() && result.options.fn_asm.empty()) {
        return std::unexpected("either -m <FILE> or -A/--asm <FILE> is required");
    }

    return result;
}

auto apply_runtime_options(simrv::core::Machine* machine, const RuntimeOptions& options)
    -> std::expected<void, std::string> {
    std::string memimg_path = options.fn_memimg;
    if (!options.fn_asm.empty()) {
        auto assembled = assemble_to_binary(options);
        if (!assembled) {
            return std::unexpected(assembled.error());
        }
        memimg_path = *assembled;
    }

    machine->s_fn_memimg = memimg_path;
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
    machine->s_rtosmode = options.rtosmode;
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

    return {};
}

void set_start_time(simrv::core::Machine& machine) {
    machine.s_start_time = std::chrono::steady_clock::now();
}

}  // namespace

[[noreturn]] static void usage(const char* program_name, int exit_code = 0) {
    const auto xlen_suffix = simrv::xlen::kIsXLen64 ? "64" : "32";
    std::print(
        "Usage: {} [options]\n\n"
        "Required:\n"
        "  -m <FILE>        Memory image file\n"
        "  -A, --asm <FILE> Assemble source file and run generated image\n\n"
        "Images and Devices:\n"
        "  -d <FILE>        Disk image file (enables disk mode)\n"
        "  -c <FILE>        Device-tree binary file\n\n"
        "Execution Control:\n"
        "  -e <N>           Stop after N instructions\n"
        "  -l <N>           Enable timer after N cycles\n"
        "  -a               App mode (start_pc=0)\n"
        "  -r               RTOS mode (start_pc=0, timer enabled at cycle 0)\n"
        "  --misa <PROFILE> Select MISA profile: rv{}i | rv{}imac | rv{}gc\n\n"
        "Tracing and Debug:\n"
        "  -t <BEGIN> <END> Write trace.txt for instruction range [BEGIN, END]\n"
        "  -q <N>           Generate tracepc.txt every 1000 instructions after N\n"
        "  -w               Generate bpred.txt branch trace\n"
        "  -i <N>           Dump init artifacts at cycle N\n"
        "  -g               Enable debug logging\n"
        "  -p               Enable disk/console transaction log\n"
        "  -P <FILE>        Write trap/SBI diagnostics to file\n"
        "  -x               Write instruction mix report\n"
        "  -b               Generate inits.bin and exit\n\n"
        "ISA Test Mode:\n"
        "  -T               Enable riscv-isa-tests tohost monitoring\n"
        "  -H <ADDR>        Set tohost address for -T (default: 0x80001000)\n\n"
        "Misc:\n"
        "  -h, --help       Show this help and exit\n"
        "  --version        Show version and exit\n\n"
        "Numeric suffixes: k/K (1e3), m/M (1e6), g/G (1e9)\n\n"
        "Examples:\n"
        "  {} -m img/bbl.bin -d img/root.bin\n"
        "  {} -m img/bbl.bin -d img/root.bin -e 40m\n"
        "  {} -m img/hello.bin -a\n"
        "  {} -A prog.S --misa rv32imac\n",
        program_name, xlen_suffix, xlen_suffix, xlen_suffix, program_name, program_name, program_name, program_name);
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

auto main(int argc, char* argv[]) -> int {
    std::println("__ {} v{} ({}@{})\n__ Please type Control+'q' to quit the simulation\n",
                 simrv::buildinfo::kProjectDescription, simrv::buildinfo::kVersion,
                 simrv::buildinfo::kGitBranch, simrv::buildinfo::kGitSha);

    // Write startup entry to MMU debug log
    simrv::get_mmu_log() << "[SIM] Simulator start" << '\n';
    simrv::get_mmu_log().flush();

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
        std::println(stderr,
                     "__ Warning: terminal raw mode setup failed; continuing in current mode");
    }
    sim_machine.run();

    sim_machine.tracer.print_summary();
    return 0;
}
