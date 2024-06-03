/**
 * @file main.cc
 * @brief SimCore/RISC-V Functional Simulator main file
 * @details This file contains the main function and other supporting functions for the SimCore/RISC-V Functional Simulator.
 *          It provides command line options for running the simulator with different configurations.
 *          The simulator simulates the execution of RISC-V instructions and provides various debugging and tracing options.
 *          It also supports the use of an I/O controller (micro-controller) for interacting with external devices.
 *          The main function initializes the simulator, sets the command line options, and starts the simulation.
 *          It also handles console input and output, and displays the simulation result at the end.
 */

#include "machine.h"

#define PROJ "simrv"
#define NAME "SimCore/RISC-V Functional Simulator"
#define VER  "Version 1.3.7 test01 2020-07-11"

Machine mm;  /* class machine                     */
Microcn cc;  /* I/O controller (micro-controller) */

/**
 * @brief Displays the usage message for the simulator command line options.
 */
void usage(){
    static char UsageMessage[] =  "\
  -h         : display this help and exit \n\
  -m <FILE>  : specify the memory image file by FILE \n\
  -d <FILE>  : specify the disk   image file by FILE (and use linux_mode) \n\
  -u <FILE>  : specify the uC program image  by FILE (optional) \n\
  -c <FILE>  : specify the devide_tree image file by FILE (optional ) \n\
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
  -l <N>     : enable timer after N cycles Linux boots \n\
  -r         : RTOS mode, start_pc=0, initial sp=0, middle trace output, \"-l 0\" \n\
  -x         : output instruction mix to instmix.txt \n\
  to specify the number, 'k' and 'K' for Kilo, 'm' and 'M' for Mega, \n\
  and 'g' and 'G' for Giga are available \n\
  \n\
  the typical command to run linux is following \n\
  $ simrv -m img/bbl.bin -d img/root.bin \n\
  the command to run linux until 40 million instructions is following \n\
  $ simrv -m img/bbl.bin -d img/root.bin -e 40m \n\
  the command to run an application in app_mode is \n\
  $ simrv -m img/hello.bin \n\
";
    printf(" Usage: %s [-option]\n", PROJ);
    printf("%s", UsageMessage);
    exit(0);
}

/**
 * @brief Converts a string representation of a number to an unsigned 64-bit integer.
 * @param num The string representation of the number.
 * @return The unsigned 64-bit integer value of the number.
 */
uint64_t to_integer(char *num){
    uint64_t ret = 0;
    for (char *p = num; *p != '\0'; p++) {
        if ('0' <= *p && *p <= '9') {
            ret *= 10ull;
            ret += (uint64_t)(*p - '0');
        }
        else if (*p == 'k' || *p == 'K') { ret *=       1000ull; } // Kilo
        else if (*p == 'm' || *p == 'M') { ret *=    1000000ull; } // Mega
        else if (*p == 'g' || *p == 'G') { ret *= 1000000000ull; } // Giga
    }
    return ret;
}

/**
 * @brief Sets the command line options for the simulator.
 * @param m Pointer to the Machine object representing the simulator.
 * @param argc The number of command line arguments.
 * @param argv The array of command line arguments.
 */
void set_options(Machine *m, int argc, char *argv[]){
    if (argc==1) usage();

//    static char buf1[256] = "img/simrv.dtb";
    static char buf2[256] = "img/iocon.bin";
    m->s_fn_dvtree = NULL;
    m->s_fn_iocon  = buf2; /* set an initial file name */
    m->s_start_pc  = D_START_PC;
    m->s_enabletimer = 70000000ul;

    for (int i=1; i<argc; i++) {
        std::string str(argv[i]);
        if      (str == "-h") usage();
        else if (str == "-m") { m->s_fn_memimg   = argv[++i];                                }
        else if (str == "-d") { m->s_fn_dskimg   = argv[++i]; m->s_use_disk=1;               }
        else if (str == "-c") { m->s_fn_dvtree   = argv[++i];                                }
        else if (str == "-u") { m->s_fn_iocon    = argv[++i];                                }
        else if (str == "-e") { m->s_fincnt      = to_integer(argv[++i]);                    }
        else if (str == "-i") { m->s_memimg      = to_integer(argv[++i]);                    }
        else if (str == "-q") { m->s_strace      = to_integer(argv[++i]);                    }
        else if (str == "-w") { m->s_bp_trace    = 1;                                        }
        else if (str == "-b") { m->s_gen_binfile = 1;                                        }
        else if (str == "-g") { m->s_debugmode   = 1;                                        }
        else if (str == "-s") { m->s_use_uc      = 1;                                        }
        else if (str == "-p") { m->s_dlog_mode   = 1;                                        }
        else if (str == "-a") { m->s_start_pc    = 0; m->s_appmode=1;                        }
        else if (str == "-l") { m->s_enabletimer = to_integer(argv[++i]);                    }
        else if (str == "-r") { m->s_start_pc    = 0; m->s_enabletimer = 0; m->s_rtosmode=1; }
        else if (str == "-x") { m->s_use_mix     = 1;                                        }
        else if (str == "-t") {
            m->s_fp_trace = fopen("trace.txt", "w");
            if (m->s_fp_trace==NULL) {printf("__ Error: cannot open trace\n"); exit(0);}
            m->s_trace_begin = to_integer(argv[++i]);
            m->s_trace_end   = to_integer(argv[++i]);
        }
        else {printf("__ Error: unknown option : %s\n", argv[i]); exit(0);}
    }
}

/**
 * @brief The main function of the SimCore/RISC-V Functional Simulator.
 * @param argc The number of command line arguments.
 * @param argv The array of command line arguments.
 * @return The exit status of the program.
 */
int main(int argc, char *argv[]){
    printf("__ %s %s\n", NAME, VER);
    printf("__ Please type Control+'q' to quit the simulation\n\n");

    signal(SIGINT, SIG_IGN); // ignore control+'C'

    mm.init(argc, argv);

    if(mm.s_use_uc){ /* I/O controller (micro-controller) */
        cc.init(mm.s_fn_iocon);
        cc.mmem       = mm.mmem;
        cc.cons_queue = mm.console->Queue;
        cc.disk_queue = mm.disk->Queue;
        cc.disk       = mm.disk->sector;
        cc.cons_fifo  = mm.console->cons_fifo;
        cc.fifo_en    = mm.console->fifo_en;
    }

    gettimeofday(&mm.s_stime, NULL);

    struct termios tty, pre_tty;  /* for console input */
    memset(&tty, 0, sizeof(tty));
    tcgetattr (0, &tty);
    memcpy(&pre_tty, &tty, sizeof(tty));
    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL|IXON);
    tty.c_oflag |= OPOST;
    tty.c_lflag &= ~(ECHO|ECHONL|ICANON|IEXTEN);
    tty.c_cflag &= ~(CSIZE|PARENB);
    tty.c_cflag |= CS8;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;
    tcsetattr (0, TCSANOW, &tty);

    mm.exec();

    mm.display_result();
    tcsetattr (0, TCSANOW, &pre_tty);
    return 0;
}
