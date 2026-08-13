#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Tracer.hpp"
#include "simrv/debug/BreakpointManager.hpp"
#include "simrv/debug/GdbStub.hpp"
#include "simrv/debug/SpikeLockstep.hpp"
#include "simrv/debug/SymbolTable.hpp"
#include "simrv/device/Audio.hpp"
#include "simrv/device/Console.hpp"
#include "simrv/device/Disk.hpp"
#include "simrv/device/Framebuffer.hpp"
#include "simrv/device/Rtc.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/device/Virtio.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/util/SdlAudio.hpp"
#include "simrv/util/SdlDisplay.hpp"

namespace simrv::device {
class PowerMmio;
class InputDevice;
}  // namespace simrv::device

namespace simrv::tui {
class Tui;
class LeftPane;
}  // namespace simrv::tui

namespace simrv::core {
// NOLINTBEGIN(misc-non-private-member-variables-in-classes)
enum class ExecutionState : uint8_t {
    Stopped = 0,
    Running = 1,
    Paused = 2,
    Stepping = 3,
};

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
    virtual ~Machine();
    Machine(const Machine&) = delete;
    auto operator=(const Machine&) -> Machine& = delete;
    Machine(Machine&&) = delete;
    auto operator=(Machine&&) -> Machine& = delete;
    /**
     * @brief Initialize machine state and load runtime images/configuration.
     * @return 0 on success, non-zero on configuration error.
     */
    auto initialize() -> int;
    /**
     * @brief Load a program binary image dynamically into simulator DRAM and reset CPU state.
     * @param filepath Path to the program binary image.
     * @return true if successfully loaded, false otherwise.
     */
    auto load_program_binary(const std::string& filepath) -> bool;
    /// Load a disk image into the virtio disk device (may be called from TUI modal).
    /// @param filepath Path to the disk image file.
    /// @return true if successfully loaded, false otherwise.
    auto load_disk_image(const std::string& filepath) -> bool;
    /// Execute the main simulation loop until termination criteria are met.
    virtual void run() = 0;
    /// Finalize cycle for tohost checks only.
    void finalize_cycle_tohost();
    /// Stop the simulation loop.
    void stop();
    /// Get current atomic execution state.
    [[nodiscard]] auto execution_state() const -> ExecutionState {
        return execution_state_.load(std::memory_order_relaxed);
    }
    /// Check if machine execution is currently paused.
    [[nodiscard]] auto is_paused() const -> bool;
    /// Check if machine execution is currently stopped.
    [[nodiscard]] auto is_stopped() const -> bool {
        return execution_state_.load(std::memory_order_relaxed) == ExecutionState::Stopped;
    }
    /// Check if machine execution is in single-stepping state.
    [[nodiscard]] auto is_stepping() const -> bool {
        return execution_state_.load(std::memory_order_relaxed) == ExecutionState::Stepping;
    }
    /// Pause machine execution.
    void pause();
    /// Resume machine execution.
    void resume();
    /// Request execution of a single instruction cycle.
    void step();
    /// Check if the simulation loop is running.
    [[nodiscard]] auto is_running() const -> bool {
        return is_running_.load(std::memory_order_relaxed);
    }
    /// Request system reboot.
    void request_reboot();
    /// Reset runtime state flags and CPU state.
    void reset_state();

    std::atomic<uint64_t> tohost{0};  // Host communication register (always 64-bit for HTIF).
    std::atomic<bool> reboot_requested = false;  // Reboot requested flag.
    int exit_code = 0;                           // Exit/status code of the simulation.
    std::atomic<bool> is_shutdown_ = false;      // System shutdown flag.

    // ========== Simulation Configuration Flags ==========
    bool s_appmode = true;             // Baremetal/app mode (default)
    bool s_tuimode = false;            // Enable TUI monitor mode
    bool s_gui_mode = false;           // Enable GUI graphics window mode
    bool s_high_contrast = false;      // Enable high-contrast TUI mode
    bool s_debugmode = false;          // Enable debug logging in MMIO paths
    bool s_debug_mode = false;         // Enable TUI debug diagnostics mode
    bool s_dlog_mode = false;          // Enable device request/response logging
    bool s_traplog_mode = false;       // Enable trap/SBI/exception logging
    bool s_use_disk = false;           // Enable disk image simulation
    bool s_use_mix = false;            // Enable instruction-mix statistics collection
    bool s_bp_trace = false;           // Enable branch prediction tracing
    bool s_misa_override = false;      // True when CLI explicitly selected MISA profile
    bool s_cycle_accurate = false;     // Enable cycle-accurate performance simulation mode
    bool s_high_performance = true;    // Enable high-performance optimized simulation mode
    bool s_mmu_ever_used = false;      // Latched true the first time satp enables translation
    bool s_multithreaded = false;      // Run simulation in a background thread
    bool s_rollback_enabled = false;   // Enable instruction rollback tracking
    double s_mouse_sensitivity = 1.0;  // Mouse relative sensitivity factor

    // ========== Debug / Co-Simulation Flags ==========
    bool s_gdb_mode = false;            // Enable GDB RSP stub
    uint16_t s_gdb_port = 1234;         // GDB stub TCP port
    bool s_lockstep_mode = false;       // Enable Spike lockstep co-simulation
    std::string s_spike_bin = "spike";  // Path to Spike binary
    std::string s_spike_elf;            // Path to Spike ELF image

    // ========== Simulation Control Parameters ==========
    Address s_start_pc = 0;                                  // Initial PC value
    Counter s_strace = 0;                                    // Starting cycle for trace generation
    Counter s_fincnt = std::numeric_limits<Counter>::max();  // Finish cycle count
    Counter s_trace_begin = std::numeric_limits<Counter>::max();  // Trace begin cycle
    Counter s_trace_end = std::numeric_limits<Counter>::max();    // Trace end cycle
    Counter s_enabletimer = std::numeric_limits<Counter>::max();  // Timer enable cycle
    Counter s_memimg = std::numeric_limits<Counter>::max();      // Memory image dump cycle

    // ========== ISA/Privilege Configuration ==========
    Address s_isatest_tohost = 0x80001000;        // ISA-test tohost RAM address
    CSRValue s_misa_profile = isa::kMisaDefault;  // Selected MISA profile (without MXL)
    unsigned int s_misa_xlen = 0;                 // Selected MISA XLEN (32 or 64, or 0 if default)
    unsigned int s_vlen = 0;                      // Selected VLEN (bits, or 0 if default)

    // ========== I/O and Logging ==========
    std::string s_fn_memimg;                             // Memory image filename
    std::string s_fn_dskimg;                             // Disk image filename
    std::string s_fn_dvtree;                             // Device-tree binary filename
    std::string s_fn_traplog;                            // Trap/exception log filename
    std::string s_fn_cpuconfig;                          // CPU config filename
    std::chrono::steady_clock::time_point s_start_time;  // Simulation start timestamp

    // ========== Mode-Switch Reboot State ==========
    // When the TUI loads a binary with a different App/OS mode, these fields
    // are populated and reboot_requested is set so Main.cpp recreates the machine.
    std::string pending_binary_path;      // Binary path to apply on next boot
    std::optional<bool> pending_appmode;  // If set, override appmode on next boot
    std::optional<std::string>
        pending_disk_path;  // Optional disk image path for next boot (empty => disable disk)

    // ========== CPU and Subsystems ==========
    simrv::core::CPU cpu;
    std::unique_ptr<simrv::device::Disk> disk;
    std::unique_ptr<simrv::device::Console> console;
    std::unique_ptr<simrv::Rtc> rtc;
    std::unique_ptr<simrv::device::Uart> uart;
    std::unique_ptr<simrv::tui::Tui> tui;
    std::unique_ptr<simrv::device::PowerMmio> power;
    std::unique_ptr<simrv::device::Framebuffer> framebuffer;
    std::unique_ptr<simrv::device::InputDevice> input_device;
    std::unique_ptr<simrv::device::Audio> audio;
    std::unique_ptr<simrv::util::SdlDisplay> sdl_display;
    std::unique_ptr<simrv::util::SdlAudio> sdl_audio;

    // ========== Debug Subsystems (null when disabled) ==========
    std::unique_ptr<simrv::debug::GdbStub> gdb_stub;
    std::unique_ptr<simrv::debug::SpikeLockstep> spike_lockstep;
    simrv::debug::BreakpointManager breakpoints;

    // ========== Memory and Interconnect ==========
    Byte* mmem{};                       // Pointer to main memory buffer
    Tracer tracer{*this};               // Tracing facility
    simrv::debug::SymbolTable symbols;  // ELF debugging symbols

   protected:
    std::unique_ptr<Byte, decltype(&std::free)> mmem_owner_{nullptr, &std::free};
    std::vector<simrv::virtio::QueueState> console_queue_owner_;
    std::vector<simrv::virtio::QueueState> disk_queue_owner_;
    friend class simrv::core::CPU;
    friend class simrv::execute::ExecuteUnit;
    friend class simrv::device::Uart;
    friend class simrv::tui::Tui;
    friend class simrv::tui::LeftPane;
    simrv::memory::MemorySubsystem memory_;
    /// Perform per-cycle initialization before CPU stage execution.
    virtual void prepare_cycle() {}
    /// Perform per-cycle finalization and completion checks.
    virtual void finalize_cycle() {}
    std::atomic<bool> is_running_ = true;  // Main-loop run flag.
    std::atomic<ExecutionState> execution_state_{ExecutionState::Running};
};
// NOLINTEND(misc-non-private-member-variables-in-classes)
}  // namespace simrv::core
