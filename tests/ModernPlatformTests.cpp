/**
 * @file ModernPlatformTests.cpp
 * @brief Unit tests for RISC-V AIA, ACLINT, PCIe ECAM, and VirtIO-PCI subsystems.
 */
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/CsrTypes.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/PlatformTiming.hpp"
#include "simrv/core/RuntimeProfile.hpp"
#include "simrv/device/AIA.hpp"
#include "simrv/device/Aclint.hpp"
#include "simrv/device/DmaController.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/device/mmio/VirtioMmioBlock.hpp"
#include "simrv/device/mmio/VirtioMmioConsole.hpp"
#include "simrv/device/mmio/VirtioMmioGpu.hpp"
#include "simrv/device/mmio/VirtioMmioNet.hpp"
#include "simrv/device/mmio/VirtioMmioRng.hpp"
#include "simrv/device/mmio/VirtioMmioSound.hpp"
#include "simrv/device/pci/PciDevice.hpp"
#include "simrv/device/pci/PcieRootComplex.hpp"
#include "simrv/device/pci/VirtioPciBlock.hpp"
#include "simrv/device/pci/VirtioPciConsole.hpp"
#include "simrv/device/pci/VirtioPciGpu.hpp"
#include "simrv/device/pci/VirtioPciInput.hpp"
#include "simrv/device/pci/VirtioPciNet.hpp"
#include "simrv/device/pci/VirtioPciRng.hpp"
#include "simrv/device/pci/VirtioPciSound.hpp"
#include "simrv/device/virtio/HostAudioSink.hpp"
#include "simrv/device/virtio/VirtioCore.hpp"
#include "simrv/device/virtio/VirtioSoundCore.hpp"
#include "simrv/memory/Axi4.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/memory/MmioDevice.hpp"
#include "simrv/tui/modals/SystemConfigModal.hpp"
#include "simrv/tui/panels/InspectorPane.hpp"
#include "simrv/xlen/IdentifierTypes.hpp"
#include "simrv/xlen/RegisterTypes.hpp"

namespace {

class DummyDevice : public simrv::memory::MmioDevice {
   public:
    explicit DummyDevice(simrv::core::Machine* m) : simrv::memory::MmioDevice(m) {}
    [[nodiscard]] auto name() const -> const char* override { return "dummy"; }
    [[nodiscard]] auto base_address() const -> simrv::xlen::Address override { return 0x1000U; }
    [[nodiscard]] auto size() const -> simrv::xlen::Address override { return 0x100U; }

    [[nodiscard]] auto read32(simrv::xlen::Address offset) -> uint32_t override {
        return static_cast<uint32_t>(0xABCD0000U + offset);
    }
    void write32(simrv::xlen::Address offset, uint32_t val) override {
        last_val_ = val + static_cast<uint32_t>(offset);
    }
    uint32_t last_val_{0};
};

class CorruptResponseNode : public simrv::memory::TileLinkNode {
   public:
    [[nodiscard]] auto name() const -> const char* override { return "corrupt-response"; }
    [[nodiscard]] auto base_address() const -> simrv::xlen::Address override { return 0x3000U; }
    [[nodiscard]] auto size() const -> simrv::xlen::Address override { return 0x100U; }
    auto handle_request(const simrv::memory::TlChannelA& request,
                        simrv::memory::TlChannelD& response) -> bool override {
        response.opcode = request.opcode == simrv::memory::TlOpcodeA::Get
                              ? simrv::memory::TlOpcodeD::AccessAckData
                              : simrv::memory::TlOpcodeD::AccessAck;
        response.source = request.source;
        response.size = request.size;
        response.corrupt = true;
        return true;
    }
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
    machine.cpu.raise_exception(static_cast<simrv::xlen::TrapCause>(ExceptionCode::FetchPageFault),
                                0x80201048);
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

    check(row(26).contains("Debug") && row(27).contains("ELF sym") &&
              row(28).contains("breakpoints") && !row(28).contains("privilege"),
          "debug summary");

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
    draft.preset = static_cast<uint8_t>(simrv::pipeline::CpuModelPreset::Performance);
    check(simrv::tui::modals::SystemConfigModal::submit(draft, machine));
    check(machine.cpu.cpu_model_config.preset == simrv::pipeline::CpuModelPreset::Performance);
    check(machine.cpu.dcache.capacity_bytes() == 16384);
    check(machine.cpu.dcache.associativity() == 4);
}

void test_mmio_device_and_dma() {
    ConcreteMachine machine;
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
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

void test_ca_dma_latency_and_contention() {
    ConcreteMachine machine;
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());

    auto& dma = machine.dma_engine();
    assert(!dma.is_enabled());

    // In default / Fast Mode: schedule_transfer completes immediately
    bool fast_completed = false;
    dma.schedule_transfer(64, 0, [&fast_completed]() { fast_completed = true; });
    assert(fast_completed);

    // Configure CA DMA timing model
    simrv::pipeline::DmaTimingConfig cfg{
        .enabled = true,
        .setup_latency = 5,
        .bandwidth_bytes_per_cycle = 4,
        .memory_contention_penalty = 12,
    };
    dma.set_config(cfg);
    assert(dma.is_enabled());

    // 64 bytes @ 4 B/cycle = 16 cycles + 5 setup = 21 cycles
    assert(dma.calculate_latency(64) == 21);

    bool ca_completed = false;
    const auto completion_cycle =
        dma.schedule_transfer(64, 100, [&ca_completed]() { ca_completed = true; });
    assert(completion_cycle == 121);
    (void)completion_cycle;
    assert(dma.pending_tasks() == 1);
    assert(!ca_completed);

    // Check active status
    assert(!dma.is_transfer_active(122));
    assert(dma.is_transfer_active(100));
    assert(dma.is_transfer_active(120));
    assert(dma.is_transfer_active(121));

    // Advance before completion
    dma.advance_cycle(120);
    assert(!ca_completed);
    assert(dma.pending_tasks() == 1);

    // Advance to completion cycle
    dma.advance_cycle(121);
    assert(ca_completed);
    assert(dma.pending_tasks() == 0);
    assert(!dma.is_transfer_active(121));

    // Test back-to-back transfer serialization
    bool first_done = false;
    bool second_done = false;
    dma.schedule_transfer(32, 200,
                          [&first_done]() { first_done = true; });  // 5 + 8 = 13 cycles -> 213
    dma.schedule_transfer(
        32, 205, [&second_done]() { second_done = true; });  // queued after 213 -> 213 + 13 = 226
    assert(dma.pending_tasks() == 2);

    dma.advance_cycle(213);
    assert(first_done);
    assert(!second_done);
    assert(dma.pending_tasks() == 1);

    dma.advance_cycle(226);
    assert(second_done);
    assert(dma.pending_tasks() == 0);

    std::cout << "[PASS] test_ca_dma_latency_and_contention\n";
}

void test_dma_controller() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };

    ConcreteMachine machine;
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.memory().initialize_mmu();

    simrv::device::DmaController dma(&machine);
    check(dma.base_address() == simrv::device::DmaController::kDefaultBaseAddress);
    check(dma.size() == simrv::device::DmaController::kDefaultSize);
    check(dma.irq_num() == simrv::device::DmaController::kDefaultIrq);
    check(std::string_view(dma.name()) == "dma-controller");

    // 1. Initial register state
    check(dma.read32(simrv::device::DmaController::kRegControl) == 0);
    check(dma.read32(simrv::device::DmaController::kRegStatus) == 0);
    check(dma.read32(simrv::device::DmaController::kRegSrcAddrLo) == 0);
    check(dma.read32(simrv::device::DmaController::kRegSrcAddrHi) == 0);
    check(dma.read32(simrv::device::DmaController::kRegDstAddrLo) == 0);
    check(dma.read32(simrv::device::DmaController::kRegDstAddrHi) == 0);
    check(dma.read32(simrv::device::DmaController::kRegByteCount) == 0);
    check(!dma.is_busy());
    check(!dma.is_done());
    check(!dma.is_error());

    // 2. TileLink read/write requests
    simrv::memory::TlChannelA write_req{};
    write_req.opcode = simrv::memory::TlOpcodeA::PutFullData;
    write_req.address = dma.base_address() + simrv::device::DmaController::kRegByteCount;
    write_req.size = 2;
    write_req.data = 512;
    simrv::memory::TlChannelD resp{};
    check(dma.handle_request(write_req, resp));
    check(dma.read32(simrv::device::DmaController::kRegByteCount) == 512);

    simrv::memory::TlChannelA read_req{};
    read_req.opcode = simrv::memory::TlOpcodeA::Get;
    read_req.address = dma.base_address() + simrv::device::DmaController::kRegByteCount;
    read_req.size = 2;
    check(dma.handle_request(read_req, resp));
    check(resp.data == 512);

    // 3. Fast mode transfer and interrupt assertion
    const simrv::xlen::Address src_addr = simrv::memory::kDramBaseAddress + 0x1000;
    const simrv::xlen::Address dst_addr = simrv::memory::kDramBaseAddress + 0x2000;
    const uint32_t transfer_size = 128;

    for (uint32_t i = 0; i < transfer_size; ++i) {
        ram[0x1000 + i] = static_cast<simrv::xlen::Byte>(0xA0 + (i & 0x3F));
        ram[0x2000 + i] = simrv::xlen::Byte{0};
    }

    dma.write32(simrv::device::DmaController::kRegSrcAddrLo, static_cast<uint32_t>(src_addr));
    dma.write32(simrv::device::DmaController::kRegSrcAddrHi, 0);
    dma.write32(simrv::device::DmaController::kRegDstAddrLo, static_cast<uint32_t>(dst_addr));
    dma.write32(simrv::device::DmaController::kRegDstAddrHi, 0);
    dma.write32(simrv::device::DmaController::kRegByteCount, transfer_size);

    constexpr simrv::xlen::Word kDmaIrqMask = static_cast<simrv::xlen::Word>(1)
                                              << simrv::device::DmaController::kDefaultIrq;
    check((machine.cpu.plic_mmio.plic_pending[0] & kDmaIrqMask) == 0);

    // Start with interrupt enabled
    dma.write32(simrv::device::DmaController::kRegControl,
                simrv::device::DmaController::kControlStart |
                    simrv::device::DmaController::kControlIntEnable);

    // In fast mode, transfer is immediate
    check(!dma.is_busy());
    check(dma.is_done());
    check(!dma.is_error());
    check((machine.cpu.plic_mmio.plic_pending[0] & kDmaIrqMask) != 0);

    // Check data integrity
    for (uint32_t i = 0; i < transfer_size; ++i) {
        check(ram[0x2000 + i] == static_cast<simrv::xlen::Byte>(0xA0 + (i & 0x3F)));
    }

    // 4. Acknowledge and clear interrupt
    dma.write32(simrv::device::DmaController::kRegInterruptAck, 1);
    check((machine.cpu.plic_mmio.plic_pending[0] & kDmaIrqMask) == 0);
    check(!dma.is_done());

    // 5. Cycle-Accurate mode deferred transfer and interrupt
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
    simrv::pipeline::DmaTimingConfig ca_cfg{
        .enabled = true,
        .setup_latency = 10,
        .bandwidth_bytes_per_cycle = 4,
        .memory_contention_penalty = 8,
    };
    machine.dma_engine().set_config(ca_cfg);

    const simrv::xlen::Address ca_src_addr = simrv::memory::kDramBaseAddress + 0x3000;
    const simrv::xlen::Address ca_dst_addr = simrv::memory::kDramBaseAddress + 0x4000;
    const uint32_t ca_size = 64;  // 10 setup + 16 transfer = 26 cycles

    for (uint32_t i = 0; i < ca_size; ++i) {
        ram[0x3000 + i] = static_cast<simrv::xlen::Byte>(0x55 ^ i);
        ram[0x4000 + i] = simrv::xlen::Byte{0};
    }

    dma.write32(simrv::device::DmaController::kRegSrcAddrLo, static_cast<uint32_t>(ca_src_addr));
    dma.write32(simrv::device::DmaController::kRegSrcAddrHi, 0);
    dma.write32(simrv::device::DmaController::kRegDstAddrLo, static_cast<uint32_t>(ca_dst_addr));
    dma.write32(simrv::device::DmaController::kRegDstAddrHi, 0);
    dma.write32(simrv::device::DmaController::kRegByteCount, ca_size);

    const auto start_cycle = machine.memory().system_bus().cycle();
    dma.write32(simrv::device::DmaController::kRegControl,
                simrv::device::DmaController::kControlStart |
                    simrv::device::DmaController::kControlIntEnable);

    // In CA mode, transfer is scheduled
    check(dma.is_busy());
    check(!dma.is_done());
    check((machine.cpu.plic_mmio.plic_pending[0] & kDmaIrqMask) == 0);
    check(ram[0x4000] == simrv::xlen::Byte{0});

    // Advance 25 cycles (1 before completion)
    machine.dma_engine().advance_cycle(start_cycle + 25);
    check(dma.is_busy());
    check(!dma.is_done());
    check((machine.cpu.plic_mmio.plic_pending[0] & kDmaIrqMask) == 0);

    // Advance to 26 cycles (completion)
    machine.dma_engine().advance_cycle(start_cycle + 26);
    check(!dma.is_busy());
    check(dma.is_done());
    check((machine.cpu.plic_mmio.plic_pending[0] & kDmaIrqMask) != 0);

    // Data must now be fully copied
    for (uint32_t i = 0; i < ca_size; ++i) {
        check(ram[0x4000 + i] == static_cast<simrv::xlen::Byte>(0x55 ^ i));
    }

    // Clear interrupt
    dma.write32(simrv::device::DmaController::kRegInterruptAck, 1);
    check((machine.cpu.plic_mmio.plic_pending[0] & kDmaIrqMask) == 0);

    // 6. Error handling
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::InstructionFast;
    dma.reset();
    dma.write32(simrv::device::DmaController::kRegByteCount, 0);
    dma.write32(simrv::device::DmaController::kRegControl,
                simrv::device::DmaController::kControlStart |
                    simrv::device::DmaController::kControlIntEnable);
    check(dma.is_error());
    check((machine.cpu.plic_mmio.plic_pending[0] & kDmaIrqMask) != 0);
    dma.write32(simrv::device::DmaController::kRegInterruptAck, 1);
    check((machine.cpu.plic_mmio.plic_pending[0] & kDmaIrqMask) == 0);

    std::cout << "[PASS] test_dma_controller\n";
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
    constexpr simrv::xlen::Word kUartIrqMask = static_cast<simrv::xlen::Word>(1) << 3;
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
    constexpr simrv::xlen::Word kUartIrqMask = static_cast<simrv::xlen::Word>(1) << 3;
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

    // A zero-length batch exercises runner eligibility and the OS fast path's interrupt
    // boundary without requiring a guest image. The production TUI supplies the same observable
    // profile, then publishes after real batches.
    const auto msip_mask = simrv::core::MipBit::Msip;
    machine.set_hart_irq(simrv::xlen::HartId{0}, simrv::core::InterruptType::Software,
                         simrv::xlen::PrivilegeLevel::Machine, true);
    check((machine.primary_hart().state().mip & msip_mask) == 0);
    check(machine.execute_fast_batch_for_testing(0));
    check((machine.primary_hart().state().mip & msip_mask) != 0);
    check(simrv::core::CPU::current_execution_cpu() == nullptr);

    machine.set_hart_irq(simrv::xlen::HartId{0}, simrv::core::InterruptType::Software,
                         simrv::xlen::PrivilegeLevel::Machine, false);
    check(machine.execute_fast_batch_for_testing(0));
    check((machine.primary_hart().state().mip & msip_mask) == 0);
    check(simrv::core::CPU::current_execution_cpu() == nullptr);

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
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    ram[0] = simrv::xlen::Byte{0x11};
    ram[8] = simrv::xlen::Byte{0x22};

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
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
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
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.cpu.machine_ = &machine;
    machine.cpu.reset();
    machine.memory().initialize_mmu();
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;

    constexpr simrv::xlen::Address physical = simrv::memory::kDramBaseAddress;
    constexpr simrv::xlen::Address root = physical + 0x1000;
    const simrv::xlen::Word root_ppn = root >> 12;
    if constexpr (simrv::xlen::kIsXLen64) {
        machine.cpu.state().satp = (simrv::xlen::Word{8} << 60) | root_ppn;
    } else {
        machine.cpu.state().satp = (simrv::xlen::Word{1} << 31) | root_ppn;
    }
    machine.cpu.state().priv = kPrivSupervisor;

    constexpr unsigned top_vpn_bits = simrv::xlen::kIsXLen64 ? 9 : 10;
    constexpr unsigned top_level = simrv::xlen::kIsXLen64 ? 2 : 1;
    const simrv::xlen::Word vpn = (physical >> (12 + top_level * top_vpn_bits)) &
                                  ((simrv::xlen::Word{1} << top_vpn_bits) - 1);
    const simrv::xlen::Address pte_address = root + vpn * sizeof(simrv::xlen::Word);
    simrv::xlen::Word pte =
        ((physical >> 12) << 10) | simrv::PteFlag::V | simrv::PteFlag::R | simrv::PteFlag::X;
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
    check((pte & simrv::PteFlag::A) != 0);

    // A different, unmapped top-level entry faults after exactly one physical read.
    constexpr simrv::xlen::Address unmapped =
        physical + (simrv::xlen::Address{1} << (12 + top_level * top_vpn_bits));
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
    check(result->error() == static_cast<simrv::xlen::TrapCause>(ExceptionCode::FetchPageFault));

    // A squashed fetch removes both the transaction and resumable architectural state.
    result = machine.cpu.translate_stage_address(machine, physical, simrv::PteAccess::Code,
                                                 kPrivSupervisor, simrv::xlen::kXLenBits,
                                                 simrv::memory::TlPort::Instruction, walk);
    check(!result.has_value());
    machine.memory().system_bus().cancel_source(walk.source);
    walk.reset();
    check(machine.memory().system_bus().pending_requests() == 0);

    // Simultaneous A and D updates cannot lose bits: the bus serializes atomic OR operations.
    simrv::xlen::Word clear_pte = 0;
    std::memcpy(ram.data() + (pte_address - physical), &clear_pte, sizeof(clear_pte));
    simrv::memory::TlChannelA accessed{};
    accessed.opcode = simrv::memory::TlOpcodeA::LogicalData;
    accessed.size = sizeof(simrv::xlen::Word) == 8 ? 3 : 2;
    accessed.source = 6;
    accessed.address = pte_address;
    accessed.data = std::to_underlying(simrv::PteFlag::A);
    auto dirty = accessed;
    dirty.source = 7;
    dirty.data = std::to_underlying(simrv::PteFlag::D);
    machine.memory().system_bus().send_request(accessed);
    machine.memory().system_bus().send_request(dirty);
    machine.memory().system_bus().advance_cycle();
    machine.memory().system_bus().advance_cycle();
    std::memcpy(&clear_pte, ram.data() + (pte_address - physical), sizeof(clear_pte));
    check((clear_pte & (simrv::PteFlag::A | simrv::PteFlag::D)) ==
          std::to_underlying(simrv::PteFlag::A | simrv::PteFlag::D));
    std::cout << "[PASS] test_timed_page_walk_transitions\n";
}

void test_cycle_kernel_golden_translated_fetch() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    const auto run = [&](simrv::pipeline::PipelineType type) {
        ConcreteMachine machine;
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.memory().initialize_mmu();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.pipeline_sim.config.pipeline_type = type;

        constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
        constexpr simrv::xlen::Address root = pc + 0x1000;
        constexpr simrv::xlen::Instruction instruction = 0x02a00093;  // addi x1, x0, 42
        std::memcpy(ram.data(), &instruction, sizeof(instruction));
        std::array<simrv::xlen::Byte, simrv::cache::ICache::kLineBytes> line{};
        std::memcpy(line.data(), &instruction, sizeof(instruction));
        machine.cpu.icache.insert(pc, line.data(), simrv::memory::MesiState::Exclusive);

        const simrv::xlen::Word root_ppn = root >> 12;
        if constexpr (simrv::xlen::kIsXLen64) {
            machine.cpu.state().satp = (simrv::xlen::Word{8} << 60) | root_ppn;
        } else {
            machine.cpu.state().satp = (simrv::xlen::Word{1} << 31) | root_ppn;
        }
        constexpr unsigned vpn_bits = simrv::xlen::kIsXLen64 ? 9 : 10;
        constexpr unsigned level = simrv::xlen::kIsXLen64 ? 2 : 1;
        const simrv::xlen::Word vpn =
            (pc >> (12 + level * vpn_bits)) & ((simrv::xlen::Word{1} << vpn_bits) - 1);
        const simrv::xlen::Address pte_address = root + vpn * sizeof(simrv::xlen::Word);
        const simrv::xlen::Word pte = ((pc >> 12) << 10) | simrv::PteFlag::V | simrv::PteFlag::R |
                                      simrv::PteFlag::X | simrv::PteFlag::A;
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
        check(machine.cpu.state().regs.read(simrv::xlen::RegId::Ra) == 42);
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
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.memory().initialize_mmu();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.pipeline_sim.config.pipeline_type = type;

        constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
        constexpr simrv::xlen::Address data_address = pc + 0x100;
        constexpr simrv::xlen::Address root = pc + 0x1000;
        constexpr std::array<simrv::xlen::Instruction, 4> program = {
            0x00000097,  // auipc x1, 0
            0x10008093,  // addi  x1, x1, 256
            0x0000a103,  // lw    x2, 0(x1)
            0x0000006f,
        };
        std::memcpy(ram.data(), program.data(), sizeof(program));
        std::array<simrv::xlen::Byte, simrv::cache::ICache::kLineBytes> instruction_line{};
        std::memcpy(instruction_line.data(), program.data(), sizeof(program));
        machine.cpu.icache.insert(pc, instruction_line.data(), simrv::memory::MesiState::Exclusive);
        constexpr uint32_t value = 73;
        std::memcpy(ram.data() + 0x100, &value, sizeof(value));
        std::array<simrv::xlen::Byte, simrv::cache::DCache::kLineBytes> data_line{};
        std::memcpy(data_line.data(), &value, sizeof(value));
        machine.cpu.dcache.insert(data_address, data_line.data(), simrv::memory::MesiState::Shared);

        const simrv::xlen::Word root_ppn = root >> 12;
        if constexpr (simrv::xlen::kIsXLen64) {
            machine.cpu.state().satp = (simrv::xlen::Word{8} << 60) | root_ppn;
        } else {
            machine.cpu.state().satp = (simrv::xlen::Word{1} << 31) | root_ppn;
        }
        constexpr unsigned vpn_bits = simrv::xlen::kIsXLen64 ? 9 : 10;
        constexpr unsigned level = simrv::xlen::kIsXLen64 ? 2 : 1;
        const simrv::xlen::Word vpn =
            (pc >> (12 + level * vpn_bits)) & ((simrv::xlen::Word{1} << vpn_bits) - 1);
        const simrv::xlen::Address pte_address = root + vpn * sizeof(simrv::xlen::Word);
        const simrv::xlen::Word pte = ((pc >> 12) << 10) | simrv::PteFlag::V | simrv::PteFlag::R |
                                      simrv::PteFlag::W | simrv::PteFlag::X | simrv::PteFlag::A |
                                      simrv::PteFlag::D;
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
        if (machine.cpu.e_icount != 3 ||
            machine.cpu.state().regs.read(simrv::xlen::RegId::Sp) != value ||
            machine.cpu.pipeline_sim.tlb_stalls() == 0) {
            std::cerr << "translated-load state: cycles=" << cycles
                      << " retired=" << machine.cpu.e_icount
                      << " x2=" << machine.cpu.state().regs.read(simrv::xlen::RegId::Sp)
                      << " tlb-stalls=" << machine.cpu.pipeline_sim.tlb_stalls() << '\n';
        }
        check(machine.cpu.e_icount == 3);
        check(machine.cpu.state().regs.read(simrv::xlen::RegId::Sp) == value);
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
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.cpu.machine_ = &machine;
    auto secondary = std::make_unique<simrv::core::CPU>();
    secondary->machine_ = &machine;
    secondary->state().mhartid = 1;
    machine.secondary_harts_.push_back(std::move(secondary));
    machine.memory().initialize_mmu();

    const simrv::xlen::Address line = simrv::memory::kDramBaseAddress + 0x100;
    ram[0x100] = simrv::xlen::Byte{0x5a};
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

void test_memory_access_fails_closed_when_request_is_rejected() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    ConcreteMachine machine;
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.cpu.machine_ = &machine;
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;

    constexpr simrv::xlen::Address address = simrv::memory::kDramBaseAddress + 0x180;
    constexpr simrv::xlen::Address blocker_address = simrv::memory::kDramBaseAddress + 0x200;
    ram[0x180] = simrv::xlen::Byte{0x78};
    ram[0x181] = simrv::xlen::Byte{0x56};
    ram[0x182] = simrv::xlen::Byte{0x34};
    ram[0x183] = simrv::xlen::Byte{0x12};

    auto& hart = machine.cpu;
    auto& bus = machine.memory().system_bus();
    const auto source = simrv::memory::make_tl_source(hart.hart_id(), simrv::memory::TlPort::Data);
    simrv::memory::TlChannelA blocker{};
    blocker.opcode = simrv::memory::TlOpcodeA::Get;
    blocker.size = 2;
    blocker.hart = hart.hart_id();
    blocker.source = source;
    blocker.address = blocker_address;

    check(bus.send_request(blocker));
    hart.ca_state.waiting_for_interconnect = false;
    hart.active_context().pending_exception.reset();
    const auto read = simrv::memory::MemoryAccess::target_read(
        machine.memory(), hart, simrv::xlen::VirtAddr{address}, simrv::isa::Funct3::Lw);
    check(read == 0);
    check(hart.active_context().pending_exception == ExceptionCode::FaultLoad);
    check(hart.active_context().pending_tval == address);
    check(!hart.ca_state.waiting_for_interconnect);
    bus.cancel_source(source);

    check(bus.send_request(blocker));
    hart.ca_state.waiting_for_interconnect = false;
    hart.active_context().pending_exception.reset();
    simrv::memory::MemoryAccess::target_write(machine.memory(), hart,
                                              simrv::xlen::VirtAddr{address}, 0xDEADBEEFU,
                                              simrv::isa::Funct3::Sw);
    check(hart.active_context().pending_exception == ExceptionCode::FaultStore);
    check(hart.active_context().pending_tval == address);
    check(!hart.ca_state.waiting_for_interconnect);
    check(ram[0x180] == simrv::xlen::Byte{0x78});
    check(ram[0x181] == simrv::xlen::Byte{0x56});
    check(ram[0x182] == simrv::xlen::Byte{0x34});
    check(ram[0x183] == simrv::xlen::Byte{0x12});
    check(hart.dcache.line_state(address) == simrv::memory::MesiState::Invalid);
    bus.cancel_source(source);

    const simrv::xlen::VirtAddr unmapped{0x1000};
    hart.ca_state.data_transfer.reset();
    hart.ca_state.waiting_for_interconnect = false;
    hart.active_context().pending_exception.reset();
    const auto denied_read = simrv::memory::MemoryAccess::target_read(
        machine.memory(), hart, unmapped, simrv::isa::Funct3::Lw);
    check(hart.ca_state.waiting_for_interconnect);
    bus.advance_cycle();
    check(denied_read == 0);
    const auto denied_response_read = simrv::memory::MemoryAccess::target_read(
        machine.memory(), hart, unmapped, simrv::isa::Funct3::Lw);
    check(denied_response_read == 0);
    check(hart.active_context().pending_exception == ExceptionCode::FaultLoad);
    check(hart.active_context().pending_tval == unmapped.raw());
    check(!hart.ca_state.data_transfer.active);

    hart.ca_state.data_transfer.reset();
    hart.ca_state.waiting_for_interconnect = false;
    hart.active_context().pending_exception.reset();
    simrv::memory::MemoryAccess::target_write(machine.memory(), hart, unmapped, 0xDEADBEEFU,
                                              simrv::isa::Funct3::Sw);
    check(hart.ca_state.waiting_for_interconnect);
    bus.advance_cycle();
    simrv::memory::MemoryAccess::target_write(machine.memory(), hart, unmapped, 0xDEADBEEFU,
                                              simrv::isa::Funct3::Sw);
    check(hart.active_context().pending_exception == ExceptionCode::FaultStore);
    check(hart.active_context().pending_tval == unmapped.raw());
    check(!hart.ca_state.data_transfer.active);

    CorruptResponseNode corrupt_node;
    check(bus.add_node(&corrupt_node));
    const simrv::xlen::VirtAddr corrupt_address{corrupt_node.base_address()};
    hart.ca_state.data_transfer.reset();
    hart.ca_state.waiting_for_interconnect = false;
    hart.active_context().pending_exception.reset();
    const auto corrupt_first_read = simrv::memory::MemoryAccess::target_read(
        machine.memory(), hart, corrupt_address, simrv::isa::Funct3::Lw);
    check(hart.ca_state.waiting_for_interconnect);
    bus.advance_cycle();
    check(corrupt_first_read == 0);
    const auto corrupt_read = simrv::memory::MemoryAccess::target_read(
        machine.memory(), hart, corrupt_address, simrv::isa::Funct3::Lw);
    check(corrupt_read == 0);
    check(hart.active_context().pending_exception == ExceptionCode::FaultLoad);
    check(hart.active_context().pending_tval == corrupt_address.raw());
    check(!hart.ca_state.data_transfer.active);

    hart.ca_state.data_transfer.reset();
    hart.ca_state.waiting_for_interconnect = false;
    hart.active_context().pending_exception.reset();
    simrv::memory::MemoryAccess::target_write(machine.memory(), hart, corrupt_address, 0xDEADBEEFU,
                                              simrv::isa::Funct3::Sw);
    check(hart.ca_state.waiting_for_interconnect);
    bus.advance_cycle();
    simrv::memory::MemoryAccess::target_write(machine.memory(), hart, corrupt_address, 0xDEADBEEFU,
                                              simrv::isa::Funct3::Sw);
    check(hart.active_context().pending_exception == ExceptionCode::FaultStore);
    check(hart.active_context().pending_tval == corrupt_address.raw());
    check(!hart.ca_state.data_transfer.active);
    std::cout << "[PASS] test_memory_access_fails_closed_when_request_is_rejected\n";
}

void test_modified_line_does_not_refill_stale_shared_data() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    ConcreteMachine machine;
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.cpu.machine_ = &machine;
    machine.memory().initialize_mmu();

    constexpr simrv::xlen::Address line = simrv::memory::kDramBaseAddress + 0x180;
    constexpr uint32_t original = 0x11223344;
    constexpr uint32_t updated = 0xa5b6c7d8;
    std::memcpy(ram.data() + 0x180, &original, sizeof(original));

    auto& bus = machine.memory().system_bus();
    simrv::memory::TlChannelA acquire{};
    acquire.opcode = simrv::memory::TlOpcodeA::AcquireBlock;
    acquire.grow = simrv::memory::TlGrow::NtoT;
    acquire.source = 1;
    acquire.hart = 0;
    acquire.address = line;
    acquire.size = simrv::memory::kTlBlockSize;
    simrv::memory::TlChannelD response{};
    std::array<simrv::xlen::Byte, simrv::memory::CoherenceHub::kLineBytes> data{};
    check(bus.acquire_block(acquire, response, data));
    bus.grant_ack(simrv::memory::TlChannelE{.sink = response.sink});
    machine.cpu.dcache.insert(line, data.data(), simrv::memory::MesiState::Exclusive);

    check(machine.cpu.dcache.write(line, updated, 2));
    bus.mark_modified(line, 0);
    std::memcpy(ram.data() + 0x180, &updated, sizeof(updated));

    // Model an L1 eviction whose write-through value is already in DRAM. The directory still
    // identifies the old owner, so a failed owner probe must fall back to DRAM rather than the
    // clean shared-cache line populated by the first acquire.
    machine.cpu.dcache.flush();
    acquire.grow = simrv::memory::TlGrow::NtoB;
    acquire.source = 2;
    check(bus.acquire_block(acquire, response, data));
    uint32_t refilled = 0;
    std::memcpy(&refilled, data.data(), sizeof(refilled));
    check(refilled == updated);
    bus.grant_ack(simrv::memory::TlChannelE{.sink = response.sink});
    std::cout << "[PASS] test_modified_line_does_not_refill_stale_shared_data\n";
}

void test_release_data_writes_back_without_directory_entry() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    ConcreteMachine machine;
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.cpu.machine_ = &machine;

    constexpr simrv::xlen::Address line = simrv::memory::kDramBaseAddress + 0x240;
    std::array<simrv::xlen::Byte, simrv::memory::CoherenceHub::kLineBytes> released{};
    for (size_t i = 0; i < released.size(); ++i)
        released[i] = static_cast<simrv::xlen::Byte>((i * 13u + 7u) & 0xffu);

    simrv::memory::TlChannelC release{};
    release.opcode = simrv::memory::TlOpcodeC::ReleaseData;
    release.report =
        simrv::memory::report_for(simrv::memory::MesiState::Modified, simrv::memory::TlCap::ToN);
    release.size = simrv::memory::kTlBlockSize;
    release.source = simrv::memory::make_tl_source(0, simrv::memory::TlPort::Data);
    release.hart = 0;
    release.address = line;

    simrv::memory::TlChannelD response{};
    auto& bus = machine.memory().system_bus();
    check(bus.release_line(release, response, &released));
    check(response.opcode == simrv::memory::TlOpcodeD::ReleaseAck);
    check(std::memcmp(ram.data() + 0x240, released.data(), released.size()) == 0);
    check(bus.coherence_hub().stats().writeback_count == 1);

    std::array<simrv::xlen::Byte, simrv::memory::CoherenceHub::kLineBytes> cached{};
    simrv::memory::MesiState state = simrv::memory::MesiState::Invalid;
    check(bus.coherence_hub().l3_cache().lookup_line(line, cached, state));
    check(cached == released);
    std::cout << "[PASS] test_release_data_writes_back_without_directory_entry\n";
}

void test_machine_reset_clears_coherence_state_after_cache_reset() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    ConcreteMachine machine;
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.cpu.machine_ = &machine;
    machine.memory().initialize_mmu();

    constexpr simrv::xlen::Address line = simrv::memory::kDramBaseAddress + 0x2a0;
    constexpr uint32_t value = 0x76543210;
    auto& bus = machine.memory().system_bus();
    simrv::memory::TlChannelA acquire{};
    acquire.opcode = simrv::memory::TlOpcodeA::AcquireBlock;
    acquire.grow = simrv::memory::TlGrow::NtoT;
    acquire.source = 3;
    acquire.hart = 0;
    acquire.address = line;
    acquire.size = simrv::memory::kTlBlockSize;
    simrv::memory::TlChannelD response{};
    std::array<simrv::xlen::Byte, simrv::memory::CoherenceHub::kLineBytes> data{};
    check(bus.acquire_block(acquire, response, data));
    bus.grant_ack(simrv::memory::TlChannelE{.sink = response.sink});
    machine.cpu.dcache.insert(line, data.data(), simrv::memory::MesiState::Exclusive);
    check(machine.cpu.dcache.write(line, value, 2));
    bus.mark_modified(line, 0);
    std::memcpy(ram.data() + 0x2a0, &value, sizeof(value));
    check(bus.coherence_hub().get_directory_state(line).state ==
          simrv::memory::MesiState::Modified);

    machine.reset_state();

    check(machine.cpu.dcache.line_state(line) == simrv::memory::MesiState::Invalid);
    check(bus.coherence_hub().get_directory_state(line).state == simrv::memory::MesiState::Invalid);
    uint32_t backing_value = 0;
    std::memcpy(&backing_value, ram.data() + 0x2a0, sizeof(backing_value));
    check(backing_value == value);
    std::cout << "[PASS] test_machine_reset_clears_coherence_state_after_cache_reset\n";
}

void test_cross_line_dma_write_invalidates_only_touched_lines() {
    size_t check_index = 0;
    const auto check = [&check_index](bool condition) {
        ++check_index;
        if (!condition) {
            std::cerr << "cross-line DMA check failed: " << check_index << '\n';
            std::abort();
        }
    };
    ConcreteMachine machine;
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.cpu.machine_ = &machine;
    machine.memory().initialize_mmu();
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;

    constexpr simrv::xlen::Address first_line = simrv::memory::kDramBaseAddress + 0x300;
    auto& bus = machine.memory().system_bus();
    for (unsigned index = 0; index < 3; ++index) {
        simrv::memory::TlChannelA acquire{};
        acquire.opcode = simrv::memory::TlOpcodeA::AcquireBlock;
        acquire.grow = simrv::memory::TlGrow::NtoB;
        acquire.source = static_cast<simrv::memory::TlSourceId>(4 + index * 2);
        acquire.hart = 0;
        acquire.address = first_line + index * simrv::memory::CoherenceHub::kLineBytes;
        acquire.size = simrv::memory::kTlBlockSize;
        simrv::memory::TlChannelD response{};
        std::array<simrv::xlen::Byte, simrv::memory::CoherenceHub::kLineBytes> data{};
        check(bus.acquire_block(acquire, response, data));
        bus.grant_ack(simrv::memory::TlChannelE{.sink = response.sink});
        machine.cpu.dcache.insert(acquire.address.raw(), data.data(),
                                  simrv::memory::mesi_for(response.cap));
    }

    constexpr std::array<simrv::xlen::Byte, 8> replacement{
        simrv::xlen::Byte{1}, simrv::xlen::Byte{2}, simrv::xlen::Byte{3}, simrv::xlen::Byte{4},
        simrv::xlen::Byte{5}, simrv::xlen::Byte{6}, simrv::xlen::Byte{7}, simrv::xlen::Byte{8}};
    const auto write_address = first_line + simrv::memory::CoherenceHub::kLineBytes - 4;
    check(machine.memory().dma_write(write_address, replacement.data(), replacement.size()));
    check(machine.cpu.dcache.line_state(first_line) == simrv::memory::MesiState::Invalid);
    check(machine.cpu.dcache.line_state(first_line + simrv::memory::CoherenceHub::kLineBytes) ==
          simrv::memory::MesiState::Invalid);
    check(machine.cpu.dcache.line_state(first_line + 2 * simrv::memory::CoherenceHub::kLineBytes) ==
          simrv::memory::MesiState::Exclusive);
    check(std::memcmp(ram.data() + 0x31c, replacement.data(), replacement.size()) == 0);

    const std::array<simrv::xlen::Byte, 8> out_of_range{};
    const auto final_bytes = ram.size() - 4;
    check(!machine.memory().dma_write(simrv::memory::kDramBaseAddress + final_bytes,
                                      out_of_range.data(), out_of_range.size()));
    std::cout << "[PASS] test_cross_line_dma_write_invalidates_only_touched_lines\n";
}

void test_profiled_shared_cache_clusters() {
    size_t check_index = 0;
    const auto check = [&check_index](bool condition) {
        ++check_index;
        if (!condition) {
            std::cerr << "shared-cache cluster check failed: " << check_index << '\n';
            std::abort();
        }
    };
    simrv::core::MachineConfig config{};
    config.execution.num_harts = 3;
    config.soc.smp.hart_count = 3;
    config.soc.smp.l3.enabled = false;
    const simrv::core::SharedCacheConfig l2_config{.enabled = true,
                                                   .capacity_bytes = 32 * 1024,
                                                   .associativity = 4,
                                                   .line_bytes = 32,
                                                   .hit_latency = 3,
                                                   .miss_latency = 7};
    config.soc.smp.clusters = {{.id = 0, .harts = {0, 1}, .l2 = l2_config},
                               {.id = 2, .harts = {2}, .l2 = l2_config}};
    ConcreteMachine machine(config);
    auto& hub = machine.memory().system_bus().coherence_hub();
    const auto configured = hub.configure_topology(config.soc.smp);
    if (!configured) {
        std::cerr << "shared-cache cluster configuration failed: " << configured.error() << '\n';
        std::abort();
    }
    auto* hart0_l2 = hub.shared_l2_for_hart(0);
    auto* hart1_l2 = hub.shared_l2_for_hart(1);
    auto* hart2_l2 = hub.shared_l2_for_hart(2);
    check(hart0_l2 != nullptr && hart0_l2 == hart1_l2 && hart2_l2 != nullptr &&
          hart2_l2 != hart0_l2);
    check(hart0_l2->capacity_bytes() == 32 * 1024 && hart0_l2->associativity() == 4);
    check(!hub.shared_l3_enabled());

    constexpr simrv::xlen::Address line = simrv::memory::kDramBaseAddress + 0x280;
    std::array<simrv::xlen::Byte, simrv::memory::CoherenceHub::kLineBytes> source{};
    std::array<simrv::xlen::Byte, simrv::memory::CoherenceHub::kLineBytes> result{};
    source[0] = simrv::xlen::Byte{0x5a};
    hart0_l2->write_line(line, source);
    check(hart1_l2->read_line(line, result) && result[0] == simrv::xlen::Byte{0x5a});
    hub.invalidate_line_external(line);
    check(!hart0_l2->read_line(line, result) && !hart2_l2->read_line(line, result));
    std::cout << "[PASS] test_profiled_shared_cache_clusters\n";
}

void test_tilelink_c_acquire_perm_upgrade() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    ConcreteMachine machine;
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.cpu.machine_ = &machine;
    auto secondary = std::make_unique<simrv::core::CPU>();
    secondary->machine_ = &machine;
    secondary->state().mhartid = 1;
    machine.secondary_harts_.push_back(std::move(secondary));
    machine.memory().initialize_mmu();

    const simrv::xlen::Address line = simrv::memory::kDramBaseAddress + 0x200;
    const uint32_t original_val = 0xdeadbeef;
    std::memcpy(ram.data() + 0x200, &original_val, sizeof(original_val));
    auto& bus = machine.memory().system_bus();

    // Hart 0 acquires line as Branch (Shared)
    simrv::memory::TlChannelA acq0{};
    acq0.opcode = simrv::memory::TlOpcodeA::AcquireBlock;
    acq0.grow = simrv::memory::TlGrow::NtoB;
    acq0.source = 10;
    acq0.hart = 0;
    acq0.address = line;
    acq0.size = simrv::memory::kTlBlockSize;
    simrv::memory::TlChannelD resp0{};
    std::array<simrv::xlen::Byte, simrv::memory::CoherenceHub::kLineBytes> data0{};
    check(bus.acquire_block(acq0, resp0, data0));
    bus.grant_ack(simrv::memory::TlChannelE{.sink = resp0.sink});
    machine.hart(0).dcache.insert(line, data0.data(), simrv::memory::MesiState::Shared);

    // Hart 1 acquires line as Branch (Shared)
    simrv::memory::TlChannelA acq1 = acq0;
    acq1.source = 11;
    acq1.hart = 1;
    simrv::memory::TlChannelD resp1{};
    std::array<simrv::xlen::Byte, simrv::memory::CoherenceHub::kLineBytes> data1{};
    check(bus.acquire_block(acq1, resp1, data1));
    bus.grant_ack(simrv::memory::TlChannelE{.sink = resp1.sink});
    machine.hart(1).dcache.insert(line, data1.data(), simrv::memory::MesiState::Shared);

    // Verify directory state: Shared, sharers = 0b11
    auto dir = bus.coherence_hub().get_directory_state(line);
    check(dir.state == simrv::memory::MesiState::Shared);
    check(dir.sharers_mask == 0b11U);

    // Hart 0 upgrades from Branch to Trunk (BtoT) via AcquirePerm
    simrv::memory::TlChannelA upg{};
    upg.opcode = simrv::memory::TlOpcodeA::AcquirePerm;
    upg.grow = simrv::memory::TlGrow::BtoT;
    upg.source = 12;
    upg.hart = 0;
    upg.address = line;
    upg.size = simrv::memory::kTlBlockSize;
    simrv::memory::TlChannelD upg_resp{};
    check(bus.acquire_perm(upg, upg_resp));
    check(upg_resp.opcode == simrv::memory::TlOpcodeD::Grant);
    check(upg_resp.cap == simrv::memory::TlCap::ToT);
    bus.grant_ack(simrv::memory::TlChannelE{.sink = upg_resp.sink});

    check(machine.hart(0).dcache.upgrade_line_state(line, simrv::memory::MesiState::Exclusive));
    check(machine.hart(0).dcache.write(line, 0xcafebeef, 2));
    bus.mark_modified(line, 0);

    // Hart 1 should have been probed and invalidated by CoherenceHub during AcquirePerm
    check(machine.hart(1).dcache.line_state(line) == simrv::memory::MesiState::Invalid);

    // Verify directory state: Exclusive / Modified, sole sharer Hart 0
    dir = bus.coherence_hub().get_directory_state(line);
    check(dir.state == simrv::memory::MesiState::Modified);
    check(dir.sharers_mask == 0b01U);
    check(dir.owner_hart.has_value() && *dir.owner_hart == 0);

    // Check transaction history contains recent transactions
    check(!bus.transaction_history().empty());

    std::cout << "[PASS] test_tilelink_c_acquire_perm_upgrade\n";
}

void test_tilelink_c_prefetch_intent() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    ConcreteMachine machine;
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.cpu.machine_ = &machine;
    machine.memory().initialize_mmu();

    const simrv::xlen::Address line = simrv::memory::kDramBaseAddress + 0x300;
    const uint32_t val = 0x87654321;
    std::memcpy(ram.data() + 0x300, &val, sizeof(val));
    auto& bus = machine.memory().system_bus();

    simrv::memory::TlChannelA intent{};
    intent.opcode = simrv::memory::TlOpcodeA::Intent;
    intent.intent = simrv::memory::TlIntent::PrefetchRead;
    intent.source = 20;
    intent.hart = 0;
    intent.address = line;
    intent.size = simrv::memory::kTlBlockSize;

    check(bus.send_request(intent));
    simrv::memory::TlChannelD resp{};
    check(bus.get_response(intent.source, resp));
    check(resp.opcode == simrv::memory::TlOpcodeD::HintAck);
    check(!resp.failed());
    check(bus.coherence_hub().stats().prefetch_count >= 1);

    // Verify L3 cache now has line cached
    std::array<simrv::xlen::Byte, simrv::memory::CoherenceHub::kLineBytes> l3_buf{};
    simrv::memory::MesiState l3_state = simrv::memory::MesiState::Invalid;
    check(bus.coherence_hub().l3_cache().lookup_line(line, l3_buf, l3_state));
    uint32_t l3_val = 0;
    std::memcpy(&l3_val, l3_buf.data(), sizeof(l3_val));
    check(l3_val == val);

    std::cout << "[PASS] test_tilelink_c_prefetch_intent\n";
}

void test_global_cycle_smp_pipeline_ordering() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    const auto run = [&] {
        simrv::core::Machine machine(
            simrv::core::MachineConfig{.execution = {.appmode = false, .smp_quantum = 1}});
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
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

        constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
        constexpr std::array<simrv::xlen::Instruction, 2> program = {
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
        check(machine.hart(0).state().regs.read(simrv::xlen::RegId::Ra) == 1);
        check(machine.hart(1).state().regs.read(simrv::xlen::RegId::Ra) == 1);
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

void test_global_cycle_smp_amo_retry() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    simrv::core::Machine machine(simrv::core::MachineConfig{
        .execution = {.appmode = false, .num_harts = 2, .smp_quantum = 1}});
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
    machine.primary_hart().machine_ = &machine;
    machine.primary_hart().reset();
    auto secondary = std::make_unique<simrv::core::CPU>();
    secondary->machine_ = &machine;
    secondary->reset();
    secondary->state().mhartid = 1;
    secondary->hart_status.store(simrv::core::HartStatus::Started, std::memory_order_relaxed);
    machine.test_secondary_harts().push_back(std::move(secondary));

    constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
    constexpr std::array<simrv::xlen::Instruction, 5> program = {
        0x00150093,  // addi x1, a0, 1 (unique value per hart)
        0x00000117,  // auipc x2, 0
        0x0fc10113,  // addi x2, x2, 252 -> shared word at pc + 0x100
        0x081121af,  // amoswap.w x3, x1, (x2)
        0x0000006f,  // jal x0, 0
    };
    std::memcpy(ram.data(), program.data(), sizeof(program));
    const uint32_t initial = 0;
    std::memcpy(ram.data() + 0x100, &initial, sizeof(initial));
    for (size_t hart = 0; hart < 2; ++hart) {
        machine.hart(hart).state().pc = pc;
        machine.hart(hart).state().regs.write(simrv::xlen::RegId::A0, hart);
    }

    for (uint32_t cycle = 0;
         cycle < 160 && (machine.hart(0).e_icount < 4 || machine.hart(1).e_icount < 4); ++cycle) {
        machine.execute_cycle_for_testing();
    }
    uint32_t result = 0;
    std::memcpy(&result, ram.data() + 0x100, sizeof(result));
    check(machine.hart(0).e_icount >= 4 && machine.hart(1).e_icount >= 4);
    check(result == 2);
    check(machine.hart(0).state().mcause == 0 && machine.hart(1).state().mcause == 0);
    std::cout << "[PASS] test_global_cycle_smp_amo_retry\n";
}

void test_multithreaded_parallel_matches_global_cycle() {
    const auto run = [](simrv::core::SmpMode mode) {
        simrv::core::Machine machine(simrv::core::MachineConfig{
            .execution = {.appmode = true, .smp_quantum = 7, .smp_mode = mode}});
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleObservable;
        machine.primary_hart().machine_ = &machine;
        machine.primary_hart().reset();
        auto secondary = std::make_unique<simrv::core::CPU>();
        secondary->machine_ = &machine;
        secondary->reset();
        secondary->state().mhartid = 1;
        secondary->hart_status.store(simrv::core::HartStatus::Started, std::memory_order_relaxed);
        machine.test_secondary_harts().push_back(std::move(secondary));

        constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
        constexpr std::array<simrv::xlen::Instruction, 2> program = {
            0x00108093,  // addi x1, x1, 1
            0x0000006f,  // jal x0, 0
        };
        std::memcpy(ram.data(), program.data(), sizeof(program));
        machine.hart(0).state().pc = pc;
        machine.hart(1).state().pc = pc;
        if (mode == simrv::core::SmpMode::MultiThreaded) {
            machine.start_runner_for_testing();
        }
        for (uint32_t cycle = 0; cycle < 40; ++cycle) {
            machine.execute_cycle_for_testing();
        }
        if (mode == simrv::core::SmpMode::MultiThreaded) {
            machine.stop_runner_for_testing();
        }
        return std::array<std::array<uint64_t, 5>, 2>{
            std::array<uint64_t, 5>{machine.hart(0).state().pc,
                                    machine.hart(0).state().regs.read(simrv::xlen::RegId::Ra),
                                    machine.hart(0).e_icount, machine.hart(0).clint_mmio.mcycle,
                                    machine.memory().system_bus().cycle()},
            std::array<uint64_t, 5>{machine.hart(1).state().pc,
                                    machine.hart(1).state().regs.read(simrv::xlen::RegId::Ra),
                                    machine.hart(1).e_icount, machine.hart(1).clint_mmio.mcycle,
                                    machine.memory().system_bus().cycle()},
        };
    };
    const auto deterministic = run(simrv::core::SmpMode::SingleThreaded);
    const auto parallel = run(simrv::core::SmpMode::MultiThreaded);
    if (parallel != deterministic) {
        std::cerr << "multi-threaded SMP diverged from global-cycle execution\n";
        std::abort();
    }
    std::cout << "[PASS] test_multithreaded_parallel_matches_global_cycle\n";
}

void test_global_cycle_timer_phase_ordering() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    simrv::core::Machine machine(
        simrv::core::MachineConfig{.execution = {.appmode = false, .smp_quantum = 1}});
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
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

    constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
    constexpr simrv::xlen::Instruction loop = 0x0000006f;  // jal x0, 0
    std::memcpy(ram.data(), &loop, sizeof(loop));
    machine.hart(0).state().pc = pc;
    machine.hart(1).state().pc = pc;
    machine.cpu.clint_mmio.mtime = 4;
    machine.cpu.clint_mmio.rtc_divider = simrv::core::timing::kCyclesPerTimebaseTick - 1;
    machine.cpu.clint_mmio.mtimecmp = 5;
    machine.cpu.clint_mmio.hart_mtimecmp.at(1) = 5;

    const auto hart0_cycles = machine.hart(0).clint_mmio.mcycle;
    const auto hart1_cycles = machine.hart(1).clint_mmio.mcycle;
    machine.execute_cycle_for_testing();

    check(machine.hart(0).clint_mmio.mcycle == hart0_cycles + 1);
    check(machine.hart(1).clint_mmio.mcycle == hart1_cycles + 1);
    check(machine.hart(0).clint_mmio.mtime == 5);
    check(machine.hart(1).clint_mmio.mtime == 5);
    check((machine.hart(0).state().mip & simrv::core::MipBit::Mtip) != 0);
    check((machine.hart(1).state().mip & simrv::core::MipBit::Mtip) != 0);
    std::cout << "[PASS] test_global_cycle_timer_phase_ordering\n";
}

void test_ca_quantum_smp_batching() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    simrv::core::Machine machine(
        simrv::core::MachineConfig{.execution = {.appmode = false, .smp_quantum = 7}});
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
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

    constexpr simrv::xlen::Instruction loop = 0x0000006f;  // jal x0, 0
    std::memcpy(ram.data(), &loop, sizeof(loop));
    for (size_t hart = 0; hart < machine.num_harts(); ++hart) {
        machine.hart(hart).state().pc = simrv::memory::kDramBaseAddress;
    }
    const auto initial_hart0_cycles = machine.hart(0).clint_mmio.mcycle;
    const auto initial_hart1_cycles = machine.hart(1).clint_mmio.mcycle;

    machine.execute_cycle_for_testing();

    check(machine.hart(0).clint_mmio.mcycle - initial_hart0_cycles == 7);
    check(machine.hart(1).clint_mmio.mcycle - initial_hart1_cycles == 7);
    check(machine.memory().system_bus().cycle() == 7);
    check(machine.hart(0).clint_mmio.rtc_divider == 7);

    const auto instruction_boundary = machine.hart(0).e_icount + 1;
    const auto cycle_before_boundary = machine.memory().system_bus().cycle();
    machine.set_instruction_limit_for_testing(instruction_boundary);
    machine.execute_cycle_for_testing();
    if (machine.hart(0).e_icount != instruction_boundary ||
        machine.memory().system_bus().cycle() - cycle_before_boundary > 7) {
        std::cerr << "CA quantum boundary mismatch: retired=" << machine.hart(0).e_icount
                  << " expected=" << instruction_boundary
                  << " cycles=" << machine.memory().system_bus().cycle() - cycle_before_boundary
                  << '\n';
    }
    check(machine.hart(0).e_icount == instruction_boundary);
    check(machine.memory().system_bus().cycle() - cycle_before_boundary <= 7);
    std::cout << "[PASS] test_ca_quantum_smp_batching\n";
}

void test_ca_quantum_single_hart_batching() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    constexpr uint32_t kQuantum = 11;
    simrv::core::Machine machine(
        simrv::core::MachineConfig{.execution = {.appmode = false, .smp_quantum = kQuantum}});
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
    machine.cpu.machine_ = &machine;
    machine.cpu.reset();

    constexpr simrv::xlen::Instruction loop = 0x0000006f;  // jal x0, 0
    std::memcpy(ram.data(), &loop, sizeof(loop));
    machine.cpu.state().pc = simrv::memory::kDramBaseAddress;

    const auto initial_cycles = machine.cpu.clint_mmio.mcycle;
    machine.execute_cycle_for_testing();
    check(machine.cpu.clint_mmio.mcycle - initial_cycles == kQuantum);
    check(machine.memory().system_bus().cycle() == kQuantum);
    check(machine.cpu.clint_mmio.rtc_divider ==
          kQuantum % simrv::core::timing::kCyclesPerTimebaseTick);
    std::cout << "[PASS] test_ca_quantum_single_hart_batching\n";
}

void test_ca_mt_smp_pause_and_snapshots() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    constexpr size_t kNumHarts = 5;  // Hart 0 plus four independent CA worker threads.
    simrv::core::Machine machine(
        simrv::core::MachineConfig{.execution = {.appmode = false,
                                                 .num_harts = kNumHarts,
                                                 .smp_quantum = 32,
                                                 .smp_mode = simrv::core::SmpMode::MultiThreaded},
                                   .tui = {.enabled = true, .inspection_output = {}}});
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
    machine.cpu.machine_ = &machine;
    machine.cpu.reset();

    for (size_t hart = 1; hart < kNumHarts; ++hart) {
        auto secondary = std::make_unique<simrv::core::CPU>();
        secondary->machine_ = &machine;
        secondary->reset();
        secondary->state().mhartid = hart;
        secondary->hart_status.store(simrv::core::HartStatus::Started, std::memory_order_relaxed);
        machine.secondary_harts_.push_back(std::move(secondary));
    }

    constexpr simrv::xlen::Instruction loop = 0x0000006f;  // jal x0, 0
    std::memcpy(ram.data(), &loop, sizeof(loop));
    for (size_t hart = 0; hart < machine.num_harts(); ++hart) {
        machine.hart(hart).state().pc = simrv::memory::kDramBaseAddress;
    }

    machine.resume();
    machine.start_runner_for_testing();
    std::jthread primary_runner([&machine](const std::stop_token& stop_token) {
        while (!stop_token.stop_requested()) {
            machine.execute_cycle_for_testing();
        }
    });
    const auto all_workers_started = [&] {
        machine.request_tui_sample();
        for (size_t hart = 0; hart < kNumHarts; ++hart) {
            if (machine.tui_execution_snapshot(hart).cycle_count == 0) return false;
        }
        return true;
    };
    const auto startup_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!all_workers_started() && std::chrono::steady_clock::now() < startup_deadline) {
        std::this_thread::yield();
    }
    check(all_workers_started());

    machine.pause();
    primary_runner.request_stop();
    primary_runner.join();
    std::array<simrv::xlen::Counter, kNumHarts> paused_cycles{};
    for (size_t hart = 0; hart < kNumHarts; ++hart) {
        paused_cycles[hart] = machine.hart(hart).clint_mmio.mcycle;
        const auto snapshot = machine.tui_execution_snapshot(hart);
        check(snapshot.hart == hart);
        check(snapshot.cycle_count == paused_cycles[hart]);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    for (size_t hart = 0; hart < kNumHarts; ++hart) {
        check(machine.hart(hart).clint_mmio.mcycle == paused_cycles[hart]);
    }

    machine.stop_runner_for_testing();
    machine.resume();
    machine.start_runner_for_testing();
    machine.request_tui_sample();
    const auto resume_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (machine.tui_execution_snapshot(1).cycle_count == paused_cycles[1] &&
           std::chrono::steady_clock::now() < resume_deadline) {
        machine.execute_cycle_for_testing();
        std::this_thread::yield();
    }
    check(machine.tui_execution_snapshot(1).cycle_count > paused_cycles[1]);
    machine.stop_runner_for_testing();

    machine.pause();
    std::array<simrv::xlen::Counter, kNumHarts> before_step{};
    for (size_t hart = 0; hart < kNumHarts; ++hart) {
        before_step[hart] = machine.hart(hart).clint_mmio.mcycle;
    }
    machine.step();
    machine.execute_cycle_for_testing();
    for (size_t hart = 0; hart < kNumHarts; ++hart) {
        check(machine.hart(hart).clint_mmio.mcycle == before_step[hart] + 1);
    }
    machine.pause();
    std::cout << "[PASS] test_ca_mt_smp_pause_and_snapshots\n";
}

void test_cycle_kernel_golden_forwarding() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    const auto run = [&](simrv::pipeline::PipelineType type) {
        ConcreteMachine machine;
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.pipeline_sim.config.pipeline_type = type;
        machine.cpu.pipeline_sim.config.enable_forwarding = true;

        constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
        constexpr std::array<simrv::xlen::Instruction, 4> program = {
            0x00100093,  // addi x1, x0, 1
            0x00108133,  // add  x2, x1, x1
            0x001101b3,  // add  x3, x2, x1
            0x0000006f,  // jal  x0, 0
        };
        std::array<simrv::xlen::Byte, simrv::cache::ICache::kLineBytes> line{};
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
        check(machine.cpu.state().regs.read(simrv::xlen::RegId::Ra) == 1);
        check(machine.cpu.state().regs.read(simrv::xlen::RegId::Sp) == 2);
        check(machine.cpu.state().regs.read(simrv::xlen::RegId::Gp) == 3);
        return cycles;
    };

    check(run(simrv::pipeline::PipelineType::FiveStage) == 8);
    check(run(simrv::pipeline::PipelineType::ThreeStage) == 6);
    std::cout << "[PASS] test_cycle_kernel_golden_forwarding\n";
}

void test_tui_mode_sim_thread_multiple_step_sync() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    ConcreteMachine machine(
        simrv::core::MachineConfig{.execution = {.appmode = true,
                                                 .num_harts = 2,
                                                 .smp_mode = simrv::core::SmpMode::MultiThreaded},
                                   .tui = {.enabled = true, .inspection_output = {}}});
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
    machine.cpu.machine_ = &machine;
    machine.cpu.reset();

    for (size_t hart = 1; hart < 2; ++hart) {
        auto secondary = std::make_unique<simrv::core::CPU>();
        secondary->machine_ = &machine;
        secondary->reset();
        secondary->state().mhartid = hart;
        secondary->hart_status.store(simrv::core::HartStatus::Started, std::memory_order_relaxed);
        machine.secondary_harts_.push_back(std::move(secondary));
    }

    constexpr simrv::xlen::Instruction loop = 0x0000006f;  // jal x0, 0
    std::memcpy(ram.data(), &loop, sizeof(loop));
    for (size_t hart = 0; hart < machine.num_harts(); ++hart) {
        machine.hart(hart).state().pc = simrv::memory::kDramBaseAddress;
    }

    machine.start_runner_for_testing();
    machine.pause();

    // Exercise the real simulation loop, including its dormant wait and completion publication.
    std::jthread sim_thread([&machine] { machine.run(); });

    // Step 10 times synchronously
    for (int step = 0; step < 10; ++step) {
        simrv::xlen::Counter before = machine.primary_hart().clint_mmio.mcycle;
        machine.step_sync(std::chrono::milliseconds(1000));
        simrv::xlen::Counter after = machine.primary_hart().clint_mmio.mcycle;
        check(after == before + 1);
        const auto snapshot = machine.tui_execution_snapshot();
        check(snapshot.cycle_count == after);
        check(snapshot.execution_state == simrv::core::ExecutionState::Paused);
    }

    machine.request_exit();
    sim_thread.request_stop();
    if (sim_thread.joinable()) {
        sim_thread.join();
    }
    machine.stop_runner_for_testing();
    std::cout << "[PASS] test_tui_mode_sim_thread_multiple_step_sync\n";
}

void test_step_sync_timeout_and_shutdown() {
    ConcreteMachine machine;
    machine.pause();
    const auto start = std::chrono::steady_clock::now();
    machine.step_sync(std::chrono::milliseconds(20));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    if (elapsed < std::chrono::milliseconds(20) || elapsed > std::chrono::seconds(2)) std::abort();

    machine.pause();
    std::jthread stopper([&] {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!machine.is_stepping() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        machine.request_exit();
    });
    machine.step_sync(std::chrono::seconds(5));
    if (!machine.is_stopped()) std::abort();
}

void test_cycle_kernel_golden_load_use() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    const auto run = [&](simrv::pipeline::PipelineType type) {
        ConcreteMachine machine;
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.pipeline_sim.config.pipeline_type = type;

        constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
        constexpr simrv::xlen::Address data_address = pc + 0x100;
        constexpr std::array<simrv::xlen::Instruction, 5> program = {
            0x00000097,  // auipc x1, 0
            0x10008093,  // addi  x1, x1, 256
            0x0000a103,  // lw    x2, 0(x1)
            0x002101b3,  // add   x3, x2, x2
            0x0000006f,  // jal   x0, 0
        };
        std::array<simrv::xlen::Byte, simrv::cache::ICache::kLineBytes> instruction_line{};
        std::memcpy(instruction_line.data(), program.data(), sizeof(program));
        std::memcpy(ram.data(), program.data(), sizeof(program));
        machine.cpu.icache.insert(pc, instruction_line.data(), simrv::memory::MesiState::Exclusive);

        std::array<simrv::xlen::Byte, simrv::cache::DCache::kLineBytes> data_line{};
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
        check(machine.cpu.state().regs.read(simrv::xlen::RegId::Sp) == value);
        check(machine.cpu.state().regs.read(simrv::xlen::RegId::Gp) == value * 2);
        check(machine.cpu.dcache.miss_count() == 0);
        check(machine.cpu.pipeline_sim.data_hazard_stalls() ==
              (type == simrv::pipeline::PipelineType::FiveStage ? 1 : 0));
        return cycles;
    };

    check(run(simrv::pipeline::PipelineType::FiveStage) == 10);
    check(run(simrv::pipeline::PipelineType::ThreeStage) == 7);
    std::cout << "[PASS] test_cycle_kernel_golden_load_use\n";
}

void test_cycle_kernel_golden_fp_dependencies() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    constexpr auto encode_op_fp = [](uint32_t funct7, simrv::xlen::RegId rd, simrv::xlen::RegId rs1,
                                     simrv::xlen::RegId rs2, uint32_t funct3 = 0) {
        return static_cast<simrv::xlen::Instruction>(
            (funct7 << 25U) | (std::to_underlying(rs2) << 20U) | (std::to_underlying(rs1) << 15U) |
            (funct3 << 12U) | (std::to_underlying(rd) << 7U) | 0x53U);
    };
    constexpr auto encode_fma = [](simrv::xlen::RegId rd, simrv::xlen::RegId rs1,
                                   simrv::xlen::RegId rs2, simrv::xlen::RegId rs3) {
        return static_cast<simrv::xlen::Instruction>(
            (std::to_underlying(rs3) << 27U) | (std::to_underlying(rs2) << 20U) |
            (std::to_underlying(rs1) << 15U) | (std::to_underlying(rd) << 7U) | 0x43U);
    };
    constexpr auto encode_fsw = [](simrv::xlen::RegId rs2, simrv::xlen::RegId rs1,
                                   uint32_t offset) {
        return static_cast<simrv::xlen::Instruction>(
            ((offset >> 5U) << 25U) | (std::to_underlying(rs2) << 20U) |
            (std::to_underlying(rs1) << 15U) | (2U << 12U) | ((offset & 0x1FU) << 7U) | 0x27U);
    };

    const auto run = [&](simrv::pipeline::PipelineType type) {
        ConcreteMachine machine;
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.pipeline_sim.config.pipeline_type = type;
        machine.cpu.state().mstatus |= simrv::core::MstatusBit::Fs;

        constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
        constexpr simrv::xlen::Address data_address = pc + 0x100;
        constexpr simrv::xlen::RegId fa0 = static_cast<simrv::xlen::RegId>(10);
        constexpr simrv::xlen::RegId fa1 = static_cast<simrv::xlen::RegId>(11);
        constexpr simrv::xlen::RegId fa2 = static_cast<simrv::xlen::RegId>(12);
        constexpr simrv::xlen::RegId fa3 = static_cast<simrv::xlen::RegId>(13);
        constexpr simrv::xlen::RegId fa4 = static_cast<simrv::xlen::RegId>(14);
        constexpr simrv::xlen::RegId fa5 = static_cast<simrv::xlen::RegId>(15);
        constexpr std::array<simrv::xlen::Instruction, 11> program = {
            0x00052507U,                        // flw      fa0, 0(a0)
            0x00452587U,                        // flw      fa1, 4(a0)
            encode_op_fp(0x00, fa2, fa0, fa1),  // fadd.s   fa2, fa0, fa1
            encode_fma(fa3, fa0, fa1, fa2),     // fmadd.s  fa3, fa0, fa1, fa2
            encode_op_fp(0x70, simrv::xlen::RegId::A1, fa3,
                         simrv::xlen::RegId::Zero),      // fmv.x.w  a1, fa3
            encode_fsw(fa2, simrv::xlen::RegId::A0, 8),  // fsw      fa2, 8(a0)
            0x00300613U,                                 // addi     a2, zero, 3
            encode_op_fp(0x68, fa4, simrv::xlen::RegId::A2,
                         simrv::xlen::RegId::Zero),  // fcvt.s.w fa4, a2
            encode_op_fp(0x00, fa5, fa4, fa0),       // fadd.s   fa5, fa4, fa0
            encode_op_fp(0x70, simrv::xlen::RegId::A3, fa5,
                         simrv::xlen::RegId::Zero),  // fmv.x.w  a3, fa5
            0x0000006fU,                             // jal      zero, 0
        };
        std::memcpy(ram.data(), program.data(), sizeof(program));
        constexpr float lhs = 1.5F;
        constexpr float rhs = 2.25F;
        const uint32_t lhs_bits = std::bit_cast<uint32_t>(lhs);
        const uint32_t rhs_bits = std::bit_cast<uint32_t>(rhs);
        std::memcpy(ram.data() + 0x100, &lhs_bits, sizeof(lhs_bits));
        std::memcpy(ram.data() + 0x104, &rhs_bits, sizeof(rhs_bits));
        machine.cpu.state().regs.write(simrv::xlen::RegId::A0, data_address);
        machine.cpu.state().pc = pc;

        uint32_t guard = 0;
        while (machine.cpu.e_icount < 10 && guard++ < 256) {
            machine.cpu.run_cycle(machine);
            machine.memory().system_bus().advance_cycle();
        }
        if (machine.cpu.e_icount != 10) {
            std::cerr << "FP dependency program retired " << machine.cpu.e_icount
                      << " instructions for pipeline " << std::to_underlying(type) << '\n';
        }
        check(machine.cpu.e_icount == 10);
        if (machine.cpu.state().regs.read(simrv::xlen::RegId::A1) !=
            std::bit_cast<uint32_t>(7.125F)) {
            std::cerr << "FP FMA dependency produced 0x" << std::hex
                      << machine.cpu.state().regs.read(simrv::xlen::RegId::A1) << " instead of 0x"
                      << std::bit_cast<uint32_t>(7.125F) << std::dec << '\n';
        }
        check(machine.cpu.state().regs.read(simrv::xlen::RegId::A1) ==
              std::bit_cast<uint32_t>(7.125F));
        if (machine.cpu.state().regs.read(simrv::xlen::RegId::A3) !=
            std::bit_cast<uint32_t>(4.5F)) {
            std::cerr << "FP conversion dependency produced 0x" << std::hex
                      << machine.cpu.state().regs.read(simrv::xlen::RegId::A3) << " instead of 0x"
                      << std::bit_cast<uint32_t>(4.5F) << std::dec << '\n';
        }
        check(machine.cpu.state().regs.read(simrv::xlen::RegId::A3) ==
              std::bit_cast<uint32_t>(4.5F));
        uint32_t stored = 0;
        std::memcpy(&stored, ram.data() + 0x108, sizeof(stored));
        if (stored != std::bit_cast<uint32_t>(3.75F)) {
            std::cerr << "FP store dependency wrote 0x" << std::hex << stored << " instead of 0x"
                      << std::bit_cast<uint32_t>(3.75F) << std::dec << '\n';
        }
        check(stored == std::bit_cast<uint32_t>(3.75F));
        if (type == simrv::pipeline::PipelineType::FiveStage &&
            machine.cpu.pipeline_sim.data_hazard_stalls() == 0) {
            std::cerr << "five-stage FP dependency program recorded no RAW stalls\n";
        }
        check(type == simrv::pipeline::PipelineType::ThreeStage ||
              machine.cpu.pipeline_sim.data_hazard_stalls() > 0);
    };

    run(simrv::pipeline::PipelineType::FiveStage);
    run(simrv::pipeline::PipelineType::ThreeStage);
    std::cout << "[PASS] test_cycle_kernel_golden_fp_dependencies\n";
}

void test_cycle_kernel_golden_data_refill() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    const auto run = [&](simrv::pipeline::PipelineType type) {
        ConcreteMachine machine;
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.pipeline_sim.config.pipeline_type = type;
        machine.memory().initialize_mmu();

        constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
        constexpr std::array<simrv::xlen::Instruction, 4> program = {
            0x00000097,  // auipc x1, 0
            0x10008093,  // addi  x1, x1, 256
            0x0000a103,  // lw    x2, 0(x1)
            0x0000006f,  // jal   x0, 0
        };
        std::array<simrv::xlen::Byte, simrv::cache::ICache::kLineBytes> instruction_line{};
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
        check(machine.cpu.state().regs.read(simrv::xlen::RegId::Sp) == value);
        check(machine.cpu.dcache.miss_count() == 1);
        check(machine.cpu.dcache.hit_count() == 0);
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
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.pipeline_sim.config.pipeline_type = type;
        machine.cpu.pipeline_sim.config.fence_flush_penalty = 2;

        constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
        constexpr std::array<simrv::xlen::Instruction, 4> program = {
            0x00100093,  // addi x1, x0, 1
            0x0ff0000f,  // fence iorw, iorw
            0x00200113,  // addi x2, x0, 2
            0x0000006f,  // jal x0, 0
        };
        std::array<simrv::xlen::Byte, simrv::cache::ICache::kLineBytes> line{};
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
        check(machine.cpu.state().regs.read(simrv::xlen::RegId::Ra) == 1);
        check(machine.cpu.state().regs.read(simrv::xlen::RegId::Sp) == 2);
        return cycles;
    };

    check(run(simrv::pipeline::PipelineType::FiveStage) == 16);
    check(run(simrv::pipeline::PipelineType::ThreeStage) == 10);
    std::cout << "[PASS] test_cycle_kernel_golden_fence_serialization\n";
}

void test_cycle_kernel_golden_amo_writeback() {
    const auto run = [&](simrv::pipeline::PipelineType type, simrv::xlen::Instruction amo,
                         uint32_t initial_value, uint32_t operand, uint32_t expected_value,
                         simrv::xlen::Register expected_old) {
        ConcreteMachine machine;
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.pipeline_sim.config.pipeline_type = type;
        machine.cpu.pipeline_sim.config.enable_forwarding = true;

        constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
        const std::array<simrv::xlen::Instruction, 7> program = {
            0x00000093U | (operand << 20),  // addi x1, x0, operand
            0x00000117,                     // auipc x2, 0
            0x0fc10113,                     // addi x2, x2, 252 -> pc + 0x100
            amo,                            // AMO x3, x1, (x2)
            0x00019463,                     // bnez x3, +8
            0x00700213,                     // addi x4, x0, 7 (lottery winner)
            0x0000006f,                     // jal x0, 0
        };
        std::memcpy(ram.data(), program.data(), sizeof(program));
        std::memcpy(ram.data() + 0x100, &initial_value, sizeof(initial_value));
        machine.cpu.state().pc = pc;

        uint32_t guard = 0;
        while (machine.cpu.e_icount < 6 && guard++ < 160) {
            machine.cpu.run_cycle(machine);
            machine.memory().system_bus().advance_cycle();
        }

        uint32_t memory_value = 0;
        std::memcpy(&memory_value, ram.data() + 0x100, sizeof(memory_value));
        if (machine.cpu.state().regs.read(simrv::xlen::RegId::Gp) != expected_old ||
            memory_value != expected_value) {
            std::cerr << "AMO mismatch: pipeline=" << static_cast<unsigned>(type)
                      << " old=" << machine.cpu.state().regs.read(simrv::xlen::RegId::Gp)
                      << " expected_old=" << expected_old << " memory=" << memory_value
                      << " expected_memory=" << expected_value << '\n';
            std::abort();
        }
        return machine.cpu.state().regs.read(simrv::xlen::RegId::Tp);
    };

    constexpr simrv::xlen::Instruction kAmoSwapW = 0x081121af;
    constexpr simrv::xlen::Instruction kAmoAddW = 0x001121af;
    for (const auto type :
         {simrv::pipeline::PipelineType::ThreeStage, simrv::pipeline::PipelineType::FiveStage}) {
        // OpenSBI's boot lottery branches immediately on the old AMOSWAP value. A stale
        // execute-stage forwarding value incorrectly sends the boot hart to its wait loop.
        const auto swap_path = run(type, kAmoSwapW, 0, 1, 1, 0);
        if (swap_path != 7) {
            std::cerr << "AMOSWAP dependent branch selected loser path: pipeline="
                      << static_cast<unsigned>(type) << " x4=" << swap_path << '\n';
            std::abort();
        }
        const auto add_path = run(type, kAmoAddW, 5, 2, 7, 5);
        if (add_path != 0) {
            std::cerr << "AMOADD dependent branch selected zero path: pipeline="
                      << static_cast<unsigned>(type) << " x4=" << add_path << '\n';
            std::abort();
        }
        const simrv::xlen::Register negative_old = static_cast<simrv::xlen::Register>(
            static_cast<int64_t>(static_cast<int32_t>(0xffffffffU)));
        const auto signed_path = run(type, kAmoSwapW, 0xffffffffU, 1, 1, negative_old);
        if (signed_path != 0) {
            std::cerr << "AMOSWAP.W failed to sign-extend its old value: pipeline="
                      << static_cast<unsigned>(type) << " x4=" << signed_path << '\n';
            std::abort();
        }
    }
    std::cout << "[PASS] test_cycle_kernel_golden_amo_writeback\n";
}

void test_cycle_kernel_golden_multicycle_execute() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    const auto run = [&](simrv::pipeline::PipelineType type) {
        ConcreteMachine machine;
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.pipeline_sim.config.pipeline_type = type;
        machine.cpu.pipeline_sim.config.mul_latency = 3;

        constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
        constexpr std::array<simrv::xlen::Instruction, 4> program = {
            0x00600093,  // addi x1, x0, 6
            0x00700113,  // addi x2, x0, 7
            0x022081b3,  // mul  x3, x1, x2
            0x0000006f,  // jal  x0, 0
        };
        std::array<simrv::xlen::Byte, simrv::cache::ICache::kLineBytes> line{};
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
        check(machine.cpu.state().regs.read(simrv::xlen::RegId::Gp) == 42);
        return cycles;
    };

    // mul_latency is a count of additional stalls after issue. The issue cycle itself is
    // outside this delay, so three configured stalls add one cycle over the legacy encoding.
    check(run(simrv::pipeline::PipelineType::FiveStage) == 11);
    check(run(simrv::pipeline::PipelineType::ThreeStage) == 9);
    std::cout << "[PASS] test_cycle_kernel_golden_multicycle_execute\n";
}

void test_cycle_kernel_golden_branch_recovery() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    const auto run = [&](simrv::pipeline::PipelineType type) {
        ConcreteMachine machine;
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.pipeline_sim.config.pipeline_type = type;

        constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
        constexpr std::array<simrv::xlen::Instruction, 6> program = {
            0x00100093,  // addi x1, x0, 1
            0x00008463,  // beq  x1, x0, +8 (not taken)
            0x00108463,  // beq  x1, x1, +8 (taken)
            0x06300113,  // addi x2, x0, 99 (must be squashed)
            0x00700193,  // addi x3, x0, 7
            0x0000006f,  // jal  x0, 0
        };
        std::array<simrv::xlen::Byte, simrv::cache::ICache::kLineBytes> line{};
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
        check(machine.cpu.state().regs.read(simrv::xlen::RegId::Sp) == 0);
        check(machine.cpu.state().regs.read(simrv::xlen::RegId::Gp) == 7);
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
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.pipeline_sim.config.pipeline_type = type;

        constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
        constexpr std::array<simrv::xlen::Instruction, 2> program = {
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
        check(machine.cpu.state().regs.read(simrv::xlen::RegId::Ra) == 42);
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
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.pipeline_sim.config.pipeline_type = type;

        constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
        constexpr simrv::xlen::Address handler = pc + 16;
        constexpr std::array<simrv::xlen::Instruction, 6> program = {
            0xffffffff,  // illegal instruction
            0x00900093,  // addi x1, x0, 9 (must be squashed)
            0x00000013,  // nop
            0x00000013,  // nop
            0x00100113,  // handler: addi x2, x0, 1
            0x0000006f,  // jal x0, 0
        };
        std::array<simrv::xlen::Byte, simrv::cache::ICache::kLineBytes> line{};
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
              static_cast<simrv::xlen::TrapCause>(ExceptionCode::IllegalInstruction));
        check(machine.cpu.state().mepc == pc);
        check(machine.cpu.state().pc == handler);
        check(machine.cpu.state().regs.read(simrv::xlen::RegId::Ra) == 0);
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
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        machine.cpu.pipeline_sim.config.pipeline_type = type;

        constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
        constexpr simrv::xlen::Address handler = pc + 16;
        constexpr std::array<simrv::xlen::Instruction, 6> program = {
            0x00100093,  // addi x1, x0, 1 (retires before interrupt)
            0x00200113,  // addi x2, x0, 2 (must be squashed)
            0x00000013,  // nop
            0x00000013,  // nop
            0x00100193,  // handler: addi x3, x0, 1
            0x0000006f,  // jal x0, 0
        };
        std::array<simrv::xlen::Byte, simrv::cache::ICache::kLineBytes> line{};
        std::memcpy(line.data(), program.data(), sizeof(program));
        std::memcpy(ram.data(), program.data(), sizeof(program));
        machine.cpu.icache.insert(pc, line.data(), simrv::memory::MesiState::Exclusive);
        machine.cpu.state().pc = pc;
        machine.cpu.state().mtvec = handler;
        machine.cpu.state().mstatus |= simrv::core::MstatusBit::Mie;
        machine.cpu.state().mie |= simrv::core::MipBit::Mtip;
        machine.cpu.state().mip |= simrv::core::MipBit::Mtip;

        uint32_t cycles = 0;
        while (!trap_is_interrupt(machine.cpu.state().mcause) && cycles < 32) {
            machine.cpu.run_cycle(machine);
            machine.memory().system_bus().advance_cycle();
            ++cycles;
        }
        check(machine.cpu.state().mcause == (kInterruptCauseBit | simrv::xlen::TrapCause{7}));
        check(machine.cpu.state().mepc == pc + 4);
        check(machine.cpu.state().pc == handler);
        check(machine.cpu.state().regs.read(simrv::xlen::RegId::Ra) == 1);
        check(machine.cpu.state().regs.read(simrv::xlen::RegId::Sp) == 0);
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
        simrv::xlen::Register x1 = 0;
        simrv::xlen::Register x2 = 0;
        simrv::xlen::Register x3 = 0;
        simrv::xlen::Word memory_value = 0;
        simrv::xlen::Counter retired = 0;
        uint64_t cycles = 0;
        size_t history_size = 0;
        uint64_t data_misses = 0;
    };
    const auto run = [&](simrv::core::ExecutionEngine engine,
                         simrv::pipeline::PipelineType pipeline_type) {
        ConcreteMachine machine;
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.memory().initialize_mmu();
        machine.runtime_profile.engine = engine;
        machine.cpu.pipeline_sim.config.pipeline_type = pipeline_type;
        machine.cpu.pipeline_sim.config.record_snapshots =
            engine == simrv::core::ExecutionEngine::CycleObservable;

        constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
        constexpr simrv::xlen::Address data_address = pc + 0x100;
        constexpr std::array<simrv::xlen::Instruction, 7> program = {
            0x02a00093,  // addi  x1, x0, 42
            0x00000117,  // auipc x2, 0
            0x0fc10113,  // addi  x2, x2, 252 -> pc + 0x100
            0x00112023,  // sw    x1, 0(x2)
            0x00012183,  // lw    x3, 0(x2)
            0x10500073,  // wfi
            0xffdff06f,  // jal   x0, -4 (repeat cached WFI)
        };
        std::array<simrv::xlen::Byte, simrv::cache::ICache::kLineBytes> instruction_line{};
        std::memcpy(instruction_line.data(), program.data(), sizeof(program));
        std::memcpy(ram.data(), program.data(), sizeof(program));
        machine.cpu.icache.insert(pc, instruction_line.data(), simrv::memory::MesiState::Exclusive);
        machine.cpu.state().pc = pc;

        uint32_t guard = 0;
        while (machine.cpu.e_icount < 5 && guard++ < 64) {
            machine.cpu.run_cycle(machine);
            machine.memory().system_bus().advance_cycle();
        }
        simrv::xlen::Word memory_value = 0;
        if (simrv::core::is_cycle_engine(engine)) {
            check(machine.cpu.dcache.read(
                data_address, memory_value,
                static_cast<simrv::xlen::Instruction>(simrv::isa::Funct3::Lw)));
        } else {
            std::memcpy(&memory_value, ram.data() + 0x100, sizeof(memory_value));
        }
        return Result{.x1 = machine.cpu.state().regs.read(simrv::xlen::RegId::Ra),
                      .x2 = machine.cpu.state().regs.read(simrv::xlen::RegId::Sp),
                      .x3 = machine.cpu.state().regs.read(simrv::xlen::RegId::Gp),
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

void test_dynamic_mode_switching_cache_behavior() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };

    ConcreteMachine machine(simrv::core::MachineConfig{.bram_prewarm = true});
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.cpu.machine_ = &machine;
    machine.cpu.reset();
    machine.memory().initialize_mmu();
    // Start in InstructionFast mode
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::InstructionFast;
    machine.cpu.pipeline_sim.config.pipeline_type = simrv::pipeline::PipelineType::FiveStage;

    constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
    constexpr simrv::xlen::Address data_address = pc + 0x100;
    constexpr std::array<simrv::xlen::Instruction, 7> program = {
        0x02a00093,  // addi  x1, x0, 42
        0x00000117,  // auipc x2, 0
        0x0fc10113,  // addi  x2, x2, 252 -> pc + 0x100
        0x00112023,  // sw    x1, 0(x2)
        0x00012183,  // lw    x3, 0(x2)
        0x10500073,  // wfi
        0xffdff06f,  // jal   x0, -4 (repeat cached WFI)
    };
    std::memcpy(ram.data(), program.data(), sizeof(program));
    machine.record_loaded_segment(pc, sizeof(program), true);
    machine.record_loaded_segment(data_address, simrv::memory::CoherenceHub::kLineBytes, false);
    machine.cpu.state().pc = pc;

    // Step 2 instructions in fast functional mode
    machine.cpu.run_cycle(machine);
    machine.cpu.run_cycle(machine);
    check(machine.cpu.e_icount == 2);
    check(machine.cpu.state().regs.read(simrv::xlen::RegId::Ra) == 42);
    // In IA mode, dcache has not seen any accesses yet
    check(machine.cpu.dcache.hit_count() == 0);
    check(machine.cpu.dcache.miss_count() == 0);

    // Switch dynamically to CycleObservable without reboot or reload!
    machine.switch_execution_engine(simrv::core::ExecutionEngine::CycleObservable);
    check(machine.runtime_profile.is_cycle_mode());
    check(machine.cpu.pipeline_sim.config.record_snapshots);
    check(machine.cpu.icache.line_state(pc) == simrv::memory::MesiState::Shared);
    check(machine.cpu.dcache.line_state(data_address) == simrv::memory::MesiState::Shared);
    const auto prewarmed_data_dir =
        machine.memory().system_bus().coherence_hub().get_directory_state(data_address);
    check(prewarmed_data_dir.state == simrv::memory::MesiState::Shared);
    check(prewarmed_data_dir.sharers_mask == 1);

    // Step in cycle mode through addi, sw, and lw
    uint32_t guard = 0;
    while (machine.cpu.e_icount < 5 && guard++ < 256) {
        machine.cpu.run_cycle(machine);
        machine.memory().system_bus().advance_cycle();
    }
    check(machine.cpu.e_icount == 5);
    check(machine.cpu.state().regs.read(simrv::xlen::RegId::Gp) == 42);
    // In cycle mode, dcache must have recorded accesses!
    check(machine.cpu.dcache.hit_count() + machine.cpu.dcache.miss_count() > 0);

    simrv::xlen::Word mem_val = 0;
    check(machine.cpu.dcache.read(data_address, mem_val,
                                  static_cast<simrv::xlen::Instruction>(simrv::isa::Funct3::Lw)));
    check(mem_val == 42);

    // Switch dynamically back to InstructionFast without reload
    machine.switch_execution_engine(simrv::core::ExecutionEngine::InstructionFast);
    check(machine.runtime_profile.is_instruction_fast());
    check(!machine.cpu.pipeline_sim.config.record_snapshots);

    std::cout << "[PASS] test_dynamic_mode_switching_cache_behavior\n";
}

void test_instruction_policy_architectural_equivalence() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    struct Result {
        simrv::xlen::Register x1 = 0;
        simrv::xlen::Register x2 = 0;
        simrv::xlen::Register x3 = 0;
        simrv::xlen::Address pc = 0;
        uint32_t memory_value = 0;
        simrv::xlen::Counter retired = 0;
        simrv::xlen::Counter cycles = 0;
    };
    const auto run = [](simrv::core::ExecutionEngine engine) {
        ConcreteMachine machine;
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.cpu.machine_ = &machine;
        machine.cpu.reset();
        machine.memory().initialize_mmu();
        machine.runtime_profile.engine = engine;

        constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
        constexpr std::array<simrv::xlen::Instruction, 6> program = {
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
        return Result{.x1 = machine.cpu.state().regs.read(simrv::xlen::RegId::Ra),
                      .x2 = machine.cpu.state().regs.read(simrv::xlen::RegId::Sp),
                      .x3 = machine.cpu.state().regs.read(simrv::xlen::RegId::Gp),
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
    assert((machine.cpu.state().mip & simrv::core::MipBit::Mtip) == 0);
    assert((machine.cpu.state().mip & simrv::core::MipBit::Msip) == 0);

    // MSWI test
    mswi.write32(0x00, 1);
    machine.cpu.apply_pending_interrupt_lines(machine);
    assert((machine.cpu.state().mip & simrv::core::MipBit::Msip) != 0);
    assert(mswi.read32(0x00) == 1);
    mswi.write32(0x00, 0);
    machine.cpu.apply_pending_interrupt_lines(machine);
    assert((machine.cpu.state().mip & simrv::core::MipBit::Msip) == 0);

    // MTIMER test: set mtimecmp = 100, mtime = 150
    mtimer.write64(0x4000, 100);
    mtimer.write64(0x7ff8, 150);
    machine.cpu.apply_pending_interrupt_lines(machine);
    assert((machine.cpu.state().mip & simrv::core::MipBit::Mtip) != 0);

    // Clear by setting mtimecmp higher
    mtimer.write64(0x4000, 200);
    machine.cpu.apply_pending_interrupt_lines(machine);
    assert((machine.cpu.state().mip & simrv::core::MipBit::Mtip) == 0);
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
    machine.cpu.apply_pending_interrupt_lines(machine);

    assert((machine.cpu.state().mip & simrv::core::MipBit::Seip) != 0);

    // MSI delivery via IMSIC testing
    imsic_s.csr_write(0, 0x70, 1);        // eidelivery = 1
    imsic_s.csr_write(0, 0xC0, 1U << 5);  // enable interrupt ID 5
    imsic_s.trigger_msi(0, 5);
    machine.cpu.apply_pending_interrupt_lines(machine);

    assert((machine.cpu.state().mip & simrv::core::MipBit::Seip) != 0);
    assert((imsic_s.csr_read(0, 0x44) & 0xffff) == 5);  // topei returns ID 5
    std::cout << "[PASS] test_aia_aplic_and_imsic\n";
}

void test_pcie_and_virtio_pci() {
    ConcreteMachine machine;
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());

    simrv::device::Imsic imsic_s(&machine, simrv::device::Imsic::Privilege::Supervisor,
                                 simrv::mmio::kImsicSBaseAddress, simrv::mmio::kImsicSSize);
    simrv::device::PcieRootComplex rc(&machine, nullptr, &imsic_s);
    auto blk = std::make_shared<simrv::device::VirtioPciBlock>();
    blk->bar_write(0, 0x00, 0, 4);
    const uint32_t block_features = blk->bar_read(0, 0x04, 4);
    if ((block_features & ((1U << 9) | (1U << 13) | (1U << 14))) !=
        ((1U << 9) | (1U << 13) | (1U << 14))) {
        std::cerr << "VirtIO PCI block features missing\n";
        std::abort();
    }
    if (blk->bar_read(0, 0x300 + 36, 4) != simrv::device::virtio::BlockBackend::kMaxRangeSectors ||
        blk->bar_read(0, 0x300 + 40, 4) != 1 ||
        blk->bar_read(0, 0x300 + 48, 4) != simrv::device::virtio::BlockBackend::kMaxRangeSectors ||
        blk->bar_read(0, 0x300 + 52, 4) != 1 || blk->bar_read(0, 0x300 + 56, 1) != 1) {
        std::cerr << "VirtIO PCI block range configuration mismatch\n";
        std::abort();
    }
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

    // Fast active BAR routing verification
    assert(rc.active_bar_count() == 1);
    assert(rc.get_device(0, 1, 0) == blk.get());
    assert(rc.get_device(0, 8, 0) == nullptr);

    // MMIO read/write routed to BAR 0
    rc.mmio_write(0x40001000 + 0x00, 1, 4);
    assert(rc.mmio_read(0x40001000 + 0x04, 4) ==
           simrv::device::virtio::kVirtioFVersion1FeatureWord);

    // Virtqueue 64-bit address writes via split 32-bit MMIO
    rc.mmio_write(0x40001000 + 0x16, 0, 2);
    rc.mmio_write(0x40001000 + 0x20, 0x80001234U, 4);
    rc.mmio_write(0x40001000 + 0x24, 0x00000001U, 4);
    assert(blk->get_queue_state(0)->desc_addr == 0x180001234ULL);

#if SIMRV_XLEN == 64
    // 64-bit MMIO access
    rc.mmio_write(0x40001000 + 0x20, 0x180005678ULL, 8);
    assert(blk->get_queue_state(0)->desc_addr == 0x180005678ULL);
    assert(rc.mmio_read(0x40001000 + 0x20, 8) == 0x180005678ULL);
#endif

    // Test INTx assertion and deassertion on reading ISR offset 0x200
    auto& plic = machine.cpu.plic_mmio;
    (void)plic;
    assert(((plic.plic_pending[0] >> 17) & 1) == 0);
    blk->trigger_irq();
    assert(((plic.plic_pending[0] >> 17) & 1) == 1);
    const auto isr_status = rc.mmio_read(0x40001000 + 0x200, 4);
    (void)isr_status;
    assert(((plic.plic_pending[0] >> 17) & 1) == 0);

    // Test MSI capability configuration and MSI interrupt delivery to IMSIC
    assert(!blk->is_msi_enabled());
    assert(rc.ecam_read(0x8000 + 0x84, 1) == 0x05);  // MSI Cap ID
    rc.ecam_write(0x8000 + 0x84 + 0x04, static_cast<uint32_t>(simrv::mmio::kImsicSBaseAddress), 4);
    rc.ecam_write(
        0x8000 + 0x84 + 0x08,
        static_cast<uint32_t>(static_cast<uint64_t>(simrv::mmio::kImsicSBaseAddress) >> 32), 4);
    rc.ecam_write(0x8000 + 0x84 + 0x0C, 9, 2);        // Message data: vector 9
    rc.ecam_write(0x8000 + 0x84 + 0x02, 0x0081U, 2);  // Enable MSI + 64-bit capable
    assert(blk->is_msi_enabled());
    assert(blk->msi_address() == simrv::mmio::kImsicSBaseAddress);
    assert(blk->msi_data() == 9);

    imsic_s.csr_write(0, 0x70, 1);        // eidelivery = 1
    imsic_s.csr_write(0, 0xC0, 1U << 9);  // enable interrupt ID 9
    blk->trigger_irq();                   // With MSI enabled, routes via send_msi to IMSIC
    assert((imsic_s.csr_read(0, 0x44) & 0xffff) == 9);  // topei returns ID 9

    // Disable MSI: subsequent interrupts fall back to INTx pin assertion
    rc.ecam_write(0x8000 + 0x84 + 0x02, 0x0080U, 2);
    assert(!blk->is_msi_enabled());

    // Attach Console (Slot 2), RNG (Slot 3), GPU (Slot 4), Input (Slot 5), Sound (Slot 6)
    auto console = std::make_shared<simrv::device::VirtioPciConsole>();
    auto rng = std::make_shared<simrv::device::VirtioPciRng>();
    auto gpu = std::make_shared<simrv::device::VirtioPciGpu>();
    gpu->bar_write(0, 0x00, 0, 4);
    if ((gpu->bar_read(0, 0x04, 4) & (1U << 1)) != 0) {
        std::cerr << "VirtIO GPU advertises unsupported VirGL\n";
        std::abort();
    }
    auto input = std::make_shared<simrv::device::VirtioPciInput>();
    auto sound = std::make_shared<simrv::device::VirtioPciSound>();
    const auto advertises_version_1 = [](simrv::device::VirtioPciDevice& device) {
        device.bar_write(0, 0x00, 1, 4);
        const bool supported =
            device.bar_read(0, 0x04, 4) == simrv::device::virtio::kVirtioFVersion1FeatureWord;
        device.bar_write(0, 0x00, 0, 4);
        return supported;
    };
    if (!advertises_version_1(*blk) || !advertises_version_1(*console) ||
        !advertises_version_1(*rng) || !advertises_version_1(*gpu) ||
        !advertises_version_1(*input) || !advertises_version_1(*sound)) {
        std::cerr << "VirtIO PCI devices must advertise the shared VERSION_1 feature word\n";
        std::abort();
    }

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
    if (!advertises_version_1(*net)) {
        std::cerr << "VirtIO PCI net must advertise the shared VERSION_1 feature word\n";
        std::abort();
    }
    rc.attach_device(0, 7, 0, net);
    const uint32_t net_id = rc.ecam_read((7 << 15) + 0x00, 4);
    assert((net_id & 0xffff) == 0x1AF4);
    assert((net_id >> 16) == 0x1041);
    (void)net_id;

    // 64-bit BAR probe sizing and addressing on a 64-bit endpoint
    class Test64BitEndpoint : public simrv::device::PciDevice {
       public:
        Test64BitEndpoint() : PciDevice(0x1234, 0x5678, 0x01, 0x020000) {
            init_bar(0, 0x4000, true, false);  // 16KB 64-bit BAR at BAR 0/1
        }
        auto bar_read(simrv::device::PciBarIndex bar_idx, simrv::device::PciBarOffset offset,
                      simrv::device::PciAccessBytes size) -> simrv::device::PciMmioData override {
            (void)bar_idx;
            (void)offset;
            (void)size;
            return 0xCAFEBABE;
        }
    };
    auto test_ep = std::make_shared<Test64BitEndpoint>();
    rc.attach_device(0, 8, 0, test_ep);
    // ECAM offset for dev 8: (8 << 15) = 0x40000
    // Probe BAR 0 (low dword):
    rc.ecam_write(0x40000 + 0x10, 0xFFFFFFFFU, 4);
    const uint32_t bar0_low_probe = rc.ecam_read(0x40000 + 0x10, 4);
    assert((bar0_low_probe & ~0xFU) == ~(0x4000U - 1U));
    assert((bar0_low_probe & 0x04) != 0);  // 64-bit flag
    (void)bar0_low_probe;

    // Probe BAR 1 (high dword):
    rc.ecam_write(0x40000 + 0x14, 0xFFFFFFFFU, 4);
    const uint32_t bar0_hi_probe = rc.ecam_read(0x40000 + 0x14, 4);
    assert(bar0_hi_probe == 0xFFFFFFFFU);
    (void)bar0_hi_probe;

    // Assign address: 0x40020000
    rc.ecam_write(0x40000 + 0x10, 0x40020000U, 4);
    rc.ecam_write(0x40000 + 0x14, 0x00000000U, 4);
    assert(rc.mmio_read(0x40020000, 4) == 0xCAFEBABE);

    std::cout << "[PASS] test_pcie_and_virtio_pci\n";
}

void test_virtio_block_overlay_preserves_base_image() {
    const auto check = [](bool condition, const char* message) {
        if (!condition) {
            std::cerr << message << '\n';
            std::abort();
        }
    };
    char path[] = "/tmp/simrv-block-overlay-test-XXXXXX";
    const int fd = ::mkstemp(path);
    check(fd >= 0, "create overlay base image");
    std::array<std::byte, 2048> original{};
    original.fill(std::byte{0x2a});
    check(::write(fd, original.data(), original.size()) == static_cast<ssize_t>(original.size()),
          "initialize overlay base image");
    ::close(fd);

    {
        simrv::device::virtio::BlockBackend backend(path);
        check(backend.is_loaded(), "load overlay base image");
        check(backend.capacity_sectors() == 4, "overlay preserves disk capacity");

        std::array<std::byte, 512> replacement{};
        replacement.fill(std::byte{0x7b});
        check(backend.write_sectors(1, replacement), "write sector into overlay");
        check(backend.write_zeroes(2, 1), "write zeroes into overlay");
        check(backend.discard_sectors(1, 1), "discard overlay sector");
        check(!backend.write_zeroes(3, 2), "reject write-zeroes past disk capacity");
        check(!backend.write_sectors(0, std::span<const std::byte>(replacement).first(511)),
              "reject partial-sector writes");
        check(backend.flush(), "flush overlay contents");

        std::array<std::byte, 2048> observed{};
        check(backend.read_sectors(0, observed), "read sectors across base and overlay");
        check(std::equal(observed.begin(), observed.begin() + 512, original.begin()),
              "unmodified leading sector remains from base image");
        check(std::equal(observed.begin() + 512, observed.begin() + 1024, original.begin() + 512),
              "discarded sector reads through from base image");
        check(std::all_of(observed.begin() + 1024, observed.begin() + 1536,
                          [](std::byte byte) { return byte == std::byte{0}; }),
              "write-zeroes reads as zero");
        check(std::equal(observed.begin() + 1536, observed.end(), original.begin() + 1536),
              "unmodified trailing sectors remain from base image");
    }

    const int verify_fd = ::open(path, O_RDONLY | O_CLOEXEC);
    check(verify_fd >= 0, "reopen original disk image");
    std::array<std::byte, 2048> persisted{};
    check(::read(verify_fd, persisted.data(), persisted.size()) ==
              static_cast<ssize_t>(persisted.size()),
          "read original disk image after overlay writes");
    ::close(verify_fd);
    check(persisted == original, "overlay writes never modify original disk image");
    ::unlink(path);
    std::cout << "[PASS] test_virtio_block_overlay_preserves_base_image\n";
}

void test_virtio_mmio_v2() {
    ConcreteMachine machine;
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());

    simrv::device::VirtioMmioBlock blk(0x10001000, 2, &machine);
    simrv::device::VirtioMmioConsole con(0x10002000, 1, &machine);
    simrv::device::VirtioMmioRng rng(0x10003000, 4, &machine);
    simrv::device::VirtioMmioNet net(0x10007000, 8, &machine);
    simrv::device::VirtioMmioGpu gpu(0x10008000, 10, &machine);

    // Test Magic (0x74726976) and Version 2
    assert(blk.read32(0x00) == 0x74726976);
    assert(blk.read32(0x04) == 2);
    assert(blk.read32(0x08) == 2);  // Block Device ID
    blk.write32(0x14, 0);           // device_features_sel = 0
    const uint32_t block_features = blk.read32(0x10);
    if ((block_features & ((1U << 9) | (1U << 13) | (1U << 14))) !=
            ((1U << 9) | (1U << 13) | (1U << 14)) ||
        (block_features & (1U << 1)) != 0 ||
        blk.read32(0x100 + 36) != simrv::device::virtio::BlockBackend::kMaxRangeSectors ||
        blk.read32(0x100 + 40) != 1 ||
        blk.read32(0x100 + 48) != simrv::device::virtio::BlockBackend::kMaxRangeSectors ||
        blk.read32(0x100 + 52) != 1 || blk.read32(0x100 + 56) != 1) {
        std::cerr << "VirtIO block feature advertisement mismatch\n";
        std::abort();
    }
    blk.write32(0x14, 1);
    if (blk.read32(0x10) != simrv::device::virtio::kVirtioFVersion1FeatureWord) {
        std::cerr << "VirtIO MMIO block VERSION_1 feature word mismatch\n";
        std::abort();
    }
    blk.write32(0x14, 0);

    assert(con.read32(0x00) == 0x74726976);
    assert(con.read32(0x04) == 2);
    assert(con.read32(0x08) == 3);  // Console Device ID

    assert(rng.read32(0x00) == 0x74726976);
    assert(rng.read32(0x04) == 2);
    assert(rng.read32(0x08) == 4);  // RNG Device ID

    assert(net.read32(0x00) == 0x74726976);
    assert(net.read32(0x04) == 2);
    assert(net.read32(0x08) == 1);  // Net Device ID
    gpu.write32(0x14, 0);
    if ((gpu.read32(0x10) & (1U << 1)) != 0) {
        std::cerr << "VirtIO MMIO GPU advertises unsupported VirGL\n";
        std::abort();
    }

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

    const auto check_block = [](bool condition, const char* message) {
        if (!condition) {
            std::cerr << message << '\n';
            std::abort();
        }
    };
    char disk_path[] = "/tmp/simrv-mmio-block-test-XXXXXX";
    const int disk_fd = ::mkstemp(disk_path);
    check_block(disk_fd >= 0, "create MMIO block test image");
    std::array<std::byte, 2048> base_disk{};
    base_disk.fill(std::byte{0x2a});
    check_block(::write(disk_fd, base_disk.data(), base_disk.size()) ==
                    static_cast<ssize_t>(base_disk.size()),
                "initialize MMIO block test image");
    ::close(disk_fd);
    check_block(blk.load_disk(disk_path), "attach MMIO block test image");

    const simrv::xlen::Address ram_base = machine.memory_geometry().dram_base;
    const auto guest_pointer = [&](simrv::xlen::Address address) {
        return ram.data() + static_cast<size_t>(address - ram_base);
    };
    constexpr simrv::xlen::Address kDescTable = 0x80001000;
    constexpr simrv::xlen::Address kAvailRing = 0x80002000;
    constexpr simrv::xlen::Address kUsedRing = 0x80003000;
    constexpr simrv::xlen::Address kHeaderAddr = 0x80004000;
    constexpr simrv::xlen::Address kRangeAddr = 0x80004200;
    constexpr simrv::xlen::Address kReadDataAddr = 0x80004400;
    constexpr simrv::xlen::Address kStatusAddr = 0x80004600;
    struct BlockHeader {
        uint32_t type;
        uint32_t ioprio;
        uint64_t sector;
    };
    struct Range {
        uint64_t sector;
        uint32_t num_sectors;
        uint32_t flags;
    };
    const auto submit_block = [&](uint16_t request_index, uint32_t type, uint64_t sector,
                                  const Range* range, bool flush) {
        BlockHeader header{type, 0, sector};
        std::memcpy(guest_pointer(kHeaderAddr), &header, sizeof(header));
        if (range != nullptr) std::memcpy(guest_pointer(kRangeAddr), range, sizeof(*range));

        std::array<simrv::device::virtio::VirtqDesc, 3> descs{};
        descs[0] = {kHeaderAddr, sizeof(header), simrv::device::virtio::kVirtqDescFNext, 1};
        if (flush) {
            descs[1] = {kStatusAddr, 1, simrv::device::virtio::kVirtqDescFWrite, 0};
        } else {
            const bool is_read = type == 0;
            const simrv::xlen::Address data_addr = is_read ? kReadDataAddr : kRangeAddr;
            const uint32_t data_len = is_read ? 512 : sizeof(Range);
            descs[1] = {
                data_addr, data_len,
                static_cast<uint16_t>(simrv::device::virtio::kVirtqDescFNext |
                                      (is_read ? simrv::device::virtio::kVirtqDescFWrite : 0)),
                2};
            descs[2] = {kStatusAddr, 1, simrv::device::virtio::kVirtqDescFWrite, 0};
        }
        std::memcpy(guest_pointer(kDescTable), descs.data(), sizeof(descs));
        const uint16_t head = 0;
        std::memcpy(guest_pointer(kAvailRing + 2), &request_index, sizeof(request_index));
        std::memcpy(guest_pointer(kAvailRing + 4 + (request_index - 1) * 2), &head, sizeof(head));
        blk.write32(0x50, 0);  // Notify queue 0

        uint8_t status = 0xff;
        std::memcpy(&status, guest_pointer(kStatusAddr), sizeof(status));
        uint16_t used_idx = 0;
        std::memcpy(&used_idx, guest_pointer(kUsedRing + 2), sizeof(used_idx));
        simrv::device::virtio::VirtqUsedElem used{};
        std::memcpy(&used, guest_pointer(kUsedRing + 4 + (used_idx - 1) % 32 * sizeof(used)),
                    sizeof(used));
        return std::pair{status, used.len};
    };

    const auto [flush_status, flush_used_len] = submit_block(1, 4, 0, nullptr, true);
    check_block(flush_status == 0 && flush_used_len == 1,
                "complete FLUSH with its status in the second descriptor");
    const Range zero_range{1, 1, 1};
    const auto [zero_status, zero_used_len] = submit_block(2, 13, 0, &zero_range, false);
    check_block(zero_status == 0 && zero_used_len == 1, "complete WRITE_ZEROES request");
    const auto [read_zero_status, read_zero_used_len] = submit_block(3, 0, 1, nullptr, false);
    check_block(read_zero_status == 0 && read_zero_used_len == 513,
                "read zeroed sector through the VirtIO queue");
    check_block(std::all_of(guest_pointer(kReadDataAddr), guest_pointer(kReadDataAddr) + 512,
                            [](std::byte byte) { return byte == std::byte{0}; }),
                "VirtIO WRITE_ZEROES returns zeroes on later reads");
    const Range discard_range{1, 1, 0};
    const auto [discard_status, discard_used_len] = submit_block(4, 11, 0, &discard_range, false);
    check_block(discard_status == 0 && discard_used_len == 1, "complete DISCARD request");
    const auto [read_base_status, read_base_used_len] = submit_block(5, 0, 1, nullptr, false);
    check_block(read_base_status == 0 && read_base_used_len == 513,
                "read discarded sector from the base image");
    check_block(std::all_of(guest_pointer(kReadDataAddr), guest_pointer(kReadDataAddr) + 512,
                            [](std::byte byte) { return byte == std::byte{0x2a}; }),
                "DISCARD reveals the unspecified base-image sector");
    ::unlink(disk_path);

    std::cout << "[PASS] test_virtio_mmio_v2\n";
}

void test_sbi_multihart_rfence() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    simrv::core::Machine machine(simrv::core::MachineConfig{.execution = {.appmode = false}});
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
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
    constexpr simrv::xlen::Address vaddr = 0x80004000;
    constexpr simrv::xlen::Address paddr = 0x80008000;
    machine.hart(1).tlb.insert_inst_r(vaddr, paddr, 0, simrv::xlen::PrivilegeLevel::Supervisor);
    machine.hart(2).tlb.insert_inst_r(vaddr, paddr, 0, simrv::xlen::PrivilegeLevel::Supervisor);
    check(machine.hart(1).tlb.lookup_inst_r(vaddr, 0, simrv::xlen::PrivilegeLevel::Supervisor) !=
          nullptr);
    check(machine.hart(2).tlb.lookup_inst_r(vaddr, 0, simrv::xlen::PrivilegeLevel::Supervisor) !=
          nullptr);

    // Broadcast RemoteSfenceVma from Hart 0 targeting Hart 1 and Hart 2 (mask = 0b110, base = 0)
    machine.cpu.state().regs.write(simrv::xlen::RegId::A0, 0b110);       // hart_mask
    machine.cpu.state().regs.write(simrv::xlen::RegId::A1, 0);           // hart_mask_base
    machine.cpu.state().regs.write(simrv::xlen::RegId::A7, 0x52464E43);  // ExtId::Rfence
    machine.cpu.state().regs.write(simrv::xlen::RegId::A6, 1);  // RfenceFid::RemoteSfenceVma
    check(machine.cpu.sbi.handle_ecall(ExceptionCode::SupervisorEcall));
    check(machine.cpu.state().regs.read(simrv::xlen::RegId::A0) == 0);  // SbiError::Success

    // Verify secondary harts had TLB flushed
    check(machine.hart(1).tlb.lookup_inst_r(vaddr, 0, simrv::xlen::PrivilegeLevel::Supervisor) ==
          nullptr);
    check(machine.hart(2).tlb.lookup_inst_r(vaddr, 0, simrv::xlen::PrivilegeLevel::Supervisor) ==
          nullptr);

    // Test RemoteFenceI with base == -1 (all harts broadcast)
    machine.cpu.state().regs.write(simrv::xlen::RegId::A0, 0);  // ignored when base is -1
    machine.cpu.state().regs.write(simrv::xlen::RegId::A1,
                                   static_cast<simrv::xlen::Word>(-1));  // all harts
    machine.cpu.state().regs.write(simrv::xlen::RegId::A7, 0x52464E43);  // ExtId::Rfence
    machine.cpu.state().regs.write(simrv::xlen::RegId::A6, 0);           // RfenceFid::RemoteFenceI
    check(machine.cpu.sbi.handle_ecall(ExceptionCode::SupervisorEcall));
    check(machine.cpu.state().regs.read(simrv::xlen::RegId::A0) == 0);

    // Test send_ipi broadcast
    machine.cpu.state().regs.write(simrv::xlen::RegId::A0, 0b10);      // hart 1 only
    machine.cpu.state().regs.write(simrv::xlen::RegId::A1, 0);         // base 0
    machine.cpu.state().regs.write(simrv::xlen::RegId::A7, 0x735049);  // ExtId::Ipi
    machine.cpu.state().regs.write(simrv::xlen::RegId::A6, 0);         // send_ipi
    check(machine.cpu.sbi.handle_ecall(ExceptionCode::SupervisorEcall));
    check(machine.cpu.state().regs.read(simrv::xlen::RegId::A0) == 0);
    check((machine.hart(1).state().mip & simrv::core::MipBit::Ssip) != 0);
    check((machine.hart(2).state().mip & simrv::core::MipBit::Ssip) == 0);

    std::cout << "[PASS] test_sbi_multihart_rfence\n";
}

void test_ia_multihart_lr_sc_coherence() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    simrv::core::Machine machine(simrv::core::MachineConfig{.execution = {.appmode = false}});
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
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

    constexpr simrv::xlen::Address target_addr = simrv::memory::kDramBaseAddress + 0x200;
    constexpr simrv::xlen::Address physical_page = simrv::memory::kDramBaseAddress;
    constexpr simrv::xlen::Address lr_alias = 0x400200;
    constexpr simrv::xlen::Address store_alias = 0x500200;
    constexpr uint32_t initial_val = 42;
    std::memcpy(&ram[0x200], &initial_val, sizeof(initial_val));

    auto& h0 = machine.hart(0);
    h0.state().priv = kPrivMachine;
    h0.state().satp = 0;
    h0.pipeline_context = {};
    h0.pipeline_context.opcode = simrv::isa::Opcode::Amo;
    h0.pipeline_context.funct5 = simrv::isa::Funct5Amo::Lr;
    h0.pipeline_context.funct3 = simrv::isa::Funct3::Lw;
    h0.pipeline_context.mem_addr = target_addr;
    h0.pipeline_context.rrs1 = target_addr;
    h0.run_memory_stage(machine);
    check(h0.pipeline_context.mem_paddr.raw() == target_addr);
    check(machine.memory().reservation_table().check_reservation(0, target_addr));

    h0.pipeline_context = {};
    h0.pipeline_context.opcode = simrv::isa::Opcode::Amo;
    h0.pipeline_context.funct5 = simrv::isa::Funct5Amo::Sc;
    h0.pipeline_context.funct3 = simrv::isa::Funct3::Sw;
    h0.pipeline_context.mem_addr = target_addr;
    h0.pipeline_context.rrs1 = target_addr;
    h0.pipeline_context.rrs2 = 43;
    h0.run_memory_stage(machine);
    check(h0.pipeline_context.wb_data == 0);
    uint32_t direct_sc_value = 0;
    std::memcpy(&direct_sc_value, &ram[0x200], sizeof(direct_sc_value));
    check(direct_sc_value == 43);

    const simrv::xlen::Word satp =
        simrv::xlen::kIsXLen64 ? (simrv::xlen::Word{8} << 60) : (simrv::xlen::Word{1} << 31);
    const auto map_alias = [&](simrv::core::CPU& hart_ref, simrv::xlen::Address vaddr) {
        hart_ref.state().priv = kPrivSupervisor;
        hart_ref.state().satp = satp;
        const simrv::xlen::Address vpn = vaddr >> 12;
        const auto tlb_index = simrv::core::CPU::soft_tlb_index(vpn);
        simrv::xlen::Byte* host_page = machine.ram_view().unchecked_ptr(physical_page);
        hart_ref.soft_tlb_read[tlb_index].set(vpn, 0, kPrivSupervisor, hart_ref.soft_tlb_epoch,
                                              physical_page, host_page);
        hart_ref.soft_tlb_write[tlb_index].set(vpn, 0, kPrivSupervisor, hart_ref.soft_tlb_epoch,
                                               physical_page, host_page);
        hart_ref.tlb.insert_data_r(vaddr, physical_page, 0, kPrivSupervisor);
        hart_ref.tlb.insert_data_w(vaddr, physical_page, 0, kPrivSupervisor);
    };

    // 1. Hart 0 executes LR.W
    map_alias(h0, lr_alias);
    map_alias(h0, store_alias);
    h0.pipeline_context = {};
    h0.pipeline_context.opcode = simrv::isa::Opcode::Amo;
    h0.pipeline_context.funct5 = simrv::isa::Funct5Amo::Lr;
    h0.pipeline_context.funct3 = simrv::isa::Funct3::Lw;
    h0.pipeline_context.mem_addr = lr_alias;
    h0.pipeline_context.rrs1 = lr_alias;
    h0.pipeline_context.rd = simrv::xlen::RegId::A1;
    h0.run_memory_stage(machine);
    check(h0.state().load_res == target_addr);
    check(h0.state().lr_reservation_active);
    check(machine.memory().reservation_table().check_reservation(0, target_addr));

    // 2. Hart 1 performs a store to the same address in IA mode
    auto& h1 = machine.hart(1);
    map_alias(h1, store_alias);
    h1.pipeline_context = {};
    h1.pipeline_context.opcode = simrv::isa::Opcode::Store;
    h1.pipeline_context.funct3 = simrv::isa::Funct3::Sw;
    h1.pipeline_context.mem_addr = store_alias;
    h1.pipeline_context.rrs1 = store_alias;
    h1.pipeline_context.rrs2 = 99;
    h1.run_memory_stage(machine);

    // 3. Hart 0's reservation must now be invalidated
    check(!machine.memory().reservation_table().check_reservation(0, target_addr));

    // 4. Hart 0 attempts SC.W, which must fail
    h0.pipeline_context = {};
    h0.pipeline_context.opcode = simrv::isa::Opcode::Amo;
    h0.pipeline_context.funct5 = simrv::isa::Funct5Amo::Sc;
    h0.pipeline_context.funct3 = simrv::isa::Funct3::Sw;
    h0.pipeline_context.mem_addr = lr_alias;
    h0.pipeline_context.rrs1 = lr_alias;
    h0.pipeline_context.rrs2 = 100;
    h0.pipeline_context.rd = simrv::xlen::RegId::A2;
    h0.run_memory_stage(machine);
    check(h0.pipeline_context.wb_data == 1);  // 1 = SC failure

    // Verify memory was not overwritten with 100
    uint32_t val_in_ram = 0;
    std::memcpy(&val_in_ram, &ram[0x200], sizeof(val_in_ram));
    check(val_in_ram == 99);

    // A reservation made through one virtual mapping also succeeds through an alias.
    h0.pipeline_context = {};
    h0.pipeline_context.opcode = simrv::isa::Opcode::Amo;
    h0.pipeline_context.funct5 = simrv::isa::Funct5Amo::Lr;
    h0.pipeline_context.funct3 = simrv::isa::Funct3::Lw;
    h0.pipeline_context.mem_addr = lr_alias;
    h0.pipeline_context.rrs1 = lr_alias;
    h0.run_memory_stage(machine);
    h0.pipeline_context = {};
    h0.pipeline_context.opcode = simrv::isa::Opcode::Amo;
    h0.pipeline_context.funct5 = simrv::isa::Funct5Amo::Sc;
    h0.pipeline_context.funct3 = simrv::isa::Funct3::Sw;
    h0.pipeline_context.mem_addr = store_alias;
    h0.pipeline_context.rrs1 = store_alias;
    h0.pipeline_context.rrs2 = 101;
    h0.run_memory_stage(machine);
    check(h0.pipeline_context.wb_data == 0);
    std::memcpy(&val_in_ram, &ram[0x200], sizeof(val_in_ram));
    check(val_in_ram == 101);

    std::cout << "[PASS] test_ia_multihart_lr_sc_coherence\n";
}

void test_ia_multithreaded_smp_execution() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    simrv::core::Machine machine(simrv::core::MachineConfig{
        .execution = {.appmode = true, .smp_mode = simrv::core::SmpMode::MultiThreaded}});
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
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
    constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
    constexpr std::array<simrv::xlen::Instruction, 10> program = {
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

    // Inspect only after worker quiescence; live architectural PCs are not atomic.
    auto start_time = std::chrono::steady_clock::now();
    bool all_finished = false;
    while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(3)) {
        machine.pause();
        all_finished = true;
        for (size_t i = 1; i < kNumHarts; ++i) {
            if (machine.hart(i).state().pc < pc + 0x20) {
                all_finished = false;
                break;
            }
        }
        if (all_finished) break;
        machine.resume();
        std::this_thread::yield();
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
    simrv::core::Machine machine(
        simrv::core::MachineConfig{.execution = {.appmode = true, .smp_quantum = 100}});
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
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
    constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
    constexpr std::array<simrv::xlen::Instruction, 10> program = {
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
    simrv::xlen::Counter summed_retired = 0;
    for (size_t i = 0; i < kNumHarts; ++i) summed_retired += machine.hart(i).e_icount;
    check(machine.retired_instruction_count() == summed_retired);

    std::cout << "[PASS] test_quantum_smp_baremetal_execution (" << kNumHarts
              << " harts quantum-scheduled AMO sum = " << final_sum << ")\n";
}

void test_smp_instruction_limit_is_machine_wide() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    simrv::core::Machine machine(simrv::core::MachineConfig{
        .execution = {.appmode = true, .num_harts = 2, .smp_quantum = 16, .fincnt = 4}});
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
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

    constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
    constexpr simrv::xlen::Instruction loop = 0x0000006f;  // jal x0, 0
    std::memcpy(ram.data(), &loop, sizeof(loop));
    machine.hart(0).state().pc = pc;
    machine.hart(1).state().pc = pc;

    machine.execute_cycle_for_testing();
    machine.finalize_for_testing();

    check(machine.stop_reason() == simrv::core::Machine::StopReason::InstructionLimit);
    check(machine.retired_instruction_count() >= 4);
    check(machine.hart(0).e_icount > 0 && machine.hart(1).e_icount > 0);
    check(machine.retired_instruction_count() ==
          machine.hart(0).e_icount + machine.hart(1).e_icount);
    std::cout << "[PASS] test_smp_instruction_limit_is_machine_wide\n";
}

void test_load_after_amo_readonly_page() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };
    ConcreteMachine machine;
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.cpu.machine_ = &machine;
    machine.cpu.reset();
    machine.memory().initialize_mmu();
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::InstructionFast;

    constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
    constexpr simrv::xlen::Address ro_page = pc + 0x2000;
    constexpr simrv::xlen::Address root = pc + 0x1000;

    constexpr uint32_t kMagicVal = 0x12345678;
    std::memcpy(ram.data() + (ro_page - pc), &kMagicVal, sizeof(kMagicVal));

    const simrv::xlen::Word root_ppn = root >> 12;
    if constexpr (simrv::xlen::kIsXLen64) {
        machine.cpu.state().satp = (simrv::xlen::Word{8} << 60) | root_ppn;
    } else {
        machine.cpu.state().satp = (simrv::xlen::Word{1} << 31) | root_ppn;
    }
    constexpr unsigned vpn_bits = simrv::xlen::kIsXLen64 ? 9 : 10;
    constexpr unsigned level = simrv::xlen::kIsXLen64 ? 2 : 1;
    const simrv::xlen::Word vpn =
        (ro_page >> (12 + level * vpn_bits)) & ((simrv::xlen::Word{1} << vpn_bits) - 1);
    const simrv::xlen::Address pte_address = root + vpn * sizeof(simrv::xlen::Word);
    const simrv::xlen::Word pte = ((pc >> 12) << 10) | simrv::PteFlag::V | simrv::PteFlag::R |
                                  simrv::PteFlag::A | simrv::PteFlag::D;
    std::memcpy(ram.data() + (pte_address - pc), &pte, sizeof(pte));

    machine.cpu.state().priv = kPrivSupervisor;
    machine.cpu.state().pc = pc;

    // Simulate an earlier AMO instruction leaving Opcode::Amo in pipeline_context
    machine.cpu.pipeline_context.opcode = simrv::isa::Opcode::Amo;
    machine.cpu.pipeline_context.funct5 = simrv::isa::Funct5Amo::Swap;

    // Execute a cached load from the read-only page with soft TLB flushed
    simrv::core::CachedOp load_op{};
    load_op.op_id = simrv::isa::OperationId::LW;
    load_op.ir = 0x00002503;  // lw a0, 0(a1)
    load_op.funct3 = simrv::isa::Funct3::Lw;
    load_op.rd = simrv::xlen::RegId::A0;
    load_op.rs1 = simrv::xlen::RegId::A1;
    load_op.imm = 0;

    machine.cpu.state().regs.write(simrv::xlen::RegId::A1, ro_page);
    machine.cpu.soft_tlb_epoch++;

    machine.cpu.execute_cached_op_fast<false, false>(machine, load_op, machine.ram_view());

    check(!machine.cpu.pipeline_context.pending_exception.has_value());
    check(machine.cpu.state().regs.read(simrv::xlen::RegId::A0) == kMagicVal);
    std::cout << "[PASS] test_load_after_amo_readonly_page\n";
}

void test_virtio_sound_negotiation_and_playback() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };

    ConcreteMachine machine;
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.memory().initialize_mmu();

    simrv::device::VirtioMmioSound sound(0x10006000, 6, &machine);

    // 1. MMIO Discovery
    check(sound.read32(0x00) == 0x74726976);                          // "virt" magic
    check(sound.read32(0x04) == 2);                                   // Version 2
    check(sound.read32(0x08) == simrv::device::virtio::kDevIdSound);  // ID 25
    check(sound.device_status() == 0);

    // 2. Config space
    check(sound.read32(0x100) == 1);  // jacks
    check(sound.read32(0x104) == 1);  // streams
    check(sound.read32(0x108) == 1);  // chmaps

    // 3. Status negotiation
    sound.write32(0x70, 0x01);  // ACKNOWLEDGE
    check(sound.device_status() == 0x01);
    sound.write32(0x70, 0x03);  // DRIVER
    check(sound.device_status() == 0x03);
    sound.write32(0x70, 0x0B);  // FEATURES_OK
    check(sound.device_status() == 0x0B);
    sound.write32(0x70, 0x0F);  // DRIVER_OK
    check(sound.device_status() == 0x0F);

    // 4. Queue 0 (Control Queue) setup
    constexpr simrv::xlen::Address kDesc0Addr = 0x80010000;
    constexpr simrv::xlen::Address kAvail0Addr = 0x80011000;
    constexpr simrv::xlen::Address kUsed0Addr = 0x80012000;
    constexpr simrv::xlen::Address kReqBufAddr = 0x80013000;
    constexpr simrv::xlen::Address kRespBufAddr = 0x80014000;

    sound.write32(0x30, simrv::device::virtio::kVirtioSndVqControl);
    check(sound.read32(0x34) == 64);
    sound.write32(0x38, 16);
    sound.write32(0x80, static_cast<uint32_t>(kDesc0Addr));
    sound.write32(0x84, 0);
    sound.write32(0x90, static_cast<uint32_t>(kAvail0Addr));
    sound.write32(0x94, 0);
    sound.write32(0xA0, static_cast<uint32_t>(kUsed0Addr));
    sound.write32(0xA4, 0);
    sound.write32(0x44, 1);
    check(sound.read32(0x44) == 1);

    auto ram_ptr = [&](simrv::xlen::Address paddr) -> uint8_t* {
        return reinterpret_cast<uint8_t*>(ram.data()) + (paddr - 0x80000000);
    };

    auto submit_control_request = [&](const void* req_data, size_t req_len, void* resp_data,
                                      size_t resp_len) {
        std::memcpy(ram_ptr(kReqBufAddr), req_data, req_len);
        std::memset(ram_ptr(kRespBufAddr), 0, resp_len);

        simrv::device::virtio::VirtqDesc descs[2]{};
        descs[0].addr = kReqBufAddr;
        descs[0].len = static_cast<uint32_t>(req_len);
        descs[0].flags = simrv::device::virtio::kVirtqDescFNext;
        descs[0].next = 1;

        descs[1].addr = kRespBufAddr;
        descs[1].len = static_cast<uint32_t>(resp_len);
        descs[1].flags = simrv::device::virtio::kVirtqDescFWrite;
        descs[1].next = 0;

        std::memcpy(ram_ptr(kDesc0Addr), descs, sizeof(descs));

        uint16_t avail_idx = 0;
        std::memcpy(&avail_idx, ram_ptr(kAvail0Addr + 2), 2);
        uint16_t head = 0;
        std::memcpy(ram_ptr(kAvail0Addr + 4 + (avail_idx % 16) * 2), &head, 2);
        avail_idx++;
        std::memcpy(ram_ptr(kAvail0Addr + 2), &avail_idx, 2);

        sound.write32(0x50, simrv::device::virtio::kVirtioSndVqControl);

        std::memcpy(resp_data, ram_ptr(kRespBufAddr), resp_len);
    };

    // Test Jack Info Query
    simrv::device::virtio::VirtioSndQueryInfo jack_query{
        .hdr = {.code = simrv::device::virtio::kVirtioSndRJackInfo},
        .start_id = 0,
        .count = 1,
        .size = sizeof(simrv::device::virtio::VirtioSndJackInfo),
    };
    simrv::device::virtio::VirtioSndJackInfo jack_info{};
    submit_control_request(&jack_query, sizeof(jack_query), &jack_info, sizeof(jack_info));
    check(jack_info.connected == 1);

    // Test PCM Info Query
    simrv::device::virtio::VirtioSndQueryInfo pcm_query{
        .hdr = {.code = simrv::device::virtio::kVirtioSndRPcmInfo},
        .start_id = 0,
        .count = 1,
        .size = sizeof(simrv::device::virtio::VirtioSndPcmInfo),
    };
    simrv::device::virtio::VirtioSndPcmInfo pcm_info{};
    submit_control_request(&pcm_query, sizeof(pcm_query), &pcm_info, sizeof(pcm_info));
    check(pcm_info.direction == simrv::device::virtio::kVirtioSndDOutput);
    check(pcm_info.channels_max == 2);

    // Test Set Params (48000 Hz, 2ch, S16_LE)
    simrv::device::virtio::VirtioSndPcmSetParams set_params{
        .hdr = {.hdr = {.code = simrv::device::virtio::kVirtioSndRPcmSetParams}, .stream_id = 0},
        .buffer_bytes = 4096,
        .period_bytes = 1024,
        .features = 0,
        .channels = 2,
        .format = simrv::device::virtio::kVirtioSndPcmFmtS16,
        .rate = simrv::device::virtio::kVirtioSndPcmRate48000,
        .padding = 0,
    };
    simrv::device::virtio::VirtioSndHdr set_resp{};
    submit_control_request(&set_params, sizeof(set_params), &set_resp, sizeof(set_resp));
    check(set_resp.code == simrv::device::virtio::kVirtioSndSOk);
    check(sound.backend().sample_rate() == 48000);
    check(sound.backend().channels() == 2);
    check(sound.backend().is_active());

    // 5. Queue 2 (TX Queue) PCM Playback
    constexpr simrv::xlen::Address kDesc2Addr = 0x80020000;
    constexpr simrv::xlen::Address kAvail2Addr = 0x80021000;
    constexpr simrv::xlen::Address kUsed2Addr = 0x80022000;
    constexpr simrv::xlen::Address kPcmBufAddr = 0x80023000;
    constexpr simrv::xlen::Address kPcmStatusAddr = 0x80024000;

    sound.write32(0x30, simrv::device::virtio::kVirtioSndVqTx);
    sound.write32(0x38, 16);
    sound.write32(0x80, static_cast<uint32_t>(kDesc2Addr));
    sound.write32(0x84, 0);
    sound.write32(0x90, static_cast<uint32_t>(kAvail2Addr));
    sound.write32(0x94, 0);
    sound.write32(0xA0, static_cast<uint32_t>(kUsed2Addr));
    sound.write32(0xA4, 0);
    sound.write32(0x44, 1);

    constexpr size_t kSampleBytes = 128;
    std::vector<uint8_t> pcm_packet(sizeof(simrv::device::virtio::VirtioSndPcmXfer) + kSampleBytes,
                                    0x42);
    reinterpret_cast<simrv::device::virtio::VirtioSndPcmXfer*>(pcm_packet.data())->stream_id = 0;
    std::memcpy(ram_ptr(kPcmBufAddr), pcm_packet.data(), pcm_packet.size());

    simrv::device::virtio::VirtqDesc tx_descs[2]{};
    tx_descs[0].addr = kPcmBufAddr;
    tx_descs[0].len = static_cast<uint32_t>(pcm_packet.size());
    tx_descs[0].flags = simrv::device::virtio::kVirtqDescFNext;
    tx_descs[0].next = 1;

    tx_descs[1].addr = kPcmStatusAddr;
    tx_descs[1].len = sizeof(simrv::device::virtio::VirtioSndPcmStatus);
    tx_descs[1].flags = simrv::device::virtio::kVirtqDescFWrite;
    tx_descs[1].next = 0;

    std::memcpy(ram_ptr(kDesc2Addr), tx_descs, sizeof(tx_descs));

    uint16_t avail2_idx = 0;
    uint16_t head2 = 0;
    std::memcpy(ram_ptr(kAvail2Addr + 4), &head2, 2);
    avail2_idx = 1;
    std::memcpy(ram_ptr(kAvail2Addr + 2), &avail2_idx, 2);

    const uint64_t prev_total = simrv::device::virtio::HostAudioSink::instance().total_bytes();
    sound.write32(0x50, simrv::device::virtio::kVirtioSndVqTx);

    check(simrv::device::virtio::HostAudioSink::instance().total_bytes() ==
          prev_total + kSampleBytes);

    simrv::device::virtio::VirtioSndPcmStatus tx_st{};
    std::memcpy(&tx_st, ram_ptr(kPcmStatusAddr), sizeof(tx_st));
    check(tx_st.status == simrv::device::virtio::kVirtioSndSOk);

    // Stop playback
    simrv::device::virtio::VirtioSndPcmHdr stop_cmd{
        .hdr = {.code = simrv::device::virtio::kVirtioSndRPcmStop},
        .stream_id = 0,
    };
    simrv::device::virtio::VirtioSndHdr stop_resp{};
    submit_control_request(&stop_cmd, sizeof(stop_cmd), &stop_resp, sizeof(stop_resp));
    check(stop_resp.code == simrv::device::virtio::kVirtioSndSOk);
    check(!sound.backend().is_active());

    // 6. Test VirtioPciSound Device Discovery
    auto pci_sound = std::make_shared<simrv::device::VirtioPciSound>();
    check(pci_sound->config_read(0x00, 2) == 0x1AF4);
    check(pci_sound->config_read(0x02, 2) == 0x1059);  // Modern Sound PCI ID

    std::cout << "[PASS] test_virtio_sound_negotiation_and_playback\n";
}

void test_axi4_bridge_and_tracer() {
    const auto check = [](bool condition) {
        if (!condition) std::abort();
    };

    ConcreteMachine machine;
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.memory().initialize_mmu();

    check(machine.axi_bridge() != nullptr);

    // 1. Configure AXI bridge
    simrv::pipeline::AxiConfig cfg{
        .enabled = true,
        .data_width_bytes = static_cast<uint32_t>(sizeof(simrv::xlen::Word)),
        .id_width_bits = 4,
        .max_outstanding_reads = 4,
        .max_outstanding_writes = 4,
        .burst_length_max = 16,
        .trace_axi = true,
    };
    machine.axi_bridge()->set_config(cfg);
    check(machine.axi_bridge()->config().enabled);
    check(machine.axi_bridge()->tracer().is_enabled());

    // 2. Pre-seed RAM with test data at 0x80004000
    constexpr simrv::xlen::Address kTestAddr = 0x80004000;
    constexpr uint8_t kBeatSize = sizeof(simrv::xlen::Word) == 8 ? 3 : 2;
    const simrv::xlen::Word val0 = static_cast<simrv::xlen::Word>(0x12345678ULL);
    const simrv::xlen::Word val1 = static_cast<simrv::xlen::Word>(0x9ABCDEF0ULL);
    std::memcpy(ram.data() + (kTestAddr - 0x80000000), &val0, sizeof(simrv::xlen::Word));
    std::memcpy(ram.data() + (kTestAddr + sizeof(simrv::xlen::Word) - 0x80000000), &val1,
                sizeof(simrv::xlen::Word));

    // 3. Test AR read burst (2 beats of sizeof(Word) bytes)
    simrv::memory::Axi4Ar ar{
        .id = 5,
        .addr = kTestAddr,
        .len = 1,  // 2 beats (len = beats - 1)
        .size = kBeatSize,
        .burst = simrv::memory::AxiBurst::Incr,
    };
    check(machine.axi_bridge()->send_ar(ar));

    simrv::memory::Axi4R r0{};
    check(machine.axi_bridge()->recv_r(r0));
    check(r0.id == 5);
    check(r0.resp == simrv::memory::AxiResp::Okay);
    check(r0.data == val0);
    check(!r0.last);

    simrv::memory::Axi4R r1{};
    check(machine.axi_bridge()->recv_r(r1));
    check(r1.id == 5);
    check(r1.resp == simrv::memory::AxiResp::Okay);
    check(r1.data == val1);
    check(r1.last);

    simrv::memory::Axi4R r2{};
    check(!machine.axi_bridge()->recv_r(r2));  // queue drained

    // 4. Test AW and W write burst
    constexpr simrv::xlen::Address kWriteAddr = 0x80005000;
    simrv::memory::Axi4Aw aw{
        .id = 7,
        .addr = kWriteAddr,
        .len = 0,  // 1 beat
        .size = kBeatSize,
        .burst = simrv::memory::AxiBurst::Incr,
    };
    check(machine.axi_bridge()->send_aw(aw));

    const simrv::xlen::Word write_val = static_cast<simrv::xlen::Word>(0xA1B2C3D4ULL);
    simrv::memory::Axi4W w{
        .data = write_val,
        .strb = 0xFF,
        .last = true,
    };
    check(machine.axi_bridge()->send_w(w));

    simrv::memory::Axi4B b{};
    check(machine.axi_bridge()->recv_b(b));
    check(b.id == 7);
    check(b.resp == simrv::memory::AxiResp::Okay);

    simrv::xlen::Word read_back{0};
    std::memcpy(&read_back, ram.data() + (kWriteAddr - 0x80000000), sizeof(simrv::xlen::Word));
    check(read_back == write_val);

    // 5. FIXED bursts repeatedly access the same address.
    constexpr simrv::xlen::Address kFixedAddr = 0x80006000;
    const simrv::xlen::Word fixed_value = static_cast<simrv::xlen::Word>(0x1020304050607080ULL);
    const simrv::xlen::Word ignored_value = static_cast<simrv::xlen::Word>(0xFFEEDDCCBBAA9988ULL);
    std::memcpy(ram.data() + (kFixedAddr - 0x80000000), &fixed_value, sizeof(fixed_value));
    std::memcpy(ram.data() + (kFixedAddr + sizeof(fixed_value) - 0x80000000), &ignored_value,
                sizeof(ignored_value));
    simrv::memory::Axi4Ar fixed_ar{
        .id = 3,
        .addr = kFixedAddr,
        .len = 1,
        .size = kBeatSize,
        .burst = simrv::memory::AxiBurst::Fixed,
    };
    check(machine.axi_bridge()->send_ar(fixed_ar));
    simrv::memory::Axi4R fixed_r{};
    check(machine.axi_bridge()->recv_r(fixed_r) && fixed_r.data == fixed_value && !fixed_r.last);
    check(machine.axi_bridge()->recv_r(fixed_r) && fixed_r.data == fixed_value && fixed_r.last);

    // 6. WRAP bursts wrap at the total burst byte boundary.
    constexpr simrv::xlen::Address kWrapBase = 0x80006100;
    std::array<simrv::xlen::Word, 4> wrap_values{
        static_cast<simrv::xlen::Word>(0x1111111111111111ULL),
        static_cast<simrv::xlen::Word>(0x2222222222222222ULL),
        static_cast<simrv::xlen::Word>(0x3333333333333333ULL),
        static_cast<simrv::xlen::Word>(0x4444444444444444ULL),
    };
    for (std::size_t i = 0; i < wrap_values.size(); ++i) {
        std::memcpy(ram.data() + (kWrapBase + i * sizeof(simrv::xlen::Word) - 0x80000000),
                    &wrap_values[i], sizeof(wrap_values[i]));
    }
    simrv::memory::Axi4Ar wrap_ar{
        .id = 4,
        .addr = kWrapBase + 3 * sizeof(simrv::xlen::Word),
        .len = 3,
        .size = kBeatSize,
        .burst = simrv::memory::AxiBurst::Wrap,
    };
    check(machine.axi_bridge()->send_ar(wrap_ar));
    constexpr std::array<std::size_t, 4> kWrapOrder{3, 0, 1, 2};
    for (std::size_t i = 0; i < kWrapOrder.size(); ++i) {
        simrv::memory::Axi4R wrap_r{};
        check(machine.axi_bridge()->recv_r(wrap_r));
        check(wrap_r.data == wrap_values[kWrapOrder[i]]);
        check(wrap_r.last == (i + 1 == kWrapOrder.size()));
    }

    // 7. WSTRB updates only selected byte lanes; an all-zero strobe writes nothing.
    constexpr simrv::xlen::Address kStrobeAddr = 0x80007000;
    const simrv::xlen::Word original_word = static_cast<simrv::xlen::Word>(0x1122334455667788ULL);
    const simrv::xlen::Word strobe_data = static_cast<simrv::xlen::Word>(0xAABBCCDDEEFF0011ULL);
    std::memcpy(ram.data() + (kStrobeAddr - 0x80000000), &original_word, sizeof(original_word));
    std::array<simrv::xlen::Byte, sizeof(simrv::xlen::Word)> expected_bytes{};
    std::memcpy(expected_bytes.data(), &original_word, sizeof(original_word));
    const auto* strobe_bytes = reinterpret_cast<const simrv::xlen::Byte*>(&strobe_data);
    for (std::size_t lane = 0; lane < expected_bytes.size(); ++lane) {
        if ((0x05U & (1U << lane)) != 0) expected_bytes[lane] = strobe_bytes[lane];
    }
    simrv::memory::Axi4Aw strobe_aw{
        .id = 5,
        .addr = kStrobeAddr,
        .len = 0,
        .size = kBeatSize,
        .burst = simrv::memory::AxiBurst::Incr,
    };
    check(machine.axi_bridge()->send_aw(strobe_aw));
    check(machine.axi_bridge()->send_w({.data = strobe_data, .strb = 0x05, .last = true}));
    check(machine.axi_bridge()->recv_b(b) && b.resp == simrv::memory::AxiResp::Okay);
    check(std::memcmp(ram.data() + (kStrobeAddr - 0x80000000), expected_bytes.data(),
                      expected_bytes.size()) == 0);

    strobe_aw.id = 6;
    check(machine.axi_bridge()->send_aw(strobe_aw));
    check(machine.axi_bridge()->send_w({.data = 0, .strb = 0, .last = true}));
    check(machine.axi_bridge()->recv_b(b) && b.resp == simrv::memory::AxiResp::Okay);
    check(std::memcmp(ram.data() + (kStrobeAddr - 0x80000000), expected_bytes.data(),
                      expected_bytes.size()) == 0);

    // 8. Reject invalid transfer sizes, IDs, reserved bursts, disabled bridges, and overlong
    // bursts.
    simrv::memory::Axi4Ar invalid_ar{
        .id = 7,
        .addr = kFixedAddr,
        .len = 0,
        .size = 0xFF,
        .burst = simrv::memory::AxiBurst::Incr,
    };
    check(!machine.axi_bridge()->send_ar(invalid_ar));
    invalid_ar.size = kBeatSize;
    invalid_ar.id = 16;
    check(!machine.axi_bridge()->send_ar(invalid_ar));
    invalid_ar.id = 7;
    invalid_ar.burst = simrv::memory::AxiBurst::Reserved;
    check(!machine.axi_bridge()->send_ar(invalid_ar));
    invalid_ar.burst = simrv::memory::AxiBurst::Incr;
    invalid_ar.len = 16;  // Configured maximum is 16 beats.
    check(!machine.axi_bridge()->send_ar(invalid_ar));
    invalid_ar.len = 0;
    invalid_ar.size = static_cast<uint8_t>(kBeatSize + 1);
    check(!machine.axi_bridge()->send_ar(invalid_ar));
    invalid_ar.size = kBeatSize;
    cfg.data_width_bytes = 3;  // The bridge accepts power-of-two widths only.
    machine.axi_bridge()->set_config(cfg);
    check(!machine.axi_bridge()->send_ar(invalid_ar));
    cfg.data_width_bytes = static_cast<uint32_t>(sizeof(simrv::xlen::Word));
    cfg.enabled = false;
    machine.axi_bridge()->set_config(cfg);
    check(!machine.axi_bridge()->send_ar(invalid_ar));
    cfg.enabled = true;
    machine.axi_bridge()->set_config(cfg);

    // 9. Test decode error (DECERR) for unmapped address via AXI bridge
    simrv::memory::Axi4Ar mmio_ar{
        .id = 9,
        .addr = 0x10000000,
        .len = 0,
        .size = 2,  // 4 bytes
        .burst = simrv::memory::AxiBurst::Incr,
    };
    check(machine.axi_bridge()->send_ar(mmio_ar));
    simrv::memory::Axi4R mmio_r{};
    check(machine.axi_bridge()->recv_r(mmio_r));
    check(mmio_r.id == 9);
    check(mmio_r.resp == simrv::memory::AxiResp::Decerr);
    check(mmio_r.last);

    std::cout << "[PASS] test_axi4_bridge_and_tracer\n";
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
    test_ca_dma_latency_and_contention();
    test_dma_controller();
    test_virtio_sound_negotiation_and_playback();
    test_axi4_bridge_and_tracer();
    test_tui_uart_input_irq_publication();
    test_ia_tui_uart_input_is_immediate();
    test_os_sampled_fast_batch_eligibility();
    test_timed_interconnect_ordering();
    test_timed_interconnect_cancellation();
    test_timed_page_walk_transitions();
    test_timed_smp_coherence_ordering();
    test_memory_access_fails_closed_when_request_is_rejected();
    test_modified_line_does_not_refill_stale_shared_data();
    test_release_data_writes_back_without_directory_entry();
    test_machine_reset_clears_coherence_state_after_cache_reset();
    test_cross_line_dma_write_invalidates_only_touched_lines();
    test_profiled_shared_cache_clusters();
    test_tilelink_c_acquire_perm_upgrade();
    test_tilelink_c_prefetch_intent();
    test_global_cycle_smp_pipeline_ordering();
    test_global_cycle_smp_amo_retry();
    test_multithreaded_parallel_matches_global_cycle();
    test_global_cycle_timer_phase_ordering();
    test_ca_quantum_smp_batching();
    test_ca_quantum_single_hart_batching();
    test_ca_mt_smp_pause_and_snapshots();
    test_cycle_kernel_golden_forwarding();
    test_tui_mode_sim_thread_multiple_step_sync();
    test_step_sync_timeout_and_shutdown();
    test_cycle_kernel_golden_load_use();
    test_cycle_kernel_golden_fp_dependencies();
    test_cycle_kernel_golden_data_refill();
    test_cycle_kernel_golden_fence_serialization();
    test_cycle_kernel_golden_amo_writeback();
    test_cycle_kernel_golden_multicycle_execute();
    test_cycle_kernel_golden_branch_recovery();
    test_cycle_kernel_golden_instruction_refill();
    test_cycle_kernel_golden_translated_fetch();
    test_cycle_kernel_golden_translated_load();
    test_cycle_kernel_golden_precise_trap();
    test_cycle_kernel_golden_interrupt_boundary();
    test_cycle_policy_architectural_equivalence();
    test_dynamic_mode_switching_cache_behavior();
    test_instruction_policy_architectural_equivalence();
    test_aclint();
    test_aia_aplic_and_imsic();
    test_pcie_and_virtio_pci();
    test_virtio_block_overlay_preserves_base_image();
    test_virtio_mmio_v2();
    test_sbi_multihart_rfence();
    test_ia_multihart_lr_sc_coherence();
    test_ia_multithreaded_smp_execution();
    test_quantum_smp_baremetal_execution();
    test_smp_instruction_limit_is_machine_wide();
    test_load_after_amo_readonly_page();
    std::cout << "All Modern Platform tests PASSED!\n";
    return 0;
}
