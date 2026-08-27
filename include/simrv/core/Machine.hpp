#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/MachineConfig.hpp"
#include "simrv/core/RuntimeProfile.hpp"
#include "simrv/core/Telemetry.hpp"
#include "simrv/core/Tracer.hpp"
#include "simrv/debug/BreakpointManager.hpp"
#include "simrv/debug/GdbStub.hpp"
#include "simrv/debug/SpikeLockstep.hpp"
#include "simrv/debug/SymbolTable.hpp"
#include "simrv/device/Rtc.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/memory/MemorySubsystem.hpp"

namespace simrv::device {
class PowerMmio;
class AclintMtimer;
class AclintMswi;
class Imsic;
class Aplic;
class PcieRootComplex;
class VirtioPciBlock;
class VirtioPciConsole;
class VirtioPciRng;
class VirtioPciGpu;
class VirtioPciInput;
class VirtioPciSound;
class VirtioPciNet;
class VirtioMmioBlock;
class VirtioMmioConsole;
class VirtioMmioRng;
class VirtioMmioGpu;
class VirtioMmioInput;
class VirtioMmioSound;
class VirtioMmioNet;
}  // namespace simrv::device

namespace simrv::tui {
class Tui;
class LeftPane;
}  // namespace simrv::tui

namespace simrv::core {

class BaremetalRunner;
class OsRunner;

/// Selects the composed execution policy for a machine instance.
enum class MachineMode : uint8_t { Baremetal, OperatingSystem };

struct PlatformStatusSnapshot {
    PlatformProfile profile = PlatformProfile::Pcie;
    bool has_pcie = false;
    bool has_mmio = false;
    bool disk_loaded = false;
    uint32_t disk_status = 0;
    uint32_t disk_isr = 0;
    uint64_t disk_capacity_sectors = 0;
    uint32_t network_status = 0;
    uint64_t network_tx_packets = 0;
    uint32_t console_status = 0;
    uint32_t rng_status = 0;
    uint32_t gpu_status = 0;
};

// NOLINTBEGIN(misc-non-private-member-variables-in-classes)
enum class ExecutionState : uint8_t {
    Stopped = 0,
    Running = 1,
    Paused = 2,
    Stepping = 3,
};

struct PendingRebootState {
    std::string binary_path;
    std::optional<bool> appmode;
    std::optional<std::string> disk_path;
};

/// Stable, inexpensive execution state consumed by the asynchronous TUI renderer.
struct TuiExecutionSnapshot {
    Register pc = 0;
    Counter instruction_count = 0;
    Counter timer_ticks = 0;
    ExecutionState execution_state = ExecutionState::Stopped;
};

/**
 * @class Machine
 * @brief Owns and orchestrates CPU, memory subsystem, and MMIO devices.
 *
 * Machine is the simulator root object and drives the pipeline-cycle loop,
 * image loading, device wiring, tracing, and run termination checks.
 */
class Machine final {
   private:
    class Runtime;
    std::unique_ptr<Runtime> runtime_;

   public:
    enum class StopReason : uint8_t {
        Running,
        InstructionLimit,
        TohostPass,
        TohostFail,
        GuestPoweroff,
        GuestCrash,
        GuestReboot,
        GuestExit,
        LockstepDivergence,
        UnhandledTrap,
        ExternalStop,
    };

    /// Construct the simulator root object.
    explicit Machine(MachineMode mode = MachineMode::Baremetal);
    /// Destroy simulator resources.
    ~Machine();
    Machine(const Machine&) = delete;
    auto operator=(const Machine&) -> Machine& = delete;
    Machine(Machine&&) = delete;
    auto operator=(Machine&&) -> Machine& = delete;
    [[nodiscard]] auto memory_geometry() const noexcept -> MemoryGeometry {
        return {.dram_base = config.memory.dram_base,
                .dram_size = s_dram_size != 0 ? static_cast<Address>(s_dram_size)
                                             : config.memory.dram_size};
    }
    [[nodiscard]] auto platform_profile() const noexcept -> PlatformProfile {
        return s_platform_profile;
    }
    void set_platform_profile(PlatformProfile profile) noexcept {
        config.platform_profile = profile;
        s_platform_profile = profile;
    }
    [[nodiscard]] auto network_mode() const noexcept -> std::string_view { return s_net_mode; }
    void set_network_mode(std::string mode) { s_net_mode = std::move(mode); }
    [[nodiscard]] auto platform_status() const -> PlatformStatusSnapshot;
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
    void run();
    /// Advance every runnable hart and the shared platform by exactly one CA global cycle.
    void advance_ca_global_cycle();
    /// Finalize cycle for tohost checks only.
    void finalize_cycle_tohost();
    /// Stop the simulation loop.
    void stop(StopReason reason = StopReason::ExternalStop);
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
    /// Request termination of the simulator process with the supplied status.
    void request_exit(int status = 0);
    [[nodiscard]] auto stop_reason() const noexcept -> StopReason {
        return stop_reason_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] static auto stop_reason_name(StopReason reason) noexcept -> std::string_view;
    /// Subscribe to lifecycle observations at the machine boundary.
    [[nodiscard]] auto add_lifecycle_observer(LifecycleObserver observer) -> LifecycleObserverId;
    void remove_lifecycle_observer(LifecycleObserverId observer_id);
    /// Reset runtime state flags and CPU state.
    void reset_state();

    /// Thread-safe getter and setter for pending reboot configuration
    void set_pending_reboot(const std::string& binary_path,
                            std::optional<bool> appmode = std::nullopt,
                            std::optional<std::string> disk_path = std::nullopt);
    [[nodiscard]] auto get_pending_reboot() const -> PendingRebootState;
    void clear_pending_reboot();

    std::atomic<uint64_t> tohost{0};  // Host communication register (always 64-bit for HTIF).
    std::atomic<bool> reboot_requested = false;  // Reboot requested flag.
    std::atomic<int> exit_code{0};               // Exit/status code of the simulation.
    std::atomic<bool> is_shutdown_ = false;      // System shutdown flag.
    std::atomic<StopReason> stop_reason_{StopReason::Running};

    MachineConfig config{};

    // ========== Simulation Configuration Flags ==========
    std::atomic<bool> s_appmode{true};         // Baremetal/app mode (default)
    std::atomic<bool> s_tuimode{false};        // Enable TUI monitor mode
    std::atomic<bool> s_high_contrast{false};  // Enable high-contrast TUI mode
    std::atomic<bool> s_class_mode{false};     // Enable educational classroom mode
    std::atomic<bool> s_debugmode{false};      // Enable debug logging in MMIO paths
    std::atomic<bool> s_debug_mode{false};     // Enable TUI debug diagnostics mode
    std::atomic<bool> s_dlog_mode{false};      // Enable device request/response logging
    std::atomic<bool> s_traplog_mode{false};   // Enable trap/SBI/exception logging
    std::atomic<bool> s_use_disk{false};       // Enable disk image simulation
    std::atomic<bool> s_use_mix{false};        // Enable instruction-mix statistics collection
    std::atomic<bool> s_bp_trace{false};       // Enable branch prediction tracing
    std::atomic<bool> s_misa_override{false};  // True when CLI explicitly selected MISA profile
    RuntimeProfile runtime_profile{};          // Resolved command-line runtime policy.
    std::atomic<bool> s_mmu_ever_used{
        false};  // Latched true the first time satp enables translation
    std::atomic<bool> s_multithreaded{false};  // Run simulation in a background thread
    uint32_t s_num_harts = 1;                  // Number of simulated harts (SMP cores)
    uint32_t s_smp_quantum = 100;  // Instruction quantum per hart in cooperative SMP mode
    std::atomic<bool> s_smp_multithreaded{false};  // Enable parallel multi-threaded SMP execution
    uint64_t s_dram_size = 0;                      // Dynamic DRAM size in bytes (0 = default 256MB)
    double s_mouse_sensitivity = 1.0;              // Mouse relative sensitivity factor

    // ========== Debug / Co-Simulation Flags ==========
    std::atomic<bool> s_gdb_mode{false};       // Enable GDB RSP stub
    uint16_t s_gdb_port = 1234;                // GDB stub TCP port
    std::atomic<bool> s_lockstep_mode{false};  // Enable Spike lockstep co-simulation
    std::string s_spike_bin = "spike";         // Path to Spike binary
    std::string s_spike_elf;                   // Path to Spike ELF image

    // ========== Simulation Control Parameters ==========
    Address s_start_pc = 0;                                  // Initial PC value
    Counter s_strace = 0;                                    // Starting cycle for trace generation
    Counter s_fincnt = std::numeric_limits<Counter>::max();  // Finish cycle count
    Counter s_trace_begin = std::numeric_limits<Counter>::max();  // Trace begin cycle
    Counter s_trace_end = std::numeric_limits<Counter>::max();    // Trace end cycle
    Counter s_enabletimer = std::numeric_limits<Counter>::max();  // Timer enable cycle
    Counter s_memimg = std::numeric_limits<Counter>::max();       // Memory image dump cycle

    // ========== ISA/Privilege Configuration ==========
    Address s_isatest_tohost = 0x80001000;        // ISA-test tohost RAM address
    CSRValue s_misa_profile = isa::kMisaDefault;  // Selected MISA profile (without MXL)
    unsigned int s_misa_xlen = 0;                 // Selected MISA XLEN (32 or 64, or 0 if default)
    unsigned int s_vlen = 0;                      // Selected VLEN (bits, or 0 if default)

    // ========== I/O and Logging ==========
    std::string s_fn_memimg;     // Memory image filename
    std::string s_fn_dskimg;     // Disk image filename
    std::string s_fn_dvtree;     // Device-tree binary filename
    std::string s_fn_traplog;    // Trap/exception log filename
    std::string s_fn_cpuconfig;  // CPU config filename
    simrv::pipeline::PipelineType s_pipeline_type =
        simrv::pipeline::PipelineType::FiveStage;        // Pipeline microarchitecture
    std::chrono::steady_clock::time_point s_start_time;  // Simulation start timestamp

   private:
    // Runtime-owned subsystem views. These aliases are private so ownership cannot be mutated by
    // adapters; public access is limited to capability-style methods below.
    simrv::core::CPU& cpu;
    std::vector<std::unique_ptr<simrv::core::CPU>>& secondary_harts_;

   public:
    /// Inspect a simulated hart by index (0 is the primary/boot hart).
    [[nodiscard]] auto hart(size_t index = 0) -> CPU& {
        if (index == 0) {
            return cpu;
        }
        return *secondary_harts_.at(index - 1);
    }
    [[nodiscard]] auto hart(size_t index = 0) const -> const CPU& {
        if (index == 0) {
            return cpu;
        }
        return *secondary_harts_.at(index - 1);
    }
    [[nodiscard]] auto num_harts() const -> size_t { return 1 + secondary_harts_.size(); }
    [[nodiscard]] auto primary_hart() noexcept -> CPU& { return cpu; }
    [[nodiscard]] auto primary_hart() const noexcept -> const CPU& { return cpu; }
    [[nodiscard]] auto ram_data() noexcept -> Byte* { return mmem; }
    [[nodiscard]] auto ram_data() const noexcept -> const Byte* { return mmem; }
    [[nodiscard]] auto ram_view() const noexcept -> simrv::memory::RamView {
        const auto geometry = memory_geometry();
        return {mmem, geometry.dram_base, geometry.dram_size};
    }
    [[nodiscard]] auto tui_execution_snapshot() const noexcept -> TuiExecutionSnapshot;
    /// Platform capability used by built-in devices to publish an external interrupt level.
    void set_platform_irq(int irq, bool asserted) { cpu.plic_set_irq(irq, asserted ? 1 : 0); }
    /// Read the shared platform timer without exposing the CPU ownership graph.
    [[nodiscard]] auto platform_time() const noexcept -> uint64_t {
        return cpu.clint_mmio.mtime.load(std::memory_order_relaxed);
    }
    [[nodiscard]] auto rtc_device() noexcept -> simrv::Rtc* { return rtc.get(); }
    [[nodiscard]] auto rtc_device() const noexcept -> const simrv::Rtc* { return rtc.get(); }
    [[nodiscard]] auto uart_device() noexcept -> simrv::device::Uart* { return uart.get(); }
    [[nodiscard]] auto uart_device() const noexcept -> const simrv::device::Uart* {
        return uart.get();
    }
    [[nodiscard]] auto tui_controller() noexcept -> simrv::tui::Tui* { return tui.get(); }
    [[nodiscard]] auto tui_controller() const noexcept -> const simrv::tui::Tui* {
        return tui.get();
    }
    [[nodiscard]] auto pcie_root() noexcept -> simrv::device::PcieRootComplex* { return pcie.get(); }
    [[nodiscard]] auto pcie_root() const noexcept -> const simrv::device::PcieRootComplex* {
        return pcie.get();
    }
    [[nodiscard]] auto debugger() noexcept -> simrv::debug::GdbStub* { return gdb_stub.get(); }
    [[nodiscard]] auto debugger() const noexcept -> const simrv::debug::GdbStub* {
        return gdb_stub.get();
    }
    [[nodiscard]] auto lockstep() noexcept -> simrv::debug::SpikeLockstep* {
        return spike_lockstep.get();
    }
    [[nodiscard]] auto lockstep() const noexcept -> const simrv::debug::SpikeLockstep* {
        return spike_lockstep.get();
    }
    [[nodiscard]] auto breakpoint_manager() noexcept -> simrv::debug::BreakpointManager& {
        return breakpoints;
    }
    [[nodiscard]] auto breakpoint_manager() const noexcept
        -> const simrv::debug::BreakpointManager& {
        return breakpoints;
    }
    [[nodiscard]] auto trace() noexcept -> Tracer& { return tracer; }
    [[nodiscard]] auto trace() const noexcept -> const Tracer& { return tracer; }
    [[nodiscard]] auto symbol_table() noexcept -> simrv::debug::SymbolTable& { return symbols; }
    [[nodiscard]] auto symbol_table() const noexcept -> const simrv::debug::SymbolTable& {
        return symbols;
    }

   private:
    std::unique_ptr<simrv::Rtc>& rtc;
    std::unique_ptr<simrv::device::Uart>& uart;
    std::unique_ptr<simrv::tui::Tui>& tui;
    std::unique_ptr<simrv::device::PowerMmio>& power;
    std::unique_ptr<simrv::device::AclintMtimer>& aclint_mtimer;
    std::unique_ptr<simrv::device::AclintMswi>& aclint_mswi;
    std::unique_ptr<simrv::device::Imsic>& imsic_m;
    std::unique_ptr<simrv::device::Imsic>& imsic_s;
    std::unique_ptr<simrv::device::Aplic>& aplic_m;
    std::unique_ptr<simrv::device::Aplic>& aplic_s;
    std::unique_ptr<simrv::device::PcieRootComplex>& pcie;
    std::shared_ptr<simrv::device::VirtioPciBlock>& pci_disk;
    std::shared_ptr<simrv::device::VirtioPciConsole>& pci_console;
    std::shared_ptr<simrv::device::VirtioPciRng>& pci_rng;
    std::shared_ptr<simrv::device::VirtioPciGpu>& pci_gpu;
    std::shared_ptr<simrv::device::VirtioPciInput>& pci_input;
    std::shared_ptr<simrv::device::VirtioPciSound>& pci_sound;
    std::shared_ptr<simrv::device::VirtioPciNet>& pci_net;

    PlatformProfile s_platform_profile = PlatformProfile::Pcie;
    std::string s_net_mode = "user";
    std::shared_ptr<simrv::device::VirtioMmioBlock>& mmio_disk;
    std::shared_ptr<simrv::device::VirtioMmioConsole>& mmio_console;
    std::shared_ptr<simrv::device::VirtioMmioRng>& mmio_rng;
    std::shared_ptr<simrv::device::VirtioMmioGpu>& mmio_gpu;
    std::shared_ptr<simrv::device::VirtioMmioInput>& mmio_input;
    std::shared_ptr<simrv::device::VirtioMmioSound>& mmio_sound;
    std::shared_ptr<simrv::device::VirtioMmioNet>& mmio_net;

    // ========== Debug Subsystems (null when disabled) ==========
    std::unique_ptr<simrv::debug::GdbStub>& gdb_stub;
    std::unique_ptr<simrv::debug::SpikeLockstep>& spike_lockstep;
    simrv::debug::BreakpointManager& breakpoints;

    // ========== Memory and Interconnect ==========
    Byte* mmem{};                       // Pointer to main memory buffer
    Tracer& tracer;                       // Non-owning compatibility view.
    simrv::debug::SymbolTable& symbols;  // Non-owning compatibility view.

   public:
    [[nodiscard]] auto memory() -> simrv::memory::MemorySubsystem& { return memory_; }
    [[nodiscard]] auto memory() const -> const simrv::memory::MemorySubsystem& { return memory_; }

   public:
    /// Internal test support for deterministic component fixtures. Not part of the SDK contract.
    void set_ram_for_testing(Byte* ram, size_t size) noexcept;
    [[nodiscard]] auto mutable_ram_data_for_testing() noexcept -> Byte*& { return mmem; }
    [[nodiscard]] auto allocate_ram_for_testing(size_t bytes) -> bool { return allocate_ram(bytes); }
    void release_ram_for_testing() noexcept { release_ram(); }
    void add_hart_for_testing(std::unique_ptr<CPU> hart);
    [[nodiscard]] auto test_secondary_harts() noexcept
        -> std::vector<std::unique_ptr<simrv::core::CPU>>& {
        return secondary_harts_;
    }
    [[nodiscard]] auto mutable_uart_for_testing() noexcept -> std::unique_ptr<simrv::device::Uart>& {
        return uart;
    }
    void finalize_for_testing() { finalize_runner_cycle(); }
    void execute_cycle_for_testing() { execute_runner_cycle(); }
    [[nodiscard]] auto execute_fast_batch_for_testing(uint32_t batch_size) -> bool {
        return execute_runner_fast_batch(batch_size);
    }
    void publish_tui_execution_snapshot_for_testing() noexcept { publish_tui_execution_snapshot(); }
    void start_runner_for_testing() { start_runner(); }
    void stop_runner_for_testing() { stop_runner(); }

   protected:
    [[nodiscard]] auto allocate_ram(size_t bytes) -> bool;
    void release_ram() noexcept;
    friend class simrv::core::CPU;
    friend class simrv::execute::ExecuteUnit;
    friend class simrv::device::Uart;
    friend class simrv::tui::Tui;
    friend class simrv::tui::LeftPane;
    friend class simrv::memory::CoherenceHub;
    simrv::memory::MemorySubsystem& memory_;

    void start_runner();
    void stop_runner();
    void execute_runner_cycle();
    [[nodiscard]] auto execute_runner_fast_batch(uint32_t batch_size) -> bool;
    void prepare_runner_cycle();
    void finalize_runner_cycle();
    void publish_tui_execution_snapshot() noexcept;
    /// Snapshot all fast-path decisions once at a runner batch boundary.
    [[nodiscard]] auto fast_batch_policy() const -> std::optional<FastBatchPolicy>;

    mutable std::mutex pending_reboot_mutex_;
    std::string pending_binary_path_;
    std::optional<bool> pending_appmode_;
    std::optional<std::string> pending_disk_path_;

    void publish_lifecycle_event(LifecycleEventKind kind, int exit_status = 0);
    mutable std::mutex lifecycle_observer_mutex_;
    std::vector<std::pair<LifecycleObserverId, LifecycleObserver>> lifecycle_observers_;
    LifecycleObserverId next_lifecycle_observer_id_ = 1;

    uint64_t last_tui_check_cycles_ = 0;
    std::chrono::steady_clock::time_point last_tui_update_{};

    std::atomic<bool> is_running_ = true;  // Main-loop run flag.
    std::atomic<ExecutionState> execution_state_{ExecutionState::Running};
    std::atomic<uint64_t> tui_snapshot_generation_{0};
    std::atomic<Register> tui_snapshot_pc_{0};
    std::atomic<Counter> tui_snapshot_instruction_count_{0};
    std::atomic<Counter> tui_snapshot_timer_ticks_{0};
    std::atomic<ExecutionState> tui_snapshot_execution_state_{ExecutionState::Stopped};

    friend class BaremetalRunner;
    friend class OsRunner;
};
// NOLINTEND(misc-non-private-member-variables-in-classes)
}  // namespace simrv::core
