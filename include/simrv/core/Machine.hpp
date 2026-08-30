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
class InspectorPane;
}  // namespace simrv::tui

namespace simrv::core {

class BaremetalRunner;
class OsRunner;
class PlatformBuilder;

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

    /// Construct the simulator root object from its complete, typed run configuration.
    explicit Machine(MachineConfig machine_config = {});
    /// Destroy simulator resources.
    ~Machine();
    Machine(const Machine&) = delete;
    auto operator=(const Machine&) -> Machine& = delete;
    Machine(Machine&&) = delete;
    auto operator=(Machine&&) -> Machine& = delete;
    [[nodiscard]] auto configuration() const noexcept -> const MachineConfig& { return config; }
    [[nodiscard]] auto tui_enabled() const noexcept -> bool { return config.tui.enabled; }
    [[nodiscard]] auto mouse_sensitivity() const noexcept -> double {
        return config.tui.mouse_sensitivity;
    }
    [[nodiscard]] auto debug_diagnostics_enabled() const noexcept -> bool {
        return config.tui.debug_diagnostics;
    }
    void set_debug_diagnostics_enabled(bool enabled) noexcept { config.tui.debug_diagnostics = enabled; }
    [[nodiscard]] auto high_contrast_enabled() const noexcept -> bool {
        return config.tui.high_contrast;
    }
    void set_high_contrast_enabled(bool enabled) noexcept { config.tui.high_contrast = enabled; }
    [[nodiscard]] auto class_mode_enabled() const noexcept -> bool { return config.tui.class_mode; }
    void set_class_mode_enabled(bool enabled) noexcept { config.tui.class_mode = enabled; }
    [[nodiscard]] auto debugmode_enabled() const noexcept -> bool { return config.debug.debugmode; }
    [[nodiscard]] auto device_log_enabled() const noexcept -> bool { return config.debug.dlog_mode; }
    [[nodiscard]] auto trap_log_enabled() const noexcept -> bool { return config.debug.traplog_mode; }
    void set_instruction_mix_enabled(bool enabled) noexcept { config.debug.use_mix = enabled; }
    void set_branch_trace_enabled(bool enabled) noexcept { config.debug.bp_trace = enabled; }
    void set_trap_log_enabled(bool enabled) noexcept { config.debug.traplog_mode = enabled; }
    void set_device_log_enabled(bool enabled) noexcept { config.debug.dlog_mode = enabled; }
    [[nodiscard]] auto appmode_enabled() const noexcept -> bool { return config.execution.appmode; }
    [[nodiscard]] auto binary_path() const noexcept -> const std::string& {
        return config.files.binary_path;
    }
    [[nodiscard]] auto disk_path() const noexcept -> const std::string& { return config.files.disk_path; }
    [[nodiscard]] auto branch_trace_enabled() const noexcept -> bool {
        return config.debug.bp_trace;
    }
    [[nodiscard]] auto instruction_mix_enabled() const noexcept -> bool {
        return config.debug.use_mix;
    }
    [[nodiscard]] auto debugger_enabled() const noexcept -> bool { return config.debug.gdb_enabled; }
    [[nodiscard]] auto debugger_port() const noexcept -> uint16_t { return config.debug.gdb_port; }
    [[nodiscard]] auto lockstep_enabled() const noexcept -> bool {
        return config.debug.lockstep_enabled;
    }
    [[nodiscard]] auto spike_binary() const noexcept -> const std::string& {
        return config.debug.spike_bin;
    }
    [[nodiscard]] auto spike_elf() const noexcept -> const std::string& { return config.debug.spike_elf; }
    [[nodiscard]] auto isa_test_tohost() const noexcept -> Address {
        return config.isa.isatest_tohost;
    }
    [[nodiscard]] auto memory_geometry() const noexcept -> MemoryGeometry {
        return config.memory;
    }
    [[nodiscard]] auto platform_profile() const noexcept -> PlatformProfile {
        return config.platform_profile;
    }
    [[nodiscard]] auto network_mode() const noexcept -> std::string_view { return config.network.mode; }
    [[nodiscard]] auto execution_config() const noexcept -> const ExecutionConfig& {
        return config.execution;
    }
    [[nodiscard]] auto isa_config() const noexcept -> const IsaConfig& { return config.isa; }
    [[nodiscard]] auto files_config() const noexcept -> const FilesConfig& { return config.files; }
    /// Stage an architectural reconfiguration. It is validated and only takes effect after reboot.
    [[nodiscard]] auto stage_reconfiguration(MachineConfig machine_config)
        -> std::expected<void, std::string>;
    [[nodiscard]] auto take_staged_reconfiguration() -> std::optional<MachineConfig>;
    void set_start_time(std::chrono::steady_clock::time_point time) noexcept { start_time_ = time; }
    [[nodiscard]] auto start_time() const noexcept -> std::chrono::steady_clock::time_point {
        return start_time_;
    }
    /// Initialization-only derived boot state; image symbols never rewrite the input config.
    void set_resolved_boot_state(Address start_pc, std::optional<Address> tohost_address) noexcept;
    [[nodiscard]] auto resolved_start_pc() const noexcept -> Address { return resolved_start_pc_; }
    [[nodiscard]] auto resolved_isatest_tohost() const noexcept -> Address {
        return resolved_isatest_tohost_;
    }
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


    std::atomic<uint64_t> tohost{0};  // Host communication register (always 64-bit for HTIF).
    std::atomic<bool> reboot_requested = false;  // Reboot requested flag.
    std::atomic<int> exit_code{0};               // Exit/status code of the simulation.
    std::atomic<bool> is_shutdown_ = false;      // System shutdown flag.
    std::atomic<StopReason> stop_reason_{StopReason::Running};

    RuntimeProfile runtime_profile{};          // Resolved command-line runtime policy.
    std::atomic<bool> s_mmu_ever_used{
        false};  // Latched true the first time satp enables translation
   private:
    /// Construction/reinitialization boundary; never a live architectural mutation API.
    void apply_configuration(MachineConfig machine_config);
    MachineConfig config{};
    std::chrono::steady_clock::time_point start_time_{};
    Address resolved_start_pc_ = 0;
    Address resolved_isatest_tohost_ = 0;
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
    void set_tui_enabled_for_testing(bool enabled) noexcept { config.tui.enabled = enabled; }
    void set_smp_parallel_for_testing(bool enabled) noexcept {
        config.execution.smp_multithreaded = enabled;
    }
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
    friend class simrv::tui::InspectorPane;
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

    mutable std::mutex staged_configuration_mutex_;
    std::optional<MachineConfig> staged_configuration_;
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
    friend class PlatformBuilder;
};
// NOLINTEND(misc-non-private-member-variables-in-classes)
}  // namespace simrv::core
