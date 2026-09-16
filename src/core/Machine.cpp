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
#include <ranges>
#include <span>
#include <thread>
#include <variant>

#include "MachineRuntime.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/device/AIA.hpp"
#include "simrv/device/Aclint.hpp"
#include "simrv/device/Power.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/device/pci/PcieRootComplex.hpp"
#include "simrv/memory/CoherenceHub.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/util/BenchmarkEvent.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

namespace {

thread_local bool g_primary_runner_active = false;
thread_local bool g_secondary_runner_active = false;

void wait_for_worker_quiescence(std::atomic<uint32_t>& workers_in_cycle) {
    const uint32_t caller_activity = g_secondary_runner_active ? 1U : 0U;
    auto active = workers_in_cycle.load(std::memory_order_acquire);
    while (active > caller_activity) {
        workers_in_cycle.wait(active, std::memory_order_relaxed);
        active = workers_in_cycle.load(std::memory_order_acquire);
    }
}

class RunnerActivityGuard {
   public:
    explicit RunnerActivityGuard(std::atomic<uint32_t>& runner_in_cycle) noexcept
        : runner_in_cycle_(runner_in_cycle) {
        runner_in_cycle_.fetch_add(1, std::memory_order_acq_rel);
    }

    ~RunnerActivityGuard() {
        runner_in_cycle_.fetch_sub(1, std::memory_order_acq_rel);
        runner_in_cycle_.notify_all();
    }

    RunnerActivityGuard(const RunnerActivityGuard&) = delete;
    auto operator=(const RunnerActivityGuard&) -> RunnerActivityGuard& = delete;

   private:
    std::atomic<uint32_t>& runner_in_cycle_;
};

}  // namespace

RunnerBase::~RunnerBase() { stop_threads(); }

void RunnerBase::wait_for_quiescence() { wait_for_worker_quiescence(workers_in_cycle_); }

void RunnerBase::stop_threads() {
    workers_running_.store(false, std::memory_order_release);
    if (machine_ != nullptr) machine_->notify_control_event();
    const auto self_id = std::this_thread::get_id();
    for (auto& thread : worker_threads_) {
        if (thread.joinable()) {
            thread.request_stop();
            if (thread.get_id() != self_id) {
                thread.join();
            } else {
                thread.detach();
            }
        }
    }
    worker_threads_.clear();
}

Machine::Runtime::Runtime(Machine& machine, bool appmode)
    : tracer(machine), memory(machine), runner(std::in_place_type<BaremetalRunner>) {
    memory.system_bus().router().set_tracer(&tracer);
    if (!appmode) {
        runner.emplace<OsRunner>();
    }
}

Machine::Machine(MachineConfig machine_config)
    : runtime_(std::make_unique<Runtime>(*this, machine_config.execution.appmode)),
      memory_(runtime_->memory) {
    runtime_->primary_cpu.machine_ = this;
    apply_configuration(std::move(machine_config));
}

void Machine::apply_configuration(MachineConfig machine_config) {
    config = std::move(machine_config);
    resolved_start_pc_ = config.execution.start_pc;
    resolved_isatest_tohost_ = config.isa.isatest_tohost;
    if (tui_enabled() || debugger_enabled()) {
        execution_state_.store(ExecutionState::Paused, std::memory_order_release);
    }
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

auto Machine::allocate_ram(size_t bytes) -> bool { return runtime_->ram.allocate(bytes); }

void Machine::release_ram() noexcept { runtime_->ram.reset(); }

void Machine::set_ram_for_testing(Byte* ram, size_t size) noexcept {
    runtime_->ram.reset();
    runtime_->ram.mutable_data_ref() = ram;
    config.memory.dram_size = static_cast<Address>(size);
}

auto Machine::mutable_ram_data_for_testing() noexcept -> Byte*& {
    return runtime_->ram.mutable_data_ref();
}

void Machine::add_hart_for_testing(std::unique_ptr<CPU> hart) {
    hart->machine_ = this;
    runtime_->secondary_harts.push_back(std::move(hart));
}

auto Machine::test_secondary_harts() noexcept -> std::vector<std::unique_ptr<simrv::core::CPU>>& {
    return runtime_->secondary_harts;
}

auto Machine::mutable_uart_for_testing() noexcept -> std::unique_ptr<simrv::device::Uart>& {
    return runtime_->uart;
}

void Machine::install_debugger_for_testing(std::unique_ptr<simrv::debug::GdbStub> debugger) {
    runtime_->gdb_stub = std::move(debugger);
}

auto Machine::hart(size_t index) -> CPU& {
    return index == 0 ? runtime_->primary_cpu : *runtime_->secondary_harts.at(index - 1);
}

auto Machine::hart(size_t index) const -> const CPU& {
    return index == 0 ? runtime_->primary_cpu : *runtime_->secondary_harts.at(index - 1);
}

auto Machine::num_harts() const -> size_t { return 1 + runtime_->secondary_harts.size(); }

auto Machine::primary_hart() noexcept -> CPU& { return runtime_->primary_cpu; }

auto Machine::primary_hart() const noexcept -> const CPU& { return runtime_->primary_cpu; }

auto Machine::ram_data() noexcept -> Byte* { return runtime_->ram.data(); }

auto Machine::ram_data() const noexcept -> const Byte* { return runtime_->ram.data(); }

auto Machine::ram_view() const noexcept -> simrv::memory::RamView {
    return simrv::memory::RamView(runtime_->ram.data(), config.memory.dram_base,
                                  config.memory.dram_size);
}

void Machine::set_platform_irq(IrqNumber irq, bool asserted) {
    runtime_->primary_cpu.plic_set_irq(irq, asserted);
}

void Machine::set_hart_irq(HartId hart_id, InterruptType type, PrivilegeLevel priv, bool asserted) {
    if (hart_id.val >= num_harts()) {
        return;
    }
    auto& target_hart = hart(hart_id);
    switch (type) {
        case InterruptType::Timer:
            if (priv == PrivilegeLevel::Machine) {
                if (asserted) {
                    target_hart.state().mip |= enum_mask(core::MipBit::Mtip);
                } else {
                    target_hart.state().mip &= ~enum_mask(core::MipBit::Mtip);
                }
            } else if (priv == PrivilegeLevel::Supervisor) {
                target_hart.state().stip_timer = asserted;
                target_hart.state().refresh_supervisor_pending();
            }
            break;
        case InterruptType::Software:
            if (priv == PrivilegeLevel::Machine) {
                if (asserted) {
                    target_hart.state().mip |= enum_mask(core::MipBit::Msip);
                } else {
                    target_hart.state().mip &= ~enum_mask(core::MipBit::Msip);
                }
            } else if (priv == PrivilegeLevel::Supervisor) {
                target_hart.state().stip_software = asserted;
                target_hart.state().refresh_supervisor_pending();
            }
            break;
        case InterruptType::External:
            if (priv == PrivilegeLevel::Machine) {
                if (asserted) {
                    target_hart.state().mip |= enum_mask(core::MipBit::Meip);
                } else {
                    target_hart.state().mip &= ~enum_mask(core::MipBit::Meip);
                }
            } else if (priv == PrivilegeLevel::Supervisor) {
                target_hart.state().seip_external = asserted;
                target_hart.state().refresh_supervisor_pending();
            }
            break;
    }
}

auto Machine::platform_time() const noexcept -> uint64_t {
    return runtime_->primary_cpu.clint_mmio.mtime.load(std::memory_order_relaxed);
}

auto Machine::rtc_device() noexcept -> simrv::Rtc* { return runtime_->rtc.get(); }

auto Machine::rtc_device() const noexcept -> const simrv::Rtc* { return runtime_->rtc.get(); }

auto Machine::uart_device() noexcept -> simrv::device::Uart* { return runtime_->uart.get(); }

auto Machine::uart_device() const noexcept -> const simrv::device::Uart* {
    return runtime_->uart.get();
}

auto Machine::pcie_root() noexcept -> simrv::device::PcieRootComplex* {
    return runtime_->pcie.get();
}

auto Machine::pcie_root() const noexcept -> const simrv::device::PcieRootComplex* {
    return runtime_->pcie.get();
}

auto Machine::debugger() noexcept -> simrv::debug::GdbStub* { return runtime_->gdb_stub.get(); }

auto Machine::debugger() const noexcept -> const simrv::debug::GdbStub* {
    return runtime_->gdb_stub.get();
}

auto Machine::lockstep() noexcept -> simrv::debug::SpikeLockstep* {
    return runtime_->spike_lockstep.get();
}

auto Machine::lockstep() const noexcept -> const simrv::debug::SpikeLockstep* {
    return runtime_->spike_lockstep.get();
}

auto Machine::breakpoint_manager() noexcept -> simrv::debug::BreakpointManager& {
    return runtime_->breakpoints;
}

auto Machine::breakpoint_manager() const noexcept -> const simrv::debug::BreakpointManager& {
    return runtime_->breakpoints;
}

auto Machine::trace() noexcept -> Tracer& { return runtime_->tracer; }

auto Machine::trace() const noexcept -> const Tracer& { return runtime_->tracer; }

auto Machine::symbol_table() noexcept -> simrv::debug::SymbolTable& { return runtime_->symbols; }

auto Machine::symbol_table() const noexcept -> const simrv::debug::SymbolTable& {
    return runtime_->symbols;
}

void Machine::start_runner() {
    if (runner_started_.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    std::visit([this](auto& runner) { runner.start(*this); }, runtime_->runner);
}

void Machine::stop_runner() {
    if (!runner_started_.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    std::visit([this](auto& runner) { runner.stop(*this); }, runtime_->runner);
}

void Machine::wait_for_runner_quiescence() {
    if (!g_primary_runner_active) {
        auto active = runner_in_cycle_.load(std::memory_order_acquire);
        while (active != 0) {
            runner_in_cycle_.wait(active, std::memory_order_relaxed);
            active = runner_in_cycle_.load(std::memory_order_acquire);
        }
    }
    std::visit([](auto& runner) { runner.wait_for_quiescence(); }, runtime_->runner);
}

void Machine::prepare_runner_cycle() {
    std::visit([this](auto& runner) { runner.prepare(*this); }, runtime_->runner);
}

void Machine::execute_runner_cycle() {
    RunnerActivityGuard runner_activity(runner_in_cycle_);
    const auto state = execution_state();
    if (state != ExecutionState::Running && state != ExecutionState::Stepping) {
        return;
    }
    g_primary_runner_active = true;
    std::visit([this](auto& runner) { runner.execute(*this); }, runtime_->runner);
    publish_requested_tui_sample(0);
    if (!config.execution.smp_multithreaded) {
        for (size_t h = 1; h < num_harts(); ++h) publish_requested_tui_sample(h);
    }
    if (is_stepping()) publish_tui_execution_snapshot();
    g_primary_runner_active = false;
}

auto Machine::execute_runner_fast_batch(uint32_t batch_size) -> bool {
    RunnerActivityGuard runner_activity(runner_in_cycle_);
    if (execution_state() != ExecutionState::Running) {
        return false;
    }
    g_primary_runner_active = true;
    const bool executed = std::visit(
        [this, batch_size](auto& runner) { return runner.execute_fast_batch(*this, batch_size); },
        runtime_->runner);
    if (executed) {
        publish_requested_tui_sample(0);
        if (!config.execution.smp_multithreaded) {
            for (size_t h = 1; h < num_harts(); ++h) publish_requested_tui_sample(h);
        }
    }
    g_primary_runner_active = false;
    return executed;
}

void Machine::publish_tui_execution_snapshot() noexcept {
    publish_tui_execution_snapshot_for_hart(0);
    if (!config.execution.smp_multithreaded || execution_state() != ExecutionState::Running) {
        for (size_t hart = 1; hart < num_harts(); ++hart) {
            publish_tui_execution_snapshot_for_hart(hart);
        }
    }
}

void Machine::publish_requested_tui_sample(size_t hart_index) noexcept {
    const auto requested = tui_sample_requested_.load(std::memory_order_acquire);
    if (hart_index >= num_harts() || hart_index >= tui_sample_published_.size() ||
        tui_sample_published_[hart_index] == requested)
        return;
    publish_tui_execution_snapshot_for_hart(hart_index);
    tui_sample_published_[hart_index] = requested;
}

void Machine::publish_tui_execution_snapshot_for_hart(size_t hart_index) noexcept {
    if (hart_index >= num_harts() || hart_index >= tui_snapshots_.size()) return;
    const auto& source = hart(hart_index);
    auto snapshot = std::make_shared<TuiExecutionSnapshot>();
    snapshot->hart = hart_index;
    snapshot->pc = source.state().pc;
    snapshot->cycle_count = source.clint_mmio.mcycle;
    snapshot->instruction_count = source.e_icount;
    snapshot->timer_ticks = primary_hart().clint_mmio.mtime.load(std::memory_order_relaxed);
    snapshot->ca_stats = source.pipeline_sim.get_stats();
    snapshot->icache_hits = source.icache.hit_count();
    snapshot->icache_misses = source.icache.miss_count();
    snapshot->dcache_hits = source.dcache.hit_count();
    snapshot->dcache_misses = source.dcache.miss_count();
    snapshot->execution_state = execution_state();
    if (runtime_profile.is_cycle_mode()) {
        const auto reserve = [&](const pipeline::CycleInstructionSlot* entry,
                                 pipeline::PipelineStage stage) {
            if (!entry || !entry->valid) return;
            if (entry->writes_int && entry->wb_dest != RegId::Zero) {
                snapshot->scoreboard.reserve(
                    pipeline::operation::RegBank::Integer, entry->wb_dest, stage,
                    static_cast<LatencyCycles>(entry->remaining_latency), entry->wb_valid);
            } else if (entry->writes_fp) {
                snapshot->scoreboard.reserve(
                    pipeline::operation::RegBank::Float, entry->wb_dest, stage,
                    static_cast<LatencyCycles>(entry->remaining_latency), entry->wb_valid);
            }
        };
        reserve(source.ca_pipeline.writeback, pipeline::PipelineStage::Writeback);
        reserve(source.ca_pipeline.memory, pipeline::PipelineStage::Memory);
        reserve(source.ca_pipeline.execute, pipeline::PipelineStage::Execute);
        reserve(source.ca_pipeline.decode, pipeline::PipelineStage::Decode);
    }
    tui_snapshots_[hart_index].store(std::move(snapshot), std::memory_order_release);
}

auto Machine::tui_execution_snapshot(size_t hart_index) const noexcept -> TuiExecutionSnapshot {
    if (hart_index < tui_snapshots_.size()) {
        if (auto snapshot = tui_snapshots_[hart_index].load(std::memory_order_acquire))
            return *snapshot;
    }
    return TuiExecutionSnapshot{.hart = hart_index};
}

void Machine::post_control(std::function<void(Machine&)> command) {
    {
        std::scoped_lock lock(control_mutex_);
        control_commands_.push_back(std::move(command));
        controls_pending_.store(true, std::memory_order_release);
    }
    notify_control_event();
}

void Machine::service_control_commands() {
    std::deque<std::function<void(Machine&)>> commands;
    {
        std::scoped_lock lock(control_mutex_);
        commands.swap(control_commands_);
        controls_pending_.store(false, std::memory_order_release);
    }
    for (auto& command : commands) command(*this);
}

bool Machine::sampled_instruction_execution() const {
    return runtime_profile.allows_fast_batch() && !is_stepping() && !lockstep_enabled() &&
           !debugger_enabled() && !branch_trace_enabled() && config.execution.strace == 0 &&
           config.execution.trace_begin == std::numeric_limits<Counter>::max() &&
           !breakpoint_manager().has_any() &&
           (!telemetry_sink_ || (!telemetry_sink_->captures_execution_detail() &&
                                 telemetry_sink_->step_delay_us() == 0));
}

void Machine::finalize_runner_cycle() {
    std::visit([this](auto& runner) { runner.finalize(*this); }, runtime_->runner);
}

auto Machine::fast_batch_policy() const -> std::optional<FastBatchPolicy> {
    if (!runtime_profile.allows_fast_batch() || is_stepping() || lockstep_enabled() ||
        branch_trace_enabled() || config.execution.strace != 0 || breakpoint_manager().has_any() ||
        config.execution.smp_multithreaded) {
        return std::nullopt;
    }
    if (tui_enabled() && (!telemetry_sink_ || telemetry_sink_->is_trace_active() ||
                          telemetry_sink_->step_delay_us() != 0)) {
        return std::nullopt;
    }
    const bool captures_execution_detail =
        tui_enabled() && telemetry_sink_ && telemetry_sink_->captures_execution_detail();
    return FastBatchPolicy{
        .copy_pipeline_context = captures_execution_detail,
        .collect_instruction_mix = instruction_mix_enabled() || captures_execution_detail,
        .poll_pause = true,
        .has_instruction_limit = config.execution.fincnt != std::numeric_limits<Counter>::max(),
    };
}

auto Machine::ca_batch_quantum() const noexcept -> uint32_t {
    if (!runtime_profile.is_cycle_mode() || config.execution.smp_multithreaded || is_stepping() ||
        debugger_enabled() || lockstep_enabled() || branch_trace_enabled() ||
        config.execution.strace != 0 ||
        config.execution.trace_begin != std::numeric_limits<Counter>::max() ||
        breakpoint_manager().has_any()) {
        return 1;
    }
    if (tui_enabled() && telemetry_sink_ &&
        (telemetry_sink_->is_trace_active() || telemetry_sink_->step_delay_us() != 0)) {
        return 1;
    }
    return config.execution.smp_quantum;
}

void RunnerBase::start_threads(Machine& machine, bool baremetal) {
    if (!machine.config.execution.smp_multithreaded || machine.runtime_->secondary_harts.empty()) {
        return;
    }
    stop_threads();
    machine_ = &machine;
    workers_running_.store(true, std::memory_order_release);
    for (size_t i = 0; i < machine.runtime_->secondary_harts.size(); ++i) {
        worker_threads_.emplace_back([&machine, this, i,
                                      baremetal](const std::stop_token& stop_token) {
            auto& hart = *machine.runtime_->secondary_harts[i];
            constexpr uint32_t kWorkerBatch = 64;
            while (!stop_token.stop_requested() && machine.is_running() &&
                   workers_running_.load(std::memory_order_relaxed)) {
                if (hart.hart_status.load(std::memory_order_relaxed) != HartStatus::Started ||
                    machine.execution_state() != ExecutionState::Running ||
                    machine.breakpoint_manager().has_any() || machine.debug_pause_requested()) {
                    const auto generation =
                        machine.control_event_generation_.load(std::memory_order_acquire);
                    if (!workers_running_.load(std::memory_order_acquire) ||
                        stop_token.stop_requested() || !machine.is_running())
                        break;
                    if (hart.hart_status.load(std::memory_order_relaxed) != HartStatus::Started ||
                        machine.execution_state() != ExecutionState::Running ||
                        machine.breakpoint_manager().has_any() || machine.debug_pause_requested()) {
                        machine.control_event_generation_.wait(generation,
                                                               std::memory_order_relaxed);
                    }
                    continue;
                }
                for (uint32_t step = 0;
                     step < kWorkerBatch && machine.is_running() &&
                     machine.execution_state() == ExecutionState::Running &&
                     !machine.breakpoint_manager().has_any() && !machine.debug_pause_requested() &&
                     hart.hart_status.load(std::memory_order_relaxed) == HartStatus::Started;
                     ++step) {
                    workers_in_cycle_.fetch_add(1, std::memory_order_acq_rel);
                    if (machine.execution_state() != ExecutionState::Running) {
                        workers_in_cycle_.fetch_sub(1, std::memory_order_acq_rel);
                        workers_in_cycle_.notify_all();
                        break;
                    }
                    g_secondary_runner_active = true;
                    hart.pipeline_context.pending_exception = std::nullopt;
                    hart.pipeline_context.pending_tval = 0;
                    if (machine.runtime_profile.is_cycle_mode()) {
                        hart.evaluate_timer_interrupt();
                        hart.advance_ca_cycle(machine);
                    } else if (baremetal) {
                        hart.run_cycle_baremetal(machine);
                    } else {
                        hart.run_cycle(machine);
                    }
                    if (step + 1 == kWorkerBatch ||
                        hart.hart_status.load(std::memory_order_relaxed) != HartStatus::Started) {
                        machine.publish_requested_tui_sample(i + 1);
                    }
                    g_secondary_runner_active = false;
                    workers_in_cycle_.fetch_sub(1, std::memory_order_acq_rel);
                    workers_in_cycle_.notify_all();
                }
            }
        });
    }
}

void BaremetalRunner::start(Machine& machine) { start_threads(machine, true); }

void RunnerBase::stop(Machine& machine) {
    workers_running_.store(false, std::memory_order_release);
    for (auto& hart : machine.runtime_->secondary_harts) hart->hart_status.notify_all();
    machine.execution_state_.notify_all();
    stop_threads();
}

void RunnerBase::reset_runner_transients(Machine& machine) {
    if (!machine.config.execution.smp_multithreaded || machine.breakpoint_manager().has_any()) {
        for (auto& hart : machine.runtime_->secondary_harts) {
            hart->pipeline_context.pending_exception = std::nullopt;
            hart->pipeline_context.pending_tval = 0;
        }
    }
    machine.primary_hart().pipeline_context.pending_exception = std::nullopt;
    machine.primary_hart().pipeline_context.pending_tval = 0;
}

void RunnerBase::execute_ca_batch(Machine& machine) {
    if (machine.config.execution.smp_multithreaded && !machine.is_stepping() &&
        !machine.breakpoint_manager().has_any()) {
        machine.advance_ca_primary_cycle();
        return;
    }
    const uint32_t quantum = machine.ca_batch_quantum();
    for (uint32_t cycle = 0; cycle < quantum && machine.is_running(); ++cycle) {
        if (cycle != 0 && machine.execution_state() != ExecutionState::Running) break;
        machine.advance_ca_global_cycle();
        if (machine.tohost != 0 ||
            (machine.config.execution.fincnt != std::numeric_limits<Counter>::max() &&
             machine.retired_instruction_count() >= machine.config.execution.fincnt)) {
            break;
        }
    }
}

void RunnerBase::execute_instruction_smp(Machine& machine, bool baremetal) {
    const auto run_hart = [&](CPU& hart) {
        if (baremetal) {
            hart.run_cycle_baremetal(machine);
        } else {
            hart.run_cycle(machine);
        }
    };
    if (machine.config.execution.smp_multithreaded && !machine.is_stepping() &&
        !machine.breakpoint_manager().has_any()) {
        run_hart(machine.primary_hart());
        return;
    }
    const uint32_t quantum = (machine.is_stepping() || machine.debugger_enabled() ||
                              machine.breakpoint_manager().has_any())
                                 ? 1
                                 : machine.config.execution.smp_quantum;
    const auto run_round = [&] {
        run_hart(machine.primary_hart());
        for (auto& hart : machine.runtime_->secondary_harts) {
            if (hart->hart_status.load(std::memory_order_relaxed) == HartStatus::Started) {
                run_hart(*hart);
            }
        }
    };
    if (machine.runtime_->secondary_harts.empty() || quantum <= 1) {
        run_round();
        return;
    }
    for (uint32_t q = 0; q < quantum && machine.is_running(); ++q) {
        run_round();
        if (machine.retired_instruction_count() >= machine.config.execution.fincnt) break;
    }
}

void BaremetalRunner::prepare(Machine& machine) { reset_runner_transients(machine); }

void BaremetalRunner::execute(Machine& machine) {
    if (machine.runtime_profile.is_cycle_mode()) {
        execute_ca_batch(machine);
    } else if (machine.lockstep() && machine.lockstep()->is_running()) {
        machine.primary_hart().run_cycle(machine);
    } else {
        execute_instruction_smp(machine, true);
    }
}

auto BaremetalRunner::execute_fast_batch(Machine& machine, uint32_t batch_size) -> bool {
    const auto policy = machine.fast_batch_policy();
    if (!simrv::compiler::likely(policy.has_value())) return false;
    if (policy->has_instruction_limit) {
        if (machine.retired_instruction_count() >= machine.config.execution.fincnt) {
            machine.stop(Machine::StopReason::InstructionLimit);
            return true;
        }
        batch_size = static_cast<uint32_t>(std::min<Counter>(
            batch_size, machine.config.execution.fincnt - machine.retired_instruction_count()));
    }
    const uint32_t quantum =
        machine.runtime_->secondary_harts.empty()
            ? batch_size
            : std::min(batch_size, static_cast<uint32_t>(machine.config.execution.smp_quantum));
    machine.primary_hart().run_fast_baremetal_batch(machine, quantum, *policy);
    for (auto& sec : machine.runtime_->secondary_harts) {
        if (!machine.is_running()) break;
        if (sec->hart_status.load(std::memory_order_relaxed) == HartStatus::Started) {
            uint32_t sec_batch = quantum;
            if (policy->has_instruction_limit) {
                if (machine.retired_instruction_count() >= machine.config.execution.fincnt) {
                    machine.stop(Machine::StopReason::InstructionLimit);
                    break;
                }
                sec_batch = static_cast<uint32_t>(std::min<Counter>(
                    sec_batch,
                    machine.config.execution.fincnt - machine.retired_instruction_count()));
            }
            sec->run_fast_baremetal_batch(machine, sec_batch, *policy);
        }
    }
    return true;
}

void BaremetalRunner::finalize(Machine& machine) {
    if (simrv::compiler::unlikely(machine.trace().fp_trace.is_open())) {
        machine.trace().write_trace_snapshot();
    }
    if (simrv::compiler::unlikely(machine.tohost != 0)) machine.finalize_cycle_tohost();
    if (simrv::compiler::unlikely(
            machine.config.execution.fincnt != std::numeric_limits<Counter>::max() &&
            machine.retired_instruction_count() >= machine.config.execution.fincnt)) {
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

void OsRunner::start(Machine& machine) { start_threads(machine, false); }

void OsRunner::prepare(Machine& machine) {
    reset_runner_transients(machine);
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
}

void OsRunner::execute(Machine& machine) {
    if (machine.runtime_profile.is_cycle_mode()) {
        execute_ca_batch(machine);
        return;
    }
    execute_instruction_smp(machine, false);
}

auto OsRunner::execute_fast_batch(Machine& machine, uint32_t batch_size) -> bool {
    const auto policy = machine.fast_batch_policy();
    // Sampled TUI execution is safe for a single Linux hart: architectural execution remains
    // instruction-by-instruction, while presentation and runner work move to the batch boundary.
    // Keep every SMP configuration on the detailed scheduler until its ordering contract has an
    // equally explicit parallel batch design.
    if (!policy.has_value() || machine.config.execution.smp_multithreaded ||
        !machine.runtime_->secondary_harts.empty()) {
        return false;
    }
    auto& cpu = machine.primary_hart();
    if (policy->has_instruction_limit) {
        if (machine.retired_instruction_count() >= machine.config.execution.fincnt) {
            machine.stop(Machine::StopReason::InstructionLimit);
            return true;
        }
        batch_size = static_cast<uint32_t>(std::min<Counter>(
            batch_size, machine.config.execution.fincnt - machine.retired_instruction_count()));
    }
    const uint32_t quantum =
        machine.runtime_->secondary_harts.empty()
            ? batch_size
            : std::min(batch_size, static_cast<uint32_t>(machine.config.execution.smp_quantum));
    cpu.run_fast_os_batch(machine, quantum, *policy);
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
    if (simrv::compiler::unlikely(
            machine.config.execution.fincnt != std::numeric_limits<Counter>::max() &&
            machine.retired_instruction_count() >= machine.config.execution.fincnt)) {
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
    retired_instruction_count_.store(0, std::memory_order_relaxed);
    last_tui_check_cycles_ = 0;
    last_tui_update_ = {};
    execution_state_.store(
        tui_enabled() || debugger_enabled() ? ExecutionState::Paused : ExecutionState::Running,
        std::memory_order_release);
    execution_state_.notify_all();
    primary_hart().reset();
}

void Machine::switch_execution_engine(ExecutionEngine engine) {
    if (runtime_profile.engine == engine) return;
    if (runner_started_.load(std::memory_order_acquire) && is_running()) {
        post_control([engine](Machine& m) { m.switch_execution_engine_sync(engine); });
    } else {
        switch_execution_engine_sync(engine);
    }
}

void Machine::switch_execution_engine_sync(ExecutionEngine engine) {
    if (runtime_profile.engine == engine) return;

    const bool to_cycle = is_cycle_engine(engine);
    for (size_t i = 0; i < num_harts(); ++i) {
        auto& cpu = hart(i);
        const auto hart_id = static_cast<HartId>(cpu.state().mhartid);
        memory().system_bus().cancel_source(
            simrv::memory::make_tl_source(hart_id, simrv::memory::TlPort::Instruction));
        memory().system_bus().cancel_source(
            simrv::memory::make_tl_source(hart_id, simrv::memory::TlPort::Data));
        cpu.ca_pipeline.reset();
        cpu.ca_state.reset_instruction();
        if (to_cycle) {
            cpu.icache.flush(true);
            cpu.dcache.flush(true);
            cpu.branch_predictor.configure(cpu.pipeline_sim.config.branch_predictor);
            cpu.branch_predictor.reset();
            cpu.pipeline_sim.config.record_snapshots = is_observable_engine(engine);
        } else {
            cpu.pipeline_sim.config.record_snapshots = false;
            cpu.decode_cache.flush();
        }
    }

    if (to_cycle) {
        memory().system_bus().coherence_hub().clear();
    }

    runtime_profile.engine = engine;
    publish_tui_execution_snapshot();

    simrv::log::info("Switched execution mode to {}", runtime_profile.execution_name());
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
        .instruction_count = primary_hart().e_icount,
        .exit_status = exit_status,
        .stop_reason = static_cast<uint8_t>(stop_reason()),
    };
    for (const auto& observer : observers) {
        observer(event);
    }
}

auto Machine::is_paused() const -> bool {
    return execution_state_.load(std::memory_order_relaxed) == ExecutionState::Paused ||
           (tui_enabled() && telemetry_sink_ && telemetry_sink_->is_paused());
}

void Machine::notify_control_event() noexcept {
    control_event_generation_.fetch_add(1, std::memory_order_release);
    control_event_generation_.notify_all();
}

auto Machine::debug_pause_requested() const noexcept -> bool {
    return debug_halt_pending_.load(std::memory_order_acquire) ||
           (runtime_->gdb_stub && runtime_->gdb_stub->pause_requested());
}

void Machine::acknowledge_step() {
    {
        const std::lock_guard lock(step_mutex_);
        ++step_ack_count_;
    }
    step_cv_.notify_all();
}

void Machine::pause() {
    execution_state_.store(ExecutionState::Paused, std::memory_order_release);
    execution_state_.notify_all();
    for (auto& hart : runtime_->secondary_harts) hart->hart_status.notify_all();
    notify_control_event();
    wait_for_runner_quiescence();
    if (telemetry_sink_ || tui_enabled()) publish_tui_execution_snapshot();
    if (telemetry_sink_) {
        telemetry_sink_->pause_loop();
    }
    acknowledge_step();
}

void Machine::resume() {
    if (is_shutdown_) {
        return;
    }
    debug_step_hart_.reset();
    execution_state_.store(ExecutionState::Running, std::memory_order_release);
    execution_state_.notify_all();
    for (auto& hart : runtime_->secondary_harts) hart->hart_status.notify_all();
    notify_control_event();
    if (telemetry_sink_) {
        telemetry_sink_->unpause_loop();
    }
    simrv::util::benchmark_event("resumed", retired_instruction_count());
}

void Machine::step() {
    if (is_shutdown_) {
        return;
    }
    execution_state_.store(ExecutionState::Stepping, std::memory_order_release);
    execution_state_.notify_all();
    for (auto& hart : runtime_->secondary_harts) hart->hart_status.notify_all();
    notify_control_event();
}

auto Machine::begin_debug_step(HartId hart) -> bool {
    if (is_shutdown_ || hart.raw() >= num_harts()) return false;
    if (this->hart(hart).hart_status.load(std::memory_order_acquire) != HartStatus::Started) {
        return false;
    }
    debug_step_hart_ = hart;
    debug_step_target_ = this->hart(hart).e_icount + 1;
    execution_state_.store(ExecutionState::Stepping, std::memory_order_release);
    execution_state_.notify_all();
    notify_control_event();
    return true;
}

void Machine::enqueue_debug_halt(PendingDebugHalt halt) {
    {
        const std::lock_guard lock(debug_halt_mutex_);
        if (!pending_debug_halt_) pending_debug_halt_ = std::move(halt);
        debug_halt_pending_.store(true, std::memory_order_release);
    }
    notify_control_event();
}

void Machine::debug_halt(HartId hart, GdbSignal signal, std::string reason) {
    if ((!runtime_->gdb_stub || !runtime_->gdb_stub->is_connected()) && !tui_enabled()) return;
    enqueue_debug_halt(
        {.hart = hart, .signal = signal, .reason = std::move(reason), .description = {}});
}

void Machine::debug_watch_hit(HartId hart, GdbSignal signal, std::string reason,
                              std::string description) {
    enqueue_debug_halt({.hart = hart,
                        .signal = signal,
                        .reason = std::move(reason),
                        .description = std::move(description),
                        .waits_for_memory = true,
                        .retirement_target = this->hart(hart).e_icount + 1});
}

void Machine::service_debug_halt() {
    if (!debug_halt_pending_.load(std::memory_order_acquire)) return;

    PendingDebugHalt halt;
    {
        const std::lock_guard lock(debug_halt_mutex_);
        if (!pending_debug_halt_) return;
        if (pending_debug_halt_->waits_for_memory) {
            auto& target = hart(pending_debug_halt_->hart);
            if (target.e_icount < pending_debug_halt_->retirement_target) return;
            if (target.active_context().pending_exception.has_value()) {
                pending_debug_halt_.reset();
                debug_halt_pending_.store(false, std::memory_order_release);
                return;
            }
        }
        halt = std::move(*pending_debug_halt_);
        pending_debug_halt_.reset();
    }

    if (telemetry_sink_ && !halt.description.empty()) {
        telemetry_sink_->set_status_override(halt.description);
    }
    pause();
    {
        const std::lock_guard lock(debug_halt_mutex_);
        debug_halt_pending_.store(pending_debug_halt_.has_value(), std::memory_order_release);
    }
    if (runtime_->gdb_stub && runtime_->gdb_stub->is_connected()) {
        runtime_->gdb_stub->notify_stop(halt.hart, halt.signal, std::move(halt.reason));
    }
}

void Machine::step_sync(std::chrono::milliseconds timeout) {
    if (is_shutdown_) {
        return;
    }
    std::unique_lock lock(step_mutex_);
    const auto target_ack = step_ack_count_ + 1;
    step();
    step_cv_.wait_for(lock, timeout, [&] {
        return step_ack_count_ >= target_ack || !is_running() || is_stopped();
    });
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
    for (auto& hart : runtime_->secondary_harts) hart->hart_status.notify_all();
    notify_control_event();
    stop_runner();
    if (telemetry_sink_ || tui_enabled()) publish_tui_execution_snapshot();
    if (!tui_enabled() && !persistent_control_) {
        is_running_ = false;
    }
    if (telemetry_sink_) {
        telemetry_sink_->pause_loop();
    }
    acknowledge_step();
    publish_lifecycle_event(LifecycleEventKind::Stopped);
}

void Machine::request_reboot() {
    stop_reason_ = StopReason::GuestReboot;
    reboot_requested = true;
    is_running_ = false;
    execution_state_.store(ExecutionState::Stopped, std::memory_order_release);
    execution_state_.notify_all();
    for (auto& hart : runtime_->secondary_harts) hart->hart_status.notify_all();
    notify_control_event();
    stop_runner();
    acknowledge_step();
    publish_lifecycle_event(LifecycleEventKind::RebootRequested);
}

void Machine::request_exit(int status) {
    stop_reason_ = StopReason::GuestExit;
    exit_code = status;
    is_shutdown_ = true;
    is_running_ = false;
    execution_state_.store(ExecutionState::Stopped, std::memory_order_release);
    execution_state_.notify_all();
    for (auto& hart : runtime_->secondary_harts) hart->hart_status.notify_all();
    notify_control_event();
    stop_runner();
    acknowledge_step();
    publish_lifecycle_event(LifecycleEventKind::ExitRequested, status);
}

void Machine::run() {
    if (platform_time() == 0 && runtime_->rtc) {
        runtime_->rtc->sync_with_system_time();
    }
    publish_lifecycle_event(LifecycleEventKind::Started);
    primary_hart().evaluate_timer_interrupt();

    // Start the selected composed execution policy.
    start_runner();

    if (tui_enabled() && execution_state() != ExecutionState::Stepping) {
        execution_state_.store(ExecutionState::Paused, std::memory_order_release);
    }

    // Start background TUI rendering and input thread if in TUI mode
    if (telemetry_sink_ && !telemetry_sink_->is_ui_thread_running()) {
        telemetry_sink_->start_ui_thread();
    }

    // Start background stdin input thread for non-TUI mode
    if (auto* uart = uart_device(); uart && !tui_enabled() && !persistent_control_) {
        uart->start_input_thread();
    }

    // In TUI mode expose the UART through a PTY for optional external terminals.
    if (auto* uart = uart_device(); uart && tui_enabled()) {
        if (uart->start_pty()) {
            simrv::log::info("[UART] PTY slave: {}", uart->pty_slave_path());
        } else {
            simrv::log::warn("[UART] openpty() failed – falling back to direct push_rx_byte");
        }
    }

    simrv::util::benchmark_event("ready", retired_instruction_count());
    if (!is_paused()) simrv::util::benchmark_event("resumed", retired_instruction_count());
    while (is_running() &&
           (persistent_control_ ||
            execution_state_.load(std::memory_order_relaxed) != ExecutionState::Stopped)) {
        service_control_commands();
        if (!is_running()) break;
        if (persistent_control_ && is_stopped()) {
            const auto generation = control_event_generation_.load(std::memory_order_acquire);
            if (!has_control_commands()) control_event_generation_.wait(generation);
            continue;
        }
        if (runtime_->gdb_stub) runtime_->gdb_stub->service_pending(*this);
        service_debug_halt();
        if (runtime_->gdb_stub && runtime_->gdb_stub->pause_requested() &&
            execution_state() == ExecutionState::Running)
            pause();

        if (is_paused() && !is_stepping()) {
            if (execution_state() != ExecutionState::Paused) {
                execution_state_.store(ExecutionState::Paused, std::memory_order_release);
            }
            if (telemetry_sink_) telemetry_sink_->set_sim_thread_sleeping(true);
            if (runtime_->gdb_stub) runtime_->gdb_stub->service_pending(*this);
            if (execution_state() != ExecutionState::Paused) continue;

            const auto generation = control_event_generation_.load(std::memory_order_acquire);
            if (execution_state() == ExecutionState::Paused && !has_control_commands() &&
                (!runtime_->gdb_stub || !runtime_->gdb_stub->has_pending_commands())) {
                control_event_generation_.wait(generation, std::memory_order_relaxed);
            }
            continue;
        }
        if (telemetry_sink_) {
            telemetry_sink_->set_sim_thread_sleeping(false);
        }

        if (execute_runner_fast_batch(runtime_profile.fast_batch_quantum())) {
            if (simrv::compiler::unlikely(trace().fp_trace.is_open())) {
                trace().write_trace_snapshot();
            }
            if (simrv::compiler::unlikely(tohost != 0)) {
                finalize_cycle_tohost();
            }
            if (simrv::compiler::unlikely(config.execution.fincnt !=
                                              std::numeric_limits<Counter>::max() &&
                                          retired_instruction_count() >= config.execution.fincnt)) {
                simrv::log::info("finished by -e option");
                if (persistent_control_) {
                    stop(StopReason::InstructionLimit);
                    simrv::util::benchmark_event("stopped", retired_instruction_count());
                } else {
                    stop_reason_ = StopReason::InstructionLimit;
                    is_running_ = false;
                }
            }
            if (auto* uart = uart_device();
                !tui_enabled() && uart && !uart->is_input_thread_running()) {
                uart->service_interrupts();
            }
            if (runtime_->gdb_stub) runtime_->gdb_stub->service_pending(*this);
            continue;
        }

        prepare_runner_cycle();
        execute_runner_cycle();
        finalize_runner_cycle();

        if (simrv::compiler::unlikely(tohost != 0)) {
            finalize_cycle_tohost();
        }
        if (simrv::compiler::unlikely(config.execution.fincnt !=
                                          std::numeric_limits<Counter>::max() &&
                                      retired_instruction_count() >= config.execution.fincnt)) {
            simrv::log::info("finished by -e option");
            if (persistent_control_) {
                stop(StopReason::InstructionLimit);
                simrv::util::benchmark_event("stopped", retired_instruction_count());
            } else {
                stop_reason_ = StopReason::InstructionLimit;
                is_running_ = false;
            }
        }

        if (telemetry_sink_) {
            telemetry_sink_->on_cycle_completed();
        }

        service_debug_halt();

        bool step_complete = is_stepping();
        HartId completed_hart{0};
        if (step_complete && debug_step_hart_.has_value()) {
            completed_hart = *debug_step_hart_;
            step_complete = hart(completed_hart).e_icount >= debug_step_target_;
        }
        if (step_complete) {
            execution_state_.store(ExecutionState::Paused, std::memory_order_release);
            execution_state_.notify_all();
            notify_control_event();
            if (telemetry_sink_) {
                telemetry_sink_->set_paused(true);
            }
            if (debug_step_hart_.has_value() && runtime_->gdb_stub) {
                debug_step_hart_.reset();
                runtime_->gdb_stub->notify_stop(completed_hart, GdbSignal::SigTrap);
            }
            wait_for_runner_quiescence();
            if (telemetry_sink_ || tui_enabled()) publish_tui_execution_snapshot();
            acknowledge_step();
            if (step_completion_) {
                auto completed = std::move(step_completion_);
                completed(*this);
            }
        }

        if (runtime_->gdb_stub) runtime_->gdb_stub->service_pending(*this);

        if (!appmode_enabled() && runtime_->spike_lockstep &&
            runtime_->spike_lockstep->is_running()) {
            runtime_->spike_lockstep->compare_and_report(primary_hart().state(),
                                                         primary_hart().pipeline_context.cpc.raw(),
                                                         primary_hart().e_icount);
            if (runtime_->spike_lockstep->should_halt()) {
                simrv::log::error("Lockstep: halting on divergence");
                stop(StopReason::LockstepDivergence);
            }
        }
    }

    stop_runner();
    if (telemetry_sink_ || tui_enabled()) publish_tui_execution_snapshot();
    simrv::util::benchmark_event("stopped", retired_instruction_count());

    // Stop background TUI thread
    if (telemetry_sink_) {
        telemetry_sink_->stop_ui_thread();
    }

    // Clean up background input thread
    if (auto* uart = uart_device(); uart && !tui_enabled()) {
        uart->stop_input_thread();
    }
    // Clean up PTY
    if (auto* uart = uart_device(); uart && tui_enabled()) {
        uart->stop_pty();
    }
    if (runtime_->gdb_stub) runtime_->gdb_stub->stop();
}

void Machine::console_write(char ch) {
    if (console_sink_) {
        console_sink_->handle_char_write(ch);
    } else {
        std::print("{}", ch);
        fflush(stdout);
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
        console_write(static_cast<char>(payload & 0xff));
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
            console_write(ch);
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
                        console_write(static_cast<char>(byte));
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

Machine::~Machine() {
    if (runtime_->gdb_stub) runtime_->gdb_stub->stop();
    stop_runner();
    // Destroy callbacks and runner objects while their Machine wake state is still alive.
    runtime_.reset();
}

void Machine::advance_ca_global_cycle() {
    primary_hart().advance_ca_cycle(*this);
    if (simrv::compiler::unlikely(!runtime_->secondary_harts.empty())) {
        for (const auto [i, secondary] : std::views::enumerate(runtime_->secondary_harts)) {
            if (secondary->hart_status.load(std::memory_order_relaxed) == HartStatus::Started) {
                secondary->advance_ca_cycle(*this);
            }
        }
    }

    advance_ca_platform_cycle(true);
}

void Machine::advance_ca_primary_cycle() {
    primary_hart().advance_ca_cycle(*this);
    advance_ca_platform_cycle(false);
}

void Machine::advance_ca_platform_cycle(bool synchronize_secondary_harts) {
    // Shared requests become visible after the scheduler's current hart transition(s). In
    // best-effort MT mode hart 0 owns this clock while secondary pipelines progress
    // independently. The timer transition follows the interconnect transition and is
    // sampled by hart pipelines at a retirement boundary in the next global cycle.
    auto& cpu = primary_hart();
    memory().system_bus().advance_cycle();
    ++cpu.clint_mmio.rtc_divider;
    if (simrv::compiler::unlikely(cpu.clint_mmio.rtc_divider >= 10)) {
        ++cpu.clint_mmio.mtime;
        cpu.clint_mmio.rtc_divider = 0;
        cpu.evaluate_timer_interrupt();
        if (synchronize_secondary_harts &&
            simrv::compiler::unlikely(!runtime_->secondary_harts.empty())) {
            const auto global_time = cpu.clint_mmio.mtime.load(std::memory_order_relaxed);
            for (auto& secondary : runtime_->secondary_harts) {
                secondary->clint_mmio.mtime.store(global_time, std::memory_order_relaxed);
                secondary->evaluate_timer_interrupt();
            }
        }
    }
}

}  // namespace simrv::core
