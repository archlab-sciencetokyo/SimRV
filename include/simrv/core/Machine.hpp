#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
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
#include "simrv/core/InterruptController.hpp"
#include "simrv/core/MachineConfig.hpp"
#include "simrv/core/RuntimeProfile.hpp"
#include "simrv/core/Telemetry.hpp"
#include "simrv/core/TelemetrySink.hpp"
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
enum class ExecutionState : uint32_t {
    Stopped = 0,
    Running = 1,
    Paused = 2,
    Stepping = 3,
};

/// Stable, inexpensive execution state consumed by the asynchronous TUI renderer.
struct TuiExecutionSnapshot {
    size_t hart = 0;
    Register pc = 0;
    Counter cycle_count = 0;
    Counter instruction_count = 0;
    Counter timer_ticks = 0;
    pipeline::PipelineStats ca_stats{};
    uint64_t icache_hits = 0;
    uint64_t icache_misses = 0;
    uint64_t dcache_hits = 0;
    uint64_t dcache_misses = 0;
    pipeline::Scoreboard scoreboard{};
    ExecutionState execution_state = ExecutionState::Stopped;
};

/**
 * @class Machine
 * @brief Owns and orchestrates CPU, memory subsystem, and MMIO devices.
 *
 * Machine is the simulator root object and drives the pipeline-cycle loop,
 * image loading, device wiring, tracing, and run termination checks.
 */
class Machine final : public core::IInterruptController {
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
    [[nodiscard]] auto high_contrast_enabled() const noexcept -> bool {
        return config.tui.high_contrast;
    }
    void set_high_contrast_enabled(bool enabled) noexcept { config.tui.high_contrast = enabled; }
    [[nodiscard]] auto class_mode_enabled() const noexcept -> bool { return config.tui.class_mode; }
    void set_class_mode_enabled(bool enabled) noexcept { config.tui.class_mode = enabled; }
    [[nodiscard]] auto mission_id() const noexcept -> const std::string& {
        return config.tui.mission;
    }
    [[nodiscard]] auto device_log_enabled() const noexcept -> bool {
        return config.debug.dlog_mode;
    }
    [[nodiscard]] auto trap_log_enabled() const noexcept -> bool {
        return config.debug.traplog_mode;
    }
    void set_instruction_mix_enabled(bool enabled) noexcept { config.debug.use_mix = enabled; }
    void set_branch_trace_enabled(bool enabled) noexcept { config.debug.bp_trace = enabled; }
    void set_trap_log_enabled(bool enabled) noexcept { config.debug.traplog_mode = enabled; }
    void set_device_log_enabled(bool enabled) noexcept { config.debug.dlog_mode = enabled; }
    [[nodiscard]] auto appmode_enabled() const noexcept -> bool { return config.execution.appmode; }
    [[nodiscard]] auto binary_path() const noexcept -> const std::string& {
        return config.files.binary_path;
    }
    [[nodiscard]] auto disk_path() const noexcept -> const std::string& {
        return config.files.disk_path;
    }
    [[nodiscard]] auto branch_trace_enabled() const noexcept -> bool {
        return config.debug.bp_trace;
    }
    [[nodiscard]] auto instruction_mix_enabled() const noexcept -> bool {
        return config.debug.use_mix;
    }
    [[nodiscard]] auto debugger_enabled() const noexcept -> bool {
        return config.debug.gdb_enabled;
    }
    [[nodiscard]] auto debugger_port() const noexcept -> uint16_t { return config.debug.gdb_port; }
    [[nodiscard]] auto lockstep_enabled() const noexcept -> bool {
        return config.debug.lockstep_enabled;
    }
    [[nodiscard]] auto spike_binary() const noexcept -> const std::string& {
        return config.debug.spike_bin;
    }
    [[nodiscard]] auto spike_elf() const noexcept -> const std::string& {
        return config.debug.spike_elf;
    }
    [[nodiscard]] auto isa_test_tohost() const noexcept -> Address {
        return config.isa.isatest_tohost;
    }
    [[nodiscard]] auto memory_geometry() const noexcept -> MemoryGeometry { return config.memory; }
    [[nodiscard]] auto platform_profile() const noexcept -> PlatformProfile {
        return config.platform_profile;
    }
    [[nodiscard]] auto network_mode() const noexcept -> std::string_view {
        return config.network.mode;
    }
    [[nodiscard]] auto execution_config() const noexcept -> const ExecutionConfig& {
        return config.execution;
    }
    void set_pipeline_type(simrv::pipeline::PipelineType type) noexcept {
        config.execution.pipeline_type = type;
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
     * @return std::expected<void, std::string> on success or diagnostic error string.
     */
    auto initialize() -> std::expected<void, std::string>;
    /**
     * @brief Load a program binary image dynamically into simulator DRAM and reset CPU state.
     * @param filepath Path to the program binary image.
     * @return std::expected<void, std::string> on success or diagnostic error string.
     */
    auto load_program_binary(const std::string& filepath) -> std::expected<void, std::string>;
    /// Load a disk image into the virtio disk device (may be called from TUI modal).
    /// @param filepath Path to the disk image file.
    /// @return std::expected<void, std::string> on success or diagnostic error string.
    auto load_disk_image(const std::string& filepath) -> std::expected<void, std::string>;
    /// Execute the main simulation loop until termination criteria are met.
    void run();
    /// Advance every runnable hart and the shared platform by exactly one CA global cycle.
    void advance_ca_global_cycle();
    /// Advance hart 0 and the shared platform while CA secondary workers run independently.
    void advance_ca_primary_cycle();
    /// Finalize cycle for tohost checks only.
    void finalize_cycle_tohost();
    /// Stop the simulation loop.
    void stop(StopReason reason = StopReason::ExternalStop);
    /// Get current atomic execution state.
    [[nodiscard]] auto execution_state() const -> ExecutionState {
        return execution_state_.load(std::memory_order_acquire);
    }
    /// Check if machine execution is currently paused.
    [[nodiscard]] auto is_paused() const -> bool;
    /// Check if machine execution is currently stopped.
    [[nodiscard]] auto is_stopped() const -> bool {
        return execution_state_.load(std::memory_order_relaxed) == ExecutionState::Stopped;
    }
    /// Check if machine execution is in single-stepping state.
    [[nodiscard]] auto is_stepping() const -> bool {
        return execution_state() == ExecutionState::Stepping;
    }
    /// Pause machine execution.
    void pause();
    /// Resume machine execution.
    void resume();
    /// Request execution of a single instruction cycle.
    void step();
    /// Request execution of a single instruction cycle and synchronously wait for completion.
    void step_sync(std::chrono::milliseconds timeout = std::chrono::milliseconds(500));
    /// Begin an all-stop GDB step that completes when the selected hart retires once.
    [[nodiscard]] auto begin_debug_step(HartId hart) -> bool;
    /// Stop execution for a debugger event and publish an RSP stop reply.
    void debug_halt(HartId hart, GdbSignal signal = GdbSignal::SigTrap, std::string reason = {});
    /// Record a memory watchpoint and stop after the modeled access completes.
    void debug_watch_hit(HartId hart, GdbSignal signal, std::string reason,
                         std::string description);
    /// Wake simulation and SMP workers after an external control-plane event.
    void notify_control_event() noexcept;
    [[nodiscard]] auto debug_pause_requested() const noexcept -> bool;
    /// Check if the simulation loop is running.
    [[nodiscard]] auto is_running() const -> bool {
        return is_running_.load(std::memory_order_relaxed);
    }
    /// Total instructions retired by every hart during the current machine run.
    [[nodiscard]] auto retired_instruction_count() const noexcept -> Counter {
        return retired_instruction_count_.load(std::memory_order_relaxed);
    }
    /// Publish retired instructions from a hart; used by execution engines and safe for MT-SMP.
    void record_retired_instructions(Counter count) noexcept {
        retired_instruction_count_.fetch_add(count, std::memory_order_relaxed);
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
    /// Dynamically switch execution engine (e.g. IA <-> CA) without reloading/rebooting.
    void switch_execution_engine(ExecutionEngine engine);
    void switch_execution_engine_sync(ExecutionEngine engine);

    std::atomic<uint64_t> tohost{0};  // Host communication register (always 64-bit for HTIF).
    std::atomic<bool> reboot_requested = false;  // Reboot requested flag.
    std::atomic<int> exit_code{0};               // Exit/status code of the simulation.
    std::atomic<bool> is_shutdown_ = false;      // System shutdown flag.
    std::atomic<StopReason> stop_reason_{StopReason::Running};
    std::atomic<Counter> retired_instruction_count_{0};

    RuntimeProfile runtime_profile{};  // Resolved command-line runtime policy.
    std::atomic<bool> s_mmu_ever_used{
        false};  // Latched true the first time satp enables translation
   private:
    /// Construction/reinitialization boundary; never a live architectural mutation API.
    void apply_configuration(MachineConfig machine_config);
    MachineConfig config{};
    std::chrono::steady_clock::time_point start_time_{};
    Address resolved_start_pc_ = 0;
    Address resolved_isatest_tohost_ = 0;

   public:
    /// Inspect a simulated hart by index (0 is the primary/boot hart).
    [[nodiscard]] auto hart(size_t index = 0) -> CPU&;
    [[nodiscard]] auto hart(size_t index = 0) const -> const CPU&;
    [[nodiscard]] auto hart(HartId id) -> CPU& { return hart(id.val); }
    [[nodiscard]] auto hart(HartId id) const -> const CPU& { return hart(id.val); }
    [[nodiscard]] auto num_harts() const -> size_t;
    [[nodiscard]] auto primary_hart() noexcept -> CPU&;
    [[nodiscard]] auto primary_hart() const noexcept -> const CPU&;
    [[nodiscard]] auto ram_data() noexcept -> Byte*;
    [[nodiscard]] auto ram_data() const noexcept -> const Byte*;
    [[nodiscard]] auto ram_view() const noexcept -> simrv::memory::RamView;
    [[nodiscard]] auto tui_execution_snapshot(size_t hart = 0) const noexcept
        -> TuiExecutionSnapshot;
    /// Platform capability used by built-in devices to publish an external interrupt level.
    void set_platform_irq(IrqNumber irq, bool asserted = true) override;
    /// Architectural interrupt signal dispatched to a specific hart.
    void set_hart_irq(HartId hart, InterruptType type, PrivilegeLevel priv, bool asserted) override;
    [[nodiscard]] auto interrupt_controller() noexcept -> core::IInterruptController& {
        return *this;
    }
    [[nodiscard]] auto interrupt_controller() const noexcept -> const core::IInterruptController& {
        return *this;
    }
    /// Read the shared platform timer without exposing the CPU ownership graph.
    [[nodiscard]] auto platform_time() const noexcept -> uint64_t;
    [[nodiscard]] auto rtc_device() noexcept -> simrv::Rtc*;
    [[nodiscard]] auto rtc_device() const noexcept -> const simrv::Rtc*;
    [[nodiscard]] auto uart_device() noexcept -> simrv::device::Uart*;
    [[nodiscard]] auto uart_device() const noexcept -> const simrv::device::Uart*;
    [[nodiscard]] auto pcie_root() noexcept -> simrv::device::PcieRootComplex*;
    [[nodiscard]] auto pcie_root() const noexcept -> const simrv::device::PcieRootComplex*;
    [[nodiscard]] auto debugger() noexcept -> simrv::debug::GdbStub*;
    [[nodiscard]] auto debugger() const noexcept -> const simrv::debug::GdbStub*;
    [[nodiscard]] auto lockstep() noexcept -> simrv::debug::SpikeLockstep*;
    [[nodiscard]] auto lockstep() const noexcept -> const simrv::debug::SpikeLockstep*;
    [[nodiscard]] auto breakpoint_manager() noexcept -> simrv::debug::BreakpointManager&;
    [[nodiscard]] auto breakpoint_manager() const noexcept
        -> const simrv::debug::BreakpointManager&;
    [[nodiscard]] auto trace() noexcept -> Tracer&;
    [[nodiscard]] auto trace() const noexcept -> const Tracer&;
    [[nodiscard]] auto symbol_table() noexcept -> simrv::debug::SymbolTable&;
    [[nodiscard]] auto symbol_table() const noexcept -> const simrv::debug::SymbolTable&;

    [[nodiscard]] auto telemetry_sink() noexcept -> std::shared_ptr<ITelemetrySink> {
        return telemetry_sink_;
    }
    [[nodiscard]] auto telemetry_sink() const noexcept -> std::shared_ptr<const ITelemetrySink> {
        return telemetry_sink_;
    }
    // Sink ownership is fixed while runners are active.
    [[nodiscard]] auto telemetry_sink_raw() const noexcept -> ITelemetrySink* {
        return telemetry_sink_.get();
    }
    void set_telemetry_sink(std::shared_ptr<ITelemetrySink> sink) noexcept {
        telemetry_sink_ = std::move(sink);
    }
    [[nodiscard]] auto console_sink() noexcept -> std::shared_ptr<IConsoleSink> {
        return console_sink_;
    }
    [[nodiscard]] auto console_sink() const noexcept -> std::shared_ptr<const IConsoleSink> {
        return console_sink_;
    }
    void set_console_sink(std::shared_ptr<IConsoleSink> sink) noexcept {
        console_sink_ = std::move(sink);
    }
    void console_write(char ch);

    [[nodiscard]] auto memory() noexcept -> simrv::memory::MemorySubsystem& { return memory_; }
    [[nodiscard]] auto memory() const noexcept -> const simrv::memory::MemorySubsystem& {
        return memory_;
    }
    simrv::memory::MemorySubsystem& memory_;

    /// Internal test support for deterministic component fixtures. Not part of the SDK contract.
    void set_ram_for_testing(Byte* ram, size_t size) noexcept;
    void set_tui_enabled_for_testing(bool enabled) noexcept { config.tui.enabled = enabled; }
    void set_smp_parallel_for_testing(bool enabled) noexcept {
        config.execution.smp_multithreaded = enabled;
    }
    void set_instruction_limit_for_testing(Counter limit) noexcept {
        config.execution.fincnt = limit;
    }
    [[nodiscard]] auto mutable_ram_data_for_testing() noexcept -> Byte*&;
    [[nodiscard]] auto allocate_ram_for_testing(size_t bytes) -> bool {
        return allocate_ram(bytes);
    }
    void release_ram_for_testing() noexcept { release_ram(); }
    void add_hart_for_testing(std::unique_ptr<CPU> hart);
    [[nodiscard]] auto test_secondary_harts() noexcept
        -> std::vector<std::unique_ptr<simrv::core::CPU>>&;
    [[nodiscard]] auto mutable_uart_for_testing() noexcept -> std::unique_ptr<simrv::device::Uart>&;
    void finalize_for_testing() { finalize_runner_cycle(); }
    void execute_cycle_for_testing() { execute_runner_cycle(); }
    [[nodiscard]] auto execute_fast_batch_for_testing(uint32_t batch_size) -> bool {
        return execute_runner_fast_batch(batch_size);
    }
    // Commands execute between runner turns, including while paused or stopped in server mode.
    void post_control(std::function<void(Machine&)> command);
    void service_control_commands();
    [[nodiscard]] bool has_control_commands() const noexcept {
        return controls_pending_.load(std::memory_order_acquire);
    }
    void set_persistent_control(bool enabled) noexcept { persistent_control_ = enabled; }
    [[nodiscard]] bool persistent_control() const noexcept { return persistent_control_; }
    void complete_step_with(std::function<void(Machine&)> callback) {
        step_completion_ = std::move(callback);
    }
    [[nodiscard]] bool sampled_instruction_execution() const;
    void request_tui_sample() noexcept {
        tui_sample_requested_.fetch_add(1, std::memory_order_release);
    }
    void publish_requested_tui_sample(size_t hart) noexcept;
    void publish_tui_execution_snapshot_for_testing() noexcept { publish_tui_execution_snapshot(); }
    void start_runner_for_testing() { start_runner(); }
    void stop_runner_for_testing() { stop_runner(); }
    void install_debugger_for_testing(std::unique_ptr<simrv::debug::GdbStub> debugger);

   protected:
    [[nodiscard]] auto allocate_ram(size_t bytes) -> bool;
    void release_ram() noexcept;
    friend class simrv::core::CPU;
    friend class simrv::execute::ExecuteUnit;
    friend class simrv::device::Uart;
    friend class simrv::memory::CoherenceHub;

    void start_runner();
    void stop_runner();
    void wait_for_runner_quiescence();
    void execute_runner_cycle();
    [[nodiscard]] auto execute_runner_fast_batch(uint32_t batch_size) -> bool;
    void prepare_runner_cycle();
    void finalize_runner_cycle();
    void publish_tui_execution_snapshot() noexcept;
    void publish_tui_execution_snapshot_for_hart(size_t hart) noexcept;
    /// Snapshot all fast-path decisions once at a runner batch boundary.
    [[nodiscard]] auto fast_batch_policy() const -> std::optional<FastBatchPolicy>;
    [[nodiscard]] auto ca_batch_quantum() const noexcept -> uint32_t;
    void advance_ca_platform_cycle(bool synchronize_secondary_harts);
    void service_debug_halt();

    std::mutex control_mutex_;
    std::deque<std::function<void(Machine&)>> control_commands_;
    std::atomic<bool> controls_pending_{false};
    bool persistent_control_ = false;
    std::function<void(Machine&)> step_completion_;
    mutable std::mutex staged_configuration_mutex_;
    std::optional<MachineConfig> staged_configuration_;
    void publish_lifecycle_event(LifecycleEventKind kind, int exit_status = 0);
    mutable std::mutex lifecycle_observer_mutex_;
    std::vector<std::pair<LifecycleObserverId, LifecycleObserver>> lifecycle_observers_;
    LifecycleObserverId next_lifecycle_observer_id_ = 1;

    uint64_t last_tui_check_cycles_ = 0;
    std::chrono::steady_clock::time_point last_tui_update_{};

    std::atomic<bool> is_running_ = true;  // Main-loop run flag.
    std::shared_ptr<ITelemetrySink> telemetry_sink_{};
    std::shared_ptr<IConsoleSink> console_sink_{};
    std::atomic<bool> runner_started_{false};
    std::atomic<ExecutionState> execution_state_{ExecutionState::Running};
    void acknowledge_step();
    std::mutex step_mutex_;
    std::condition_variable step_cv_;
    uint64_t step_ack_count_ = 0;
    std::atomic<uint64_t> control_event_generation_{0};
    std::atomic<uint32_t> runner_in_cycle_{0};
    std::optional<HartId> debug_step_hart_;
    Counter debug_step_target_ = 0;
    struct PendingDebugHalt {
        HartId hart{0};
        GdbSignal signal = GdbSignal::SigTrap;
        std::string reason;
        std::string description;
        bool waits_for_memory = false;
        Counter retirement_target = 0;
    };
    void enqueue_debug_halt(PendingDebugHalt halt);
    std::atomic<bool> debug_halt_pending_{false};
    std::mutex debug_halt_mutex_;
    std::optional<PendingDebugHalt> pending_debug_halt_;
    static constexpr size_t kMaxTuiSnapshotHarts = 64;
    // Immutable snapshots: readers never spin on a running producer or read live registers.
    std::array<std::atomic<std::shared_ptr<const TuiExecutionSnapshot>>, kMaxTuiSnapshotHarts>
        tui_snapshots_{};
    std::atomic<uint64_t> tui_sample_requested_{0};
    std::array<uint64_t, kMaxTuiSnapshotHarts> tui_sample_published_{};

    friend class RunnerBase;
    friend class BaremetalRunner;
    friend class OsRunner;
    friend class PlatformBuilder;
};
// NOLINTEND(misc-non-private-member-variables-in-classes)
}  // namespace simrv::core
