#pragma once

#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Tracer.hpp"
#include "simrv/debug/GdbStub.hpp"
#include "simrv/debug/SpikeLockstep.hpp"
#include "simrv/device/Console.hpp"
#include "simrv/device/Disk.hpp"
#include "simrv/device/Rtc.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/device/Virtio.hpp"
#include "simrv/memory/MemorySubsystem.hpp"

namespace simrv::device {
class PowerMmio;
}

namespace simrv::core {
// NOLINTBEGIN(misc-non-private-member-variables-in-classes)
/**
 * @class Machine
 * @brief Owns and orchestrates CPU, memory subsystem, and MMIO devices.
 *
 * Machine is the simulator root object and drives the pipeline-cycle loop,
 * image loading, device wiring, tracing, and run termination checks.
 */
class Machine {
   public:
    /// Construct the simulator root object.
    Machine();
    /// Destroy simulator resources.
    ~Machine();
    Machine(const Machine&) = delete;
    auto operator=(const Machine&) -> Machine& = delete;
    Machine(Machine&&) = delete;
    auto operator=(Machine&&) -> Machine& = delete;
    /**
     * @brief Initialize machine state and load runtime images/configuration.
     * @param argc CLI argument count.
     * @param argv CLI argument vector.
     * @return 0 on success, non-zero on configuration error.
     */
    auto initialize(int argc, char* const* argv) -> int;
    /// Execute the main simulation loop until termination criteria are met.
    void run();
    /// Stop the simulation loop.
    void stop() { is_running_ = false; }

    /// Generate binary image file for FPGA
    void generate_binfile() const;

    // ========== Execution State & Metrics ==========
    Counter tohost = 0;  // Host communication register (always 64-bit for HTIF).

    // ========== Simulation Configuration Flags ==========
    bool s_appmode = false;        // Binary mode (start_pc=0, no OS)
    bool s_tuimode = false;        // Enable TUI monitor mode
    bool s_debugmode = false;      // Enable debug logging in MMIO paths
    bool s_dlog_mode = false;      // Enable device request/response logging
    bool s_traplog_mode = false;   // Enable trap/SBI/exception logging
    bool s_use_disk = false;       // Enable disk image simulation
    bool s_use_mix = false;        // Enable instruction-mix statistics collection
    bool s_bp_trace = false;       // Enable branch prediction tracing
    bool s_isatest = false;        // Enable riscv-isa-tests tohost handling
    bool s_misa_override = false;  // True when CLI explicitly selected MISA profile
    bool s_gen_binfile = false;    // Generate binary image file for FPGA

    // ========== Debug / Co-Simulation Flags ==========
    bool     s_gdb_mode      = false;      // Enable GDB RSP stub
    uint16_t s_gdb_port      = 1234;       // GDB stub TCP port
    bool     s_lockstep_mode = false;      // Enable Spike lockstep co-simulation
    std::string s_spike_bin  = "spike";    // Path to Spike binary

    // ========== Simulation Control Parameters ==========
    Address s_start_pc = 0;                                  // Initial PC value
    Counter s_strace = 0;                                    // Starting cycle for trace generation
    Counter s_fincnt = std::numeric_limits<Counter>::max();  // Finish cycle count
    Counter s_trace_begin = std::numeric_limits<Counter>::max();  // Trace begin cycle
    Counter s_trace_end = std::numeric_limits<Counter>::max();    // Trace end cycle
    Counter s_enabletimer = std::numeric_limits<Counter>::max();  // Timer enable cycle
    Counter s_memimg = 0;                                         // Memory image dump cycle

    // ========== ISA/Privilege Configuration ==========
    Address s_isatest_tohost = 0x80001000;   // ISA-test tohost RAM address
    CSRValue s_misa_profile = kMisaDefault;  // Selected MISA profile (without MXL)

    // ========== I/O and Logging ==========
    std::string s_fn_memimg;                             // Memory image filename
    std::string s_fn_dskimg;                             // Disk image filename
    std::string s_fn_dvtree;                             // Device-tree binary filename
    std::string s_fn_traplog;                            // Trap/exception log filename
    std::chrono::steady_clock::time_point s_start_time;  // Simulation start timestamp

    // ========== CPU and Subsystems ==========
    simrv::core::CPU cpu;
    std::unique_ptr<simrv::device::Disk> disk;
    std::unique_ptr<simrv::device::Console> console;
    std::unique_ptr<simrv::Rtc> rtc;
    std::unique_ptr<simrv::device::Uart> uart;
    std::unique_ptr<simrv::device::PowerMmio> power;

    // ========== Debug Subsystems (null when disabled) ==========
    std::unique_ptr<simrv::debug::GdbStub>       gdb_stub;
    std::unique_ptr<simrv::debug::SpikeLockstep> spike_lockstep;

    // ========== Memory and Interconnect ==========
    Byte* mmem{};          // Pointer to main memory buffer
    Tracer tracer{*this};  // Tracing facility

   private:
    std::unique_ptr<Byte, decltype(&std::free)> mmem_owner_{nullptr, &std::free};
    std::vector<simrv::virtio::QueueState> console_queue_owner_;
    std::vector<simrv::virtio::QueueState> disk_queue_owner_;
    friend class simrv::core::CPU;
    friend class simrv::device::Uart;
    simrv::memory::MemorySubsystem memory_;
    /// Perform per-cycle initialization before CPU stage execution.
    void prepare_cycle();
    /// Perform per-cycle finalization and completion checks.
    void finalize_cycle();
    bool is_running_ = true;  // Main-loop run flag.
};
// NOLINTEND(misc-non-private-member-variables-in-classes)
}  // namespace simrv::core
