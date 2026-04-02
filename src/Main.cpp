/**
 * @file Main.cpp
 * @brief SimRV entry point and command-line option handling.
 *
 * SimCore/RISC-V functional simulator (ArchLab, Science Tokyo (former TokyoTech)).
 */
#include <signal.h>
#include <sys/time.h>

#include <cctype>
#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <span>
#include <string>
#include <string_view>

#include "BuildInfo.hpp"
#include "Machine.hpp"

Machine sim_machine; /* simulator machine instance */
Microcn micro_controller; /* I/O controller (micro-controller) */

void usage() {
    static char UsageMessage[] =
        "\
  -h         : display this help and exit \n\
  -m <FILE>  : specify the memory image file by FILE \n\
  -d <FILE>  : specify the disk   image file by FILE (and use linux_mode) \n\
  -u <FILE>  : specify the uC program image  by FILE (optional) \n\
    -c <FILE>  : specify the device-tree image file by FILE (optional) \n\
  -e <N>     : end the simulation after N cycles run \n\
  -i <N>     : generate some init_ files after N cycles for Verilog simulation\n\
  -t <N> <M> : generate the trace file trace.txt from N to M insns \n\
  -q <N>     : generate the trace file of PCs named tracepc.txt after N insns \n\
  -g         : output the debug info \n\
  -s         : use I/O controller (micro-controller) \n\
  -b         : generate binary image file inits.bin for FPGA run \n\
  -p         : generate disk/console log file \n\
  -a         : app mode, start_pc=0, initial sp=0, simple trace output \n\
  -w         : generate trace file bpred.txt for branch prediction \n\
    -T         : enable riscv-isa-tests mode (watch RAM tohost and stop on pass/fail) \n\
    -H <ADDR>  : set tohost RAM address for -T mode (default: 0x80001000) \n\
  -l <N>     : enable timer after N cycles Linux boots \n\
  -r         : RTOS mode, start_pc=0, initial sp=0, middle trace output, \"-l 0\" \n\
  -x         : output instruction mix to instmix.txt \n\
  to specify the number, 'k' and 'K' for Kilo, 'm' and 'M' for Mega, \n\
  and 'g' and 'G' for Giga are available \n\
  \n\
  the typical command to run linux is following \n\
    $ SimRV -m img/bbl.bin -d img/root.bin \n\
  the command to run linux until 40 million instructions is following \n\
    $ SimRV -m img/bbl.bin -d img/root.bin -e 40m \n\
  the command to run an application in app_mode is \n\
    $ SimRV -m img/hello.bin \n\
";
    printf(" Usage: %s [-option]\n", simrv::buildinfo::kProjectName);
    printf("%s", UsageMessage);
    exit(0);
}

uint64_t to_integer(std::string_view num) {
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

    uint64_t value = 0;
    const char* begin = num.data();
    const char* end = num.data() + num.size();
    if (begin != end) {
        const auto result = std::from_chars(begin, end, value);
        if (result.ec != std::errc{}) {
            return 0;
        }
    }
    return value * multiplier;
}

uint32_t to_integer32_base0(std::string_view num) {
    uint64_t value = 0;
    int base = 10;
    if (num.size() > 2 && num[0] == '0' && (num[1] == 'x' || num[1] == 'X')) {
        num.remove_prefix(2);
        base = 16;
    } else if (num.size() > 1 && num[0] == '0') {
        num.remove_prefix(1);
        base = 8;
    }

    const char* begin = num.data();
    const char* end = num.data() + num.size();
    if (begin != end) {
        const auto result = std::from_chars(begin, end, value, base);
        if (result.ec != std::errc{}) {
            return 0;
        }
    }
    return static_cast<uint32_t>(value);
}

std::string_view require_arg(std::span<char* const> args, std::size_t& index, const char* option) {
    if (index + 1 >= args.size()) {
        printf("__ Error: missing value for %s\n", option);
        exit(0);
    }
    ++index;
    return args[index];
}

void set_options(Machine* m, int argc, char* argv[]) {
    if (argc == 1) usage();

    //    static char buf1[256] = "img/simrv.dtb";
    static char buf2[256] = "img/iocon.bin";
    m->s_fn_dvtree = NULL;
    m->s_fn_iocon = buf2; /* set an initial file name */
    m->s_start_pc = simrv::boot::kStartPc;
    m->s_enabletimer = 70000000ul;

    std::span<char* const> args(argv, static_cast<std::size_t>(argc));
    for (std::size_t i = 1; i < args.size(); ++i) {
        std::string_view arg = args[i];
        if (arg == "-h")
            usage();
        else if (arg == "-m") {
            m->s_fn_memimg = const_cast<char*>(require_arg(args, i, "-m").data());
        } else if (arg == "-d") {
            m->s_fn_dskimg = const_cast<char*>(require_arg(args, i, "-d").data());
            m->s_use_disk = 1;
        } else if (arg == "-c") {
            m->s_fn_dvtree = const_cast<char*>(require_arg(args, i, "-c").data());
        } else if (arg == "-u") {
            m->s_fn_iocon = const_cast<char*>(require_arg(args, i, "-u").data());
        } else if (arg == "-e") {
            m->s_fincnt = to_integer(require_arg(args, i, "-e"));
        } else if (arg == "-i") {
            m->s_memimg = to_integer(require_arg(args, i, "-i"));
        } else if (arg == "-q") {
            m->s_strace = to_integer(require_arg(args, i, "-q"));
        } else if (arg == "-w") {
            m->s_bp_trace = 1;
        } else if (arg == "-T") {
            m->s_isatest = 1;
        } else if (arg == "-H") {
            m->s_isatest_tohost = to_integer32_base0(require_arg(args, i, "-H"));
        } else if (arg == "-b") {
            m->s_gen_binfile = 1;
        } else if (arg == "-g") {
            m->s_debugmode = 1;
        } else if (arg == "-s") {
            m->s_use_uc = 1;
        } else if (arg == "-p") {
            m->s_dlog_mode = 1;
        } else if (arg == "-a") {
            m->s_start_pc = 0;
            m->s_appmode = 1;
        } else if (arg == "-l") {
            m->s_enabletimer = to_integer(require_arg(args, i, "-l"));
        } else if (arg == "-r") {
            m->s_start_pc = 0;
            m->s_enabletimer = 0;
            m->s_rtosmode = 1;
        } else if (arg == "-x") {
            m->s_use_mix = 1;
        } else if (arg == "-t") {
            m->s_fp_trace = fopen("trace.txt", "w");
            if (m->s_fp_trace == NULL) {
                printf("__ Error: cannot open trace\n");
                exit(0);
            }
            m->s_trace_begin = to_integer(require_arg(args, i, "-t"));
            m->s_trace_end = to_integer(require_arg(args, i, "-t"));
        } else {
            printf("__ Error: unknown option : %s\n", args[i]);
            exit(0);
        }
    }
}

int main(int argc, char* argv[]) {
    printf("__ %s v%s (%s@%s)\n", simrv::buildinfo::kProjectDescription, simrv::buildinfo::kVersion,
           simrv::buildinfo::kGitBranch, simrv::buildinfo::kGitSha);
    printf("__ Please type Control+'q' to quit the simulation\n\n");

    signal(SIGINT, SIG_IGN);  // ignore control+'C'

    sim_machine.initialize(argc, argv);

    if (sim_machine.s_use_uc) {
        // Initialize micro-controller path when requested.
        micro_controller.init(sim_machine.s_fn_iocon);
        micro_controller.mmem = sim_machine.mmem;
        micro_controller.cons_queue = sim_machine.console->Queue;
        micro_controller.disk_queue = sim_machine.disk->Queue;
        micro_controller.disk = sim_machine.disk->sector;
        micro_controller.cons_fifo = sim_machine.console->cons_fifo;
        micro_controller.fifo_en = sim_machine.console->fifo_en;
    }

    gettimeofday(&sim_machine.s_stime, NULL);

    // Initialize terminal in raw mode for simulator I/O.
    struct termios tty, pre_tty;
    memset(&tty, 0, sizeof(tty));
    tcgetattr(0, &tty);
    memcpy(&pre_tty, &tty, sizeof(tty));
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tty.c_oflag |= OPOST;
    tty.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN);
    tty.c_cflag &= ~(CSIZE | PARENB);
    tty.c_cflag |= CS8;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;
    tcsetattr(0, TCSANOW, &tty);
    sim_machine.run();

    sim_machine.print_summary();
    tcsetattr(0, TCSANOW, &pre_tty);
    return 0;
}
