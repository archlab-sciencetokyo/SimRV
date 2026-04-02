/**
 * @file Machine.hpp
 * @brief SimRV declarations.
 */
#pragma once

#include <fstream>

#include "Console.hpp"
#include "Cpu.hpp"
#include "Define.hpp"
#include "Disk.hpp"
#include "MemorySubsystem.hpp"
#include "MmioRouter.hpp"

class Machine {
   public:
    Machine();
    ~Machine();
    /// Initialize machine state and load runtime images/configuration.
    int initialize(int argc, char* argv[]);
    /// Execute the main simulation loop until termination criteria are met.
    void run();
    /// Emit one architectural trace snapshot to the configured trace stream.
    void write_trace_snapshot();
    /// Print final simulation statistics.
    void print_summary();
    /// Dump instruction-mix report to disk.
    void write_instruction_mix_report();
    Word tohost = 0;  // Host communication register used by tests and app mode.

    Counter e_icount = 0;             // the number of executed instructions
    Counter e_uc_cnt = 0;             // the number of executed instructions on I/O controller
    Counter e_ccount = 0;             // the number of executed compressed instructions
    int e_instmix[OperationIdCount];  // the array for measuring instruction mix

    int s_appmode = 0;                      // flag to identify whether the application mode or not
    int s_rtosmode = 0;                     // flag to identify whether the rtos mode or not
    int s_debugmode = 0;                    // Enable debug logging in memory-mapped paths.
    int s_dlog_mode = 0;                    // Enable disk/console request logging.
    int s_use_uc = 0;                       // flag to use IO controller (micro-controller)
    int s_use_disk = 0;                     // flag to use disk image
    int s_use_mix = 0;                      // flag to measure instruction mix
    int s_bp_trace = 0;                     //
    int s_isatest = 0;                      // flag to enable riscv-isa-tests tohost handling
    Address s_start_pc = 0;                 // start PC
    Counter s_strace = 0;                   // Start cycle for tracepc generation.
    Address s_isatest_tohost = 0x80001000;  // RAM tohost address for riscv-isa-tests
    Counter s_gen_binfile = 0;              // flag: generate binary image file for FPGA run
    Counter s_memimg = 0;                   // Cycle to emit init dump artifacts.
    Counter s_fincnt = ~0ull;               // instruction count to finish the simulation
    Counter s_trace_begin = ~0ull;          // First cycle included in full trace output.
    Counter s_trace_end = ~0ull;            // Last cycle included in full trace output.
    Counter s_enabletimer = ~0ull;          // enable timer after N cycles Linux boots
    FILE* s_fp_trace;                       // file pointer of trace file
    std::ofstream s_fp_dlog;                // File handle for disk/console activity log.
    char* s_fn_memimg;                      // file name of memory image
    char* s_fn_dskimg;                      // file name of disk   image
    char* s_fn_dvtree;                      // file name of device-tree binary
    char* s_fn_iocon;                       // file name of I/O controller program binary
    struct timeval s_stime;                 // start time stamp

    CPU cpu;
    Disk* disk;
    Console* console;

    Byte* mmem;  // main memory
    MmioRouter mmio_router_;

   private:
    friend class CPU;
    MemorySubsystem memory_;
    /// Perform per-cycle initialization before CPU stage execution.
    void prepare_cycle();
    /// Perform per-cycle finalization and completion checks.
    void finalize_cycle();
    int is_running_ = 1;  // Main-loop run flag.
};

class Microcn {
   public:
    void init(char*);
    int exec();
    Byte* cmem; /* local memory */
    Byte* mmem; /* main memory of processor */

    QueueState* cons_queue; /* Queue of Console */
    QueueState* disk_queue; /* Queue of Disk */

    Byte cons_fifo;
    Byte fifo_en;

    Byte* disk; /* Disk */

    Word Mode;
    Word Qnum;
    Word Qsel;

    Register pc = 0;  // simrv::boot::kStartPc;      // program counter
    Register cpc;
    Register reg[32];  // general purpose registers
    Counter icnt = 0;  // instruction count

    // IF_ stage
    Instruction r_ir;
    // ID_ stage
    Instruction r_opcode;
    Instruction r_rd;
    Instruction r_rs1;
    Instruction r_rs2;
    Instruction r_funct3;
    Instruction r_funct5;
    Instruction r_funct7;
    Instruction r_funct12;
    Instruction r_imm;
    // OF_ stage
    Register r_rrs1;
    Register r_rrs2;
    // EX1 stage
    Word r_tkn;  // Flag for branch/jump taken or not taken.
    Register r_jmp_pc;
    Address r_mem_addr;
    Register r_wb_data;
    CSRValue r_wb_data_csr;
    // MEM stage
    Register r_mem_rdata;
};
