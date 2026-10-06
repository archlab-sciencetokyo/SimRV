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
#include <filesystem>
#include <fstream>
#include <limits>
#include <new>
#include <print>
#include <ranges>
#include <span>
#include <string_view>
#include <thread>
#include <type_traits>
#include <variant>

#include "MachineRuntime.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/PlatformTiming.hpp"
#include "simrv/device/AIA.hpp"
#include "simrv/device/Aclint.hpp"
#include "simrv/device/Power.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/device/pci/PcieRootComplex.hpp"
#include "simrv/memory/CoherenceHub.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/util/BenchmarkEvent.hpp"
#include "simrv/util/Sha256.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

namespace {

struct CheckpointHeader {
    char magic[8] = {'S', 'I', 'M', 'R', 'V', 'C', 'P', 0};
    uint32_t version = 2;
    uint32_t endian_marker = 0x01020304;
    uint32_t header_size = 0;
    uint32_t arch_state_size = 0;
    uint32_t counter_size = 0;
    uint32_t xlen = 0;
    uint32_t vlen = 0;
    uint32_t harts = 0;
    uint64_t ram_base = 0;
    uint64_t ram_size = 0;
};

static_assert(std::is_trivially_copyable_v<CheckpointHeader>);
static_assert(std::is_trivially_copyable_v<ArchState>);

struct CheckpointHart {
    ArchState state{};
    Counter instruction_count{};
    Counter cycle_count{};
    Counter mcycle{};
};

static_assert(std::is_trivially_copyable_v<CheckpointHart>);

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
    : axi_bridge(std::make_unique<simrv::memory::Axi4Bridge>(&machine)),
      tracer(machine),
      memory(machine),
      runner(std::in_place_type<BaremetalRunner>) {
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
    if (runtime_->axi_bridge != nullptr) {
        runtime_->axi_bridge->tracer().set_path(
            (std::filesystem::path(config.debug.trace_dir) / "axi.txt").string());
    }
    resolved_start_pc_ = config.execution.start_pc;
    resolved_isatest_tohost_ = config.isa.isatest_tohost;
    memory_.system_bus().set_smp_enabled(config.execution.smp_multithreaded);
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
    return {runtime_->ram.data(), config.memory.dram_base, config.memory.dram_size};
}

auto Machine::framebuffer_view() const noexcept -> simrv::memory::RamView {
    constexpr Address base = 0x84000000ULL;
    constexpr Address size = 640ULL * 480ULL * 4ULL;
    const auto ram = ram_view();
    if (!ram.contains(base, static_cast<std::size_t>(size))) return {};
    return {ram.unchecked_ptr(base), base, size};
}

auto Machine::save_checkpoint(const std::string& filepath) const
    -> std::expected<void, std::string> {
    const auto ram = ram_view();
    if (ram.data() == nullptr || ram.size() == 0) {
        return std::unexpected("cannot save checkpoint without allocated RAM");
    }
    std::ofstream out(filepath, std::ios::binary | std::ios::trunc);
    if (!out) return std::unexpected("cannot open checkpoint for writing: " + filepath);

    const CheckpointHeader header{.header_size = sizeof(CheckpointHeader),
                                  .arch_state_size = sizeof(ArchState),
                                  .counter_size = sizeof(Counter),
                                  .xlen = simrv::xlen::kXLenBits,
                                  .vlen = config.isa.vlen,
                                  .harts = static_cast<uint32_t>(num_harts()),
                                  .ram_base = config.memory.dram_base,
                                  .ram_size = config.memory.dram_size};
    out.write(reinterpret_cast<const char*>(&header), sizeof(header));
    for (size_t i = 0; i < num_harts(); ++i) {
        const auto& cpu = hart(i);
        const CheckpointHart hart_state{.state = cpu.state(),
                                        .instruction_count = cpu.e_icount,
                                        .cycle_count = cpu.e_ccount,
                                        .mcycle = cpu.clint_mmio.mcycle};
        out.write(reinterpret_cast<const char*>(&hart_state), sizeof(hart_state));
    }
    out.write(reinterpret_cast<const char*>(ram.data()), static_cast<std::streamsize>(ram.size()));
    if (!out) return std::unexpected("failed while writing checkpoint: " + filepath);
    return {};
}

auto Machine::load_checkpoint(const std::string& filepath) -> std::expected<void, std::string> {
    std::ifstream in(filepath, std::ios::binary);
    if (!in) return std::unexpected("cannot open checkpoint for reading: " + filepath);

    CheckpointHeader header{};
    in.read(reinterpret_cast<char*>(&header), sizeof(header));
    const CheckpointHeader expected{};
    if (!in || std::memcmp(header.magic, expected.magic, sizeof(header.magic)) != 0 ||
        header.version != 2 || header.endian_marker != 0x01020304 ||
        header.header_size != sizeof(CheckpointHeader) ||
        header.arch_state_size != sizeof(ArchState) || header.counter_size != sizeof(Counter)) {
        return std::unexpected("unsupported or corrupt checkpoint header");
    }
    if (header.xlen != simrv::xlen::kXLenBits || header.vlen != config.isa.vlen ||
        header.harts != num_harts() || header.ram_base != config.memory.dram_base ||
        header.ram_size != config.memory.dram_size) {
        return std::unexpected("checkpoint configuration does not match this machine");
    }
    std::vector<CheckpointHart> hart_states(num_harts());
    for (auto& hart_state : hart_states) {
        in.read(reinterpret_cast<char*>(&hart_state), sizeof(hart_state));
        if (!in) return std::unexpected("truncated checkpoint architectural state");
    }
    const auto ram = ram_view();
    if (ram.data() == nullptr || ram.size() == 0) {
        return std::unexpected("cannot load checkpoint without allocated RAM");
    }
    std::vector<Byte> ram_image(ram.size());
    in.read(reinterpret_cast<char*>(ram_image.data()),
            static_cast<std::streamsize>(ram_image.size()));
    if (!in) return std::unexpected("truncated checkpoint RAM image");
    for (size_t i = 0; i < num_harts(); ++i) {
        auto& cpu = hart(i);
        auto& hart_state = hart_states[i];
        cpu.state() = hart_state.state;
        cpu.e_icount = hart_state.instruction_count;
        cpu.e_ccount = hart_state.cycle_count;
        cpu.clint_mmio.mcycle = hart_state.mcycle;
        cpu.TLB_flush();
    }
    std::memcpy(ram.data(), ram_image.data(), ram_image.size());
    return {};
}

void Machine::maybe_save_periodic_checkpoint() {
    const auto interval = config.debug.checkpoint_every;
    if (interval == 0 || config.debug.checkpoint_dir.empty()) return;
    const auto cycle = primary_hart().clint_mmio.mcycle;
    if (cycle < next_checkpoint_cycle_) return;

    // Stop worker harts at a cycle boundary while serializing shared machine state.
    const auto prior_state = execution_state();
    if (prior_state == ExecutionState::Running) {
        execution_state_.store(ExecutionState::Paused, std::memory_order_release);
        execution_state_.notify_all();
        for (auto& secondary : runtime_->secondary_harts) secondary->hart_status.notify_all();
        wait_for_runner_quiescence();
    }

    std::error_code fs_error;
    const std::filesystem::path directory(config.debug.checkpoint_dir);
    std::filesystem::create_directories(directory, fs_error);
    if (fs_error) {
        simrv::log::error("Cannot create checkpoint directory '{}': {}", directory.string(),
                          fs_error.message());
    } else {
        const auto stem = std::format("checkpoint-{}", cycle);
        auto checkpoint_path = directory / (stem + ".ckpt");
        for (uint32_t suffix = 1; std::filesystem::exists(checkpoint_path, fs_error); ++suffix) {
            checkpoint_path = directory / std::format("{}-{}.ckpt", stem, suffix);
            if (suffix == std::numeric_limits<uint32_t>::max()) break;
        }
        const auto saved = save_checkpoint(checkpoint_path.string());
        if (!saved) {
            simrv::log::error("Periodic checkpoint failed: {}", saved.error());
        } else {
            const auto metadata_path = checkpoint_path.string() + ".json";
            std::ofstream metadata(metadata_path, std::ios::trunc);
            const auto digest = simrv::util::sha256_file(checkpoint_path);
            const auto ram = ram_view();
            const auto ram_digest = simrv::util::sha256(std::string_view(
                reinterpret_cast<const char*>(ram.data()), static_cast<size_t>(ram.size())));
            metadata << std::format(
                "{{\"schema_version\":1,\"kind\":\"simrv_architectural_checkpoint\","
                "\"cycle\":{},\"checkpoint_file\":\"{}\",\"checkpoint_sha256\":{},"
                "\"ram_sha256\":\"{}\",\"xlen\":{},\"harts\":[",
                cycle, checkpoint_path.filename().string(),
                digest ? std::format("\"{}\"", *digest) : "null", ram_digest,
                simrv::xlen::kXLenBits);
            for (size_t h = 0; h < num_harts(); ++h) {
                if (h != 0) metadata << ',';
                metadata << std::format(
                    "{{\"hart\":{},\"pc\":\"0x{:x}\","
                    "\"privilege\":\"{}\",\"pending_interrupts\":\"0x{:x}\"}}",
                    h, static_cast<uint64_t>(hart(h).state().pc),
                    hart(h).state().priv == PrivilegeLevel::User         ? "U"
                    : hart(h).state().priv == PrivilegeLevel::Supervisor ? "S"
                                                                         : "M",
                    static_cast<uint64_t>(hart(h).state().mip));
            }
            metadata << "],\"device_state_included\":false,\"trace_sequence\":null,"
                        "\"replay_complete\":false}\n";
            if (!metadata) {
                simrv::log::error("Failed to write checkpoint metadata '{}': {}", metadata_path,
                                  "stream write error");
            } else {
                simrv::log::info("Periodic checkpoint saved at cycle {}: {}", cycle,
                                 checkpoint_path.string());
            }
        }
    }

    do {
        if (next_checkpoint_cycle_ > std::numeric_limits<Counter>::max() - interval) {
            next_checkpoint_cycle_ = std::numeric_limits<Counter>::max();
            break;
        }
        next_checkpoint_cycle_ += interval;
    } while (next_checkpoint_cycle_ <= cycle);
    if (prior_state == ExecutionState::Running) {
        execution_state_.store(ExecutionState::Running, std::memory_order_release);
        execution_state_.notify_all();
        notify_control_event();
    }
}

void Machine::set_platform_irq(IrqNumber irq, bool asserted) {
    runtime_->primary_cpu.plic_set_irq(irq, asserted);
}

void Machine::set_hart_irq(HartId hart_id, InterruptType type, PrivilegeLevel priv, bool asserted) {
    if (hart_id.val >= num_harts()) {
        return;
    }
    auto& target_hart = hart(hart_id);
    MipBit pending_bit = MipBit::Msip;
    TrapCause cause_code = 3;
    if (type == InterruptType::Software) {
        pending_bit = priv == PrivilegeLevel::Supervisor ? MipBit::Ssip : MipBit::Msip;
        cause_code = priv == PrivilegeLevel::Supervisor ? 1 : 3;
    } else if (type == InterruptType::Timer) {
        pending_bit = priv == PrivilegeLevel::Supervisor ? MipBit::Stip : MipBit::Mtip;
        cause_code = priv == PrivilegeLevel::Supervisor ? 5 : 7;
    } else if (type == InterruptType::External) {
        pending_bit = priv == PrivilegeLevel::Supervisor ? MipBit::Seip : MipBit::Meip;
        cause_code = priv == PrivilegeLevel::Supervisor ? 9 : 11;
    }
    const bool was_asserted = (target_hart.state().mip & enum_mask(pending_bit)) != 0;
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
    const bool is_asserted = (target_hart.state().mip & enum_mask(pending_bit)) != 0;
    if (was_asserted != is_asserted) {
        const std::string_view source = type == InterruptType::Timer ? "aclint_mtimer"
                                        : type == InterruptType::Software
                                            ? "aclint_mswi"
                                            : "external_interrupt_controller";
        trace().log_interrupt_signal(target_hart, kInterruptCauseBit | cause_code, is_asserted,
                                     source);
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

auto Machine::dma_controller() noexcept -> simrv::device::DmaController* {
    return runtime_ ? runtime_->dma_controller.get() : nullptr;
}

auto Machine::dma_controller() const noexcept -> const simrv::device::DmaController* {
    return runtime_ ? runtime_->dma_controller.get() : nullptr;
}

auto Machine::axi_bridge() noexcept -> simrv::memory::Axi4Bridge* {
    return runtime_ ? runtime_->axi_bridge.get() : nullptr;
}

auto Machine::axi_bridge() const noexcept -> const simrv::memory::Axi4Bridge* {
    return runtime_ ? runtime_->axi_bridge.get() : nullptr;
}

void Machine::send_input_key(uint16_t code, bool pressed) {
    const std::scoped_lock lock(input_mutex_);
    pending_input_events_.push_back(
        {.type = PendingInputEvent::Type::Key, .code = code, .pressed = pressed});
}

void Machine::send_input_mouse_motion(int32_t x, int32_t y) {
    const std::scoped_lock lock(input_mutex_);
    pending_input_events_.push_back({.type = PendingInputEvent::Type::MouseMotion, .x = x, .y = y});
}

void Machine::send_input_mouse_button(uint16_t button, bool pressed) {
    const std::scoped_lock lock(input_mutex_);
    pending_input_events_.push_back(
        {.type = PendingInputEvent::Type::MouseButton, .code = button, .pressed = pressed});
}

void Machine::send_input_mouse_wheel(int32_t delta) {
    const std::scoped_lock lock(input_mutex_);
    pending_input_events_.push_back({.type = PendingInputEvent::Type::MouseWheel, .x = delta});
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
    if (auto* baremetal = std::get_if<BaremetalRunner>(&runtime_->runner)) {
        baremetal->stop(*this);
    } else if (auto* os = std::get_if<OsRunner>(&runtime_->runner)) {
        os->stop(*this);
    }
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

void Machine::service_pending_input() {
    std::deque<PendingInputEvent> events;
    {
        const std::scoped_lock lock(input_mutex_);
        events.swap(pending_input_events_);
    }

    for (const auto& event : events) {
        if (runtime_->mmio_input) {
            switch (event.type) {
                case PendingInputEvent::Type::Key:
                    runtime_->mmio_input->push_key_event(event.code, event.pressed);
                    break;
                case PendingInputEvent::Type::MouseMotion:
                    runtime_->mmio_input->push_mouse_motion(event.x, event.y);
                    break;
                case PendingInputEvent::Type::MouseButton:
                    runtime_->mmio_input->push_mouse_button(event.code, event.pressed);
                    break;
                case PendingInputEvent::Type::MouseWheel:
                    runtime_->mmio_input->push_mouse_wheel(event.x);
                    break;
            }
        } else if (runtime_->pci_input) {
            switch (event.type) {
                case PendingInputEvent::Type::Key:
                    runtime_->pci_input->push_key_event(event.code, event.pressed);
                    break;
                case PendingInputEvent::Type::MouseMotion:
                    runtime_->pci_input->push_mouse_motion(event.x, event.y);
                    break;
                case PendingInputEvent::Type::MouseButton:
                    runtime_->pci_input->push_mouse_button(event.code, event.pressed);
                    break;
                case PendingInputEvent::Type::MouseWheel:
                    runtime_->pci_input->push_mouse_wheel(event.x);
                    break;
            }
        }
    }
}

void Machine::service_network() {
    if (runtime_->pci_net) runtime_->pci_net->poll_backend();
    if (runtime_->mmio_net) runtime_->mmio_net->poll_backend();
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
        snapshot->scoreboard.sync_from_pipeline(source.ca_pipeline, /*include_decode=*/true);
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
    auto quantum = config.execution.smp_quantum;
    if (config.debug.checkpoint_every != 0 &&
        primary_hart().clint_mmio.mcycle < next_checkpoint_cycle_) {
        const auto cycles_remaining = next_checkpoint_cycle_ - primary_hart().clint_mmio.mcycle;
        quantum = static_cast<uint32_t>(std::min<Counter>(quantum, cycles_remaining));
    }
    return quantum;
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
    const auto fincnt = machine.config.execution.fincnt;
    for (uint32_t cycle = 0; cycle < quantum && machine.is_running(); ++cycle) {
        if (simrv::compiler::unlikely(cycle != 0 &&
                                      machine.execution_state() != ExecutionState::Running)) {
            break;
        }
        machine.advance_ca_global_cycle();
        if (simrv::compiler::unlikely(machine.tohost != 0 ||
                                      (fincnt != std::numeric_limits<Counter>::max() &&
                                       machine.retired_instruction_count() >= fincnt))) {
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
    const auto opt_policy = machine.fast_batch_policy();
    if (!opt_policy.has_value()) return false;
    const auto& policy = *opt_policy;
    if (policy.has_instruction_limit) {
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
    machine.primary_hart().run_fast_baremetal_batch(machine, quantum, policy);
    for (auto& sec : machine.runtime_->secondary_harts) {
        if (!machine.is_running()) break;
        if (sec->hart_status.load(std::memory_order_relaxed) == HartStatus::Started) {
            uint32_t sec_batch = quantum;
            if (policy.has_instruction_limit) {
                if (machine.retired_instruction_count() >= machine.config.execution.fincnt) {
                    machine.stop(Machine::StopReason::InstructionLimit);
                    break;
                }
                sec_batch = static_cast<uint32_t>(std::min<Counter>(
                    sec_batch,
                    machine.config.execution.fincnt - machine.retired_instruction_count()));
            }
            sec->run_fast_baremetal_batch(machine, sec_batch, policy);
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
    if (auto* uart = machine.uart_device(); uart) {
        if (machine.tui_enabled() ||
            (!uart->is_input_thread_running() &&
             simrv::compiler::unlikely((machine.primary_hart().clint_mmio.mtime & 8191) == 0))) {
            uart->service_interrupts();
        }
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
                cpu.clint_mmio.mtime, cpu.pipeline_context.cpc.raw(),
                cpu.pipeline_context.jmp_pc.raw(), cpu.pipeline_context.opcode,
                cpu.pipeline_context.tkn);
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
    reset_realtime_anchor();
    execution_state_.store(
        tui_enabled() || debugger_enabled() ? ExecutionState::Paused : ExecutionState::Running,
        std::memory_order_release);
    execution_state_.notify_all();
    primary_hart().reset();
    if (config.bram_prewarm) {
        prewarm_bram_caches();
    }
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
            cpu.pipeline_sim.config.record_snapshots =
                is_observable_engine(engine) ||
                (engine == ExecutionEngine::CycleFast && runtime_profile.gdb);
        } else {
            cpu.pipeline_sim.config.record_snapshots = false;
            cpu.decode_cache.flush();
        }
    }

    if (to_cycle) {
        memory().system_bus().coherence_hub().clear();
        if (config.bram_prewarm) {
            prewarm_bram_caches();
        }
    }

    runtime_profile.engine = engine;
    publish_tui_execution_snapshot();

    simrv::log::info("Switched execution mode to {}", runtime_profile.execution_name());
}

void Machine::prewarm_bram_caches() {
    auto ram = ram_view();
    if (ram.data() == nullptr || ram.size() == 0) return;

    for (size_t h = 0; h < num_harts(); ++h) {
        auto& cpu = hart(h);

        if (!loaded_segments_.empty()) {
            for (const auto& seg : loaded_segments_) {
                if (seg.is_executable) {
                    constexpr Address kLineBytes = simrv::cache::ICache::kLineBytes;
                    const Address start = seg.paddr & ~(kLineBytes - 1u);
                    Address end = (seg.paddr + seg.size + kLineBytes - 1u) & ~(kLineBytes - 1u);
                    const size_t max_bytes = cpu.icache.capacity_bytes();
                    if (end > start + max_bytes) {
                        end = start + max_bytes;
                    }
                    for (Address addr = start; addr < end; addr += kLineBytes) {
                        if (ram.contains(addr, kLineBytes)) {
                            cpu.icache.insert(addr, ram.unchecked_ptr(addr),
                                              simrv::memory::MesiState::Exclusive);
                        }
                    }
                } else {
                    constexpr Address kLineBytes = simrv::cache::DCache::kLineBytes;
                    const Address start = seg.paddr & ~(kLineBytes - 1u);
                    Address end = (seg.paddr + seg.size + kLineBytes - 1u) & ~(kLineBytes - 1u);
                    const size_t max_bytes = cpu.dcache.capacity_bytes();
                    if (end > start + max_bytes) {
                        end = start + max_bytes;
                    }
                    for (Address addr = start; addr < end; addr += kLineBytes) {
                        if (ram.contains(addr, kLineBytes)) {
                            cpu.dcache.insert(addr, ram.unchecked_ptr(addr),
                                              simrv::memory::MesiState::Exclusive);
                        }
                    }
                }
            }
        } else {
            // Fallback: pre-warm IMEM range into ICache and DMEM range into DCache
            const Address dram_base = ram.base();
            constexpr Address kILineBytes = simrv::cache::ICache::kLineBytes;
            const Address i_end = dram_base + cpu.icache.capacity_bytes();
            for (Address addr = dram_base; addr < i_end; addr += kILineBytes) {
                if (ram.contains(addr, kILineBytes)) {
                    cpu.icache.insert(addr, ram.unchecked_ptr(addr),
                                      simrv::memory::MesiState::Exclusive);
                }
            }

            constexpr Address kDLineBytes = simrv::cache::DCache::kLineBytes;
            const Address dmem_base =
                ram.contains(0x10000000ULL, kDLineBytes) ? 0x10000000ULL : dram_base;
            const Address d_end = dmem_base + cpu.dcache.capacity_bytes();
            for (Address addr = dmem_base; addr < d_end; addr += kDLineBytes) {
                if (ram.contains(addr, kDLineBytes)) {
                    cpu.dcache.insert(addr, ram.unchecked_ptr(addr),
                                      simrv::memory::MesiState::Exclusive);
                }
            }
        }
    }
}

auto Machine::add_lifecycle_observer(LifecycleObserver observer) -> LifecycleObserverId {
    std::scoped_lock lock(lifecycle_observer_mutex_);
    const auto id = next_lifecycle_observer_id_++;
    lifecycle_observers_.emplace_back(id, std::move(observer));
    return id;
}

void Machine::remove_lifecycle_observer(LifecycleObserverId observer_id) {
    std::scoped_lock lock(lifecycle_observer_mutex_);
    std::erase_if(lifecycle_observers_,
                  [observer_id](const auto& entry) { return entry.first == observer_id; });
}

void Machine::publish_lifecycle_event(LifecycleEventKind kind, int exit_status) {
    std::vector<LifecycleObserver> observers;
    {
        std::scoped_lock lock(lifecycle_observer_mutex_);
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
        const std::scoped_lock lock(step_mutex_);
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
    reset_realtime_anchor();
    debug_step_hart_.reset();
    execution_state_.store(ExecutionState::Running, std::memory_order_release);
    execution_state_.notify_all();
    for (auto& hart : runtime_->secondary_harts) hart->hart_status.notify_all();
    notify_control_event();
    publish_lifecycle_event(LifecycleEventKind::Running);
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
        const std::scoped_lock lock(debug_halt_mutex_);
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
        const std::scoped_lock lock(debug_halt_mutex_);
        if (!pending_debug_halt_.has_value()) return;
        auto& pending = *pending_debug_halt_;
        if (pending.waits_for_memory) {
            auto& target = hart(pending.hart);
            if (target.e_icount < pending.retirement_target) return;
            if (target.active_context().pending_exception.has_value()) {
                pending_debug_halt_.reset();
                debug_halt_pending_.store(false, std::memory_order_release);
                return;
            }
        }
        halt = std::move(pending);
        pending_debug_halt_.reset();
    }

    if (telemetry_sink_ && !halt.description.empty()) {
        telemetry_sink_->set_status_override(halt.description);
    }
    pause();
    {
        const std::scoped_lock lock(debug_halt_mutex_);
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
    if (config.debug.checkpoint_every != 0) {
        const auto cycle = primary_hart().clint_mmio.mcycle;
        next_checkpoint_cycle_ =
            cycle > std::numeric_limits<Counter>::max() - config.debug.checkpoint_every
                ? std::numeric_limits<Counter>::max()
                : cycle + config.debug.checkpoint_every;
    }
    if (platform_time() == 0 && runtime_->rtc) {
        runtime_->rtc->sync_with_system_time();
    }
    publish_lifecycle_event(LifecycleEventKind::Started);
    primary_hart().evaluate_timer_interrupt();

    // Start the selected composed execution policy.
    start_runner();
    if (execution_state() == ExecutionState::Running) {
        publish_lifecycle_event(LifecycleEventKind::Running);
    }

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
    reset_realtime_anchor();
    bool was_paused = false;
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
            was_paused = true;
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
        if (was_paused) {
            was_paused = false;
            reset_realtime_anchor();
        }
        if (telemetry_sink_) {
            telemetry_sink_->set_sim_thread_sleeping(false);
        }

        service_network();
        service_pending_input();

        auto batch_quantum = runtime_profile.fast_batch_quantum();
        if (config.debug.checkpoint_every != 0 &&
            primary_hart().clint_mmio.mcycle < next_checkpoint_cycle_) {
            const auto cycles_remaining = next_checkpoint_cycle_ - primary_hart().clint_mmio.mcycle;
            batch_quantum = static_cast<uint32_t>(
                std::min<Counter>(batch_quantum, std::max<Counter>(1, cycles_remaining)));
        }
        if (execute_runner_fast_batch(batch_quantum)) {
            if (simrv::compiler::unlikely(trace().fp_trace.is_open())) {
                trace().write_trace_snapshot();
            }
            if (simrv::compiler::unlikely(tohost != 0)) {
                finalize_cycle_tohost();
            }
            maybe_save_periodic_checkpoint();
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
            pace_realtime();
            continue;
        }

        prepare_runner_cycle();
        execute_runner_cycle();
        finalize_runner_cycle();

        if (simrv::compiler::unlikely(tohost != 0)) {
            finalize_cycle_tohost();
        }
        maybe_save_periodic_checkpoint();
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
        pace_realtime();
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
    publish_lifecycle_event(LifecycleEventKind::Completed);
}

void Machine::reset_realtime_anchor() noexcept {
    realtime_anchor_host_ = std::chrono::steady_clock::now();
    realtime_anchor_mtime_ = platform_time();
    last_pace_check_mtime_ = realtime_anchor_mtime_;
}

void Machine::set_realtime_pacing_enabled(bool enabled) noexcept {
    config.execution.realtime_pacing = enabled;
    reset_realtime_anchor();
}

void Machine::pace_realtime() noexcept {
    if (!config.execution.realtime_pacing || is_stepping()) {
        return;
    }
    const uint64_t cur_mtime = platform_time();
    if (cur_mtime < realtime_anchor_mtime_) {
        reset_realtime_anchor();
        return;
    }
    // Check pacing every millisecond of architectural virtual time.
    constexpr uint64_t kPaceIntervalTicks = timing::kTimebaseHz / 1'000;
    if (cur_mtime < last_pace_check_mtime_ + kPaceIntervalTicks) {
        return;
    }
    last_pace_check_mtime_ = cur_mtime;

    const auto now = std::chrono::steady_clock::now();
    const auto sim_elapsed_ns = static_cast<int64_t>((cur_mtime - realtime_anchor_mtime_) *
                                                     timing::kNanosecondsPerTimebaseTick);
    const auto host_elapsed_ns =
        std::chrono::duration_cast<std::chrono::nanoseconds>(now - realtime_anchor_host_).count();

    const int64_t lead_ns = sim_elapsed_ns - host_elapsed_ns;

    if (lead_ns >= 1'000'000) {  // Simulation is ahead of host time by >= 1 ms
        // Sleep to throttle down to host wall-clock time; cap each sleep slice at 20 ms
        // so external events (quit, pause, terminal input) remain immediately responsive.
        const auto sleep_ns = std::min<int64_t>(lead_ns, 20'000'000);
        std::this_thread::sleep_for(std::chrono::nanoseconds(sleep_ns));
    } else if (lead_ns < -50'000'000) {  // Lagging behind host time by > 50 ms
        // Re-anchor baseline so simulation does not burst-speed catch up.
        realtime_anchor_host_ = now;
        realtime_anchor_mtime_ = cur_mtime;
    }
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
    if (runtime_ && runtime_->gdb_stub) {
        try {
            runtime_->gdb_stub->stop();
        } catch (...) {  // NOLINT(bugprone-empty-catch)
        }
    }
    stop_runner();
    // Destroy callbacks and runner objects while their Machine wake state is still alive.
    runtime_.reset();
}

void Machine::advance_ca_global_cycle() {
    primary_hart().run_cycle(*this);
    if (simrv::compiler::unlikely(!runtime_->secondary_harts.empty())) {
        for (const auto [i, secondary] : std::views::enumerate(runtime_->secondary_harts)) {
            if (secondary->hart_status.load(std::memory_order_relaxed) == HartStatus::Started) {
                secondary->run_cycle(*this);
            }
        }
    }

    advance_ca_platform_cycle(true);
}

void Machine::advance_ca_primary_cycle() {
    primary_hart().run_cycle(*this);
    advance_ca_platform_cycle(false);
}

void Machine::advance_ca_platform_cycle(bool synchronize_secondary_harts) {
    // Shared requests become visible after the scheduler's current hart transition(s). In
    // best-effort MT mode hart 0 owns this clock while secondary pipelines progress
    // independently. The timer transition follows the interconnect transition and is
    // sampled by hart pipelines at a retirement boundary in the next global cycle.
    auto& cpu = primary_hart();
    memory_.system_bus().advance_cycle();
    dma_engine_.advance_cycle(memory_.system_bus().cycle());
    if (simrv::compiler::unlikely(++cpu.clint_mmio.rtc_divider >= timing::kCyclesPerTimebaseTick)) {
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
