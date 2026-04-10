/**
 * @file Machine.hpp
 * @brief SimRV declarations.
 */
#pragma once

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <memory>
#include <string>

#include "Console.hpp"
#include "Cpu.hpp"
#include "Define.hpp"
#include "Disk.hpp"
#include "IoController.hpp"
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

    bool s_appmode = false;                 // flag to identify whether the application mode or not
    bool s_rtosmode = false;                // flag to identify whether the rtos mode or not
    bool s_debugmode = false;               // Enable debug logging in memory-mapped paths.
    bool s_dlog_mode = false;               // Enable disk/console request logging.
    bool s_use_uc = false;                  // flag to use IO controller (micro-controller)
    bool s_use_disk = false;                // flag to use disk image
    bool s_use_mix = false;                 // flag to measure instruction mix
    bool s_bp_trace = false;                //
    bool s_isatest = false;                 // flag to enable riscv-isa-tests tohost handling
    bool s_misa_override = false;           // true when CLI explicitly selected a MISA profile
    Address s_start_pc = 0;                 // start PC
    Counter s_strace = 0;                   // Start cycle for tracepc generation.
    Address s_isatest_tohost = 0x80001000;  // RAM tohost address for riscv-isa-tests
    CSRValue s_misa_profile = kMisaDefault;  // Selected MISA extension bits (without MXL).
    bool s_gen_binfile = false;             // flag: generate binary image file for FPGA run
    Counter s_memimg = 0;                   // Cycle to emit init dump artifacts.
    Counter s_fincnt = ~0ull;               // instruction count to finish the simulation
    Counter s_trace_begin = ~0ull;          // First cycle included in full trace output.
    Counter s_trace_end = ~0ull;            // Last cycle included in full trace output.
    Counter s_enabletimer = ~0ull;          // enable timer after N cycles Linux boots
    std::ofstream s_fp_trace;               // trace output stream
    std::ofstream s_fp_dlog;                // File handle for disk/console activity log.
    std::string s_fn_memimg;                // file name of memory image
    std::string s_fn_dskimg;                // file name of disk image
    std::string s_fn_dvtree;                // file name of device-tree binary
    std::string s_fn_iocon;                 // file name of I/O controller program binary
    std::chrono::steady_clock::time_point s_start_time;  // simulation start timestamp

    CPU cpu;
    std::unique_ptr<IoController> micro_controller;
    std::unique_ptr<Disk> disk;
    std::unique_ptr<Console> console;

    Byte* mmem;  // main memory
    MmioRouter mmio_router_;

   private:
    std::unique_ptr<Byte, decltype(&std::free)> mmem_owner_{nullptr, &std::free};
    std::unique_ptr<QueueState[]> console_queue_owner_;
    std::unique_ptr<QueueState[]> disk_queue_owner_;
    friend class CPU;
    MemorySubsystem memory_;
    /// Perform per-cycle initialization before CPU stage execution.
    void prepare_cycle();
    /// Perform per-cycle finalization and completion checks.
    void finalize_cycle();
    bool is_running_ = true;  // Main-loop run flag.
};
