/**
 * @file Machine.cpp
 * @brief Machine top-level orchestration and cycle-loop implementation.
 */
#include "simrv/core/Machine.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <print>
#include <span>
#include <thread>
#include <variant>

#include "simrv/core/Logger.hpp"
#include "simrv/device/AIA.hpp"
#include "simrv/device/Aclint.hpp"
#include "simrv/device/Power.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/device/pci/PcieRootComplex.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

/// Bare-metal and OS scheduling stay outside CPU's per-instruction fast path.  The selected
/// runner is a value in Machine::Runtime, avoiding the old Machine subclass hierarchy.
class BaremetalRunner {
   public:
    ~BaremetalRunner() { stop_threads(); }
    void start(Machine& machine);
    void stop(Machine& machine);
    void prepare(Machine& machine);
    void execute(Machine& machine);
    [[nodiscard]] auto execute_fast_batch(Machine& machine, uint32_t batch_size) -> bool;
    void finalize(Machine& machine);

   private:
    void stop_threads();
    std::vector<std::jthread> worker_threads_;
    std::atomic<bool> workers_running_{false};
};

class OsRunner {
   public:
    ~OsRunner() { stop_threads(); }
    void start(Machine& machine);
    void stop(Machine& machine);
    void prepare(Machine& machine);
    void execute(Machine& machine);
    [[nodiscard]] auto execute_fast_batch(Machine& machine, uint32_t batch_size) -> bool;
    void finalize(Machine& machine);

   private:
    void stop_threads();
    std::vector<std::jthread> worker_threads_;
    std::atomic<bool> workers_running_{false};
};

/// Owns zero-initialized DRAM without eagerly touching every host page.  calloc is permitted to
/// use demand-zero pages for large allocations while retaining the guest-visible zero-fill
/// semantics required for RAM and ELF BSS.
class RamStorage {
   public:
    RamStorage() = default;
    ~RamStorage() { reset(); }
    RamStorage(const RamStorage&) = delete;
    auto operator=(const RamStorage&) -> RamStorage& = delete;

    [[nodiscard]] auto allocate(size_t bytes) -> bool {
        if (bytes == 0) return false;
        auto* replacement = static_cast<Byte*>(std::calloc(bytes, sizeof(Byte)));
        if (replacement == nullptr) return false;
        reset();
        data_ = replacement;
        return true;
    }

    void reset() noexcept {
        std::free(data_);
        data_ = nullptr;
    }

    [[nodiscard]] auto data() const noexcept -> Byte* { return data_; }

   private:
    Byte* data_ = nullptr;
};

class Machine::Runtime {
   public:
    using ExecutionRunner = std::variant<BaremetalRunner, OsRunner>;

    explicit Runtime(Machine& machine, bool appmode)
        : tracer(machine), memory(machine), runner(std::in_place_type<BaremetalRunner>) {
        if (!appmode) {
            runner.emplace<OsRunner>();
        }
    }

    CPU primary_cpu;
    std::vector<std::unique_ptr<CPU>> secondary_harts;
    RamStorage ram;
    std::unique_ptr<simrv::Rtc> rtc;
    std::unique_ptr<simrv::device::Uart> uart;
    std::unique_ptr<simrv::tui::Tui> tui;
    std::unique_ptr<simrv::device::PowerMmio> power;
    std::unique_ptr<simrv::device::AclintMtimer> aclint_mtimer;
    std::unique_ptr<simrv::device::AclintMswi> aclint_mswi;
    std::unique_ptr<simrv::device::Imsic> imsic_m;
    std::unique_ptr<simrv::device::Imsic> imsic_s;
    std::unique_ptr<simrv::device::Aplic> aplic_m;
    std::unique_ptr<simrv::device::Aplic> aplic_s;
    std::unique_ptr<simrv::device::PcieRootComplex> pcie;
    std::shared_ptr<simrv::device::VirtioPciBlock> pci_disk;
    std::shared_ptr<simrv::device::VirtioPciConsole> pci_console;
    std::shared_ptr<simrv::device::VirtioPciRng> pci_rng;
    std::shared_ptr<simrv::device::VirtioPciGpu> pci_gpu;
    std::shared_ptr<simrv::device::VirtioPciInput> pci_input;
    std::shared_ptr<simrv::device::VirtioPciSound> pci_sound;
    std::shared_ptr<simrv::device::VirtioPciNet> pci_net;
    std::shared_ptr<simrv::device::VirtioMmioBlock> mmio_disk;
    std::shared_ptr<simrv::device::VirtioMmioConsole> mmio_console;
    std::shared_ptr<simrv::device::VirtioMmioRng> mmio_rng;
    std::shared_ptr<simrv::device::VirtioMmioGpu> mmio_gpu;
    std::shared_ptr<simrv::device::VirtioMmioInput> mmio_input;
    std::shared_ptr<simrv::device::VirtioMmioSound> mmio_sound;
    std::shared_ptr<simrv::device::VirtioMmioNet> mmio_net;
    std::unique_ptr<simrv::debug::GdbStub> gdb_stub;
    std::unique_ptr<simrv::debug::SpikeLockstep> spike_lockstep;
    simrv::debug::BreakpointManager breakpoints;
    Tracer tracer;
    simrv::debug::SymbolTable symbols;
    simrv::memory::MemorySubsystem memory;
    ExecutionRunner runner;
};

Machine::Machine(MachineConfig machine_config)
    : runtime_(std::make_unique<Runtime>(*this, machine_config.execution.appmode)),
      cpu(runtime_->primary_cpu),
      secondary_harts_(runtime_->secondary_harts),
      rtc(runtime_->rtc),
      uart(runtime_->uart),
      tui(runtime_->tui),
      power(runtime_->power),
      aclint_mtimer(runtime_->aclint_mtimer),
      aclint_mswi(runtime_->aclint_mswi),
      imsic_m(runtime_->imsic_m),
      imsic_s(runtime_->imsic_s),
      aplic_m(runtime_->aplic_m),
      aplic_s(runtime_->aplic_s),
      pcie(runtime_->pcie),
      pci_disk(runtime_->pci_disk),
      pci_console(runtime_->pci_console),
      pci_rng(runtime_->pci_rng),
      pci_gpu(runtime_->pci_gpu),
      pci_input(runtime_->pci_input),
      pci_sound(runtime_->pci_sound),
      pci_net(runtime_->pci_net),
      mmio_disk(runtime_->mmio_disk),
      mmio_console(runtime_->mmio_console),
      mmio_rng(runtime_->mmio_rng),
      mmio_gpu(runtime_->mmio_gpu),
      mmio_input(runtime_->mmio_input),
      mmio_sound(runtime_->mmio_sound),
      mmio_net(runtime_->mmio_net),
      gdb_stub(runtime_->gdb_stub),
      spike_lockstep(runtime_->spike_lockstep),
      breakpoints(runtime_->breakpoints),
      tracer(runtime_->tracer),
      symbols(runtime_->symbols),
      memory_(runtime_->memory) {
    cpu.machine_ = this;
    apply_configuration(std::move(machine_config));
}

void Machine::apply_configuration(MachineConfig machine_config) {
    config = std::move(machine_config);
    resolved_start_pc_ = config.execution.start_pc;
    resolved_isatest_tohost_ = config.isa.isatest_tohost;
}

void Machine::set_resolved_boot_state(Address start_pc,
                                      std::optional<Address> tohost_address) noexcept {
    resolved_start_pc_ = start_pc;
    if (tohost_address.has_value()) resolved_isatest_tohost_ = *tohost_address;
}

auto Machine::stage_reconfiguration(MachineConfig machine_config)
    -> std::expected<void, std::string> {
    if (const auto valid = machine_config.validate(); !valid) return std::unexpected(valid.error());
    {
        const std::scoped_lock lock(staged_configuration_mutex_);
        staged_configuration_ = std::move(machine_config);
    }
    request_reboot();
    return {};
}

auto Machine::take_staged_reconfiguration() -> std::optional<MachineConfig> {
    const std::scoped_lock lock(staged_configuration_mutex_);
    return std::exchange(staged_configuration_, std::nullopt);
}

auto Machine::allocate_ram(size_t bytes) -> bool {
    if (!runtime_->ram.allocate(bytes)) {
        return false;
    }
    mmem = runtime_->ram.data();
    return true;
}

void Machine::release_ram() noexcept {
    runtime_->ram.reset();
    mmem = nullptr;
}

void Machine::set_ram_for_testing(Byte* ram, size_t size) noexcept {
    runtime_->ram.reset();
    mmem = ram;
    config.memory.dram_size = static_cast<Address>(size);
}

void Machine::add_hart_for_testing(std::unique_ptr<CPU> hart) {
    hart->machine_ = this;
    secondary_harts_.push_back(std::move(hart));
}

void Machine::start_runner() {
    std::visit([this](auto& runner) { runner.start(*this); }, runtime_->runner);
}

void Machine::stop_runner() {
    std::visit([this](auto& runner) { runner.stop(*this); }, runtime_->runner);
}

void Machine::prepare_runner_cycle() {
    std::visit([this](auto& runner) { runner.prepare(*this); }, runtime_->runner);
}

void Machine::execute_runner_cycle() {
    std::visit([this](auto& runner) { runner.execute(*this); }, runtime_->runner);
    if (tui_enabled()) publish_tui_execution_snapshot();
}

auto Machine::execute_runner_fast_batch(uint32_t batch_size) -> bool {
    const bool executed = std::visit(
        [this, batch_size](auto& runner) { return runner.execute_fast_batch(*this, batch_size); },
        runtime_->runner);
    if (executed && tui_enabled()) publish_tui_execution_snapshot();
    return executed;
}

void Machine::publish_tui_execution_snapshot() noexcept {
    tui_snapshot_generation_.fetch_add(1, std::memory_order_release);
    tui_snapshot_pc_.store(primary_hart().state().pc, std::memory_order_relaxed);
    tui_snapshot_instruction_count_.store(primary_hart().e_icount, std::memory_order_relaxed);
    tui_snapshot_timer_ticks_.store(primary_hart().clint_mmio.mtime.load(std::memory_order_relaxed),
                                    std::memory_order_relaxed);
    tui_snapshot_execution_state_.store(execution_state_.load(std::memory_order_relaxed),
                                        std::memory_order_relaxed);
    tui_snapshot_generation_.fetch_add(1, std::memory_order_release);
}

auto Machine::tui_execution_snapshot() const noexcept -> TuiExecutionSnapshot {
    TuiExecutionSnapshot snapshot;
    while (true) {
        const uint64_t before = tui_snapshot_generation_.load(std::memory_order_acquire);
        if ((before & 1U) != 0) continue;
        snapshot.pc = tui_snapshot_pc_.load(std::memory_order_relaxed);
        snapshot.instruction_count =
            tui_snapshot_instruction_count_.load(std::memory_order_relaxed);
        snapshot.timer_ticks = tui_snapshot_timer_ticks_.load(std::memory_order_relaxed);
        snapshot.execution_state = tui_snapshot_execution_state_.load(std::memory_order_relaxed);
        const uint64_t after = tui_snapshot_generation_.load(std::memory_order_acquire);
        if (before == after) return snapshot;
    }
}

void Machine::finalize_runner_cycle() {
    std::visit([this](auto& runner) { runner.finalize(*this); }, runtime_->runner);
}

auto Machine::fast_batch_policy() const -> std::optional<FastBatchPolicy> {
    if (!runtime_profile.allows_fast_batch() || lockstep_enabled() || debugger_enabled() ||
        branch_trace_enabled() || config.execution.strace != 0 || breakpoints.has_any()) {
        return std::nullopt;
    }
    if (tui_enabled() &&
        (!tui || tui->is_trace_active() ||
         tui->step_delay_us_.load(std::memory_order_relaxed) != 0 || is_stepping())) {
        return std::nullopt;
    }
    const bool captures_execution_detail = tui_enabled() && tui && tui->captures_execution_detail();
    return FastBatchPolicy{
        .copy_pipeline_context = captures_execution_detail,
        .collect_instruction_mix = instruction_mix_enabled() || captures_execution_detail,
        .poll_pause = true,
        .has_instruction_limit = config.execution.fincnt != std::numeric_limits<Counter>::max(),
    };
}

void BaremetalRunner::start(Machine& machine) {
    if (!machine.config.execution.smp_multithreaded || machine.secondary_harts_.empty() ||
        !machine.runtime_profile.is_instruction_mode()) {
        return;
    }
    stop_threads();
    workers_running_.store(true, std::memory_order_release);
    for (size_t i = 0; i < machine.secondary_harts_.size(); ++i) {
        worker_threads_.emplace_back([&machine, this, i](const std::stop_token& stop_token) {
            auto& hart = *machine.secondary_harts_[i];
            constexpr uint32_t kWorkerBatch = 2048;
            while (!stop_token.stop_requested() && machine.is_running() &&
                   workers_running_.load(std::memory_order_relaxed)) {
                if (hart.hart_status.load(std::memory_order_relaxed) != HartStatus::Started) {
                    hart.hart_status.wait(HartStatus::Stopped, std::memory_order_relaxed);
                    continue;
                }
                if (machine.execution_state_.load(std::memory_order_relaxed) ==
                    ExecutionState::Paused) {
                    machine.execution_state_.wait(ExecutionState::Paused,
                                                  std::memory_order_relaxed);
                    continue;
                }
                for (uint32_t step = 0;
                     step < kWorkerBatch && machine.is_running() &&
                     hart.hart_status.load(std::memory_order_relaxed) == HartStatus::Started;
                     ++step) {
                    hart.run_cycle_baremetal(machine);
                }
            }
        });
    }
}

void BaremetalRunner::stop_threads() {
    workers_running_.store(false, std::memory_order_release);
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.request_stop();
            thread.join();
        }
    }
    worker_threads_.clear();
}

void BaremetalRunner::stop(Machine& machine) {
    workers_running_.store(false, std::memory_order_release);
    for (auto& hart : machine.secondary_harts_) hart->hart_status.notify_all();
    machine.execution_state_.notify_all();
    stop_threads();
}

void BaremetalRunner::prepare(Machine& machine) {
    for (auto& hart : machine.secondary_harts_) {
        hart->pipeline_context.pending_exception = std::nullopt;
        hart->pipeline_context.pending_tval = 0;
    }
    machine.primary_hart().pipeline_context.pending_exception = std::nullopt;
    machine.primary_hart().pipeline_context.pending_tval = 0;
}

void BaremetalRunner::execute(Machine& machine) {
    if (machine.runtime_profile.is_cycle_mode()) {
        machine.advance_ca_global_cycle();
    } else if (machine.lockstep() && machine.lockstep()->is_running()) {
        machine.primary_hart().run_cycle(machine);
    } else {
        const uint32_t quantum = machine.config.execution.smp_quantum;
        if (machine.secondary_harts_.empty() || quantum <= 1) {
            machine.primary_hart().run_cycle_baremetal(machine);
            for (auto& hart : machine.secondary_harts_) {
                if (hart->hart_status.load(std::memory_order_relaxed) == HartStatus::Started) {
                    hart->run_cycle_baremetal(machine);
                }
            }
        } else {
            for (uint32_t q = 0; q < quantum && machine.is_running(); ++q) {
                machine.primary_hart().run_cycle_baremetal(machine);
            }
            for (auto& hart : machine.secondary_harts_) {
                if (hart->hart_status.load(std::memory_order_relaxed) == HartStatus::Started) {
                    for (uint32_t q = 0;
                         q < quantum && machine.is_running() &&
                         hart->hart_status.load(std::memory_order_relaxed) == HartStatus::Started;
                         ++q) {
                        hart->run_cycle_baremetal(machine);
                    }
                }
            }
        }
    }
}

auto BaremetalRunner::execute_fast_batch(Machine& machine, uint32_t batch_size) -> bool {
    const auto policy = machine.fast_batch_policy();
    if (!simrv::compiler::likely(policy.has_value())) return false;
    if (policy->has_instruction_limit) {
        if (machine.primary_hart().e_icount >= machine.config.execution.fincnt) {
            machine.stop(Machine::StopReason::InstructionLimit);
            return true;
        }
        batch_size = static_cast<uint32_t>(std::min<Counter>(
            batch_size, machine.config.execution.fincnt - machine.primary_hart().e_icount));
    }
    machine.primary_hart().run_fast_baremetal_batch(machine, batch_size, *policy);
    return true;
}

void BaremetalRunner::finalize(Machine& machine) {
    if (simrv::compiler::unlikely(machine.trace().fp_trace.is_open())) {
        machine.trace().write_trace_snapshot();
    }
    if (simrv::compiler::unlikely(machine.tohost != 0)) machine.finalize_cycle_tohost();
    if (simrv::compiler::unlikely(
            machine.config.execution.fincnt != std::numeric_limits<Counter>::max() &&
            machine.primary_hart().e_icount >= machine.config.execution.fincnt)) {
        simrv::log::info("finished by -e option");
        machine.stop(Machine::StopReason::InstructionLimit);
    }
    if (auto* uart = machine.uart_device(); uart && machine.tui_enabled()) {
        uart->service_interrupts();
    } else if (uart && !uart->is_input_thread_running() &&
               simrv::compiler::unlikely((machine.primary_hart().clint_mmio.mtime & 8191) == 0)) {
        uart->service_interrupts();
    }
}

void OsRunner::start(Machine& machine) {
    if (!machine.config.execution.smp_multithreaded || machine.secondary_harts_.empty() ||
        !machine.runtime_profile.is_instruction_mode()) {
        return;
    }
    stop_threads();
    workers_running_.store(true, std::memory_order_release);
    for (size_t i = 0; i < machine.secondary_harts_.size(); ++i) {
        worker_threads_.emplace_back([&machine, this, i](const std::stop_token& stop_token) {
            auto& hart = *machine.secondary_harts_[i];
            constexpr uint32_t kWorkerBatch = 2048;
            while (!stop_token.stop_requested() && machine.is_running() &&
                   workers_running_.load(std::memory_order_relaxed)) {
                if (hart.hart_status.load(std::memory_order_relaxed) != HartStatus::Started) {
                    hart.hart_status.wait(HartStatus::Stopped, std::memory_order_relaxed);
                    continue;
                }
                if (machine.execution_state_.load(std::memory_order_relaxed) ==
                    ExecutionState::Paused) {
                    machine.execution_state_.wait(ExecutionState::Paused,
                                                  std::memory_order_relaxed);
                    continue;
                }
                for (uint32_t step = 0;
                     step < kWorkerBatch && machine.is_running() &&
                     hart.hart_status.load(std::memory_order_relaxed) == HartStatus::Started;
                     ++step) {
                    hart.run_cycle(machine);
                }
            }
        });
    }
}

void OsRunner::stop_threads() {
    workers_running_.store(false, std::memory_order_release);
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.request_stop();
            thread.join();
        }
    }
    worker_threads_.clear();
}

void OsRunner::stop(Machine& machine) {
    workers_running_.store(false, std::memory_order_release);
    for (auto& hart : machine.secondary_harts_) hart->hart_status.notify_all();
    machine.execution_state_.notify_all();
    stop_threads();
}

void OsRunner::prepare(Machine& machine) {
    for (auto& hart : machine.secondary_harts_) {
        hart->pipeline_context.pending_exception = std::nullopt;
        hart->pipeline_context.pending_tval = 0;
    }
    if (simrv::compiler::likely(machine.runtime_profile.is_instruction_fast() &&
                                machine.primary_hart().clint_mmio.mtime <=
                                    machine.config.execution.enabletimer)) {
        if (simrv::compiler::unlikely(machine.primary_hart().clint_mmio.mtime ==
                                      machine.config.execution.memimg_cycle)) {
            machine.trace().dump_init_artifacts();
        }
    } else if (machine.primary_hart().clint_mmio.mtime == machine.config.execution.memimg_cycle) {
        machine.trace().dump_init_artifacts();
    }
    machine.primary_hart().pipeline_context.pending_exception = std::nullopt;
    machine.primary_hart().pipeline_context.pending_tval = 0;
}

void OsRunner::execute(Machine& machine) {
    if (machine.runtime_profile.is_cycle_mode()) {
        machine.advance_ca_global_cycle();
        return;
    }
    const uint32_t quantum = machine.config.execution.smp_quantum;
    if (machine.secondary_harts_.empty() || quantum <= 1) {
        machine.primary_hart().run_cycle(machine);
        for (auto& hart : machine.secondary_harts_) {
            if (hart->hart_status.load(std::memory_order_relaxed) == HartStatus::Started) {
                hart->run_cycle(machine);
            }
        }
    } else {
        for (uint32_t q = 0; q < quantum && machine.is_running(); ++q) {
            machine.primary_hart().run_cycle(machine);
        }
        for (auto& hart : machine.secondary_harts_) {
            if (hart->hart_status.load(std::memory_order_relaxed) == HartStatus::Started) {
                for (uint32_t q = 0;
                     q < quantum && machine.is_running() &&
                     hart->hart_status.load(std::memory_order_relaxed) == HartStatus::Started;
                     ++q) {
                    hart->run_cycle(machine);
                }
            }
        }
    }
}

auto OsRunner::execute_fast_batch(Machine& machine, uint32_t batch_size) -> bool {
    const auto policy = machine.fast_batch_policy();
    // Sampled TUI execution is safe for a single Linux hart: architectural execution remains
    // instruction-by-instruction, while presentation and runner work move to the batch boundary.
    // Keep every SMP configuration on the detailed scheduler until its ordering contract has an
    // equally explicit parallel batch design.
    if (!policy.has_value() || machine.config.execution.smp_multithreaded ||
        !machine.secondary_harts_.empty()) {
        return false;
    }
    auto& cpu = machine.primary_hart();
    if (policy->has_instruction_limit) {
        if (cpu.e_icount >= machine.config.execution.fincnt) {
            machine.stop(Machine::StopReason::InstructionLimit);
            return true;
        }
        batch_size = static_cast<uint32_t>(
            std::min<Counter>(batch_size, machine.config.execution.fincnt - cpu.e_icount));
    }
    const uint32_t quantum = std::min(batch_size, machine.secondary_harts_.empty() ? 4096u : 2048u);
    for (uint32_t i = 0; i < quantum && machine.is_running(); ++i) cpu.run_cycle(machine);
    // Functional TUI batches do not enter the per-cycle finalizer, so surface pending UART RX at
    // the same explicit boundary that publishes the sampled UI snapshot.
    if (auto* uart = machine.uart_device(); uart && machine.tui_enabled()) {
        uart->service_interrupts();
    }
    return true;
}

void OsRunner::finalize(Machine& machine) {
    auto& cpu = machine.primary_hart();
    const bool trace_window = cpu.clint_mmio.mtime >= machine.config.execution.trace_begin &&
                              cpu.clint_mmio.mtime <= machine.config.execution.trace_end;
    if (simrv::compiler::likely(
            machine.runtime_profile.is_instruction_fast() &&
            (!machine.tui_enabled() || machine.config.execution.ui_worker_threaded) &&
            machine.config.execution.strace == 0 && !trace_window &&
            !machine.branch_trace_enabled())) {
        if (simrv::compiler::unlikely(machine.tohost != 0)) machine.finalize_cycle_tohost();
    } else {
        if (simrv::compiler::unlikely(machine.config.execution.strace != 0 &&
                                      cpu.clint_mmio.mtime >= machine.config.execution.strace))
            machine.trace().emit_periodic_pc_trace(cpu.clint_mmio.mtime,
                                                   cpu.pipeline_context.cpc.raw());
        if (simrv::compiler::unlikely(trace_window)) machine.trace().write_trace_snapshot();
        if (simrv::compiler::unlikely(machine.branch_trace_enabled()))
            machine.trace().emit_branch_prediction_trace(
                cpu.clint_mmio.mtime, cpu.pipeline_context.cpc.raw(), cpu.pipeline_context.jmp_pc,
                cpu.pipeline_context.opcode, cpu.pipeline_context.tkn);
        machine.finalize_cycle_tohost();
    }
    if (simrv::compiler::unlikely(machine.config.execution.fincnt !=
                                      std::numeric_limits<Counter>::max() &&
                                  cpu.e_icount >= machine.config.execution.fincnt)) {
        simrv::log::info("finished by -e option");
        machine.stop(Machine::StopReason::InstructionLimit);
    }
    if (auto* uart = machine.uart_device();
        uart && (machine.tui_enabled() ||
                 (!uart->is_input_thread_running() &&
                  simrv::compiler::unlikely((cpu.clint_mmio.mtime & 8191) == 0)))) {
        uart->service_interrupts();
    }
}

void Machine::reset_state() {
    tohost = 0;
    reboot_requested = false;
    exit_code = 0;
    is_shutdown_ = false;
    is_running_ = true;
    stop_reason_ = StopReason::Running;
    last_tui_check_cycles_ = 0;
    last_tui_update_ = {};
    execution_state_.store(ExecutionState::Running, std::memory_order_release);
    execution_state_.notify_all();
    cpu.reset();
}

auto Machine::add_lifecycle_observer(LifecycleObserver observer) -> LifecycleObserverId {
    std::lock_guard lock(lifecycle_observer_mutex_);
    const auto id = next_lifecycle_observer_id_++;
    lifecycle_observers_.emplace_back(id, std::move(observer));
    return id;
}

void Machine::remove_lifecycle_observer(LifecycleObserverId observer_id) {
    std::lock_guard lock(lifecycle_observer_mutex_);
    std::erase_if(lifecycle_observers_,
                  [observer_id](const auto& entry) { return entry.first == observer_id; });
}

void Machine::publish_lifecycle_event(LifecycleEventKind kind, int exit_status) {
    std::vector<LifecycleObserver> observers;
    {
        std::lock_guard lock(lifecycle_observer_mutex_);
        observers.reserve(lifecycle_observers_.size());
        for (const auto& [_, observer] : lifecycle_observers_) {
            observers.push_back(observer);
        }
    }
    const LifecycleEvent event{
        .kind = kind,
        .instruction_count = cpu.e_icount,
        .exit_status = exit_status,
        .stop_reason = static_cast<uint8_t>(stop_reason()),
    };
    for (const auto& observer : observers) {
        observer(event);
    }
}

auto Machine::is_paused() const -> bool {
    return execution_state_.load(std::memory_order_relaxed) == ExecutionState::Paused ||
           (tui_enabled() && tui && tui->is_paused());
}

void Machine::pause() {
    execution_state_.store(ExecutionState::Paused, std::memory_order_release);
    execution_state_.notify_all();
    if (tui_enabled() && tui) {
        tui->pause_loop();
    }
}

void Machine::resume() {
    if (is_shutdown_) {
        return;
    }
    execution_state_.store(ExecutionState::Running, std::memory_order_release);
    execution_state_.notify_all();
    if (tui_enabled() && tui) {
        tui->unpause_loop();
    }
}

void Machine::step() {
    if (is_shutdown_) {
        return;
    }
    execution_state_.store(ExecutionState::Stepping, std::memory_order_release);
    execution_state_.notify_all();
}

auto Machine::stop_reason_name(StopReason reason) noexcept -> std::string_view {
    switch (reason) {
        case StopReason::Running:
            return "running";
        case StopReason::InstructionLimit:
            return "instruction limit reached";
        case StopReason::TohostPass:
            return "guest tohost pass";
        case StopReason::TohostFail:
            return "guest tohost failure";
        case StopReason::GuestPoweroff:
            return "guest poweroff";
        case StopReason::GuestCrash:
            return "guest crash";
        case StopReason::GuestReboot:
            return "guest reboot";
        case StopReason::GuestExit:
            return "guest exit";
        case StopReason::LockstepDivergence:
            return "lockstep divergence";
        case StopReason::UnhandledTrap:
            return "unhandled trap";
        case StopReason::ExternalStop:
            return "external/user stop";
    }
    return "unknown";
}

void Machine::stop(StopReason reason) {
    stop_reason_ = reason;
    is_shutdown_ = true;
    execution_state_.store(ExecutionState::Stopped, std::memory_order_release);
    execution_state_.notify_all();
    stop_runner();
    if (!tui_enabled()) {
        is_running_ = false;
    }
    if (tui_enabled() && tui) {
        tui->pause_loop();
    }
    publish_lifecycle_event(LifecycleEventKind::Stopped);
}

void Machine::request_reboot() {
    stop_reason_ = StopReason::GuestReboot;
    reboot_requested = true;
    is_running_ = false;
    execution_state_.store(ExecutionState::Stopped, std::memory_order_release);
    execution_state_.notify_all();
    stop_runner();
    publish_lifecycle_event(LifecycleEventKind::RebootRequested);
}

void Machine::request_exit(int status) {
    stop_reason_ = StopReason::GuestExit;
    exit_code = status;
    is_shutdown_ = true;
    is_running_ = false;
    execution_state_.store(ExecutionState::Stopped, std::memory_order_release);
    execution_state_.notify_all();
    stop_runner();
    publish_lifecycle_event(LifecycleEventKind::ExitRequested, status);
}

void Machine::run() {
    publish_lifecycle_event(LifecycleEventKind::Started);
    cpu.evaluate_timer_interrupt();

    // Start the selected composed execution policy.
    start_runner();

    // Start background TUI rendering and input thread if in TUI mode
    if (tui_enabled() && tui && !tui->is_ui_thread_running()) {
        tui->start_ui_thread();
    }

    // Start background stdin input thread for non-TUI mode
    if (uart && !tui_enabled()) {
        uart->start_input_thread();
    }

    // In TUI mode expose the UART through a PTY for optional external terminals.
    if (uart && tui_enabled()) {
        if (uart->start_pty()) {
            simrv::log::info("[UART] PTY slave: {}", uart->pty_slave_path());
        } else {
            simrv::log::warn("[UART] openpty() failed – falling back to direct push_rx_byte");
        }
    }

    while (is_running() &&
           execution_state_.load(std::memory_order_relaxed) != ExecutionState::Stopped) {
        if (tui_enabled() && tui && tui->is_tui_paused()) {
            tui->set_sim_thread_sleeping(true);
            execution_state_.wait(ExecutionState::Paused, std::memory_order_relaxed);
            if (execution_state_.load(std::memory_order_relaxed) == ExecutionState::Paused) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            continue;
        }
        if (tui_enabled() && tui) {
            tui->set_sim_thread_sleeping(false);
        }

        if (execute_runner_fast_batch(runtime_profile.fast_batch_quantum())) {
            if (simrv::compiler::unlikely(tracer.fp_trace.is_open())) {
                tracer.write_trace_snapshot();
            }
            if (simrv::compiler::unlikely(tohost != 0)) {
                finalize_cycle_tohost();
            }
            if (simrv::compiler::unlikely(config.execution.fincnt !=
                                              std::numeric_limits<Counter>::max() &&
                                          cpu.e_icount >= config.execution.fincnt)) {
                simrv::log::info("finished by -e option");
                stop_reason_ = StopReason::InstructionLimit;
                is_running_ = false;
            }
            if (!tui_enabled() && uart && !uart->is_input_thread_running()) {
                uart->service_interrupts();
            }
            continue;
        }

        prepare_runner_cycle();
        execute_runner_cycle();
        finalize_runner_cycle();

        if (tui_enabled() && tui) {
            tui->on_cycle_completed();
        }

        if (is_stepping()) {
            execution_state_.store(ExecutionState::Paused, std::memory_order_release);
            if (tui_enabled() && tui) {
                tui->set_paused(true);
            }
        }

        if (gdb_stub && gdb_stub->is_connected()) {
            if (gdb_stub->single_step()) {
                gdb_stub->notify_breakpoint(*this);
            } else {
                gdb_stub->poll(*this);
            }
        }

        if (!appmode_enabled() && spike_lockstep && spike_lockstep->is_running()) {
            spike_lockstep->compare_and_report(cpu.state(), cpu.pipeline_context.cpc.raw(),
                                               cpu.e_icount);
            if (spike_lockstep->should_halt()) {
                simrv::log::error("Lockstep: halting on divergence");
                stop(StopReason::LockstepDivergence);
            }
        }
    }

    stop_runner();

    // Stop background TUI thread
    if (tui_enabled() && tui) {
        tui->stop_ui_thread();
    }

    // Clean up background input thread
    if (uart && !tui_enabled()) {
        uart->stop_input_thread();
    }
    // Clean up PTY
    if (uart && tui_enabled()) {
        uart->stop_pty();
    }
}

void Machine::finalize_cycle_tohost() {
    if (tohost == 0) {
        return;
    }

    // Standard 64-bit HTIF handling
    const auto dev = static_cast<uint8_t>(tohost >> 56);
    const auto cmd = static_cast<uint8_t>(tohost >> 48);
    const uint64_t payload = tohost & 0x0000FFFFFFFFFFFFULL;

    if (dev == 1 && cmd == 1) {
        // HTIF Console Print
        if (tui_enabled() && tui) {
            tui->handle_char_write(static_cast<char>(payload & 0xff));
        } else {
            std::print("{}", static_cast<char>(payload & 0xff));
            fflush(stdout);
        }
        tohost = 0;
        return;
    }

    // Compatibility for older 32-bit SimRV HTIF protocol:
    // writes of ((CMD_PRINT_CHAR << 16) | c) or (CMD_POWER_OFF << 16)
    if (dev == 0 && cmd == 0) {
        const auto old_cmd = static_cast<uint16_t>(tohost >> 16);
        const auto old_payload = static_cast<uint16_t>(tohost & 0xffffULL);
        if (old_cmd == 1) {  // CMD_PRINT_CHAR
            const char ch = static_cast<char>(old_payload & 0xff);
            if (tui_enabled() && tui) {
                tui->handle_char_write(ch);
            } else {
                std::print("{}", ch);
                fflush(stdout);
            }
            tohost = 0;
            return;
        } else if (old_cmd == 2) {  // CMD_POWER_OFF
            simrv::log::info(
                "[Power] Compatibility: guest requested poweroff via tohost (old protocol).");
            exit_code = 0;
            stop(StopReason::GuestPoweroff);
            tohost = 0;
            return;
        } else {
            // HTIF Syscall handling: payload is a pointer to the syscall block in guest DRAM
            const auto ram = ram_view();
            if (ram.contains(payload, 4 * sizeof(uint64_t))) {
                uint64_t syscall_num = 0;
                uint64_t arg0 = 0;
                uint64_t arg1 = 0;
                uint64_t arg2 = 0;

                const auto* syscall_block = ram.unchecked_ptr(payload);
                std::memcpy(&syscall_num, syscall_block + 0, sizeof(syscall_num));
                std::memcpy(&arg0, syscall_block + 8, sizeof(arg0));
                std::memcpy(&arg1, syscall_block + 16, sizeof(arg1));
                std::memcpy(&arg2, syscall_block + 24, sizeof(arg2));

                if (syscall_num == 64) {  // SYS_write
                    const Address fromhost_addr =
                        (isa_test_tohost() != 0 ? isa_test_tohost() : 0x80001000) + 8;
                    if (!ram.contains(arg1, static_cast<size_t>(arg2)) ||
                        !ram.contains(fromhost_addr, sizeof(uint64_t))) {
                        simrv::log::warn(
                            "HTIF SYS_write references RAM outside the configured DRAM");
                        tohost = 0;
                        return;
                    }
                    const std::span<const Byte> bytes(ram.unchecked_ptr(arg1),
                                                      static_cast<size_t>(arg2));
                    for (const Byte byte : bytes) {
                        const char ch = static_cast<char>(byte);
                        if (tui_enabled() && tui) {
                            tui->handle_char_write(ch);
                        } else {
                            std::print("{}", ch);
                        }
                    }
                    if (!tui_enabled()) {
                        std::fflush(stdout);
                    }

                    // Write success response (bytes written) to fromhost
                    uint64_t resp = arg2;
                    std::memcpy(ram.unchecked_ptr(fromhost_addr), &resp, sizeof(resp));
                    tohost = 0;
                    return;
                } else if (syscall_num == 93) {  // SYS_exit
                    const int code = static_cast<int>(arg0);
                    if (appmode_enabled()) {
                        if (code == 0) {
                            simrv::log::info("ISA TEST PASS");
                        } else {
                            simrv::log::error("ISA TEST FAIL code={}", code);
                        }
                    } else {
                        if (code == 0) {
                            simrv::log::info("Program Halted (SUCCESS / PASS)");
                        } else {
                            simrv::log::error("Program Halted (FAIL / EXIT code={})", code);
                        }
                    }
                    exit_code = code;
                    stop(code == 0 ? StopReason::TohostPass : StopReason::TohostFail);
                    tohost = 0;
                    return;
                }
                tohost = 0;
                return;
            }
        }
    }

    // Universal tohost halting check (e.g. exit code via tohost)
    if (tohost == 1) {
        if (appmode_enabled()) {
            simrv::log::info("ISA TEST PASS");
        } else {
            simrv::log::info("Program Halted (SUCCESS / PASS)");
        }
        exit_code = 0;
        stop(StopReason::TohostPass);
        tohost = 0;
        return;
    } else if ((tohost & 1) != 0u) {
        const int code = static_cast<int>(tohost >> 1);
        if (appmode_enabled()) {
            simrv::log::error("ISA TEST FAIL code={} (tohost=0x{:016x})", code, tohost.load());
        } else {
            simrv::log::error("Program Halted (FAIL / EXIT code={})", code);
        }
        exit_code = code == 0 ? 1 : code;
        stop(StopReason::TohostFail);
        tohost = 0;
        return;
    }
}

Machine::~Machine() = default;

void Machine::advance_ca_global_cycle() {
    cpu.advance_ca_cycle(*this);
    if (simrv::compiler::unlikely(!secondary_harts_.empty())) {
        for (auto& secondary : secondary_harts_) {
            if (secondary->hart_status.load(std::memory_order_relaxed) == HartStatus::Started) {
                secondary->advance_ca_cycle(*this);
            }
        }
    }

    // Shared requests become visible only after every hart has observed the same start-of-cycle
    // platform state. The timer transition follows the interconnect transition and is sampled by
    // hart pipelines at a retirement boundary in the next global cycle.
    memory().system_bus().advance_cycle();
    ++cpu.clint_mmio.rtc_divider;
    if (simrv::compiler::unlikely(cpu.clint_mmio.rtc_divider >= 10)) {
        ++cpu.clint_mmio.mtime;
        cpu.clint_mmio.rtc_divider = 0;
        cpu.evaluate_timer_interrupt();
        if (simrv::compiler::unlikely(!secondary_harts_.empty())) {
            const auto global_time = cpu.clint_mmio.mtime.load(std::memory_order_relaxed);
            for (auto& secondary : secondary_harts_) {
                secondary->clint_mmio.mtime.store(global_time, std::memory_order_relaxed);
                secondary->evaluate_timer_interrupt();
            }
        }
    }
}

}  // namespace simrv::core
