/**
 * @file Main.cpp
 * @brief SimRV entry point and command-line option handling.
 *
 * SimCore/RISC-V functional simulator (ArchLab, Science Tokyo (former TokyoTech)).
 */
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <iostream>
#include <span>
#include <string>
#include <string_view>

#include "BuildInfo.hpp"
#include "Machine.hpp"

Machine sim_machine; /* simulator machine instance */

bool parse_scaled_u64(std::string_view num, uint64_t& out);
bool parse_u32_base0(std::string_view num, uint32_t& out);

namespace {

enum class CliAction { Run, ShowHelp, ShowVersion };

struct RuntimeOptions {
    std::string fn_memimg;
    std::string fn_dskimg;
    std::string fn_dvtree;
    std::string fn_iocon = "img/iocon.bin";

    Address start_pc = simrv::boot::kStartPc;
    Counter fincnt = ~0ull;
    Counter memimg = 0;
    Counter strace = 0;
    Counter trace_begin = ~0ull;
    Counter trace_end = ~0ull;
    Counter enabletimer = 70000000ul;
    Address isatest_tohost = 0x80001000;

    bool appmode = false;
    bool rtosmode = false;
    bool debugmode = false;
    bool dlog_mode = false;
    bool use_uc = false;
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

    bool enable_raw_mode() {
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
    std::cerr << "__ Error: " << message << '\n';
    std::exit(code);
}

std::expected<std::string_view, std::string> next_argument(std::span<char* const> args,
                                                           std::size_t& index,
                                                           std::string_view option_name) {
    if (index + 1 >= args.size()) {
        return std::unexpected("missing value for " + std::string(option_name));
    }
    ++index;
    return std::string_view(args[index]);
}

std::expected<uint64_t, std::string> parse_scaled_required(std::span<char* const> args,
                                                           std::size_t& index,
                                                           std::string_view option_name) {
    auto value_text = next_argument(args, index, option_name);
    if (!value_text) {
        return std::unexpected(value_text.error());
    }

    uint64_t parsed_value = 0;
    if (!parse_scaled_u64(*value_text, parsed_value)) {
        return std::unexpected("invalid numeric value for " + std::string(option_name));
    }
    return parsed_value;
}

std::expected<uint32_t, std::string> parse_u32_required(std::span<char* const> args,
                                                        std::size_t& index,
                                                        std::string_view option_name) {
    auto value_text = next_argument(args, index, option_name);
    if (!value_text) {
        return std::unexpected(value_text.error());
    }

    uint32_t parsed_value = 0;
    if (!parse_u32_base0(*value_text, parsed_value)) {
        return std::unexpected("invalid address value for " + std::string(option_name));
    }
    return parsed_value;
}

std::expected<void, std::string> parse_trace_window(RuntimeOptions& options,
                                                    std::span<char* const> args,
                                                    std::size_t& index) {
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

std::expected<ParseResult, std::string> parse_command_line(std::span<char* const> args) {
    ParseResult result{};

    for (std::size_t i = 1; i < args.size(); ++i) {
        std::string_view arg = args[i];
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
        if (arg == "-u") {
            auto value = next_argument(args, i, "-u");
            if (!value) {
                return std::unexpected(value.error());
            }
            result.options.fn_iocon = std::string(*value);
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
        if (arg == "-s") {
            result.options.use_uc = true;
            continue;
        }
        if (arg == "-p") {
            result.options.dlog_mode = true;
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

        return std::unexpected("unknown option : " + std::string(arg));
    }

    if (result.options.fn_memimg.empty()) {
        return std::unexpected("-m <FILE> is required");
    }

    return result;
}

std::expected<void, std::string> apply_runtime_options(Machine* machine,
                                                       const RuntimeOptions& options) {
    machine->s_fn_memimg = options.fn_memimg;
    machine->s_fn_dskimg = options.fn_dskimg;
    machine->s_fn_dvtree = options.fn_dvtree;
    machine->s_fn_iocon = options.fn_iocon;

    machine->s_start_pc = options.start_pc;
    machine->s_fincnt = options.fincnt;
    machine->s_memimg = options.memimg;
    machine->s_strace = options.strace;
    machine->s_trace_begin = options.trace_begin;
    machine->s_trace_end = options.trace_end;
    machine->s_enabletimer = options.enabletimer;
    machine->s_isatest_tohost = options.isatest_tohost;

    machine->s_appmode = options.appmode;
    machine->s_rtosmode = options.rtosmode;
    machine->s_debugmode = options.debugmode;
    machine->s_dlog_mode = options.dlog_mode;
    machine->s_use_uc = options.use_uc;
    machine->s_use_disk = options.use_disk;
    machine->s_use_mix = options.use_mix;
    machine->s_bp_trace = options.bp_trace;
    machine->s_isatest = options.isatest;
    machine->s_gen_binfile = options.gen_binfile;

    machine->s_fp_trace.close();
    if (options.trace_enabled) {
        machine->s_fp_trace.clear();
        machine->s_fp_trace.open("trace.txt");
        if (!machine->s_fp_trace.is_open()) {
            return std::unexpected("cannot open trace");
        }
    }

    return {};
}

void set_start_time(Machine& machine) { machine.s_start_time = std::chrono::steady_clock::now(); }

}  // namespace

[[noreturn]] void usage(const char* program_name, int exit_code = 0) {
    std::cout << "Usage: " << program_name << " [options]\n\n"
              << "Required:\n"
              << "  -m <FILE>        Memory image file\n\n"
              << "Images and Devices:\n"
              << "  -d <FILE>        Disk image file (enables disk mode)\n"
              << "  -c <FILE>        Device-tree binary file\n"
              << "  -u <FILE>        Micro-controller program image\n\n"
              << "Execution Control:\n"
              << "  -e <N>           Stop after N instructions\n"
              << "  -l <N>           Enable timer after N cycles\n"
              << "  -a               App mode (start_pc=0)\n"
              << "  -r               RTOS mode (start_pc=0, timer enabled at cycle 0)\n\n"
              << "Tracing and Debug:\n"
              << "  -t <BEGIN> <END> Write trace.txt for instruction range [BEGIN, END]\n"
              << "  -q <N>           Generate tracepc.txt every 1000 instructions after N\n"
              << "  -w               Generate bpred.txt branch trace\n"
              << "  -i <N>           Dump init artifacts at cycle N\n"
              << "  -g               Enable debug logging\n"
              << "  -p               Enable disk/console transaction log\n"
              << "  -x               Write instruction mix report\n"
              << "  -b               Generate inits.bin and exit\n\n"
              << "ISA Test Mode:\n"
              << "  -T               Enable riscv-isa-tests tohost monitoring\n"
              << "  -H <ADDR>        Set tohost address for -T (default: 0x80001000)\n\n"
              << "Misc:\n"
              << "  -s               Enable micro-controller I/O path\n"
              << "  -h, --help       Show this help and exit\n"
              << "  --version        Show version and exit\n\n"
              << "Numeric suffixes: k/K (1e3), m/M (1e6), g/G (1e9)\n\n"
              << "Examples:\n"
              << "  " << program_name << " -m img/bbl.bin -d img/root.bin\n"
              << "  " << program_name << " -m img/bbl.bin -d img/root.bin -e 40m\n"
              << "  " << program_name << " -m img/hello.bin -a\n";
    std::exit(exit_code);
}

bool parse_scaled_u64(std::string_view num, uint64_t& out) {
    uint64_t multiplier = 1;
    if (!num.empty()) {
        switch (num.back()) {
            case 'k':
            case 'K':
                multiplier = 1000ull;
                num.remove_suffix(1);
                break;
            case 'm':
            case 'M':
                multiplier = 1000000ull;
                num.remove_suffix(1);
                break;
            case 'g':
            case 'G':
                multiplier = 1000000000ull;
                num.remove_suffix(1);
                break;
            default:
                break;
        }
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

bool parse_u32_base0(std::string_view num, uint32_t& out) {
    uint64_t value = 0;
    int base = 10;
    if (num.size() > 2 && num[0] == '0' && (num[1] == 'x' || num[1] == 'X')) {
        num.remove_prefix(2);
        base = 16;
    } else if (num.size() > 1 && num[0] == '0') {
        num.remove_prefix(1);
        base = 8;
    }

    if (num.empty()) {
        return false;
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

void set_options(Machine* m, int argc, char* argv[]) {
    if (argc == 1) usage(argv[0], 1);

    std::span<char* const> args(argv, static_cast<std::size_t>(argc));
    auto parsed = parse_command_line(args);
    if (!parsed) {
        option_error(parsed.error());
    }

    switch (parsed->action) {
        case CliAction::ShowHelp:
            usage(args[0], 0);
        case CliAction::ShowVersion:
            std::cout << simrv::buildinfo::kVersion << '\n';
            std::exit(0);
        case CliAction::Run:
            break;
    }

    auto applied = apply_runtime_options(m, parsed->options);
    if (!applied) {
        option_error(applied.error(), 0);
    }
}

int main(int argc, char* argv[]) {
    std::cout << "__ " << simrv::buildinfo::kProjectDescription << " v"
              << simrv::buildinfo::kVersion << " (" << simrv::buildinfo::kGitBranch << "@"
              << simrv::buildinfo::kGitSha << ")\n"
              << "__ Please type Control+'q' to quit the simulation\n\n";

    std::signal(SIGINT, SIG_IGN);  // ignore control+'C'

    const int init_result = sim_machine.initialize(argc, argv);
    if (init_result != 0) {
        return init_result;
    }

    if (sim_machine.s_use_uc && sim_machine.micro_controller != nullptr) {
        // Initialize micro-controller path when requested.
        sim_machine.micro_controller->init(sim_machine.s_fn_iocon);
        sim_machine.micro_controller->mmem = sim_machine.mmem;
        sim_machine.micro_controller->cons_queue = sim_machine.console->Queue;
        sim_machine.micro_controller->disk_queue = sim_machine.disk->Queue;
        sim_machine.micro_controller->disk = sim_machine.disk->sector;
        sim_machine.micro_controller->cons_fifo = sim_machine.console->cons_fifo;
        sim_machine.micro_controller->fifo_en = sim_machine.console->fifo_en;
    }

    set_start_time(sim_machine);

    // Initialize terminal in raw mode for simulator I/O.
    TerminalModeGuard terminal_mode;
    if (!terminal_mode.enable_raw_mode()) {
        std::cerr << "__ Warning: terminal raw mode setup failed; continuing in current mode\n";
    }
    sim_machine.run();

    sim_machine.print_summary();
    return 0;
}
