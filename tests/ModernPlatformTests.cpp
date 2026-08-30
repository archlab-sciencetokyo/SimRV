/**
 * @file ModernPlatformTests.cpp
 * @brief Unit tests for RISC-V AIA, ACLINT, PCIe ECAM, and VirtIO-PCI subsystems.
 */
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <vector>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/CsrTypes.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/RuntimeProfile.hpp"
#include "simrv/device/AIA.hpp"
#include "simrv/device/Aclint.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/device/mmio/VirtioMmioBlock.hpp"
#include "simrv/device/mmio/VirtioMmioConsole.hpp"
#include "simrv/device/mmio/VirtioMmioNet.hpp"
#include "simrv/device/mmio/VirtioMmioRng.hpp"
#include "simrv/device/pci/PciDevice.hpp"
#include "simrv/device/pci/PcieRootComplex.hpp"
#include "simrv/device/pci/VirtioPciBlock.hpp"
#include "simrv/device/pci/VirtioPciConsole.hpp"
#include "simrv/device/pci/VirtioPciGpu.hpp"
#include "simrv/device/pci/VirtioPciInput.hpp"
#include "simrv/device/pci/VirtioPciNet.hpp"
#include "simrv/device/pci/VirtioPciRng.hpp"
#include "simrv/device/pci/VirtioPciSound.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/memory/MmioDevice.hpp"
#include "simrv/tui/modals/SystemConfigModal.hpp"
#include "simrv/tui/panels/InspectorPane.hpp"
#include "simrv/xlen/Types.hpp"

namespace {

class DummyDevice : public simrv::memory::MmioDevice {
   public:
    explicit DummyDevice(simrv::core::Machine* m) : simrv::memory::MmioDevice(m) {}
    [[nodiscard]] auto name() const -> const char* override { return "dummy"; }
    [[nodiscard]] auto base_address() const -> Address override { return 0x1000U; }
    [[nodiscard]] auto size() const -> Address override { return 0x100U; }

    [[nodiscard]] auto read32(Address offset) -> uint32_t override {
        return static_cast<uint32_t>(0xABCD0000U + offset);
    }
    void write32(Address offset, uint32_t val) override {
        last_val_ = val + static_cast<uint32_t>(offset);
    }
    uint32_t last_val_{0};
};

using ConcreteMachine = simrv::core::Machine;

// Preserve concise fixture spelling while exercising the final, composed Machine.
#define cpu primary_hart()
#define mmem mutable_ram_data_for_testing()
#define secondary_harts_ test_secondary_harts()

void test_left_pane_runtime_summaries() {
    const auto check = [](bool condition, const char* message) {
        if (!condition) {
            std::cerr << message << '\n';
            std::abort();
        }
    };
    ConcreteMachine machine;
    simrv::tui::InspectorPane pane(machine);
    pane.set_visible_rows(60);
    constexpr int kWidth = 80;
    auto row = [&](int logical_row) { return pane.render_row(logical_row + 2, kWidth); };

    check(row(1).contains("x1") && row(16).contains("Machine State"), "machine state heading");
    check(row(18).contains("interrupts") && row(19).contains("translation"),
          "machine state health");
    check(!row(18).contains("mstatus") && !row(19).contains("satp"), "raw CSRs hidden");
    machine.cpu.state().mtvec = 0x80000100;
    machine.cpu.raise_exception(static_cast<TrapCause>(ExceptionCode::FetchPageFault), 0x80201048);
    check(
        !machine.is_stopped() && machine.stop_reason() == simrv::core::Machine::StopReason::Running,
        "handled traps continue execution");
    check(row(20).contains("I-page fault") && row(20).contains("address"),
          "trap summary is compact and includes fault address");
    check(row(21).contains("Performance") && row(22).contains("retired") &&
              row(23).contains("engine") && row(24).contains("speed") &&
              row(25).contains("average"),
          "IA summary flow");

    pane.set_paused(false);
    check(row(0).contains("RUNNING") && row(0).contains("sampled state"),
          "running view uses a compact sampled-state banner");
    check(row(1).contains("x1"), "running view preserves cached register context");
    pane.set_kips(12'345);
    check(row(21).contains("Performance") && row(24).contains("12.35 MIPS"),
          "running view keeps performance telemetry live");
    pane.set_paused(true);

    machine.set_debug_diagnostics_enabled(true);
    check(row(26).contains("Debug") && row(27).contains("ELF sym") &&
              row(28).contains("breakpoints") && !row(28).contains("privilege"),
          "debug summary");

    machine.set_debug_diagnostics_enabled(false);
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
    check(row(21).contains("Cycle Accurate") && row(27).contains("I-cache") &&
              row(28).contains("D-cache") && row(29).contains("limit"),
          "CA summary");
}

void test_ca_model_cache_application() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    ConcreteMachine machine;
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleObservable;

    simrv::tui::SysConfigDraft draft{};
    draft.cycle_accurate = true;
    draft.profile = static_cast<uint8_t>(simrv::pipeline::CpuModelProfile::Performance);
    check(simrv::tui::modals::SystemConfigModal::submit(draft, machine));
    check(machine.cpu.cpu_model_config.profile == simrv::pipeline::CpuModelProfile::Performance);
    check(machine.cpu.dcache.capacity_bytes() == 16384);
    check(machine.cpu.dcache.associativity() == 4);
}

void test_mmio_device_and_dma() {
    ConcreteMachine machine;
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());

    DummyDevice dev(&machine);
    assert(dev.base_address() == 0x1000U);
    assert(dev.read32(0x04) == 0xABCD0004U);
    assert(dev.read8(0x04) == 0x04U);
    assert(dev.read16(0x04) == 0x0004U);

    // Test DMA
    std::vector<uint8_t> src = {0x11, 0x22, 0x33, 0x44};
    assert(dev.dma_write(simrv::memory::kDramBaseAddress + 0x100, src));

    std::vector<uint8_t> dst(4, 0);
    assert(dev.dma_read(simrv::memory::kDramBaseAddress + 0x100, dst));
    assert(dst == src);
    std::cout << "[PASS] test_mmio_device_and_dma\n";
}

void test_tui_uart_input_irq_publication() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    ConcreteMachine machine;
    machine.cpu.reset();
    machine.set_tui_enabled_for_testing(true);
    simrv::device::Uart uart(machine);

    simrv::memory::TlChannelA enable_rx_irq{};
    enable_rx_irq.opcode = simrv::memory::TlOpcodeA::PutFullData;
    enable_rx_irq.address = simrv::mmio::kUartBaseAddress + simrv::mmio::kUartRegIerDlm;
    enable_rx_irq.size = 0;
    enable_rx_irq.data = 1;
    simrv::memory::TlChannelD response{};
    check(uart.handle_request(enable_rx_irq, response));

    uart.push_rx_byte('x');
    constexpr Word kUartIrqMask = static_cast<Word>(1) << 3;
    check((machine.cpu.plic_mmio.plic_pending[0] & kUartIrqMask) == 0);
    uart.service_interrupts();
    check((machine.cpu.plic_mmio.plic_pending[0] & kUartIrqMask) != 0);
    std::cout << "[PASS] test_tui_uart_input_irq_publication\n";
}

void test_ia_tui_uart_input_is_immediate() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    simrv::core::Machine machine(simrv::core::MachineConfig{.execution = {.appmode = false}});
    machine.cpu.reset();
    machine.set_tui_enabled_for_testing(true);
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::InstructionObservable;
    machine.mutable_uart_for_testing() = std::make_unique<simrv::device::Uart>(machine);

    simrv::memory::TlChannelA enable_rx_irq{};
    enable_rx_irq.opcode = simrv::memory::TlOpcodeA::PutFullData;
    enable_rx_irq.address = simrv::mmio::kUartBaseAddress + simrv::mmio::kUartRegIerDlm;
    enable_rx_irq.size = 0;
    enable_rx_irq.data = 1;
    simrv::memory::TlChannelD response{};
    check(machine.mutable_uart_for_testing()->handle_request(enable_rx_irq, response));

    machine.mutable_uart_for_testing()->push_rx_byte('i');
    constexpr Word kUartIrqMask = static_cast<Word>(1) << 3;
    check((machine.cpu.plic_mmio.plic_pending[0] & kUartIrqMask) == 0);
    machine.finalize_for_testing();
    check((machine.cpu.plic_mmio.plic_pending[0] & kUartIrqMask) != 0);
    std::cout << "[PASS] test_ia_tui_uart_input_is_immediate\n";
}

void test_os_sampled_fast_batch_eligibility() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    simrv::core::Machine machine(simrv::core::MachineConfig{.execution = {.appmode = false}});
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::InstructionObservable;
    machine.runtime_profile.interaction = simrv::core::InteractionMode::Tui;

    // A zero-length batch exercises runner eligibility without requiring a guest image. The
    // production TUI supplies the same observable profile, then publishes after real batches.
    check(machine.execute_fast_batch_for_testing(0));

    auto secondary = std::make_unique<simrv::core::CPU>();
    secondary->reset();
    machine.add_hart_for_testing(std::move(secondary));
    check(!machine.execute_fast_batch_for_testing(0));

    machine.test_secondary_harts().clear();
    machine.set_smp_parallel_for_testing(true);
    check(!machine.execute_fast_batch_for_testing(0));
    std::cout << "[PASS] test_os_sampled_fast_batch_eligibility\n";
}

void test_timed_interconnect_ordering() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    ConcreteMachine machine;
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    ram[0] = Byte{0x11};
    ram[8] = Byte{0x22};

    auto& bus = machine.memory().system_bus();
    simrv::memory::TlChannelA first{};
    first.opcode = simrv::memory::TlOpcodeA::Get;
    first.source = 2;
    first.address = simrv::memory::kDramBaseAddress;
    simrv::memory::TlChannelA second = first;
    second.source = 1;
    second.address += 8;

    check(bus.send_request(first));
    check(bus.send_request(second));
    simrv::memory::TileLinkBus::TimedResponse response{};
    check(!bus.try_get_timed_response(2, response));

    bus.advance_cycle();
    check(bus.try_get_timed_response(2, response));
    check(response.sequence == 0);
    check(response.ready_cycle == 1);
    check((response.payload.data & 0xffU) == 0x11);
    check(!bus.try_get_timed_response(1, response));

    bus.advance_cycle();
    check(bus.try_get_timed_response(1, response));
    check(response.sequence == 1);
    check(response.ready_cycle == 2);
    check((response.payload.data & 0xffU) == 0x22);

    // One cycle is the baseline transport delay; larger values add full CA cycles after the
    // request reaches the bus.
    bus.configure_timing(2, 2);
    simrv::memory::TlChannelA delayed = first;
    delayed.source = 3;
    check(bus.send_request(delayed));
    bus.advance_cycle();
    check(!bus.try_get_timed_response(3, response));
    bus.advance_cycle();
    check(!bus.try_get_timed_response(3, response));
    bus.advance_cycle();
    check(bus.try_get_timed_response(3, response));
    check(response.ready_cycle == 5);
    std::cout << "[PASS] test_timed_interconnect_ordering\n";
}

void test_timed_interconnect_cancellation() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    ConcreteMachine machine;
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());

    auto& bus = machine.memory().system_bus();
    simrv::memory::TlChannelA request{};
    request.opcode = simrv::memory::TlOpcodeA::Get;
    request.source = 7;
    request.address = simrv::memory::kDramBaseAddress;

    check(bus.send_request(request));
    check(bus.pending_requests() == 1);
    bus.cancel_source(request.source);
    check(bus.pending_requests() == 0);
    bus.advance_cycle();
    simrv::memory::TileLinkBus::TimedResponse response{};
    check(!bus.try_get_timed_response(request.source, response));

    check(bus.send_request(request));
    bus.advance_cycle();
    check(bus.pending_responses() == 1);
    bus.cancel_source(request.source);
    check(bus.pending_responses() == 0);
    check(!bus.try_get_timed_response(request.source, response));
    std::cout << "[PASS] test_timed_interconnect_cancellation\n";
}

void test_timed_page_walk_transitions() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    ConcreteMachine machine;
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.cpu.machine_ = &machine;
    machine.cpu.reset();
    machine.memory().initialize_mmu();
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;

    constexpr Address physical = simrv::memory::kDramBaseAddress;
    constexpr Address root = physical + 0x1000;
    const Word root_ppn = root >> 12;
    if constexpr (simrv::xlen::kIsXLen64) {
        machine.cpu.state().satp = (Word{8} << 60) | root_ppn;
    } else {
        machine.cpu.state().satp = (Word{1} << 31) | root_ppn;
    }
    machine.cpu.state().priv = kPrivSupervisor;

    constexpr unsigned top_vpn_bits = simrv::xlen::kIsXLen64 ? 9 : 10;
    constexpr unsigned top_level = simrv::xlen::kIsXLen64 ? 2 : 1;
    const Word vpn =
        (physical >> (12 + top_level * top_vpn_bits)) & ((Word{1} << top_vpn_bits) - 1);
    const Address pte_address = root + vpn * sizeof(Word);
    Word pte = ((physical >> 12) << 10) | enum_mask(simrv::PteFlag::V) |
               enum_mask(simrv::PteFlag::R) | enum_mask(simrv::PteFlag::X);
    std::memcpy(ram.data() + (pte_address - physical), &pte, sizeof(pte));

    auto& walk = machine.cpu.ca_state.instruction_walk;
    auto result = machine.cpu.translate_stage_address(machine, physical, simrv::PteAccess::Code,
                                                      kPrivSupervisor, simrv::xlen::kXLenBits,
                                                      simrv::memory::TlPort::Instruction, walk);
    check(!result.has_value());
    check(machine.memory().system_bus().pending_requests() == 1);

    machine.memory().system_bus().advance_cycle();
    machine.cpu.ca_state.waiting_for_interconnect = false;
    result = machine.cpu.translate_stage_address(machine, physical, simrv::PteAccess::Code,
                                                 kPrivSupervisor, simrv::xlen::kXLenBits,
                                                 simrv::memory::TlPort::Instruction, walk);
    check(!result.has_value());  // Accessed-bit update is a distinct timed write.
    check(machine.memory().system_bus().pending_requests() == 1);

    machine.memory().system_bus().advance_cycle();
    machine.cpu.ca_state.waiting_for_interconnect = false;
    result = machine.cpu.translate_stage_address(machine, physical, simrv::PteAccess::Code,
                                                 kPrivSupervisor, simrv::xlen::kXLenBits,
                                                 simrv::memory::TlPort::Instruction, walk);
    check(result.has_value() && result->has_value() && **result == physical);
    check(!walk.active);
    std::memcpy(&pte, ram.data() + (pte_address - physical), sizeof(pte));
    check((pte & enum_mask(simrv::PteFlag::A)) != 0);

    // A different, unmapped top-level entry faults after exactly one physical read.
    constexpr Address unmapped = physical + (Address{1} << (12 + top_level * top_vpn_bits));
    result = machine.cpu.translate_stage_address(machine, unmapped, simrv::PteAccess::Code,
                                                 kPrivSupervisor, simrv::xlen::kXLenBits,
                                                 simrv::memory::TlPort::Instruction, walk);
    check(!result.has_value());
    machine.memory().system_bus().advance_cycle();
    machine.cpu.ca_state.waiting_for_interconnect = false;
    result = machine.cpu.translate_stage_address(machine, unmapped, simrv::PteAccess::Code,
                                                 kPrivSupervisor, simrv::xlen::kXLenBits,
                                                 simrv::memory::TlPort::Instruction, walk);
    check(result.has_value() && !result->has_value());
    check(result->error() == static_cast<TrapCause>(ExceptionCode::FetchPageFault));

    // A squashed fetch removes both the transaction and resumable architectural state.
    result = machine.cpu.translate_stage_address(machine, physical, simrv::PteAccess::Code,
                                                 kPrivSupervisor, simrv::xlen::kXLenBits,
                                                 simrv::memory::TlPort::Instruction, walk);
    check(!result.has_value());
    machine.memory().system_bus().cancel_source(walk.source);
    walk.reset();
    check(machine.memory().system_bus().pending_requests() == 0);

    // Simultaneous A and D updates cannot lose bits: the bus serializes atomic OR operations.
    Word clear_pte = 0;
    std::memcpy(ram.data() + (pte_address - physical), &clear_pte, sizeof(clear_pte));
    simrv::memory::TlChannelA accessed{};
    accessed.opcode = simrv::memory::TlOpcodeA::LogicalData;
    accessed.size = sizeof(Word) == 8 ? 3 : 2;
    accessed.source = 6;
    accessed.address = pte_address;
    accessed.data = enum_mask(simrv::PteFlag::A);
    auto dirty = accessed;
    dirty.source = 7;
    dirty.data = enum_mask(simrv::PteFlag::D);
    machine.memory().system_bus().send_request(accessed);
    machine.memory().system_bus().send_request(dirty);
    machine.memory().system_bus().advance_cycle();
    machine.memory().system_bus().advance_cycle();
    std::memcpy(&clear_pte, ram.data() + (pte_address - physical), sizeof(clear_pte));
    check((clear_pte & (enum_mask(simrv::PteFlag::A) | enum_mask(simrv::PteFlag::D))) ==
          (enum_mask(simrv::PteFlag::A) | enum_mask(simrv::PteFlag::D)));
    std::cout << "[PASS] test_timed_page_walk_transitions\n";
}

void test_cycle_kernel_golden_translated_fetch() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    const auto run = [&](simrv::pipeline::PipelineType type) {
        ConcreteMachine machine;
        std::vector<Byte> ram(1024 * 1024, Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.memory().initialize_mmu();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.pipeline_sim.config.pipeline_type = type;

        constexpr Address pc = simrv::memory::kDramBaseAddress;
        constexpr Address root = pc + 0x1000;
        constexpr Instruction instruction = 0x02a00093;  // addi x1, x0, 42
        std::memcpy(ram.data(), &instruction, sizeof(instruction));
        std::array<Byte, simrv::cache::ICache::kLineBytes> line{};
        std::memcpy(line.data(), &instruction, sizeof(instruction));
        machine.cpu.icache.insert(pc, line.data(), simrv::memory::MesiState::Exclusive);

        const Word root_ppn = root >> 12;
        if constexpr (simrv::xlen::kIsXLen64) {
            machine.cpu.state().satp = (Word{8} << 60) | root_ppn;
        } else {
            machine.cpu.state().satp = (Word{1} << 31) | root_ppn;
        }
        constexpr unsigned vpn_bits = simrv::xlen::kIsXLen64 ? 9 : 10;
        constexpr unsigned level = simrv::xlen::kIsXLen64 ? 2 : 1;
        const Word vpn = (pc >> (12 + level * vpn_bits)) & ((Word{1} << vpn_bits) - 1);
        const Address pte_address = root + vpn * sizeof(Word);
        const Word pte = ((pc >> 12) << 10) | enum_mask(simrv::PteFlag::V) |
                         enum_mask(simrv::PteFlag::R) | enum_mask(simrv::PteFlag::X) |
                         enum_mask(simrv::PteFlag::A);
        std::memcpy(ram.data() + (pte_address - pc), &pte, sizeof(pte));
        machine.cpu.state().priv = kPrivSupervisor;
        machine.cpu.state().pc = pc;

        uint32_t cycles = 0;
        while (machine.cpu.e_icount == 0 && cycles < 32) {
            machine.cpu.run_cycle(machine);
            machine.memory().system_bus().advance_cycle();
            ++cycles;
        }
        check(machine.cpu.e_icount == 1);
        check(machine.cpu.state().regs.read(RegId::Ra) == 42);
        check(machine.cpu.pipeline_sim.tlb_stalls() > 0);
        return cycles;
    };

    check(run(simrv::pipeline::PipelineType::FiveStage) == 7);
    check(run(simrv::pipeline::PipelineType::ThreeStage) == 5);
    std::cout << "[PASS] test_cycle_kernel_golden_translated_fetch\n";
}

void test_cycle_kernel_golden_translated_load() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    const auto run = [&](simrv::pipeline::PipelineType type) {
        ConcreteMachine machine;
        std::vector<Byte> ram(1024 * 1024, Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.memory().initialize_mmu();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.pipeline_sim.config.pipeline_type = type;

        constexpr Address pc = simrv::memory::kDramBaseAddress;
        constexpr Address data_address = pc + 0x100;
        constexpr Address root = pc + 0x1000;
        constexpr std::array<Instruction, 4> program = {
            0x00000097,  // auipc x1, 0
            0x10008093,  // addi  x1, x1, 256
            0x0000a103,  // lw    x2, 0(x1)
            0x0000006f,
        };
        std::memcpy(ram.data(), program.data(), sizeof(program));
        std::array<Byte, simrv::cache::ICache::kLineBytes> instruction_line{};
        std::memcpy(instruction_line.data(), program.data(), sizeof(program));
        machine.cpu.icache.insert(pc, instruction_line.data(), simrv::memory::MesiState::Exclusive);
        constexpr uint32_t value = 73;
        std::memcpy(ram.data() + 0x100, &value, sizeof(value));
        std::array<Byte, simrv::cache::DCache::kLineBytes> data_line{};
        std::memcpy(data_line.data(), &value, sizeof(value));
        machine.cpu.dcache.insert(data_address, data_line.data(), simrv::memory::MesiState::Shared);

        const Word root_ppn = root >> 12;
        if constexpr (simrv::xlen::kIsXLen64) {
            machine.cpu.state().satp = (Word{8} << 60) | root_ppn;
        } else {
            machine.cpu.state().satp = (Word{1} << 31) | root_ppn;
        }
        constexpr unsigned vpn_bits = simrv::xlen::kIsXLen64 ? 9 : 10;
        constexpr unsigned level = simrv::xlen::kIsXLen64 ? 2 : 1;
        const Word vpn = (pc >> (12 + level * vpn_bits)) & ((Word{1} << vpn_bits) - 1);
        const Address pte_address = root + vpn * sizeof(Word);
        const Word pte = ((pc >> 12) << 10) | enum_mask(simrv::PteFlag::V) |
                         enum_mask(simrv::PteFlag::R) | enum_mask(simrv::PteFlag::W) |
                         enum_mask(simrv::PteFlag::X) | enum_mask(simrv::PteFlag::A) |
                         enum_mask(simrv::PteFlag::D);
        std::memcpy(ram.data() + (pte_address - pc), &pte, sizeof(pte));
        machine.cpu.state().priv = kPrivSupervisor;
        machine.cpu.state().pc = pc;
        machine.cpu.tlb.insert_inst_r(pc, pc, 0, kPrivSupervisor);

        uint32_t cycles = 0;
        while (machine.cpu.e_icount < 3 && cycles < 32) {
            machine.cpu.run_cycle(machine);
            machine.memory().system_bus().advance_cycle();
            ++cycles;
        }
        if (machine.cpu.e_icount != 3 || machine.cpu.state().regs.read(RegId::Sp) != value ||
            machine.cpu.pipeline_sim.tlb_stalls() == 0) {
            std::cerr << "translated-load state: cycles=" << cycles
                      << " retired=" << machine.cpu.e_icount
                      << " x2=" << machine.cpu.state().regs.read(RegId::Sp)
                      << " tlb-stalls=" << machine.cpu.pipeline_sim.tlb_stalls() << '\n';
        }
        check(machine.cpu.e_icount == 3);
        check(machine.cpu.state().regs.read(RegId::Sp) == value);
        check(machine.cpu.pipeline_sim.tlb_stalls() > 0);
        return cycles;
    };

    check(run(simrv::pipeline::PipelineType::FiveStage) == 9);
    check(run(simrv::pipeline::PipelineType::ThreeStage) == 7);
    std::cout << "[PASS] test_cycle_kernel_golden_translated_load\n";
}

void test_timed_smp_coherence_ordering() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    ConcreteMachine machine;
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.cpu.machine_ = &machine;
    auto secondary = std::make_unique<simrv::core::CPU>();
    secondary->machine_ = &machine;
    secondary->state().mhartid = 1;
    machine.secondary_harts_.push_back(std::move(secondary));
    machine.memory().initialize_mmu();

    const Address line = simrv::memory::kDramBaseAddress + 0x100;
    ram[0x100] = Byte{0x5a};
    auto& bus = machine.memory().system_bus();

    simrv::memory::TlChannelA owner{};
    owner.opcode = simrv::memory::TlOpcodeA::AcquireBlock;
    owner.grow = simrv::memory::TlGrow::NtoT;
    owner.source = 9;  // Deliberately unrelated to hart identity.
    owner.hart = 0;
    owner.address = line;
    owner.size = simrv::memory::kTlBlockSize;
    check(bus.send_request(owner));

    simrv::memory::TileLinkBus::TimedResponse response{};
    for (unsigned beat = 0; beat < simrv::memory::kTlBlockBytes / simrv::memory::kTlBeatBytes;
         ++beat) {
        bus.advance_cycle();
        const bool complete = bus.try_get_timed_response(owner.source, response);
        check(complete == (beat + 1 == simrv::memory::kTlBlockBytes / simrv::memory::kTlBeatBytes));
    }
    check(response.has_line_data);
    check(std::to_integer<uint8_t>(response.line_data[0]) == 0x5a);
    bus.grant_ack(simrv::memory::TlChannelE{.sink = response.payload.sink});
    machine.hart(0).dcache.insert(line, response.line_data.data(),
                                  simrv::memory::MesiState::Exclusive);

    simrv::memory::TlChannelA reader = owner;
    reader.grow = simrv::memory::TlGrow::NtoB;
    reader.source = 3;
    reader.hart = 1;
    check(bus.send_request(reader));
    for (unsigned beat = 0; beat < simrv::memory::kTlBlockBytes / simrv::memory::kTlBeatBytes;
         ++beat) {
        bus.advance_cycle();
        const bool complete = bus.try_get_timed_response(reader.source, response);
        check(complete == (beat + 1 == simrv::memory::kTlBlockBytes / simrv::memory::kTlBeatBytes));
    }
    check(response.has_line_data);

    const auto directory = bus.coherence_hub().get_directory_state(line);
    check(directory.state == simrv::memory::MesiState::Shared);
    check(directory.sharers_mask == 0b11U);
    check(bus.coherence_hub().validate_directory().has_value());
    std::cout << "[PASS] test_timed_smp_coherence_ordering\n";
}

void test_global_cycle_smp_pipeline_ordering() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    const auto run = [&] {
        simrv::core::Machine machine(simrv::core::MachineConfig{.execution = {.appmode = false}});
        std::vector<Byte> ram(1024 * 1024, Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();

        auto secondary = std::make_unique<simrv::core::CPU>();
        secondary->machine_ = &machine;
        secondary->reset();
        secondary->state().mhartid = 1;
        secondary->hart_status.store(simrv::core::HartStatus::Started, std::memory_order_relaxed);
        machine.secondary_harts_.push_back(std::move(secondary));

        constexpr Address pc = simrv::memory::kDramBaseAddress;
        constexpr std::array<Instruction, 2> program = {
            0x00100093,  // addi x1, x0, 1
            0x0000006f,  // jal x0, 0
        };
        std::memcpy(ram.data(), program.data(), sizeof(program));
        machine.hart(0).state().pc = pc;
        machine.hart(1).state().pc = pc;
        const auto initial_mcycle0 = machine.hart(0).clint_mmio.mcycle;
        const auto initial_mcycle1 = machine.hart(1).clint_mmio.mcycle;

        std::array<uint32_t, 2> retirement_cycle{};
        uint32_t global_cycle = 0;
        while ((machine.hart(0).e_icount == 0 || machine.hart(1).e_icount == 0) &&
               global_cycle < 64) {
            machine.execute_cycle_for_testing();
            ++global_cycle;
            for (size_t hart = 0; hart < 2; ++hart) {
                if (machine.hart(hart).e_icount != 0 && retirement_cycle[hart] == 0) {
                    retirement_cycle[hart] = global_cycle;
                }
            }
        }
        check(machine.hart(0).state().regs.read(RegId::Ra) == 1);
        check(machine.hart(1).state().regs.read(RegId::Ra) == 1);
        check(machine.hart(0).clint_mmio.mcycle - initial_mcycle0 == global_cycle);
        check(machine.hart(1).clint_mmio.mcycle - initial_mcycle1 == global_cycle);
        return retirement_cycle;
    };

    const auto first = run();
    const auto second = run();
    check(first == second);
    constexpr std::array<uint32_t, 2> expected = std::array<uint32_t, 2>{10, 11};
    if (first != expected) {
        std::cerr << "unexpected SMP retirement cycles: " << first[0] << ", " << first[1] << '\n';
    }
    check(first == expected);
    std::cout << "[PASS] test_global_cycle_smp_pipeline_ordering\n";
}

void test_global_cycle_timer_phase_ordering() {
    simrv::core::Machine machine(simrv::core::MachineConfig{.execution = {.appmode = false}});
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
    machine.cpu.machine_ = &machine;
    machine.cpu.reset();

    auto secondary = std::make_unique<simrv::core::CPU>();
    secondary->machine_ = &machine;
    secondary->reset();
    secondary->state().mhartid = 1;
    secondary->hart_status.store(simrv::core::HartStatus::Started, std::memory_order_relaxed);
    machine.secondary_harts_.push_back(std::move(secondary));

    constexpr Address pc = simrv::memory::kDramBaseAddress;
    constexpr Instruction loop = 0x0000006f;  // jal x0, 0
    std::memcpy(ram.data(), &loop, sizeof(loop));
    machine.hart(0).state().pc = pc;
    machine.hart(1).state().pc = pc;
    machine.cpu.clint_mmio.mtime = 4;
    machine.cpu.clint_mmio.rtc_divider = 9;
    machine.cpu.clint_mmio.mtimecmp = 5;
    machine.cpu.clint_mmio.hart_mtimecmp.at(1) = 5;

    machine.execute_cycle_for_testing();

    assert(machine.hart(0).clint_mmio.mcycle == 1);
    assert(machine.hart(1).clint_mmio.mcycle == 1);
    assert(machine.hart(0).clint_mmio.mtime == 5);
    assert(machine.hart(1).clint_mmio.mtime == 5);
    assert((machine.hart(0).state().mip & enum_mask(simrv::core::MipBit::Mtip)) != 0);
    assert((machine.hart(1).state().mip & enum_mask(simrv::core::MipBit::Mtip)) != 0);
    std::cout << "[PASS] test_global_cycle_timer_phase_ordering\n";
}

void test_cycle_kernel_golden_forwarding() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    const auto run = [&](simrv::pipeline::PipelineType type) {
        ConcreteMachine machine;
        std::vector<Byte> ram(1024 * 1024, Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.pipeline_sim.config.pipeline_type = type;
        machine.cpu.pipeline_sim.config.enable_forwarding = true;

        constexpr Address pc = simrv::memory::kDramBaseAddress;
        constexpr std::array<Instruction, 4> program = {
            0x00100093,  // addi x1, x0, 1
            0x00108133,  // add  x2, x1, x1
            0x001101b3,  // add  x3, x2, x1
            0x0000006f,  // jal  x0, 0
        };
        std::array<Byte, simrv::cache::ICache::kLineBytes> line{};
        std::memcpy(line.data(), program.data(), sizeof(program));
        std::memcpy(ram.data(), program.data(), sizeof(program));
        machine.cpu.icache.insert(pc, line.data(), simrv::memory::MesiState::Exclusive);
        machine.cpu.state().pc = pc;

        uint32_t cycles = 0;
        while (machine.cpu.e_icount < 3 && cycles < 32) {
            machine.cpu.run_cycle(machine);
            machine.memory().system_bus().advance_cycle();
            ++cycles;
        }
        check(machine.cpu.e_icount == 3);
        check(machine.cpu.state().regs.read(RegId::Ra) == 1);
        check(machine.cpu.state().regs.read(RegId::Sp) == 2);
        check(machine.cpu.state().regs.read(RegId::Gp) == 3);
        return cycles;
    };

    check(run(simrv::pipeline::PipelineType::FiveStage) == 8);
    check(run(simrv::pipeline::PipelineType::ThreeStage) == 6);
    std::cout << "[PASS] test_cycle_kernel_golden_forwarding\n";
}

void test_cycle_kernel_golden_load_use() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    const auto run = [&](simrv::pipeline::PipelineType type) {
        ConcreteMachine machine;
        std::vector<Byte> ram(1024 * 1024, Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.pipeline_sim.config.pipeline_type = type;

        constexpr Address pc = simrv::memory::kDramBaseAddress;
        constexpr Address data_address = pc + 0x100;
        constexpr std::array<Instruction, 5> program = {
            0x00000097,  // auipc x1, 0
            0x10008093,  // addi  x1, x1, 256
            0x0000a103,  // lw    x2, 0(x1)
            0x002101b3,  // add   x3, x2, x2
            0x0000006f,  // jal   x0, 0
        };
        std::array<Byte, simrv::cache::ICache::kLineBytes> instruction_line{};
        std::memcpy(instruction_line.data(), program.data(), sizeof(program));
        std::memcpy(ram.data(), program.data(), sizeof(program));
        machine.cpu.icache.insert(pc, instruction_line.data(), simrv::memory::MesiState::Exclusive);

        std::array<Byte, simrv::cache::DCache::kLineBytes> data_line{};
        constexpr uint32_t value = 21;
        std::memcpy(data_line.data(), &value, sizeof(value));
        std::memcpy(ram.data() + 0x100, &value, sizeof(value));
        machine.cpu.dcache.insert(data_address, data_line.data(), simrv::memory::MesiState::Shared);
        machine.cpu.state().pc = pc;

        uint32_t cycles = 0;
        while (machine.cpu.e_icount < 4 && cycles < 32) {
            machine.cpu.run_cycle(machine);
            machine.memory().system_bus().advance_cycle();
            ++cycles;
        }
        check(machine.cpu.e_icount == 4);
        check(machine.cpu.state().regs.read(RegId::Sp) == value);
        check(machine.cpu.state().regs.read(RegId::Gp) == value * 2);
        check(machine.cpu.dcache.miss_count() == 0);
        check(machine.cpu.pipeline_sim.data_hazard_stalls() ==
              (type == simrv::pipeline::PipelineType::FiveStage ? 1 : 0));
        return cycles;
    };

    check(run(simrv::pipeline::PipelineType::FiveStage) == 10);
    check(run(simrv::pipeline::PipelineType::ThreeStage) == 7);
    std::cout << "[PASS] test_cycle_kernel_golden_load_use\n";
}

void test_cycle_kernel_golden_data_refill() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    const auto run = [&](simrv::pipeline::PipelineType type) {
        ConcreteMachine machine;
        std::vector<Byte> ram(1024 * 1024, Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.pipeline_sim.config.pipeline_type = type;
        machine.memory().initialize_mmu();

        constexpr Address pc = simrv::memory::kDramBaseAddress;
        constexpr std::array<Instruction, 4> program = {
            0x00000097,  // auipc x1, 0
            0x10008093,  // addi  x1, x1, 256
            0x0000a103,  // lw    x2, 0(x1)
            0x0000006f,  // jal   x0, 0
        };
        std::array<Byte, simrv::cache::ICache::kLineBytes> instruction_line{};
        std::memcpy(instruction_line.data(), program.data(), sizeof(program));
        std::memcpy(ram.data(), program.data(), sizeof(program));
        constexpr uint32_t value = 37;
        std::memcpy(ram.data() + 0x100, &value, sizeof(value));
        machine.cpu.icache.insert(pc, instruction_line.data(), simrv::memory::MesiState::Exclusive);
        machine.cpu.state().pc = pc;

        uint32_t cycles = 0;
        while (machine.cpu.e_icount < 3 && cycles < 32) {
            machine.cpu.run_cycle(machine);
            machine.memory().system_bus().advance_cycle();
            ++cycles;
        }
        check(machine.cpu.e_icount == 3);
        check(machine.cpu.state().regs.read(RegId::Sp) == value);
        check(machine.cpu.dcache.miss_count() == 1);
        check(machine.cpu.pipeline_sim.dcache_stalls() >= 1);
        return cycles;
    };

    const auto five_cycles = run(simrv::pipeline::PipelineType::FiveStage);
    const auto three_cycles = run(simrv::pipeline::PipelineType::ThreeStage);
    // The interconnect owns refill timing. A cache hit completes in its memory stage, so the
    // configured one-cycle hit latency does not add a second pipeline delay.
    if (five_cycles != 12 || three_cycles != 10) {
        std::cerr << "unexpected data-refill cycles: " << five_cycles << ", " << three_cycles
                  << '\n';
    }
    check(five_cycles == 12);
    check(three_cycles == 10);
    std::cout << "[PASS] test_cycle_kernel_golden_data_refill\n";
}

void test_cycle_kernel_golden_fence_serialization() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    const auto run = [&](simrv::pipeline::PipelineType type) {
        ConcreteMachine machine;
        std::vector<Byte> ram(1024 * 1024, Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.pipeline_sim.config.pipeline_type = type;
        machine.cpu.pipeline_sim.config.fence_flush_penalty = 2;

        constexpr Address pc = simrv::memory::kDramBaseAddress;
        constexpr std::array<Instruction, 4> program = {
            0x00100093,  // addi x1, x0, 1
            0x0ff0000f,  // fence iorw, iorw
            0x00200113,  // addi x2, x0, 2
            0x0000006f,  // jal x0, 0
        };
        std::array<Byte, simrv::cache::ICache::kLineBytes> line{};
        std::memcpy(line.data(), program.data(), sizeof(program));
        std::memcpy(ram.data(), program.data(), sizeof(program));
        machine.cpu.icache.insert(pc, line.data(), simrv::memory::MesiState::Exclusive);
        machine.cpu.state().pc = pc;

        uint32_t cycles = 0;
        while (machine.cpu.e_icount < 3 && cycles < 32) {
            machine.cpu.run_cycle(machine);
            machine.memory().system_bus().advance_cycle();
            ++cycles;
        }
        check(machine.cpu.e_icount == 3);
        check(machine.cpu.state().regs.read(RegId::Ra) == 1);
        check(machine.cpu.state().regs.read(RegId::Sp) == 2);
        return cycles;
    };

    check(run(simrv::pipeline::PipelineType::FiveStage) == 16);
    check(run(simrv::pipeline::PipelineType::ThreeStage) == 10);
    std::cout << "[PASS] test_cycle_kernel_golden_fence_serialization\n";
}

void test_cycle_kernel_golden_multicycle_execute() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    const auto run = [&](simrv::pipeline::PipelineType type) {
        ConcreteMachine machine;
        std::vector<Byte> ram(1024 * 1024, Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.pipeline_sim.config.pipeline_type = type;
        machine.cpu.pipeline_sim.config.mul_latency = 3;

        constexpr Address pc = simrv::memory::kDramBaseAddress;
        constexpr std::array<Instruction, 4> program = {
            0x00600093,  // addi x1, x0, 6
            0x00700113,  // addi x2, x0, 7
            0x022081b3,  // mul  x3, x1, x2
            0x0000006f,  // jal  x0, 0
        };
        std::array<Byte, simrv::cache::ICache::kLineBytes> line{};
        std::memcpy(line.data(), program.data(), sizeof(program));
        std::memcpy(ram.data(), program.data(), sizeof(program));
        machine.cpu.icache.insert(pc, line.data(), simrv::memory::MesiState::Exclusive);
        machine.cpu.state().pc = pc;

        uint32_t cycles = 0;
        while (machine.cpu.e_icount < 3 && cycles < 32) {
            machine.cpu.run_cycle(machine);
            machine.memory().system_bus().advance_cycle();
            ++cycles;
        }
        check(machine.cpu.e_icount == 3);
        check(machine.cpu.state().regs.read(RegId::Gp) == 42);
        return cycles;
    };

    check(run(simrv::pipeline::PipelineType::FiveStage) == 10);
    check(run(simrv::pipeline::PipelineType::ThreeStage) == 8);
    std::cout << "[PASS] test_cycle_kernel_golden_multicycle_execute\n";
}

void test_cycle_kernel_golden_branch_recovery() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    const auto run = [&](simrv::pipeline::PipelineType type) {
        ConcreteMachine machine;
        std::vector<Byte> ram(1024 * 1024, Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.pipeline_sim.config.pipeline_type = type;

        constexpr Address pc = simrv::memory::kDramBaseAddress;
        constexpr std::array<Instruction, 6> program = {
            0x00100093,  // addi x1, x0, 1
            0x00008463,  // beq  x1, x0, +8 (not taken)
            0x00108463,  // beq  x1, x1, +8 (taken)
            0x06300113,  // addi x2, x0, 99 (must be squashed)
            0x00700193,  // addi x3, x0, 7
            0x0000006f,  // jal  x0, 0
        };
        std::array<Byte, simrv::cache::ICache::kLineBytes> line{};
        std::memcpy(line.data(), program.data(), sizeof(program));
        std::memcpy(ram.data(), program.data(), sizeof(program));
        machine.cpu.icache.insert(pc, line.data(), simrv::memory::MesiState::Exclusive);
        machine.cpu.state().pc = pc;

        uint32_t cycles = 0;
        while (machine.cpu.e_icount < 4 && cycles < 64) {
            machine.cpu.run_cycle(machine);
            machine.memory().system_bus().advance_cycle();
            ++cycles;
        }
        check(machine.cpu.e_icount == 4);
        check(machine.cpu.state().regs.read(RegId::Sp) == 0);
        check(machine.cpu.state().regs.read(RegId::Gp) == 7);
        return cycles;
    };

    check(run(simrv::pipeline::PipelineType::FiveStage) == 11);
    check(run(simrv::pipeline::PipelineType::ThreeStage) == 8);
    std::cout << "[PASS] test_cycle_kernel_golden_branch_recovery\n";
}

void test_cycle_kernel_golden_instruction_refill() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    const auto run = [&](simrv::pipeline::PipelineType type) {
        ConcreteMachine machine;
        std::vector<Byte> ram(1024 * 1024, Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.pipeline_sim.config.pipeline_type = type;

        constexpr Address pc = simrv::memory::kDramBaseAddress;
        constexpr std::array<Instruction, 2> program = {
            0x02a00093,  // addi x1, x0, 42
            0x0000006f,  // jal  x0, 0
        };
        std::memcpy(ram.data(), program.data(), sizeof(program));
        machine.cpu.state().pc = pc;

        uint32_t cycles = 0;
        while (machine.cpu.e_icount < 1 && cycles < 64) {
            machine.cpu.run_cycle(machine);
            machine.memory().system_bus().advance_cycle();
            ++cycles;
        }
        check(machine.cpu.e_icount == 1);
        check(machine.cpu.state().regs.read(RegId::Ra) == 42);
        check(machine.cpu.icache.miss_count() == 1);
        return cycles;
    };

    const auto five_stage = run(simrv::pipeline::PipelineType::FiveStage);
    const auto three_stage = run(simrv::pipeline::PipelineType::ThreeStage);
    constexpr auto expected_five = 10u;
    constexpr auto expected_three = 8u;
    if (five_stage != expected_five || three_stage != expected_three) {
        std::cerr << "unexpected coherent refill cycles: " << five_stage << ", " << three_stage
                  << '\n';
    }
    check(five_stage == expected_five);
    check(three_stage == expected_three);
    std::cout << "[PASS] test_cycle_kernel_golden_instruction_refill\n";
}

void test_cycle_kernel_golden_precise_trap() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    const auto run = [&](simrv::pipeline::PipelineType type) {
        ConcreteMachine machine;
        std::vector<Byte> ram(1024 * 1024, Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.pipeline_sim.config.pipeline_type = type;

        constexpr Address pc = simrv::memory::kDramBaseAddress;
        constexpr Address handler = pc + 16;
        constexpr std::array<Instruction, 6> program = {
            0xffffffff,  // illegal instruction
            0x00900093,  // addi x1, x0, 9 (must be squashed)
            0x00000013,  // nop
            0x00000013,  // nop
            0x00100113,  // handler: addi x2, x0, 1
            0x0000006f,  // jal x0, 0
        };
        std::array<Byte, simrv::cache::ICache::kLineBytes> line{};
        std::memcpy(line.data(), program.data(), sizeof(program));
        std::memcpy(ram.data(), program.data(), sizeof(program));
        machine.cpu.icache.insert(pc, line.data(), simrv::memory::MesiState::Exclusive);
        machine.cpu.state().pc = pc;
        machine.cpu.state().mtvec = handler;

        uint32_t cycles = 0;
        while (machine.cpu.state().mcause == 0 && cycles < 32) {
            machine.cpu.run_cycle(machine);
            machine.memory().system_bus().advance_cycle();
            ++cycles;
        }
        check(machine.cpu.state().mcause ==
              static_cast<TrapCause>(ExceptionCode::IllegalInstruction));
        check(machine.cpu.state().mepc == pc);
        check(machine.cpu.state().pc == handler);
        check(machine.cpu.state().regs.read(RegId::Ra) == 0);
        check(machine.cpu.e_icount == 0);
        return cycles;
    };

    check(run(simrv::pipeline::PipelineType::FiveStage) == 6);
    check(run(simrv::pipeline::PipelineType::ThreeStage) == 4);
    std::cout << "[PASS] test_cycle_kernel_golden_precise_trap\n";
}

void test_cycle_kernel_golden_interrupt_boundary() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    const auto run = [&](simrv::pipeline::PipelineType type) {
        ConcreteMachine machine;
        std::vector<Byte> ram(1024 * 1024, Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.pipeline_sim.config.pipeline_type = type;

        constexpr Address pc = simrv::memory::kDramBaseAddress;
        constexpr Address handler = pc + 16;
        constexpr std::array<Instruction, 6> program = {
            0x00100093,  // addi x1, x0, 1 (retires before interrupt)
            0x00200113,  // addi x2, x0, 2 (must be squashed)
            0x00000013,  // nop
            0x00000013,  // nop
            0x00100193,  // handler: addi x3, x0, 1
            0x0000006f,  // jal x0, 0
        };
        std::array<Byte, simrv::cache::ICache::kLineBytes> line{};
        std::memcpy(line.data(), program.data(), sizeof(program));
        std::memcpy(ram.data(), program.data(), sizeof(program));
        machine.cpu.icache.insert(pc, line.data(), simrv::memory::MesiState::Exclusive);
        machine.cpu.state().pc = pc;
        machine.cpu.state().mtvec = handler;
        machine.cpu.state().mstatus |= enum_mask(simrv::core::MstatusBit::Mie);
        machine.cpu.state().mie |= enum_mask(simrv::core::MipBit::Mtip);
        machine.cpu.state().mip |= enum_mask(simrv::core::MipBit::Mtip);

        uint32_t cycles = 0;
        while (!trap_is_interrupt(machine.cpu.state().mcause) && cycles < 32) {
            machine.cpu.run_cycle(machine);
            machine.memory().system_bus().advance_cycle();
            ++cycles;
        }
        check(machine.cpu.state().mcause == (kInterruptCauseBit | TrapCause{7}));
        check(machine.cpu.state().mepc == pc + 4);
        check(machine.cpu.state().pc == handler);
        check(machine.cpu.state().regs.read(RegId::Ra) == 1);
        check(machine.cpu.state().regs.read(RegId::Sp) == 0);
        check(machine.cpu.e_icount == 1);
        return cycles;
    };

    check(run(simrv::pipeline::PipelineType::FiveStage) == 6);
    check(run(simrv::pipeline::PipelineType::ThreeStage) == 4);
    std::cout << "[PASS] test_cycle_kernel_golden_interrupt_boundary\n";
}

void test_cycle_policy_architectural_equivalence() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    struct Result {
        Register x1 = 0;
        Register x2 = 0;
        Register x3 = 0;
        Word memory_value = 0;
        Counter retired = 0;
        uint64_t cycles = 0;
        size_t history_size = 0;
        uint64_t data_misses = 0;
    };
    const auto run = [&](simrv::core::ExecutionEngine engine,
                         simrv::pipeline::PipelineType pipeline_type) {
        ConcreteMachine machine;
        std::vector<Byte> ram(1024 * 1024, Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.memory().initialize_mmu();
        machine.runtime_profile.engine = engine;
        machine.cpu.pipeline_sim.config.pipeline_type = pipeline_type;
        machine.cpu.pipeline_sim.config.record_snapshots =
            engine == simrv::core::ExecutionEngine::CycleObservable;

        constexpr Address pc = simrv::memory::kDramBaseAddress;
        constexpr Address data_address = pc + 0x100;
        constexpr std::array<Instruction, 7> program = {
            0x02a00093,  // addi  x1, x0, 42
            0x00000117,  // auipc x2, 0
            0x0fc10113,  // addi  x2, x2, 252 -> pc + 0x100
            0x00112023,  // sw    x1, 0(x2)
            0x00012183,  // lw    x3, 0(x2)
            0x10500073,  // wfi
            0xffdff06f,  // jal   x0, -4 (repeat cached WFI)
        };
        std::array<Byte, simrv::cache::ICache::kLineBytes> instruction_line{};
        std::memcpy(instruction_line.data(), program.data(), sizeof(program));
        std::memcpy(ram.data(), program.data(), sizeof(program));
        machine.cpu.icache.insert(pc, instruction_line.data(), simrv::memory::MesiState::Exclusive);
        machine.cpu.state().pc = pc;

        uint32_t guard = 0;
        while (machine.cpu.e_icount < 5 && guard++ < 64) {
            machine.cpu.run_cycle(machine);
            machine.memory().system_bus().advance_cycle();
        }
        Word memory_value = 0;
        if (simrv::core::is_cycle_engine(engine)) {
            check(machine.cpu.dcache.read(data_address, memory_value,
                                          static_cast<Instruction>(simrv::isa::Funct3::Lw)));
        } else {
            std::memcpy(&memory_value, ram.data() + 0x100, sizeof(memory_value));
        }
        return Result{.x1 = machine.cpu.state().regs.read(RegId::Ra),
                      .x2 = machine.cpu.state().regs.read(RegId::Sp),
                      .x3 = machine.cpu.state().regs.read(RegId::Gp),
                      .memory_value = memory_value,
                      .retired = machine.cpu.e_icount,
                      .cycles = machine.cpu.pipeline_sim.cycle_count(),
                      .history_size = machine.cpu.pipeline_sim.cycle_history().size(),
                      .data_misses = machine.cpu.dcache.miss_count()};
    };

    const auto functional = run(simrv::core::ExecutionEngine::InstructionFast,
                                simrv::pipeline::PipelineType::FiveStage);
    const auto fast_five =
        run(simrv::core::ExecutionEngine::CycleFast, simrv::pipeline::PipelineType::FiveStage);
    const auto observable_five = run(simrv::core::ExecutionEngine::CycleObservable,
                                     simrv::pipeline::PipelineType::FiveStage);
    const auto fast_three =
        run(simrv::core::ExecutionEngine::CycleFast, simrv::pipeline::PipelineType::ThreeStage);
    const auto observable_three = run(simrv::core::ExecutionEngine::CycleObservable,
                                      simrv::pipeline::PipelineType::ThreeStage);
    const auto check_architectural_equivalence = [&](std::string_view model,
                                                     const Result& candidate) {
        const auto compare = [&](std::string_view field, auto expected, auto actual) {
            if (expected != actual) {
                std::cerr << "semantic mismatch: model=" << model
                          << " hart=0 retirement=" << candidate.retired << " field=" << field
                          << " expected=" << expected << " actual=" << actual << '\n';
                std::abort();
            }
        };
        compare("x1", functional.x1, candidate.x1);
        compare("x2", functional.x2, candidate.x2);
        compare("x3", functional.x3, candidate.x3);
        compare("memory[0x100]", functional.memory_value, candidate.memory_value);
        compare("retired", functional.retired, candidate.retired);
    };
    check(functional.x1 == 42 && functional.x2 == simrv::memory::kDramBaseAddress + 0x100 &&
          functional.x3 == 42 && functional.memory_value == 42 && functional.retired == 5);
    check_architectural_equivalence("five-stage-ca-fast", fast_five);
    check_architectural_equivalence("five-stage-ca-observable", observable_five);
    check_architectural_equivalence("three-stage-ca-fast", fast_three);
    check_architectural_equivalence("three-stage-ca-observable", observable_three);
    check(fast_five.cycles == observable_five.cycles);
    check(fast_three.cycles == observable_three.cycles);
    check(fast_five.data_misses == observable_five.data_misses);
    check(fast_three.data_misses == observable_three.data_misses);
    check(fast_five.history_size == 0 && observable_five.history_size == observable_five.cycles);
    check(fast_three.history_size == 0 && observable_three.history_size == observable_three.cycles);
    std::cout << "[PASS] test_cycle_policy_architectural_equivalence\n";
}

void test_instruction_policy_architectural_equivalence() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    struct Result {
        Register x1 = 0;
        Register x2 = 0;
        Register x3 = 0;
        Address pc = 0;
        uint32_t memory_value = 0;
        Counter retired = 0;
        Counter cycles = 0;
    };
    const auto run = [](simrv::core::ExecutionEngine engine) {
        ConcreteMachine machine;
        std::vector<Byte> ram(1024 * 1024, Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.memory().initialize_mmu();
        machine.runtime_profile.engine = engine;

        constexpr Address pc = simrv::memory::kDramBaseAddress;
        constexpr std::array<Instruction, 6> program = {
            0x02a00093,  // addi  x1, x0, 42
            0x00000117,  // auipc x2, 0
            0x0fc10113,  // addi  x2, x2, 252 -> pc + 0x100
            0x00112023,  // sw    x1, 0(x2)
            0x00012183,  // lw    x3, 0(x2)
            0x0000006f,  // jal   x0, 0
        };
        std::memcpy(ram.data(), program.data(), sizeof(program));
        machine.cpu.state().pc = pc;

        uint32_t guard = 0;
        while (machine.cpu.e_icount < 8 && guard++ < 16) {
            machine.cpu.run_cycle(machine);
        }
        uint32_t memory_value = 0;
        std::memcpy(&memory_value, ram.data() + 0x100, sizeof(memory_value));
        return Result{.x1 = machine.cpu.state().regs.read(RegId::Ra),
                      .x2 = machine.cpu.state().regs.read(RegId::Sp),
                      .x3 = machine.cpu.state().regs.read(RegId::Gp),
                      .pc = machine.cpu.state().pc,
                      .memory_value = memory_value,
                      .retired = machine.cpu.e_icount,
                      .cycles = machine.cpu.clint_mmio.mcycle};
    };

    const auto fast = run(simrv::core::ExecutionEngine::InstructionFast);
    const auto observable = run(simrv::core::ExecutionEngine::InstructionObservable);
    check(fast.x1 == 42 && fast.x2 == simrv::memory::kDramBaseAddress + 0x100 && fast.x3 == 42);
    check(fast.memory_value == 42 && fast.retired == 8);
    check(fast.x1 == observable.x1 && fast.x2 == observable.x2 && fast.x3 == observable.x3);
    check(fast.pc == observable.pc && fast.memory_value == observable.memory_value);
    check(fast.retired == observable.retired && fast.cycles == observable.cycles);
    std::cout << "[PASS] test_instruction_policy_architectural_equivalence\n";
}

void test_aclint() {
    ConcreteMachine machine;
    machine.cpu.reset();

    simrv::device::AclintMtimer mtimer(&machine);
    simrv::device::AclintMswi mswi(&machine);

    // Initial state: no timer / software interrupts
    assert((machine.cpu.state().mip & enum_mask(simrv::core::MipBit::Mtip)) == 0);
    assert((machine.cpu.state().mip & enum_mask(simrv::core::MipBit::Msip)) == 0);

    // MSWI test
    mswi.write32(0x00, 1);
    assert((machine.cpu.state().mip & enum_mask(simrv::core::MipBit::Msip)) != 0);
    assert(mswi.read32(0x00) == 1);
    mswi.write32(0x00, 0);
    assert((machine.cpu.state().mip & enum_mask(simrv::core::MipBit::Msip)) == 0);

    // MTIMER test: set mtimecmp = 100, mtime = 150
    mtimer.write64(0x4000, 100);
    mtimer.write64(0x7ff8, 150);
    assert((machine.cpu.state().mip & enum_mask(simrv::core::MipBit::Mtip)) != 0);

    // Clear by setting mtimecmp higher
    mtimer.write64(0x4000, 200);
    assert((machine.cpu.state().mip & enum_mask(simrv::core::MipBit::Mtip)) == 0);
    std::cout << "[PASS] test_aclint\n";
}

void test_aia_aplic_and_imsic() {
    ConcreteMachine machine;
    machine.cpu.reset();

    simrv::device::Imsic imsic_s(&machine, simrv::device::Imsic::Privilege::Supervisor,
                                 simrv::mmio::kImsicSBaseAddress, simrv::mmio::kImsicSSize);
    simrv::device::Aplic aplic_s(&machine, simrv::device::Aplic::Privilege::Supervisor,
                                 simrv::mmio::kAplicSBaseAddress, simrv::mmio::kAplicSSize,
                                 &imsic_s);

    // Direct mode interrupt testing
    aplic_s.write32(0x0000, 0x100);    // IE=1, DM=0 (Direct mode)
    aplic_s.write32(0x1e00, 1U << 1);  // Enable source 1
    aplic_s.set_irq(1, true);

    assert((machine.cpu.state().mip & enum_mask(simrv::core::MipBit::Seip)) != 0);

    // MSI delivery via IMSIC testing
    imsic_s.csr_write(0, 0x70, 1);        // eidelivery = 1
    imsic_s.csr_write(0, 0xC0, 1U << 5);  // enable interrupt ID 5
    imsic_s.trigger_msi(0, 5);

    assert((machine.cpu.state().mip & enum_mask(simrv::core::MipBit::Seip)) != 0);
    assert((imsic_s.csr_read(0, 0x44) & 0xffff) == 5);  // topei returns ID 5
    std::cout << "[PASS] test_aia_aplic_and_imsic\n";
}

void test_pcie_and_virtio_pci() {
    ConcreteMachine machine;
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());

    simrv::device::PcieRootComplex rc(&machine);
    auto blk = std::make_shared<simrv::device::VirtioPciBlock>();
    rc.attach_device(0, 1, 0, blk);

    // Test ECAM discovery of device at Bus 0, Dev 1, Func 0
    // Offset in ECAM: (0 << 20) | (1 << 15) | (0 << 12) = 0x8000
    const uint32_t vendor_device = rc.ecam_read(0x8000 + 0x00, 4);
    assert((vendor_device & 0xffff) == 0x1AF4);  // VirtIO Vendor ID
    assert((vendor_device >> 16) == 0x1042);     // Modern VirtIO-PCI Block ID
    (void)vendor_device;

    // BAR 0 sizing and assignment
    rc.ecam_write(0x8000 + 0x10, 0xffffffffU, 4);
    const uint32_t bar_size_query = rc.ecam_read(0x8000 + 0x10, 4);
    assert((bar_size_query & ~0xfU) == ~(0x1000U - 1U));
    (void)bar_size_query;

    // Assign BAR 0 address in MMIO window: 0x40001000
    rc.ecam_write(0x8000 + 0x10, 0x40001000U, 4);

    // Attach Console (Slot 2), RNG (Slot 3), GPU (Slot 4), Input (Slot 5), Sound (Slot 6)
    auto console = std::make_shared<simrv::device::VirtioPciConsole>();
    auto rng = std::make_shared<simrv::device::VirtioPciRng>();
    auto gpu = std::make_shared<simrv::device::VirtioPciGpu>();
    auto input = std::make_shared<simrv::device::VirtioPciInput>();
    auto sound = std::make_shared<simrv::device::VirtioPciSound>();

    rc.attach_device(0, 2, 0, console);
    rc.attach_device(0, 3, 0, rng);
    rc.attach_device(0, 4, 0, gpu);
    rc.attach_device(0, 5, 0, input);
    rc.attach_device(0, 6, 0, sound);

    // Verify Console at 00:02.0 (ECAM offset 0x10000)
    const uint32_t console_id = rc.ecam_read((2 << 15) + 0x00, 4);
    assert((console_id & 0xffff) == 0x1AF4);
    assert((console_id >> 16) == 0x1043);
    (void)console_id;

    // Verify RNG at 00:03.0 (ECAM offset 0x18000)
    const uint32_t rng_id = rc.ecam_read((3 << 15) + 0x00, 4);
    assert((rng_id & 0xffff) == 0x1AF4);
    assert((rng_id >> 16) == 0x1044);
    (void)rng_id;

    // Verify GPU at 00:04.0 (ECAM offset 0x20000)
    const uint32_t gpu_id = rc.ecam_read((4 << 15) + 0x00, 4);
    assert((gpu_id & 0xffff) == 0x1AF4);
    assert((gpu_id >> 16) == 0x1050);
    (void)gpu_id;

    // Verify Input at 00:05.0 (ECAM offset 0x28000)
    const uint32_t input_id = rc.ecam_read((5 << 15) + 0x00, 4);
    assert((input_id & 0xffff) == 0x1AF4);
    assert((input_id >> 16) == 0x1052);
    (void)input_id;

    // Verify Sound at 00:06.0 (ECAM offset 0x30000)
    const uint32_t sound_id = rc.ecam_read((6 << 15) + 0x00, 4);
    assert((sound_id & 0xffff) == 0x1AF4);
    assert((sound_id >> 16) == 0x1059);
    (void)sound_id;

    // Attach & Verify Net at 00:07.0 (ECAM offset 0x38000)
    auto net = std::make_shared<simrv::device::VirtioPciNet>();
    rc.attach_device(0, 7, 0, net);
    const uint32_t net_id = rc.ecam_read((7 << 15) + 0x00, 4);
    assert((net_id & 0xffff) == 0x1AF4);
    assert((net_id >> 16) == 0x1041);
    (void)net_id;

    std::cout << "[PASS] test_pcie_and_virtio_pci\n";
}

void test_virtio_mmio_v2() {
    ConcreteMachine machine;
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());

    simrv::device::VirtioMmioBlock blk(0x10001000, 2, &machine);
    simrv::device::VirtioMmioConsole con(0x10002000, 1, &machine);
    simrv::device::VirtioMmioRng rng(0x10003000, 4, &machine);
    simrv::device::VirtioMmioNet net(0x10007000, 8, &machine);

    // Test Magic (0x74726976) and Version 2
    assert(blk.read32(0x00) == 0x74726976);
    assert(blk.read32(0x04) == 2);
    assert(blk.read32(0x08) == 2);  // Block Device ID

    assert(con.read32(0x00) == 0x74726976);
    assert(con.read32(0x04) == 2);
    assert(con.read32(0x08) == 3);  // Console Device ID

    assert(rng.read32(0x00) == 0x74726976);
    assert(rng.read32(0x04) == 2);
    assert(rng.read32(0x08) == 4);  // RNG Device ID

    assert(net.read32(0x00) == 0x74726976);
    assert(net.read32(0x04) == 2);
    assert(net.read32(0x08) == 1);  // Net Device ID

    // Test Queue config and status transitions
    blk.write32(0x30, 0);            // queue_sel = 0
    assert(blk.read32(0x34) == 64);  // queue_num_max = 64
    blk.write32(0x38, 32);           // queue_num = 32
    blk.write32(0x80, 0x80001000);   // desc_lo
    blk.write32(0x84, 0);            // desc_hi
    blk.write32(0x90, 0x80002000);   // driver_lo
    blk.write32(0x94, 0);            // driver_hi
    blk.write32(0xA0, 0x80003000);   // device_lo
    blk.write32(0xA4, 0);            // device_hi
    blk.write32(0x44, 1);            // queue_ready = 1
    assert(blk.read32(0x44) == 1);

    const auto* q = blk.get_queue_state(0);
    assert(q != nullptr);
    assert(q->num == 32);
    assert(q->desc_addr == 0x80001000);
    assert(q->driver_addr == 0x80002000);
    assert(q->device_addr == 0x80003000);
    assert(q->ready == 1);
    (void)q;

    // Test Device Status
    blk.write32(0x70, 0x0F);  // DRIVER_OK
    assert(blk.device_status() == 0x0F);

    std::cout << "[PASS] test_virtio_mmio_v2\n";
}

void test_sbi_multihart_rfence() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    simrv::core::Machine machine(simrv::core::MachineConfig{.execution = {.appmode = false}});
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::InstructionFast;
    machine.cpu.machine_ = &machine;
    machine.cpu.reset();

    for (uint32_t i = 1; i <= 2; ++i) {
        auto sec = std::make_unique<simrv::core::CPU>();
        sec->machine_ = &machine;
        sec->reset();
        sec->state().mhartid = i;
        sec->hart_status.store(simrv::core::HartStatus::Started, std::memory_order_relaxed);
        machine.secondary_harts_.push_back(std::move(sec));
    }

    // Insert TLB entry in Hart 1 and Hart 2
    constexpr Address vaddr = 0x80004000;
    constexpr Address paddr = 0x80008000;
    machine.hart(1).tlb.insert_inst_r(vaddr, paddr, 0, PrivilegeLevel::Supervisor);
    machine.hart(2).tlb.insert_inst_r(vaddr, paddr, 0, PrivilegeLevel::Supervisor);
    check(machine.hart(1).tlb.lookup_inst_r(vaddr, 0, PrivilegeLevel::Supervisor) != nullptr);
    check(machine.hart(2).tlb.lookup_inst_r(vaddr, 0, PrivilegeLevel::Supervisor) != nullptr);

    // Broadcast RemoteSfenceVma from Hart 0 targeting Hart 1 and Hart 2 (mask = 0b110, base = 0)
    machine.cpu.state().regs.write(RegId::A0, 0b110);       // hart_mask
    machine.cpu.state().regs.write(RegId::A1, 0);           // hart_mask_base
    machine.cpu.state().regs.write(RegId::A7, 0x52464E43);  // ExtId::Rfence
    machine.cpu.state().regs.write(RegId::A6, 1);           // RfenceFid::RemoteSfenceVma
    check(machine.cpu.sbi.handle_ecall(enum_mask(ExceptionCode::SupervisorEcall)));
    check(machine.cpu.state().regs.read(RegId::A0) == 0);  // SbiError::Success

    // Verify secondary harts had TLB flushed
    check(machine.hart(1).tlb.lookup_inst_r(vaddr, 0, PrivilegeLevel::Supervisor) == nullptr);
    check(machine.hart(2).tlb.lookup_inst_r(vaddr, 0, PrivilegeLevel::Supervisor) == nullptr);

    // Test RemoteFenceI with base == -1 (all harts broadcast)
    machine.cpu.state().regs.write(RegId::A0, 0);                      // ignored when base is -1
    machine.cpu.state().regs.write(RegId::A1, static_cast<Word>(-1));  // all harts
    machine.cpu.state().regs.write(RegId::A7, 0x52464E43);             // ExtId::Rfence
    machine.cpu.state().regs.write(RegId::A6, 0);                      // RfenceFid::RemoteFenceI
    check(machine.cpu.sbi.handle_ecall(enum_mask(ExceptionCode::SupervisorEcall)));
    check(machine.cpu.state().regs.read(RegId::A0) == 0);

    // Test send_ipi broadcast
    machine.cpu.state().regs.write(RegId::A0, 0b10);      // hart 1 only
    machine.cpu.state().regs.write(RegId::A1, 0);         // base 0
    machine.cpu.state().regs.write(RegId::A7, 0x735049);  // ExtId::Ipi
    machine.cpu.state().regs.write(RegId::A6, 0);         // send_ipi
    check(machine.cpu.sbi.handle_ecall(enum_mask(ExceptionCode::SupervisorEcall)));
    check(machine.cpu.state().regs.read(RegId::A0) == 0);
    check((machine.hart(1).state().mip & enum_mask(simrv::core::MipBit::Ssip)) != 0);
    check((machine.hart(2).state().mip & enum_mask(simrv::core::MipBit::Ssip)) == 0);

    std::cout << "[PASS] test_sbi_multihart_rfence\n";
}

void test_ia_multihart_lr_sc_coherence() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    simrv::core::Machine machine(simrv::core::MachineConfig{.execution = {.appmode = false}});
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::InstructionFast;
    machine.cpu.machine_ = &machine;
    machine.cpu.reset();

    auto secondary = std::make_unique<simrv::core::CPU>();
    secondary->machine_ = &machine;
    secondary->reset();
    secondary->state().mhartid = 1;
    secondary->hart_status.store(simrv::core::HartStatus::Started, std::memory_order_relaxed);
    machine.secondary_harts_.push_back(std::move(secondary));

    constexpr Address target_addr = simrv::memory::kDramBaseAddress + 0x200;
    constexpr uint32_t initial_val = 42;
    std::memcpy(&ram[0x200], &initial_val, sizeof(initial_val));

    // 1. Hart 0 executes LR.W
    auto& h0 = machine.hart(0);
    h0.state().regs.write(RegId::A0, target_addr);
    h0.pipeline_context = {};
    h0.pipeline_context.opcode = simrv::isa::Opcode::Amo;
    h0.pipeline_context.funct5 = simrv::isa::Funct5Amo::Lr;
    h0.pipeline_context.funct3 = simrv::isa::Funct3::Lw;
    h0.pipeline_context.mem_addr = target_addr;
    h0.pipeline_context.rrs1 = target_addr;
    h0.pipeline_context.rd = RegId::A1;
    h0.run_memory_stage_baremetal(machine);
    check(h0.state().load_res == target_addr);
    check(h0.state().reserved == 1);
    check(machine.memory().reservation_table().check_reservation(0, target_addr));

    // 2. Hart 1 performs a store to the same address in IA mode
    auto& h1 = machine.hart(1);
    h1.state().regs.write(RegId::A0, target_addr);
    h1.state().regs.write(RegId::A1, 99);
    h1.pipeline_context = {};
    h1.pipeline_context.opcode = simrv::isa::Opcode::Store;
    h1.pipeline_context.funct3 = simrv::isa::Funct3::Sw;
    h1.pipeline_context.mem_addr = target_addr;
    h1.pipeline_context.rrs1 = target_addr;
    h1.pipeline_context.rrs2 = 99;
    h1.run_memory_stage_baremetal(machine);

    // 3. Hart 0's reservation must now be invalidated
    check(!machine.memory().reservation_table().check_reservation(0, target_addr));

    // 4. Hart 0 attempts SC.W, which must fail
    h0.pipeline_context = {};
    h0.pipeline_context.opcode = simrv::isa::Opcode::Amo;
    h0.pipeline_context.funct5 = simrv::isa::Funct5Amo::Sc;
    h0.pipeline_context.funct3 = simrv::isa::Funct3::Sw;
    h0.pipeline_context.mem_addr = target_addr;
    h0.pipeline_context.rrs1 = target_addr;
    h0.pipeline_context.rrs2 = 100;
    h0.pipeline_context.rd = RegId::A2;
    (void)h0.execute_stage(machine);
    check(h0.pipeline_context.wb_data == 1);  // 1 = SC failure

    // Verify memory was not overwritten with 100
    uint32_t val_in_ram = 0;
    std::memcpy(&val_in_ram, &ram[0x200], sizeof(val_in_ram));
    check(val_in_ram == 99);

    std::cout << "[PASS] test_ia_multihart_lr_sc_coherence\n";
}

void test_ia_multithreaded_smp_execution() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    simrv::core::Machine machine(
        simrv::core::MachineConfig{.execution = {.appmode = true, .smp_multithreaded = true}});
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::InstructionFast;
    machine.set_smp_parallel_for_testing(true);
    machine.cpu.machine_ = &machine;
    machine.cpu.reset();

    constexpr size_t kNumHarts = 4;
    for (uint32_t i = 1; i < kNumHarts; ++i) {
        auto sec = std::make_unique<simrv::core::CPU>();
        sec->machine_ = &machine;
        sec->reset();
        sec->state().mhartid = i;
        sec->hart_status.store(simrv::core::HartStatus::Started, std::memory_order_relaxed);
        machine.secondary_harts_.push_back(std::move(sec));
    }

    // Binary: each hart runs 500 atomic increments to 0x80000200
    constexpr Address pc = simrv::memory::kDramBaseAddress;
    constexpr std::array<Instruction, 10> program = {
        0x1f400093,  // addi x1, x0, 500
        0x00100213,  // addi x4, x0, 1
        0x400001b7,  // lui  x3, 0x40000
        0x00119193,  // slli x3, x3, 1 (x3 = 0x80000000 in both RV32 and RV64)
        0x20018193,  // addi x3, x3, 0x200 (x3 = 0x80000200)
        0x0041a2af,  // amoadd.w x5, x4, (x3)
        0xfff08093,  // addi x1, x1, -1
        0xfe009ce3,  // bnez x1, -8 (back to amoadd)
        0x10500073,  // wfi
        0x0000006f,  // jal x0, 0
    };
    std::memcpy(ram.data(), program.data(), sizeof(program));

    for (size_t i = 0; i < kNumHarts; ++i) {
        machine.hart(i).state().pc = pc;
    }

    // Start background SMP threads
    machine.start_runner_for_testing();

    // Hart 0 runs until it reaches WFI
    while (machine.hart(0).state().pc < pc + 0x20) {
        machine.hart(0).run_cycle(machine);
    }

    // Wait briefly for all secondary harts to finish their 500 iterations
    auto start_time = std::chrono::steady_clock::now();
    bool all_finished = false;
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(3)) {
        all_finished = true;
        for (size_t i = 1; i < kNumHarts; ++i) {
            if (machine.hart(i).state().pc < pc + 0x20) {
                all_finished = false;
                break;
            }
        }
        if (all_finished) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    machine.stop_runner_for_testing();

    check(all_finished);
    uint32_t final_sum = 0;
    std::memcpy(&final_sum, &ram[0x200], sizeof(final_sum));
    check(final_sum == kNumHarts * 500);

    std::cout << "[PASS] test_ia_multithreaded_smp_execution (" << kNumHarts
              << " harts parallel AMO sum = " << final_sum << ")\n";
}

void test_quantum_smp_baremetal_execution() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    simrv::core::Machine machine(simrv::core::MachineConfig{
        .execution = {.appmode = true, .smp_quantum = 100, .smp_multithreaded = false}});
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::InstructionFast;
    machine.cpu.machine_ = &machine;
    machine.cpu.reset();

    constexpr size_t kNumHarts = 4;
    for (uint32_t i = 1; i < kNumHarts; ++i) {
        auto sec = std::make_unique<simrv::core::CPU>();
        sec->machine_ = &machine;
        sec->reset();
        sec->state().mhartid = i;
        sec->hart_status.store(simrv::core::HartStatus::Started, std::memory_order_relaxed);
        machine.secondary_harts_.push_back(std::move(sec));
    }

    // Binary: each hart runs 250 atomic increments to 0x80000200
    constexpr Address pc = simrv::memory::kDramBaseAddress;
    constexpr std::array<Instruction, 10> program = {
        0x0fa00093,  // addi x1, x0, 250
        0x00100213,  // addi x4, x0, 1
        0x400001b7,  // lui  x3, 0x40000
        0x00119193,  // slli x3, x3, 1 (x3 = 0x80000000)
        0x20018193,  // addi x3, x3, 0x200 (x3 = 0x80000200)
        0x0041a2af,  // amoadd.w x5, x4, (x3)
        0xfff08093,  // addi x1, x1, -1
        0xfe009ce3,  // bnez x1, -8 (back to amoadd)
        0x10500073,  // wfi
        0x0000006f,  // jal x0, 0
    };
    std::memcpy(ram.data(), program.data(), sizeof(program));

    for (size_t i = 0; i < kNumHarts; ++i) {
        machine.hart(i).state().pc = pc;
    }

    // Run execution turns using OsRunner quantum batching
    for (uint32_t turn = 0; turn < 2000; ++turn) {
        machine.execute_cycle_for_testing();
        bool all_done = true;
        for (size_t i = 0; i < kNumHarts; ++i) {
            if (machine.hart(i).state().pc < pc + 0x20) {
                all_done = false;
                break;
            }
        }
        if (all_done) break;
    }

    for (size_t i = 0; i < kNumHarts; ++i) {
        check(machine.hart(i).state().pc >= pc + 0x20);
    }

    uint32_t final_sum = 0;
    std::memcpy(&final_sum, &ram[0x200], sizeof(final_sum));
    check(final_sum == kNumHarts * 250);

    std::cout << "[PASS] test_quantum_smp_baremetal_execution (" << kNumHarts
              << " harts quantum-scheduled AMO sum = " << final_sum << ")\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--semantic-equivalence") {
        test_cycle_policy_architectural_equivalence();
        test_instruction_policy_architectural_equivalence();
        std::cout << "Semantic equivalence tests PASSED!\n";
        return 0;
    }
    test_left_pane_runtime_summaries();
    test_ca_model_cache_application();
    test_mmio_device_and_dma();
    test_tui_uart_input_irq_publication();
    test_ia_tui_uart_input_is_immediate();
    test_os_sampled_fast_batch_eligibility();
    test_timed_interconnect_ordering();
    test_timed_interconnect_cancellation();
    test_timed_page_walk_transitions();
    test_timed_smp_coherence_ordering();
    test_global_cycle_smp_pipeline_ordering();
    test_global_cycle_timer_phase_ordering();
    test_cycle_kernel_golden_forwarding();
    test_cycle_kernel_golden_load_use();
    test_cycle_kernel_golden_data_refill();
    test_cycle_kernel_golden_fence_serialization();
    test_cycle_kernel_golden_multicycle_execute();
    test_cycle_kernel_golden_branch_recovery();
    test_cycle_kernel_golden_instruction_refill();
    test_cycle_kernel_golden_translated_fetch();
    test_cycle_kernel_golden_translated_load();
    test_cycle_kernel_golden_precise_trap();
    test_cycle_kernel_golden_interrupt_boundary();
    test_cycle_policy_architectural_equivalence();
    test_instruction_policy_architectural_equivalence();
    test_aclint();
    test_aia_aplic_and_imsic();
    test_pcie_and_virtio_pci();
    test_virtio_mmio_v2();
    test_sbi_multihart_rfence();
    test_ia_multihart_lr_sc_coherence();
    test_ia_multithreaded_smp_execution();
    test_quantum_smp_baremetal_execution();
    std::cout << "All Modern Platform tests PASSED!\n";
    return 0;
}
