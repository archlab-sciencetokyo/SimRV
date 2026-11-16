/**
 * @file Rva23ProfileTests.cpp
 * @brief Regression tests for RVA23 Profile: Zicond, CMO (Zicboz/Zicbom/Zicbop), Svinval, and
 * profile engine.
 */
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

#include "TestHelper.hpp"
#include "simrv/cache/DCache.hpp"
#include "simrv/cache/ICache.hpp"
#include "simrv/cache/L2Cache.hpp"
#include "simrv/cache/L3Cache.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/CsrFile.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/device/Aclint.hpp"
#include "simrv/execute/ExecuteUnit.hpp"
#include "simrv/isa/Common.hpp"
#include "simrv/isa/FpConversions.hpp"
#include "simrv/isa/IsaConfig.hpp"
#include "simrv/isa/IsaTypes.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/isa/Zk.hpp"
#include "simrv/memory/Bus.hpp"
#include "simrv/memory/CoherenceHub.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/ReservationTable.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "simrv/pipeline/RetirementEffects.hpp"
#include "simrv/xlen/AddressTypes.hpp"
#include "simrv/xlen/RegisterTypes.hpp"

namespace {

#define TEST_ASSERT(cond) TEST_CHECK(cond)

void test_zicond_semantics() {
    using namespace simrv::isa;
    using namespace simrv::execute;

    // 1. czero.eqz rd, rs1, rs2: rd = (rs2 == 0) ? 0 : rs1
    // rs2 == 0 -> rd = 0
    TEST_ASSERT(ExecuteUnit::aluInt(42, 0, OperationId::CZERO_EQZ, simrv::xlen::kXLenBits) == 0);
    // rs2 != 0 -> rd = rs1
    TEST_ASSERT(ExecuteUnit::aluInt(42, 1, OperationId::CZERO_EQZ, simrv::xlen::kXLenBits) == 42);
    TEST_ASSERT(ExecuteUnit::aluInt(0x12345678, 0xABCDEF, OperationId::CZERO_EQZ,
                                    simrv::xlen::kXLenBits) == 0x12345678);

    // 2. czero.nez rd, rs1, rs2: rd = (rs2 != 0) ? 0 : rs1
    // rs2 == 0 -> rd = rs1
    TEST_ASSERT(ExecuteUnit::aluInt(42, 0, OperationId::CZERO_NEZ, simrv::xlen::kXLenBits) == 42);
    // rs2 != 0 -> rd = 0
    TEST_ASSERT(ExecuteUnit::aluInt(42, 1, OperationId::CZERO_NEZ, simrv::xlen::kXLenBits) == 0);
    TEST_ASSERT(ExecuteUnit::aluInt(0x12345678, 0xABCDEF, OperationId::CZERO_NEZ,
                                    simrv::xlen::kXLenBits) == 0);

    // 3. Decoder verification
    // czero.eqz x3, x1, x2 -> funct7=0x07, rs2=2, rs1=1, funct3=5, rd=3, opcode=0x33
    const uint32_t czero_eqz_insn =
        (0x07U << 25U) | (2U << 20U) | (1U << 15U) | (5U << 12U) | (3U << 7U) | 0x33U;
    TEST_ASSERT(simrv::pipeline::decoder(czero_eqz_insn) == OperationId::CZERO_EQZ);

    // czero.nez x3, x1, x2 -> funct7=0x07, rs2=2, rs1=1, funct3=7, rd=3, opcode=0x33
    const uint32_t czero_nez_insn =
        (0x07U << 25U) | (2U << 20U) | (1U << 15U) | (7U << 12U) | (3U << 7U) | 0x33U;
    TEST_ASSERT(simrv::pipeline::decoder(czero_nez_insn) == OperationId::CZERO_NEZ);
}

void test_zic64b_cache_geometry() {
    TEST_ASSERT(simrv::cache::ICache::kLineBytes == 64);
    TEST_ASSERT(simrv::cache::DCache::kLineBytes == 64);
    TEST_ASSERT(simrv::cache::L2Cache::kLineBytes == 64);
    TEST_ASSERT(simrv::cache::L3Cache::kLineBytes == 64);
    TEST_ASSERT(simrv::cache::ICache::kLineShift == 6);
    TEST_ASSERT(simrv::cache::DCache::kLineShift == 6);
}

void test_zicclsm_unaligned_main_memory() {
    if constexpr (simrv::xlen::kIsXLen64) {
        simrv::core::Machine machine;
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0xA5});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.memory().initialize_mmu();
        auto& cpu = machine.primary_hart();
        cpu.reset();

        constexpr simrv::xlen::Address kBase = simrv::memory::kDramBaseAddress.raw();
        constexpr simrv::xlen::Register kValue = 0x0123456789ABCDEFULL;
        const auto check_unaligned = [&](simrv::xlen::Address offset) {
            cpu.active_context().pending_exception.reset();
            cpu.active_context().opcode = simrv::isa::Opcode::Store;
            simrv::memory::MemoryAccess::target_write(machine.memory(), cpu,
                                                      simrv::xlen::VirtAddr{kBase + offset}, kValue,
                                                      simrv::isa::Funct3::Sd);
            TEST_ASSERT(!cpu.active_context().pending_exception.has_value());

            cpu.active_context().pending_exception.reset();
            cpu.active_context().opcode = simrv::isa::Opcode::Load;
            const auto loaded = simrv::memory::MemoryAccess::target_read(
                machine.memory(), cpu, simrv::xlen::VirtAddr{kBase + offset},
                simrv::isa::Funct3::Ld);
            TEST_ASSERT(!cpu.active_context().pending_exception.has_value());
            TEST_ASSERT(loaded == kValue);
        };

        check_unaligned(0x1003);  // Misaligned within a cache block.
        check_unaligned(0x103D);  // Crosses a 64-byte cache-block boundary.
        check_unaligned(0x1FFD);  // Crosses a 4 KiB page boundary in main memory.
    }
}

void test_za64rs_reservation_set() {
    using simrv::memory::ReservationTable;
    static_assert(ReservationTable::kReservationGranule == 8);

    ReservationTable table;
    constexpr simrv::xlen::Address kAddress = 0x8000103FULL;
    table.set_reservation(simrv::xlen::HartId{0}, simrv::xlen::PhysAddr{kAddress});
    TEST_ASSERT(
        table.check_reservation(simrv::xlen::HartId{0}, simrv::xlen::PhysAddr{0x80001038ULL}));
    TEST_ASSERT(
        !table.check_reservation(simrv::xlen::HartId{0}, simrv::xlen::PhysAddr{0x80001030ULL}));
    TEST_ASSERT(
        table.check_reservation(simrv::xlen::HartId{0}, simrv::xlen::PhysAddr{0x8000103FULL}));
    TEST_ASSERT(
        !table.check_reservation(simrv::xlen::HartId{0}, simrv::xlen::PhysAddr{0x80001040ULL}));

    table.invalidate_matching(simrv::xlen::PhysAddr{0x80001038ULL}, simrv::xlen::HartId{1});
    TEST_ASSERT(!table.check_reservation(simrv::xlen::HartId{0}, simrv::xlen::PhysAddr{kAddress}));
}

void test_ziccrse_eventual_lr_sc_success() {
    simrv::core::Machine machine;
    auto secondary = std::make_unique<simrv::core::CPU>();
    secondary->reset();
    secondary->state().mhartid = 1;
    machine.add_hart_for_testing(std::move(secondary));
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.memory().initialize_mmu();
    constexpr simrv::xlen::Address kAddress = (simrv::memory::kDramBaseAddress + 0x200).raw();

    const auto lr = [&](simrv::core::CPU& cpu) {
        auto& ctx = cpu.active_context();
        ctx = {};
        ctx.opcode = simrv::isa::Opcode::Amo;
        ctx.funct5 = simrv::isa::Funct5Amo::Lr;
        ctx.funct3 = simrv::isa::Funct3::Sw;
        ctx.mem_addr = simrv::xlen::VirtAddr{kAddress};
        ctx.rrs1 = kAddress;
        cpu.run_memory_stage(machine);
        TEST_ASSERT(!ctx.pending_exception.has_value());
        TEST_ASSERT(cpu.state().lr_reservation_active);
    };
    const auto sc = [&](simrv::core::CPU& cpu, uint32_t value) {
        auto& ctx = cpu.active_context();
        ctx = {};
        ctx.opcode = simrv::isa::Opcode::Amo;
        ctx.funct5 = simrv::isa::Funct5Amo::Sc;
        ctx.funct3 = simrv::isa::Funct3::Sw;
        ctx.mem_addr = simrv::xlen::VirtAddr{kAddress};
        ctx.rrs1 = kAddress;
        ctx.rrs2 = value;
        cpu.run_memory_stage(machine);
        TEST_ASSERT(!ctx.pending_exception.has_value());
        return ctx.wb_data;
    };

    auto& hart0 = machine.primary_hart();
    auto& hart1 = machine.hart(1);
    hart0.state().priv = simrv::xlen::PrivilegeLevel::Machine;
    hart1.state().priv = simrv::xlen::PrivilegeLevel::Machine;

    // Both harts reserve the same word. With no unconditional writer, one SC makes progress
    // and invalidates the competing reservation; the losing hart can retry from a fresh LR.
    lr(hart0);
    lr(hart1);
    TEST_ASSERT(sc(hart0, 1) == 0);
    TEST_ASSERT(sc(hart1, 2) != 0);

    lr(hart0);
    lr(hart1);
    TEST_ASSERT(sc(hart1, 3) == 0);
    TEST_ASSERT(sc(hart0, 4) != 0);

    uint32_t final_value = 0;
    std::memcpy(&final_value, ram.data() + 0x200, sizeof(final_value));
    TEST_ASSERT(final_value == 3);

    // Run a real constrained loop (LR, ADDI, SC, BNE) on two interleaved harts. Neither loop
    // performs an unconditional write, so both must eventually leave the retry branch.
    constexpr simrv::xlen::Address kCode = (simrv::memory::kDramBaseAddress + 0x100).raw();
    const auto amo = [](uint32_t funct5, uint32_t rs2, uint32_t rd) {
        return (funct5 << 27U) | (rs2 << 20U) | (10U << 15U) | (2U << 12U) | (rd << 7U) | 0x2FU;
    };
    constexpr uint32_t kAddiT0T0One = 0x00128293U;
    constexpr uint32_t kBneT1ZeroMinus12 = 0xFE031AE3U;  // bne t1, zero, -12
    const std::array<uint32_t, 4> loop = {amo(2, 0, 5), kAddiT0T0One, amo(3, 5, 6),
                                          kBneT1ZeroMinus12};
    std::memcpy(ram.data() + 0x100, loop.data(), sizeof(loop));
    constexpr uint32_t kLoopInitial = 0;
    std::memcpy(ram.data() + 0x204, &kLoopInitial, sizeof(kLoopInitial));
    hart0.state().pc = simrv::xlen::VirtAddr{kCode};
    hart1.state().pc = simrv::xlen::VirtAddr{kCode};
    hart0.state().regs.write(simrv::xlen::RegId::A0, kAddress + 4);
    hart1.state().regs.write(simrv::xlen::RegId::A0, kAddress + 4);
    const auto done = [](const simrv::core::CPU& cpu) { return cpu.state().pc == kCode + 16; };
    for (unsigned steps = 0; steps < 32 && (!done(hart0) || !done(hart1)); ++steps) {
        if (!done(hart0)) hart0.run_cycle_baremetal(machine);
        if (!done(hart1)) hart1.run_cycle_baremetal(machine);
    }
    TEST_ASSERT(done(hart0));
    TEST_ASSERT(done(hart1));
    uint32_t loop_value = 0;
    std::memcpy(&loop_value, ram.data() + 0x204, sizeof(loop_value));
    TEST_ASSERT(loop_value == 2);
}

void test_ziccif_atomic_instruction_fetch_under_smp_store() {
    simrv::core::Machine machine;
    machine.set_smp_parallel_for_testing(true);
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::InstructionFast;
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.memory().initialize_mmu();
    auto secondary = std::make_unique<simrv::core::CPU>();
    secondary->reset();
    secondary->state().mhartid = 1;
    machine.add_hart_for_testing(std::move(secondary));

    constexpr simrv::xlen::Address kTarget = (simrv::memory::kDramBaseAddress + 0x100).raw();
    constexpr simrv::xlen::Address kWriter = (simrv::memory::kDramBaseAddress + 0x300).raw();
    constexpr uint32_t kInstructionA = 0x00100193U;  // addi gp, zero, 1
    constexpr uint32_t kInstructionB = 0x00200193U;  // addi gp, zero, 2
    constexpr std::array<uint32_t, 6> kWriterProgram = {
        0x001002B7U,  // lui t0, 0x00100
        0x19328293U,  // addi t0, t0, 0x193
        0x00300337U,  // lui t1, 0x00300
        0x00552023U,  // sw t0, 0(a0)
        0x0062C2B3U,  // xor t0, t0, t1
        0xFF9FF06FU,  // jal zero, -8
    };
    std::memcpy(ram.data() + 0x100, &kInstructionA, sizeof(kInstructionA));
    std::memcpy(ram.data() + 0x300, kWriterProgram.data(), sizeof(kWriterProgram));

    auto& writer = machine.hart(1);
    writer.state().priv = simrv::xlen::PrivilegeLevel::Machine;
    writer.state().pc = simrv::xlen::VirtAddr{kWriter};
    writer.state().regs.write(simrv::xlen::RegId::A0, kTarget);

    auto& fetcher = machine.primary_hart();
    fetcher.state().priv = simrv::xlen::PrivilegeLevel::Machine;
    std::atomic<bool> writer_done{false};
    std::jthread writer_thread([&] {
        for (unsigned i = 0; i < 20'000; ++i) {
            writer.run_cycle_baremetal(machine);
            if ((i & 63U) == 0) std::this_thread::yield();
        }
        writer_done.store(true, std::memory_order_release);
    });

    bool saw_a = false;
    bool saw_b = false;
    for (unsigned i = 0;
         i < 500'000 && (!writer_done.load(std::memory_order_acquire) || !saw_b || !saw_a); ++i) {
        TEST_ASSERT(fetcher.fetch_stage(machine, simrv::xlen::VirtAddr{kTarget}));
        const auto fetched = fetcher.active_context().ir_org;
        TEST_ASSERT(fetched == kInstructionA || fetched == kInstructionB);
        saw_a |= fetched == kInstructionA;
        saw_b |= fetched == kInstructionB;
        if ((i & 63U) == 0) std::this_thread::yield();
    }
    writer_thread.join();
    TEST_ASSERT(saw_a);
    TEST_ASSERT(saw_b);
}

void test_ziccamoa_main_memory_atomics() {
    using simrv::isa::Funct3;
    using simrv::isa::Funct5Amo;
    using simrv::isa::Opcode;

    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    constexpr simrv::xlen::Address kBase = simrv::memory::kDramBaseAddress.raw();
    constexpr std::array<Funct5Amo, 9> kOps = {Funct5Amo::Swap, Funct5Amo::Add,  Funct5Amo::Xor,
                                               Funct5Amo::And,  Funct5Amo::Or,   Funct5Amo::Min,
                                               Funct5Amo::Max,  Funct5Amo::Minu, Funct5Amo::Maxu};

    const auto exercise = [&](uint32_t width_bytes, Funct3 funct3, uint64_t initial,
                              uint64_t operand, const std::array<uint64_t, 9>& expected) {
        simrv::core::Machine machine;
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.memory().initialize_mmu();
        auto& cpu = machine.primary_hart();
        cpu.reset();

        for (size_t i = 0; i < kOps.size(); ++i) {
            const auto address = kBase + 0x200 + (i * 16);
            std::memcpy(ram.data() + 0x200 + (i * 16), &initial, width_bytes);
            auto& ctx = cpu.active_context();
            ctx = {};
            ctx.opcode = Opcode::Amo;
            ctx.funct5 = kOps[i];
            ctx.funct3 = funct3;
            ctx.mem_addr = simrv::xlen::VirtAddr{static_cast<simrv::xlen::Word>(address)};
            ctx.rrs1 = address;
            ctx.rrs2 = operand;
            cpu.run_memory_stage(machine);
            TEST_ASSERT(!ctx.pending_exception.has_value());

            ctx.opcode = Opcode::Load;
            const auto observed = simrv::memory::MemoryAccess::target_read(
                machine.memory(), cpu,
                simrv::xlen::VirtAddr{static_cast<simrv::xlen::Register>(address)}, funct3);
            TEST_ASSERT(!ctx.pending_exception.has_value());
            const uint64_t width_mask = width_bytes == 4 ? UINT64_C(0xFFFFFFFF) : UINT64_MAX;
            TEST_ASSERT((observed & width_mask) == (expected[i] & width_mask));
        }
    };

    constexpr std::array<uint64_t, 9> kExpectedW = {
        3, 0x80000004ULL, 0x80000002ULL, 1, 0x80000003ULL, 0x80000001ULL, 3, 3, 0x80000001ULL};
    exercise(4, Funct3::Sw, 0x80000001ULL, 3, kExpectedW);

    if constexpr (simrv::xlen::kIsXLen64) {
        constexpr std::array<uint64_t, 9> kExpectedD = {3,
                                                        0x8000000000000004ULL,
                                                        0x8000000000000002ULL,
                                                        1,
                                                        0x8000000000000003ULL,
                                                        0x8000000000000001ULL,
                                                        3,
                                                        3,
                                                        0x8000000000000001ULL};
        exercise(8, Funct3::Sd, 0x8000000000000001ULL, 3, kExpectedD);
    }
}

void test_zfa_scalar_instructions() {
    using namespace simrv::isa;
    const auto encode = [](uint32_t funct7, uint32_t rs2, uint32_t rs1, uint32_t rm, uint32_t rd) {
        return (funct7 << 25U) | (rs2 << 20U) | (rs1 << 15U) | (rm << 12U) | (rd << 7U) | 0x53U;
    };
    TEST_ASSERT(simrv::pipeline::decoder(encode(0x78, 1, 16, 0, 1)) == OperationId::FLI_S);
    TEST_ASSERT(simrv::pipeline::decoder(encode(0x79, 1, 1, 0, 1)) == OperationId::FLI_D);
    TEST_ASSERT(simrv::pipeline::decoder(encode(0x14, 0, 2, 2, 1)) == OperationId::FMINM_S);
    TEST_ASSERT(simrv::pipeline::decoder(encode(0x50, 0, 2, 4, 1)) == OperationId::FLEQ_S);
    TEST_ASSERT(simrv::pipeline::decoder(encode(0x20, 4, 2, 0, 1)) == OperationId::FROUND_S);
    TEST_ASSERT(simrv::pipeline::decoder(encode(0x20, 5, 2, 0, 1)) == OperationId::FROUNDNX_S);
    TEST_ASSERT(simrv::pipeline::decoder(encode(0x15, 0, 2, 3, 1)) == OperationId::FMAXM_D);
    TEST_ASSERT(simrv::pipeline::decoder(encode(0x51, 0, 2, 5, 1)) == OperationId::FLTQ_D);
    TEST_ASSERT(simrv::pipeline::decoder(encode(0x21, 5, 2, 0, 1)) == OperationId::FROUNDNX_D);
    TEST_ASSERT(simrv::pipeline::decoder(encode(0x61, 8, 2, 1, 1)) == OperationId::FCVTMOD_W_D);
    TEST_ASSERT(required_misa_extensions(OperationId::FLI_S) ==
                misa_extension_bit(IsaExtension::F));
    TEST_ASSERT(required_misa_extensions(OperationId::FCVTMOD_W_D) ==
                (misa_extension_bit(IsaExtension::F) | misa_extension_bit(IsaExtension::D)));
    TEST_ASSERT(is_f_op(OperationId::FROUNDNX_S));
    TEST_ASSERT(is_d_op(OperationId::FCVTMOD_W_D));

    std::array<simrv::xlen::FloatingRegister, 32> fp{};
    simrv::xlen::CsrValue fcsr = 0;
    const auto execute = [&](uint32_t funct7, uint32_t rs2, uint32_t rs1, uint32_t rm,
                             uint32_t rs2_reg = 0) {
        const uint32_t encoded_rs2 =
            (funct7 == 0x78 || funct7 == 0x79 || funct7 == 0x20 || funct7 == 0x21 || funct7 == 0x61)
                ? rs2
                : rs2_reg;
        return simrv::execute::ExecuteUnit::opFp(
            funct7, static_cast<Funct3>(rm), simrv::xlen::to_fp_reg_id(rs1),
            simrv::xlen::to_fp_reg_id(encoded_rs2), 0, fp.data(), fcsr);
    };
    auto result = execute(0x78, 1, 16, 0);
    TEST_ASSERT(result.fp_wb_enable &&
                result.fp_wb_data ==
                    (static_cast<uint64_t>(simrv::xlen::kF32BoxerBits) | UINT32_C(0x3F800000)));
    TEST_ASSERT(fcsr == 0);  // FLI never raises floating-point flags.
    result = execute(0x78, 1, 1, 0);
    TEST_ASSERT((result.fp_wb_data & UINT64_C(0xFFFFFFFF)) == UINT32_C(0x00800000));
    result = execute(0x79, 1, 1, 0);
    TEST_ASSERT(result.fp_wb_data == UINT64_C(0x0010000000000000));

    fp[2] = UINT64_C(0x7FF8000000001234);
    fp[3] = std::bit_cast<uint64_t>(1.0);
    result = execute(0x15, 0, 2, 3, 3);
    TEST_ASSERT(result.fp_wb_enable && result.fp_wb_data == simrv::xlen::kF64Qnan);
    fcsr = 0;
    result = execute(0x51, 0, 2, 5, 3);
    TEST_ASSERT(result.int_wb_enable && result.int_wb_data == 0 && (fcsr & FflagsBit::Nv) == 0);

    fp[2] = static_cast<uint64_t>(simrv::xlen::kF32BoxerBits) | UINT32_C(0x7FC12345);
    fp[3] = static_cast<uint64_t>(simrv::xlen::kF32BoxerBits) | UINT32_C(0x3F800000);
    fcsr = 0;
    result = execute(0x14, 0, 2, 2, 3);
    TEST_ASSERT(result.fp_wb_enable &&
                result.fp_wb_data ==
                    (static_cast<uint64_t>(simrv::xlen::kF32BoxerBits) | UINT32_C(0x7FC00000)));
    TEST_ASSERT((fcsr & FflagsBit::Nv) == 0);
    fp[2] = static_cast<uint64_t>(simrv::xlen::kF32BoxerBits) | UINT32_C(0x3F000000);
    result = execute(0x14, 0, 2, 2, 3);
    TEST_ASSERT((result.fp_wb_data & UINT64_C(0xFFFFFFFF)) == UINT32_C(0x3F000000));

    fp[2] = static_cast<uint64_t>(simrv::xlen::kF32BoxerBits) | UINT32_C(0x7FC12345);
    fcsr = 0;
    result = execute(0x50, 0, 2, 4, 3);
    TEST_ASSERT(result.int_wb_enable && result.int_wb_data == 0);
    TEST_ASSERT((fcsr & FflagsBit::Nv) == 0);

    fp[2] = static_cast<uint64_t>(simrv::xlen::kF32BoxerBits) | UINT32_C(0x40200000);  // 2.5
    result = execute(0x20, 4, 2, 4);
    TEST_ASSERT(result.fp_wb_enable &&
                (result.fp_wb_data & UINT64_C(0xFFFFFFFF)) == UINT32_C(0x40400000));
    TEST_ASSERT((fcsr & (FflagsBit::Nv | FflagsBit::Nx)) == 0);
    fp[2] = static_cast<uint64_t>(simrv::xlen::kF32BoxerBits) | UINT32_C(0x40200000);  // 2.5
    fcsr = 0;
    result = execute(0x20, 5, 2, 0);
    TEST_ASSERT((result.fp_wb_data & UINT64_C(0xFFFFFFFF)) == UINT32_C(0x40000000));
    TEST_ASSERT((fcsr & FflagsBit::Nx) != 0);
    fp[2] = std::bit_cast<uint64_t>(1.1);
    fcsr = 0;
    result = execute(0x21, 5, 2, 0);
    TEST_ASSERT(result.fp_wb_enable && result.fp_wb_data == std::bit_cast<uint64_t>(1.0));
    TEST_ASSERT((fcsr & FflagsBit::Nx) != 0);
    fp[2] = std::bit_cast<uint64_t>(4.75);
    fcsr = 0;
    result = execute(0x61, 8, 2, 1);
    TEST_ASSERT(result.int_wb_enable && result.int_wb_data == 4);
    TEST_ASSERT((fcsr & FflagsBit::Nx) != 0);
    fp[2] = std::bit_cast<uint64_t>(4294967297.75);
    fcsr = 0;
    result = execute(0x61, 8, 2, 1);
    TEST_ASSERT(result.int_wb_enable && result.int_wb_data == 1);
    TEST_ASSERT((fcsr & FflagsBit::Nv) != 0);
}

void test_zfhmin_scalar_instructions() {
    using namespace simrv::isa;
    const auto encode_fp = [](uint32_t funct7, uint32_t rs2, uint32_t rs1, uint32_t rm,
                              uint32_t rd) {
        return (funct7 << 25U) | (rs2 << 20U) | (rs1 << 15U) | (rm << 12U) | (rd << 7U) | 0x53U;
    };
    TEST_ASSERT(simrv::pipeline::decoder((1U << 12U) | (1U << 7U) | 0x07U) == OperationId::FLH);
    TEST_ASSERT(simrv::pipeline::decoder((2U << 20U) | (1U << 12U) | 0x27U) == OperationId::FSH);
    TEST_ASSERT(simrv::pipeline::decoder(encode_fp(0x72, 0, 2, 0, 1)) == OperationId::FMV_X_H);
    TEST_ASSERT(simrv::pipeline::decoder(encode_fp(0x7A, 0, 2, 0, 1)) == OperationId::FMV_H_X);
    TEST_ASSERT(simrv::pipeline::decoder(encode_fp(0x20, 2, 2, 0, 1)) == OperationId::FCVT_S_H);
    TEST_ASSERT(simrv::pipeline::decoder(encode_fp(0x22, 0, 2, 0, 1)) == OperationId::FCVT_H_S);
    TEST_ASSERT(simrv::pipeline::decoder(encode_fp(0x21, 2, 2, 0, 1)) == OperationId::FCVT_D_H);
    TEST_ASSERT(simrv::pipeline::decoder(encode_fp(0x22, 1, 2, 0, 1)) == OperationId::FCVT_H_D);
    TEST_ASSERT(simrv::pipeline::decoder(encode_fp(0x02, 0, 2, 0, 1)) == OperationId::FADD_H);
    {
        ExtensionSet zfhmin_only{};
        zfhmin_only.set(MultiLetterExtension::Zfhmin);
        TEST_ASSERT(!instruction_enabled(kMisaDefault | misa_extension_bit(IsaExtension::F),
                                         zfhmin_only, OperationId::FADD_H));
    }

    std::array<simrv::xlen::FloatingRegister, 32> fp{};
    simrv::xlen::CsrValue fcsr = 0;
    const auto execute = [&](uint32_t funct7, uint32_t rm, uint32_t rs1, uint32_t rs2,
                             simrv::xlen::Register int_source = 0) {
        return simrv::execute::ExecuteUnit::opFp(
            funct7, static_cast<Funct3>(rm), simrv::xlen::to_fp_reg_id(rs1),
            simrv::xlen::to_fp_reg_id(rs2), int_source, fp.data(), fcsr);
    };
    fp[2] = ~simrv::xlen::FloatingRegister{0xFFFF} | UINT16_C(0x3C00);  // Half 1.0
    auto result = execute(0x20, 0, 2, 2);
    TEST_ASSERT(result.fp_wb_enable &&
                result.fp_wb_data ==
                    (static_cast<uint64_t>(simrv::xlen::kF32BoxerBits) | UINT32_C(0x3F800000)));

    fp[2] = static_cast<uint64_t>(simrv::xlen::kF32BoxerBits) | UINT32_C(0x3FC00000);  // 1.5
    result = execute(0x22, 0, 2, 0);
    TEST_ASSERT(result.fp_wb_enable && (result.fp_wb_data & UINT64_C(0xFFFF)) == UINT16_C(0x3E00));

    fp[2] = static_cast<uint64_t>(simrv::xlen::kF32BoxerBits) | UINT32_C(0x3F801000);
    fcsr = 0;
    result = execute(0x22, 3, 2, 0);  // RUP
    TEST_ASSERT((result.fp_wb_data & UINT64_C(0xFFFF)) == UINT16_C(0x3C01));
    TEST_ASSERT((fcsr & simrv::isa::FflagsBit::Nx) != 0);

    const double half_midpoint = 1.00048828125;
    fp[2] = std::bit_cast<simrv::xlen::FloatingRegister>(std::nextafter(half_midpoint, INFINITY));
    fcsr = 0;
    result = execute(0x22, 0, 2, 1);
    TEST_ASSERT(result.fp_wb_enable && (result.fp_wb_data & UINT64_C(0xFFFF)) == UINT16_C(0x3C01));
    TEST_ASSERT((fcsr & simrv::isa::FflagsBit::Nx) != 0);
    fp[2] = ~simrv::xlen::FloatingRegister{0xFFFF} | UINT16_C(0x3E00);
    result = execute(0x21, 0, 2, 2);
    TEST_ASSERT(result.fp_wb_enable &&
                result.fp_wb_data == std::bit_cast<simrv::xlen::FloatingRegister>(1.5));

    result = execute(0x7A, 0, 0, 0, UINT64_C(0x1234FFFE));
    TEST_ASSERT(result.fp_wb_enable &&
                result.fp_wb_data == (~simrv::xlen::FloatingRegister{0xFFFF} | UINT16_C(0xFFFE)));
    fp[2] = ~simrv::xlen::FloatingRegister{0xFFFF} | UINT16_C(0xFFFE);
    result = execute(0x72, 0, 2, 0);
    TEST_ASSERT(result.int_wb_enable &&
                result.int_wb_data == static_cast<simrv::xlen::Register>(-2));

    simrv::core::Machine machine;
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.memory().initialize_mmu();
    auto& cpu = machine.primary_hart();
    cpu.reset();
    const auto address = simrv::xlen::VirtAddr{(simrv::memory::kDramBaseAddress + 0x200).raw()};
    simrv::memory::MemoryAccess::storeFp(machine.memory(), cpu, address, fp[2], Funct3::Fsh);
    TEST_ASSERT(!cpu.active_context().pending_exception.has_value());
    const auto loaded =
        simrv::memory::MemoryAccess::loadFp(machine.memory(), cpu, address, Funct3::Flh);
    TEST_ASSERT(!cpu.active_context().pending_exception.has_value());
    TEST_ASSERT(loaded == fp[2]);
}

void test_binary16_narrowing_rounding_and_flags() {
    using simrv::isa::RoundingMode;
    using simrv::isa::fp::to_binary16;
    constexpr float kMidpoint = std::bit_cast<float>(UINT32_C(0x3F801000));

    auto converted = to_binary16(kMidpoint, RoundingMode::Rne);
    TEST_ASSERT(converted.bits == UINT16_C(0x3C00));
    TEST_ASSERT(converted.flags == static_cast<simrv::xlen::CsrValue>(simrv::isa::FflagsBit::Nx));
    converted = to_binary16(kMidpoint, RoundingMode::Rmm);
    TEST_ASSERT(converted.bits == UINT16_C(0x3C01));
    converted = to_binary16(kMidpoint, RoundingMode::Rup);
    TEST_ASSERT(converted.bits == UINT16_C(0x3C01));
    converted = to_binary16(kMidpoint, RoundingMode::Rdn);
    TEST_ASSERT(converted.bits == UINT16_C(0x3C00));
    converted = to_binary16(-kMidpoint, RoundingMode::Rdn);
    TEST_ASSERT(converted.bits == UINT16_C(0xBC01));
    converted = to_binary16(-kMidpoint, RoundingMode::Rup);
    TEST_ASSERT(converted.bits == UINT16_C(0xBC00));

    converted = to_binary16(std::ldexp(1.0F, -25), RoundingMode::Rne);
    TEST_ASSERT(converted.bits == 0);
    TEST_ASSERT((converted.flags & (simrv::isa::FflagsBit::Uf | simrv::isa::FflagsBit::Nx)) ==
                (simrv::isa::FflagsBit::Uf | simrv::isa::FflagsBit::Nx));
    converted = to_binary16(std::ldexp(1.0F, -25), RoundingMode::Rmm);
    TEST_ASSERT(converted.bits == 1);

    converted = to_binary16(65520.0F, RoundingMode::Rne);
    TEST_ASSERT(converted.bits == UINT16_C(0x7C00));
    TEST_ASSERT((converted.flags & (simrv::isa::FflagsBit::Of | simrv::isa::FflagsBit::Nx)) ==
                (simrv::isa::FflagsBit::Of | simrv::isa::FflagsBit::Nx));
    converted = to_binary16(65520.0F, RoundingMode::Rtz);
    TEST_ASSERT(converted.bits == UINT16_C(0x7BFF));

    const double half_midpoint = 1.00048828125;
    const double next_double = std::nextafter(half_midpoint, INFINITY);
    converted = to_binary16(next_double, RoundingMode::Rne);
    TEST_ASSERT(converted.bits == UINT16_C(0x3C01));
}

void test_scalar_rmm_rounding() {
    using namespace simrv;
    std::array<xlen::FloatingRegister, 32> fp{};
    xlen::CsrValue fcsr = 0;
    const auto boxed = [](uint32_t bits) {
        return static_cast<uint64_t>(xlen::kF32BoxerBits) | bits;
    };
    const auto execute = [&](uint32_t funct7, uint32_t rm, uint32_t rs1, uint32_t rs2,
                             xlen::Register integer_source = 0) {
        return execute::ExecuteUnit::opFp(funct7, static_cast<isa::Funct3>(rm),
                                          xlen::to_fp_reg_id(rs1), xlen::to_fp_reg_id(rs2),
                                          integer_source, fp.data(), fcsr);
    };

    fp[1] = boxed(UINT32_C(0x3F800000));  // 1.0
    fp[2] = boxed(UINT32_C(0x33800000));  // 2^-24
    fcsr = 0;
    auto result = execute(0x00, 4, 1, 2);
    TEST_ASSERT((result.fp_wb_data & UINT64_C(0xFFFFFFFF)) == UINT32_C(0x3F800001));
    TEST_ASSERT((fcsr & isa::FflagsBit::Nx) != 0);

    fp[1] = std::bit_cast<uint64_t>(1.0);
    fp[2] = std::bit_cast<uint64_t>(0x1p-53);
    fcsr = 0;
    result = execute(0x01, 4, 1, 2);
    TEST_ASSERT(result.fp_wb_data == UINT64_C(0x3FF0000000000001));
    TEST_ASSERT((fcsr & isa::FflagsBit::Nx) != 0);

    fp[1] = std::bit_cast<uint64_t>(1.0 + 0x1p-24);
    fcsr = 0;
    result = execute(0x20, 4, 1, 0);  // FCVT.S.D
    TEST_ASSERT((result.fp_wb_data & UINT64_C(0xFFFFFFFF)) == UINT32_C(0x3F800001));
    TEST_ASSERT((fcsr & isa::FflagsBit::Nx) != 0);

    fcsr = 0;
    result = execute(0x68, 4, 0, 0, 16777217);  // FCVT.S.W
    TEST_ASSERT((result.fp_wb_data & UINT64_C(0xFFFFFFFF)) == UINT32_C(0x4B800001));
    TEST_ASSERT((fcsr & isa::FflagsBit::Nx) != 0);

    if constexpr (xlen::kIsXLen64) {
        fcsr = 0;
        result = execute(0x69, 4, 0, 2, 16777217);  // FCVT.D.L
        TEST_ASSERT(result.fp_wb_data == std::bit_cast<uint64_t>(16777217.0));
        TEST_ASSERT((fcsr & isa::FflagsBit::Nx) == 0);
        fcsr = 0;
        result = execute(0x69, 4, 0, 2, static_cast<xlen::Register>(9007199254740993ULL));
        TEST_ASSERT(result.fp_wb_data == std::bit_cast<uint64_t>(9007199254740994.0));
        TEST_ASSERT((fcsr & isa::FflagsBit::Nx) != 0);
    }

    fp[1] = boxed(UINT32_C(0x3F800001));
    fp[2] = boxed(UINT32_C(0x3F7FFFFE));
    fp[3] = boxed(std::bit_cast<uint32_t>(0x1p-24F + 0x1p-46F));
    fcsr = 0;
    result = execute::ExecuteUnit::fusedFp(isa::Opcode::MAdd, FpFmt::Single, xlen::to_fp_reg_id(1),
                                           xlen::to_fp_reg_id(2), xlen::to_fp_reg_id(3),
                                           isa::RoundingMode::Rmm, fp.data(), fcsr);
    TEST_ASSERT((result.fp_wb_data & UINT64_C(0xFFFFFFFF)) == UINT32_C(0x3F800001));
    TEST_ASSERT((fcsr & isa::FflagsBit::Nx) != 0);
}

void test_zvfhmin_vector_conversions() {
    simrv::core::Machine machine;
    auto& cpu = machine.primary_hart();
    cpu.reset();
    cpu.state().misa = simrv::isa::kMisaDefault;
    cpu.state().mstatus |= simrv::core::MstatusBit::Fs;
    cpu.state().vtype = 1U << 3U;  // SEW=16, LMUL=1.
    cpu.state().vl = 1;
    cpu.state().vstart = 0;
    cpu.state().fcsr = 0;

    constexpr auto vd = static_cast<simrv::xlen::RegId>(4);
    constexpr auto vs2 = static_cast<simrv::xlen::RegId>(2);
    const auto ir = [](simrv::xlen::RegId rd, simrv::xlen::RegId rs2) {
        return static_cast<simrv::xlen::Instruction>(
            (std::to_underlying(rd) << 7U) | (std::to_underlying(rs2) << 20U) | (1U << 25U));
    };
    auto& source = cpu.state().regs.read_vector(simrv::xlen::to_vec_reg_id(vs2));
    auto& dest = cpu.state().regs.read_vector(simrv::xlen::to_vec_reg_id(vd));

    source.u16[0] = UINT16_C(0x3C00);
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFWCVT_F_F_V, ir(vd, vs2));
    TEST_ASSERT(!cpu.active_context().pending_exception.has_value());
    TEST_ASSERT(dest.u32[0] == UINT32_C(0x3F800000));

    source.u32[0] = UINT32_C(0x3F801000);
    cpu.state().fcsr = 0;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFNCVT_F_F_W, ir(vd, vs2));
    TEST_ASSERT(!cpu.active_context().pending_exception.has_value());
    TEST_ASSERT(dest.u16[0] == UINT16_C(0x3C00));
    TEST_ASSERT((cpu.state().fcsr & simrv::isa::FflagsBit::Nx) != 0);

    cpu.state().fcsr = simrv::isa::RoundingMode::Rmm << 5U;
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFNCVT_F_F_W, ir(vd, vs2));
    TEST_ASSERT(dest.u16[0] == UINT16_C(0x3C01));

    cpu.active_context().pending_exception.reset();
    simrv::execute::ExecuteUnit::execute_vector(cpu, machine.memory(),
                                                simrv::isa::OperationId::VFADD_VV, ir(vd, vs2));
    TEST_ASSERT(cpu.active_context().pending_exception ==
                simrv::isa::ExceptionCode::IllegalInstruction);
}

void test_zihintntl_encodings() {
    // NTL.P1/PALL/S1/ALL are base ADD HINTs that software may use before a memory access.
    for (uint32_t rs2 = 2; rs2 <= 5; ++rs2) {
        const uint32_t ntl = (rs2 << 20U) | 0x33U;  // ADD x0, x0, x[rs2]
        TEST_ASSERT(simrv::pipeline::decoder(ntl) == simrv::isa::OperationId::ADD);

        // C.NTL variants are C.ADD x0, x[rs2] in quadrant 2, funct3=100.
        const uint16_t compressed = static_cast<uint16_t>(0x9002U | (rs2 << 2U));
        const auto expanded = simrv::pipeline::decompressInstruction(compressed, true);
        TEST_ASSERT(simrv::pipeline::decoder(expanded) == simrv::isa::OperationId::ADD);
        const simrv::pipeline::Decoder decoded(expanded);
        TEST_ASSERT(decoded.rd() == simrv::xlen::RegId::Zero);
        TEST_ASSERT(decoded.rs1() == simrv::xlen::RegId::Zero);
        TEST_ASSERT(std::to_underlying(decoded.rs2()) == rs2);
    }
}

void test_zihintpause_encoding() {
    // PAUSE is encoded as FENCE W,0, a valid fence with no successor set.
    constexpr uint32_t pause = (1U << 24U) | 0x0FU;
    TEST_ASSERT(simrv::pipeline::decoder(pause) == simrv::isa::OperationId::FENCE);
}

void test_zifencei_flushes_local_instruction_state() {
    simrv::core::Machine machine;
    auto& cpu = machine.primary_hart();
    constexpr simrv::xlen::Address kLineAddress = 0x1000;
    std::array<simrv::xlen::Byte, 64> line{};
    cpu.icache.insert(simrv::xlen::PhysAddr{kLineAddress}, line.data());
    uint32_t cached_word = 0;
    TEST_ASSERT(cpu.icache.read(simrv::xlen::PhysAddr{kLineAddress}, cached_word));

    simrv::pipeline::DecodedInstruction decoded{};
    decoded.cpc = simrv::xlen::VirtAddr{kLineAddress};
    decoded.ir = 0x00000013;
    decoded.op_id = simrv::isa::OperationId::ADDI;
    simrv::core::CachedOp cached{};
    cached.copy_from(decoded);
    cpu.decode_cache.insert(decoded.cpc, cached);
    TEST_ASSERT(cpu.decode_cache.lookup(decoded.cpc) != nullptr);

    auto& context = cpu.active_context();
    context.ir = 0x0000100F;
    context.opcode = simrv::isa::Opcode::MiscMem;
    context.funct3 = simrv::isa::Funct3::FenceI;
    context.op_id = simrv::isa::OperationId::FENCE_I;
    TEST_ASSERT(cpu.execute_stage(machine));

    uint32_t instruction = 0;
    TEST_ASSERT(!cpu.icache.read(simrv::xlen::PhysAddr{kLineAddress}, instruction));
    TEST_ASSERT(cpu.decode_cache.lookup(decoded.cpc) == nullptr);
}

void test_svade_faults_on_clear_accessed_and_dirty_bits() {
    if constexpr (simrv::xlen::kIsXLen64) {
        simrv::core::Machine machine;
        std::vector<simrv::xlen::Byte> ram(0x10000, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.memory().initialize_mmu();
        auto& cpu = machine.primary_hart();
        const auto memory = machine.memory().memory_geometry();
        const simrv::xlen::Address root = (memory.dram_base + 0x1000).raw();
        const simrv::xlen::Address level1 = (memory.dram_base + 0x2000).raw();
        const simrv::xlen::Address level0 = (memory.dram_base + 0x3000).raw();
        const simrv::xlen::Address physical = (memory.dram_base + 0x5000).raw();
        constexpr simrv::xlen::Address kVirtual = 0x4000;
        constexpr uint64_t kValid = std::to_underlying(simrv::PteFlag::V);
        constexpr uint64_t kPermissions = std::to_underlying(simrv::PteFlag::R) |
                                          std::to_underlying(simrv::PteFlag::W) |
                                          std::to_underlying(simrv::PteFlag::X);
        const auto write_pte = [&](simrv::xlen::Address address, uint64_t pte) {
            TEST_ASSERT(
                machine.memory().dma_write(simrv::xlen::PhysAddr{address}, &pte, sizeof(pte)));
        };
        const auto read_pte = [&](simrv::xlen::Address address) {
            uint64_t pte = 0;
            TEST_ASSERT(
                machine.memory().dma_read(simrv::xlen::PhysAddr{address}, &pte, sizeof(pte)));
            return pte;
        };

        write_pte(root, ((level1 >> 12U) << 10U) | kValid);
        write_pte(level1, ((level0 >> 12U) << 10U) | kValid);
        const simrv::xlen::Address leaf_address = level0 + 4U * sizeof(uint64_t);
        const uint64_t leaf_without_ad = ((physical >> 12U) << 10U) | kValid | kPermissions;
        write_pte(leaf_address, leaf_without_ad);
        cpu.state().priv = simrv::xlen::PrivilegeLevel::Supervisor;
        cpu.state().satp = (uint64_t{8} << 60U) | (root >> 12U);

        auto load = cpu.translate_stage_address(
            machine, simrv::xlen::VirtAddr{kVirtual}, simrv::PteAccess::Read,
            simrv::xlen::PrivilegeLevel::Supervisor, 64, simrv::memory::TlPort::Data,
            cpu.ca_state.data_walk);
        TEST_ASSERT(load.has_value() && !load->has_value());
        TEST_ASSERT(load->error() == std::to_underlying(simrv::isa::ExceptionCode::LoadPageFault));
        TEST_ASSERT(read_pte(leaf_address) == leaf_without_ad);

        auto fetch = cpu.translate_stage_address(
            machine, simrv::xlen::VirtAddr{kVirtual}, simrv::PteAccess::Code,
            simrv::xlen::PrivilegeLevel::Supervisor, 64, simrv::memory::TlPort::Instruction,
            cpu.ca_state.instruction_walk);
        TEST_ASSERT(fetch.has_value() && !fetch->has_value());
        TEST_ASSERT(fetch->error() ==
                    std::to_underlying(simrv::isa::ExceptionCode::FetchPageFault));
        TEST_ASSERT(read_pte(leaf_address) == leaf_without_ad);

        const uint64_t leaf_accessed = leaf_without_ad | std::to_underlying(simrv::PteFlag::A);
        write_pte(leaf_address, leaf_accessed);
        machine.memory().mmu()->flush_pdc();
        auto store = cpu.translate_stage_address(
            machine, simrv::xlen::VirtAddr{kVirtual}, simrv::PteAccess::Write,
            simrv::xlen::PrivilegeLevel::Supervisor, 64, simrv::memory::TlPort::Data,
            cpu.ca_state.data_walk);
        TEST_ASSERT(store.has_value() && !store->has_value());
        TEST_ASSERT(store->error() ==
                    std::to_underlying(simrv::isa::ExceptionCode::StorePageFault));
        TEST_ASSERT(read_pte(leaf_address) == leaf_accessed);

        constexpr uint64_t kAdue = uint64_t{1} << 61U;
        TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Menvcfg, kAdue).has_value());
        const auto menvcfg = cpu.csr_file.read(simrv::core::Csr::Menvcfg);
        TEST_ASSERT(menvcfg.has_value() && (*menvcfg & kAdue) == 0);
    }
}

void test_sstvecd_and_sstvala_supervisor_traps() {
    simrv::core::CPU cpu;
    cpu.reset();
    cpu.state().refresh_misa_status();
    constexpr simrv::xlen::CsrValue kVectorBase = 0x12345000;
    TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Stvec, kVectorBase | 1U).has_value());
    const auto stvec = cpu.csr_file.read(simrv::core::Csr::Stvec);
    TEST_ASSERT(stvec.has_value());
    TEST_ASSERT((*stvec & ~simrv::xlen::CsrValue{3}) == kVectorBase);
    TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Stvec, kVectorBase).has_value());
    const auto direct_stvec = cpu.csr_file.read(simrv::core::Csr::Stvec);
    TEST_ASSERT(direct_stvec.has_value() && *direct_stvec == kVectorBase);

    constexpr simrv::xlen::CsrValue kTrapValue = 0x12345678;
    constexpr std::array causes = {
        simrv::isa::ExceptionCode::Breakpoint,     simrv::isa::ExceptionCode::MisalignedFetch,
        simrv::isa::ExceptionCode::FaultFetch,     simrv::isa::ExceptionCode::MisalignedLoad,
        simrv::isa::ExceptionCode::FaultLoad,      simrv::isa::ExceptionCode::MisalignedStore,
        simrv::isa::ExceptionCode::FaultStore,     simrv::isa::ExceptionCode::IllegalInstruction,
        simrv::isa::ExceptionCode::FetchPageFault, simrv::isa::ExceptionCode::LoadPageFault,
        simrv::isa::ExceptionCode::StorePageFault,
    };
    simrv::xlen::CsrValue delegation = 0;
    for (const auto cause : causes) {
        delegation |= simrv::xlen::CsrValue{1} << std::to_underlying(cause);
    }

    for (const auto cause : causes) {
        auto& state = cpu.state();
        state.priv = simrv::xlen::PrivilegeLevel::User;
        state.pc = simrv::xlen::VirtAddr{0x2000};
        state.medeleg = delegation;
        state.stvec = kVectorBase;
        cpu.raise_exception(cause, kTrapValue);
        TEST_ASSERT(cpu.state().priv == simrv::xlen::PrivilegeLevel::Supervisor);
        TEST_ASSERT(cpu.state().pc == kVectorBase);
        const auto expected_tval = cause == simrv::isa::ExceptionCode::Breakpoint
                                       ? simrv::xlen::CsrValue{0x2000}
                                       : kTrapValue;
        TEST_ASSERT(cpu.state().stval == expected_tval);
    }
}

void test_cmo_decoder_and_predicates() {
    using namespace simrv::isa;

    // cbo.clean (rs1=x1) -> funct12=0x001, rs1=1, funct3=2, rd=0, opcode=0x0F
    const uint32_t cbo_clean_insn = (0x001U << 20U) | (1U << 15U) | (2U << 12U) | 0x0FU;
    TEST_ASSERT(simrv::pipeline::decoder(cbo_clean_insn) == OperationId::CBO_CLEAN);
    TEST_ASSERT(is_cmo_op(OperationId::CBO_CLEAN));

    // cbo.flush (rs1=x1) -> funct12=0x002, rs1=1, funct3=2, rd=0, opcode=0x0F
    const uint32_t cbo_flush_insn = (0x002U << 20U) | (1U << 15U) | (2U << 12U) | 0x0FU;
    TEST_ASSERT(simrv::pipeline::decoder(cbo_flush_insn) == OperationId::CBO_FLUSH);
    TEST_ASSERT(is_cmo_op(OperationId::CBO_FLUSH));

    // cbo.inval (rs1=x1) -> funct12=0x000, rs1=1, funct3=2, rd=0, opcode=0x0F
    const uint32_t cbo_inval_insn = (0x000U << 20U) | (1U << 15U) | (2U << 12U) | 0x0FU;
    TEST_ASSERT(simrv::pipeline::decoder(cbo_inval_insn) == OperationId::CBO_INVAL);
    TEST_ASSERT(is_cmo_op(OperationId::CBO_INVAL));

    // cbo.zero (rs1=x1) -> funct12=0x004, rs1=1, funct3=2, rd=0, opcode=0x0F
    const uint32_t cbo_zero_insn = (0x004U << 20U) | (1U << 15U) | (2U << 12U) | 0x0FU;
    TEST_ASSERT(simrv::pipeline::decoder(cbo_zero_insn) == OperationId::CBO_ZERO);
    TEST_ASSERT(is_cmo_op(OperationId::CBO_ZERO));

    // prefetch.r (offset=0, rs1=x1) -> funct5=1, funct3=6, rd=0, opcode=0x13
    const uint32_t prefetch_r_insn = (0x01U << 20U) | (1U << 15U) | (6U << 12U) | 0x13U;
    TEST_ASSERT(simrv::pipeline::decoder(prefetch_r_insn) == OperationId::PREFETCH_R);
    TEST_ASSERT(is_cmo_op(OperationId::PREFETCH_R));

    // prefetch.w (offset=0, rs1=x1) -> funct5=3, funct3=6, rd=0, opcode=0x13
    const uint32_t prefetch_w_insn = (0x03U << 20U) | (1U << 15U) | (6U << 12U) | 0x13U;
    TEST_ASSERT(simrv::pipeline::decoder(prefetch_w_insn) == OperationId::PREFETCH_W);
    TEST_ASSERT(is_cmo_op(OperationId::PREFETCH_W));

    // prefetch.i (offset=0, rs1=x1) -> funct5=0, funct3=6, rd=0, opcode=0x13
    const uint32_t prefetch_i_insn = (0x00U << 20U) | (1U << 15U) | (6U << 12U) | 0x13U;
    TEST_ASSERT(simrv::pipeline::decoder(prefetch_i_insn) == OperationId::PREFETCH_I);
    TEST_ASSERT(is_cmo_op(OperationId::PREFETCH_I));
}

void test_svinval_decoder() {
    using namespace simrv::isa;

    // sinval.vma rs1=x1, rs2=x2 -> funct7=0x0B, rs2=2, rs1=1, funct3=0, rd=0, opcode=0x73
    const uint32_t sinval_vma_insn = (0x0BU << 25U) | (2U << 20U) | (1U << 15U) | 0x73U;
    TEST_ASSERT(simrv::pipeline::decoder(sinval_vma_insn) == OperationId::SINVAL_VMA);

    // sfence.w.inval -> funct12=0x180, rs1=0, funct3=0, rd=0, opcode=0x73
    const uint32_t sfence_w_inval_insn = (0x180U << 20U) | 0x73U;
    TEST_ASSERT(simrv::pipeline::decoder(sfence_w_inval_insn) == OperationId::SFENCE_W_INVAL);

    // sfence.inval.ir -> funct12=0x181, rs1=0, funct3=0, rd=0, opcode=0x73
    const uint32_t sfence_inval_ir_insn = (0x181U << 20U) | 0x73U;
    TEST_ASSERT(simrv::pipeline::decoder(sfence_inval_ir_insn) == OperationId::SFENCE_INVAL_IR);
}

void test_zcmop_noop_decompression() {
    // RVA23 requires Zcmop. C.MOP.n encodings are no-ops unless an extension redefines them;
    // in particular they must not be mistaken for C.LUI rd, 0, which clobbers rd.
    constexpr std::array<uint16_t, 8> kCmop{0x6081U, 0x6181U, 0x6281U, 0x6381U,
                                            0x6481U, 0x6581U, 0x6681U, 0x6781U};
    for (const uint16_t compressed : kCmop) {
        const auto expanded = simrv::pipeline::decompressInstruction(compressed, true);
        TEST_ASSERT(expanded == 0x00000013U);  // addi x0, x0, 0
        TEST_ASSERT(simrv::pipeline::decoder(expanded) == simrv::isa::OperationId::ADDI);
    }
}

void test_zimop_decoder() {
    constexpr std::array<uint32_t, 40> kMopMatches{
        0x81c04073U, 0x81d04073U, 0x81e04073U, 0x81f04073U, 0x85c04073U, 0x85d04073U, 0x85e04073U,
        0x85f04073U, 0x89c04073U, 0x89d04073U, 0x89e04073U, 0x89f04073U, 0x8dc04073U, 0x8dd04073U,
        0x8de04073U, 0x8df04073U, 0xc1c04073U, 0xc1d04073U, 0xc1e04073U, 0xc1f04073U, 0xc5c04073U,
        0xc5d04073U, 0xc5e04073U, 0xc5f04073U, 0xc9c04073U, 0xc9d04073U, 0xc9e04073U, 0xc9f04073U,
        0xcdc04073U, 0xcdd04073U, 0xcde04073U, 0xcdf04073U, 0x82004073U, 0x86004073U, 0x8a004073U,
        0x8e004073U, 0xc2004073U, 0xc6004073U, 0xca004073U, 0xce004073U};
    for (size_t index = 0; index < kMopMatches.size(); ++index) {
        const uint32_t encoded =
            kMopMatches[index] | (17U << 7U) | (9U << 15U) | (index >= 32 ? (23U << 20U) : 0U);
        TEST_ASSERT(simrv::pipeline::decoder(encoded) == simrv::isa::OperationId::MOP);
    }
    // Keep reserved SYSTEM encodings illegal; Zimop only claims its defined MOP spaces.
    TEST_ASSERT(simrv::pipeline::decoder(0x80004073U) == simrv::isa::OperationId::ILLEGAL);

    simrv::pipeline::PipelineContext context{};
    context.opcode = simrv::isa::Opcode::System;
    context.op_id = simrv::isa::OperationId::MOP;
    context.rd = static_cast<simrv::xlen::RegId>(7);
    context.wb_data = 0;
    const auto effects = simrv::pipeline::build_writeback_effects(context);
    TEST_ASSERT(effects.integer_write.enabled);
    TEST_ASSERT(effects.integer_write.destination == static_cast<simrv::xlen::RegId>(7));
    TEST_ASSERT(effects.integer_write.value == 0);
}

void test_zawrs_instructions() {
    using namespace simrv::isa;
    constexpr uint32_t kWrsNto = (0x00DU << 20U) | 0x73U;
    constexpr uint32_t kWrsSto = (0x01DU << 20U) | 0x73U;
    TEST_ASSERT(simrv::pipeline::decoder(kWrsNto) == OperationId::WRS_NTO);
    TEST_ASSERT(simrv::pipeline::decoder(kWrsSto) == OperationId::WRS_STO);
    TEST_ASSERT(required_misa_extensions(OperationId::WRS_NTO) ==
                misa_extension_bit(IsaExtension::A));
    TEST_ASSERT(simrv::pipeline::decoder(kWrsNto | (1U << 15U)) == OperationId::ILLEGAL);
    TEST_ASSERT(simrv::pipeline::decoder(kWrsSto | (1U << 7U)) == OperationId::ILLEGAL);

    for (const auto priv :
         {simrv::xlen::PrivilegeLevel::User, simrv::xlen::PrivilegeLevel::Supervisor,
          simrv::xlen::PrivilegeLevel::Machine}) {
        TEST_ASSERT(
            simrv::core::TrapController::can_execute_privileged_instruction(priv, 0, 0, 0x00D, 0));
        TEST_ASSERT(
            simrv::core::TrapController::can_execute_privileged_instruction(priv, 0, 0, 0x01D, 0));
    }

    simrv::pipeline::PipelineContext context{};
    context.opcode = Opcode::System;
    context.op_id = OperationId::WRS_NTO;
    const auto effects = simrv::pipeline::build_writeback_effects(context);
    TEST_ASSERT(effects.increments_instruction_count);
    TEST_ASSERT(!effects.integer_write.enabled && !effects.floating_write.enabled);
}

void test_ssu64xl_user_xlen_csr() {
    if constexpr (simrv::xlen::kIsXLen64) {
        simrv::core::Machine machine;
        auto& cpu = machine.hart(0);
        cpu.state().misa = (simrv::xlen::CsrValue{2} << 62U) |
                           simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::I);
        cpu.state().initialize_lower_xlen_fields();

        constexpr uint64_t kUxlMask = uint64_t{0x3} << 32U;
        constexpr uint64_t kSxlMask = uint64_t{0x3} << 34U;
        const uint64_t requested =
            (static_cast<uint64_t>(cpu.state().mstatus) & ~(kUxlMask | kSxlMask)) |
            (uint64_t{2} << 32U) | (uint64_t{2} << 34U);
        const auto write = cpu.csr_file.write(simrv::xlen::CsrNumber{0x300},
                                              static_cast<simrv::xlen::CsrValue>(requested));
        TEST_ASSERT(write.has_value());
        const auto status = cpu.csr_file.read(simrv::xlen::CsrNumber{0x100});
        TEST_ASSERT(status.has_value());
        TEST_ASSERT(((static_cast<uint64_t>(*status) >> 32U) & 0x3U) == 2U);
        TEST_ASSERT(((static_cast<uint64_t>(cpu.state().mstatus) >> 34U) & 0x3U) == 2U);
    }
}

void test_supm_user_data_pointer_masking() {
    if constexpr (simrv::xlen::kIsXLen64) {
        simrv::core::Machine machine;
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.memory().initialize_mmu();
        auto& cpu = machine.primary_hart();
        cpu.reset();
        auto& state = cpu.state();
        state.initialize_lower_xlen_fields();

        constexpr simrv::xlen::Address kBase = (simrv::memory::kDramBaseAddress + 0x100).raw();
        constexpr simrv::xlen::Register kValue = 0x0123456789ABCDEFULL;
        constexpr uint64_t kPmmMask = uint64_t{0x3} << 32U;
        constexpr uint64_t kPmmSeven = uint64_t{2} << 32U;
        const simrv::xlen::VirtAddr tagged{
            static_cast<simrv::xlen::Word>(kBase | (uint64_t{0x55} << 57U))};

        state.priv = simrv::xlen::PrivilegeLevel::Machine;
        cpu.active_context().opcode = simrv::isa::Opcode::Store;
        cpu.active_context().pending_exception.reset();
        simrv::memory::MemoryAccess::target_write(
            machine.memory(), cpu, simrv::xlen::VirtAddr{kBase}, kValue, simrv::isa::Funct3::Sd);
        TEST_ASSERT(!cpu.active_context().pending_exception.has_value());

        state.priv = simrv::xlen::PrivilegeLevel::Supervisor;
        state.mstateen[0] |= uint64_t{1} << 62U;  // Permit S-mode senvcfg accesses.
        const auto invalid_pmm = cpu.csr_file.write(
            simrv::xlen::CsrNumber{0x10A}, static_cast<simrv::xlen::CsrValue>(uint64_t{1} << 32U));
        TEST_ASSERT(invalid_pmm.has_value());
        TEST_ASSERT((static_cast<uint64_t>(state.senvcfg) & kPmmMask) == 0);

        state.priv = simrv::xlen::PrivilegeLevel::User;
        cpu.active_context().opcode = simrv::isa::Opcode::Load;
        cpu.active_context().pending_exception.reset();
        (void)simrv::memory::MemoryAccess::target_read(machine.memory(), cpu, tagged,
                                                       simrv::isa::Funct3::Ld);
        TEST_ASSERT(cpu.active_context().pending_exception.has_value());

        state.priv = simrv::xlen::PrivilegeLevel::Supervisor;
        const auto enable_pmm = cpu.csr_file.write(simrv::xlen::CsrNumber{0x10A},
                                                   static_cast<simrv::xlen::CsrValue>(kPmmSeven));
        TEST_ASSERT(enable_pmm.has_value());
        TEST_ASSERT((static_cast<uint64_t>(state.senvcfg) & kPmmMask) == kPmmSeven);

        state.priv = simrv::xlen::PrivilegeLevel::User;
        cpu.active_context().opcode = simrv::isa::Opcode::Load;
        cpu.active_context().pending_exception.reset();
        const auto loaded = simrv::memory::MemoryAccess::target_read(machine.memory(), cpu, tagged,
                                                                     simrv::isa::Funct3::Ld);
        TEST_ASSERT(!cpu.active_context().pending_exception.has_value());
        TEST_ASSERT(loaded == kValue);

        cpu.active_context().opcode = simrv::isa::Opcode::Store;
        cpu.active_context().pending_exception.reset();
        simrv::memory::MemoryAccess::target_write(machine.memory(), cpu,
                                                  simrv::xlen::VirtAddr{tagged.raw() + 8U}, kValue,
                                                  simrv::isa::Funct3::Sd);
        TEST_ASSERT(!cpu.active_context().pending_exception.has_value());

        state.priv = simrv::xlen::PrivilegeLevel::Machine;
        cpu.active_context().opcode = simrv::isa::Opcode::Load;
        cpu.active_context().pending_exception.reset();
        const auto stored = simrv::memory::MemoryAccess::target_read(
            machine.memory(), cpu, simrv::xlen::VirtAddr{kBase + 8U}, simrv::isa::Funct3::Ld);
        TEST_ASSERT(!cpu.active_context().pending_exception.has_value());
        TEST_ASSERT(stored == kValue);
    }
}

void test_sscofpmf_counter_overflow_and_filtering() {
    if constexpr (simrv::xlen::kIsXLen64) {
        simrv::core::Machine machine;
        auto& cpu = machine.hart(0);
        auto& state = cpu.state();
        auto& counters = cpu.perf_counters;
        constexpr simrv::xlen::CsrNumber kMhpmevent3{0x323};
        constexpr simrv::xlen::CsrNumber kMcountinhibit{0x320};
        constexpr simrv::xlen::CsrNumber kMip{0x344};
        constexpr simrv::xlen::CsrNumber kMideleg{0x303};
        constexpr simrv::xlen::CsrNumber kSip{0x144};
        constexpr uint64_t kCounterMask = uint64_t{1} << 3U;
        constexpr uint64_t kUserInhibit = uint64_t{1} << 60U;
        constexpr uint64_t kSupervisorInhibit = uint64_t{1} << 61U;
        constexpr uint64_t kMachineInhibit = uint64_t{1} << 62U;
        constexpr uint64_t kOverflow = uint64_t{1} << 63U;
        constexpr simrv::xlen::CsrValue kLcofip = simrv::xlen::CsrValue{1} << 13U;
        const auto event = static_cast<uint64_t>(simrv::core::HpmEvent::Cycles);

        counters.reset();
        TEST_ASSERT(cpu.csr_file.write(kMhpmevent3, event | kUserInhibit).has_value());
        counters.write_hpmcounter(3, 7);
        state.priv = simrv::xlen::PrivilegeLevel::User;
        cpu.record_hpm_event(simrv::core::HpmEvent::Cycles);
        TEST_ASSERT(counters.read_hpmcounter(3) == 7);

        state.priv = simrv::xlen::PrivilegeLevel::Machine;
        TEST_ASSERT(cpu.csr_file.write(kMhpmevent3, event | kSupervisorInhibit).has_value());
        state.priv = simrv::xlen::PrivilegeLevel::Supervisor;
        cpu.record_hpm_event(simrv::core::HpmEvent::Cycles);
        TEST_ASSERT(counters.read_hpmcounter(3) == 7);

        state.priv = simrv::xlen::PrivilegeLevel::Machine;
        TEST_ASSERT(cpu.csr_file.write(kMhpmevent3, event).has_value());
        state.priv = simrv::xlen::PrivilegeLevel::Supervisor;
        cpu.record_hpm_event(simrv::core::HpmEvent::Cycles);
        TEST_ASSERT(counters.read_hpmcounter(3) == 8);

        state.priv = simrv::xlen::PrivilegeLevel::Machine;
        TEST_ASSERT(cpu.csr_file.write(kMhpmevent3, event | kMachineInhibit).has_value());
        cpu.record_hpm_event(simrv::core::HpmEvent::Cycles);
        TEST_ASSERT(counters.read_hpmcounter(3) == 8);
        TEST_ASSERT(cpu.csr_file.write(kMhpmevent3, event).has_value());
        TEST_ASSERT(cpu.csr_file.write(kMcountinhibit, kCounterMask).has_value());
        state.priv = simrv::xlen::PrivilegeLevel::Supervisor;
        cpu.record_hpm_event(simrv::core::HpmEvent::Cycles);
        TEST_ASSERT(counters.read_hpmcounter(3) == 8);

        state.priv = simrv::xlen::PrivilegeLevel::Machine;
        TEST_ASSERT(cpu.csr_file.write(kMcountinhibit, 0).has_value());
        TEST_ASSERT(cpu.csr_file.write(kMhpmevent3, event).has_value());
        counters.write_hpmcounter(3, UINT64_MAX);
        cpu.record_hpm_event(simrv::core::HpmEvent::Cycles);
        TEST_ASSERT(counters.read_hpmcounter(3) == 0);
        TEST_ASSERT((counters.read_overflow_mask() & kCounterMask) != 0);
        TEST_ASSERT((counters.read_hpmevent(3) & kOverflow) != 0);
        TEST_ASSERT((state.mip & kLcofip) != 0);

        TEST_ASSERT(cpu.csr_file.write(kMideleg, kLcofip).has_value());
        state.priv = simrv::xlen::PrivilegeLevel::Supervisor;
        const auto sip = cpu.csr_file.read(kSip);
        TEST_ASSERT(sip.has_value() && (*sip & kLcofip) != 0);

        state.priv = simrv::xlen::PrivilegeLevel::Machine;
        TEST_ASSERT(cpu.csr_file.write(kMhpmevent3, event).has_value());
        TEST_ASSERT((counters.read_overflow_mask() & kCounterMask) == 0);
        TEST_ASSERT(cpu.csr_file.write(kMip, 0).has_value());
        counters.write_hpmcounter(3, UINT64_MAX);
        cpu.record_hpm_event(simrv::core::HpmEvent::Cycles);
        TEST_ASSERT((state.mip & kLcofip) != 0);
    }
}

void test_sscounterenw_allows_enabled_hpm_counters() {
    simrv::core::Machine machine;
    auto& cpu = machine.primary_hart();
    constexpr simrv::xlen::CsrValue kHpmEnableBits =
        (simrv::xlen::CsrValue{1} << 3U) | (simrv::xlen::CsrValue{1} << 31U);
    constexpr simrv::xlen::CsrNumber kHpmcounter3{0xC03};
    constexpr simrv::xlen::CsrValue kHpmRead = 0x000C03F3U;

    TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Mcounteren, kHpmEnableBits).has_value());
    TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Scounteren, kHpmEnableBits).has_value());
    const auto mcounteren = cpu.csr_file.read(simrv::core::Csr::Mcounteren);
    const auto scounteren = cpu.csr_file.read(simrv::core::Csr::Scounteren);
    TEST_ASSERT(mcounteren.has_value() && *mcounteren == kHpmEnableBits);
    TEST_ASSERT(scounteren.has_value() && *scounteren == kHpmEnableBits);
    cpu.perf_counters.write_hpmcounter(3, kHpmRead);
    cpu.state().mcounteren = 0;
    cpu.state().scounteren = 0;

    const auto decode_hpm_read = [&]() {
        auto& context = cpu.active_context();
        context = {};
        context.ir = (static_cast<uint32_t>(kHpmcounter3.value()) << 20U) | (2U << 12U) |
                     (5U << 7U) | 0x73U;  // csrrs x5, hpmcounter3, x0
        context.ir_org = context.ir;
        context.op_id = simrv::isa::OperationId::ILLEGAL;
        cpu.run_decode_stage(machine);
    };

    cpu.state().priv = simrv::xlen::PrivilegeLevel::User;
    decode_hpm_read();
    const auto& denied = cpu.active_context();
    TEST_ASSERT(denied.pending_exception == simrv::isa::ExceptionCode::IllegalInstruction);

    cpu.state().mcounteren = simrv::xlen::CsrValue{1} << 3U;
    cpu.state().scounteren = 0;
    decode_hpm_read();
    const auto& supervisor_enabled_only = cpu.active_context();
    TEST_ASSERT(supervisor_enabled_only.pending_exception ==
                simrv::isa::ExceptionCode::IllegalInstruction);

    cpu.state().scounteren = simrv::xlen::CsrValue{1} << 3U;
    decode_hpm_read();
    const auto& user_enabled = cpu.active_context();
    TEST_ASSERT(!user_enabled.pending_exception.has_value());
    TEST_ASSERT(user_enabled.rcsr == kHpmRead);

    cpu.state().priv = simrv::xlen::PrivilegeLevel::Supervisor;
    cpu.state().scounteren = 0;
    decode_hpm_read();
    const auto& supervisor_enabled = cpu.active_context();
    TEST_ASSERT(!supervisor_enabled.pending_exception.has_value());
    TEST_ASSERT(supervisor_enabled.rcsr == kHpmRead);

    cpu.state().misa |= simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::H);
    cpu.state().virt = true;
    cpu.state().priv = simrv::xlen::PrivilegeLevel::Supervisor;
    cpu.state().hcounteren = 0;
    decode_hpm_read();
    TEST_ASSERT(cpu.active_context().pending_exception ==
                simrv::isa::ExceptionCode::VirtualInstruction);

    cpu.state().hcounteren = simrv::xlen::CsrValue{1} << 3U;
    decode_hpm_read();
    TEST_ASSERT(!cpu.active_context().pending_exception.has_value());

    cpu.state().priv = simrv::xlen::PrivilegeLevel::User;
    cpu.state().scounteren = 0;
    decode_hpm_read();
    TEST_ASSERT(cpu.active_context().pending_exception ==
                simrv::isa::ExceptionCode::VirtualInstruction);

    cpu.state().mcounteren = 0;
    decode_hpm_read();
    TEST_ASSERT(cpu.active_context().pending_exception ==
                simrv::isa::ExceptionCode::IllegalInstruction);
}

void test_sha_state_enable_csrs() {
    simrv::core::Machine machine;
    auto& cpu = machine.primary_hart();
    constexpr uint64_t kStateenSuperEnable = uint64_t{1} << 63U;
    const auto read_stateen =
        [&cpu](simrv::core::Csr low_csr,
               simrv::core::Csr high_csr) -> std::expected<uint64_t, simrv::isa::ExceptionCode> {
        const auto low = cpu.csr_file.read(low_csr);
        if (!low) return std::unexpected(low.error());
        uint64_t value = *low;
        if constexpr (!simrv::xlen::kIsXLen64) {
            const auto high = cpu.csr_file.read(high_csr);
            if (!high) return std::unexpected(high.error());
            value |= static_cast<uint64_t>(*high) << 32U;
        }
        return value;
    };
    const auto write_stateen =
        [&cpu](simrv::core::Csr low_csr, simrv::core::Csr high_csr,
               uint64_t value) -> std::expected<void, simrv::isa::ExceptionCode> {
        auto result = cpu.csr_file.write(low_csr, static_cast<simrv::xlen::CsrValue>(value));
        if (!result) return result;
        if constexpr (!simrv::xlen::kIsXLen64) {
            return cpu.csr_file.write(high_csr, static_cast<simrv::xlen::CsrValue>(value >> 32U));
        }
        return {};
    };

    TEST_ASSERT(write_stateen(simrv::core::Csr::Mstateen0, simrv::core::Csr::Mstateen0h,
                              kStateenSuperEnable)
                    .has_value());
    auto mstateen = read_stateen(simrv::core::Csr::Mstateen0, simrv::core::Csr::Mstateen0h);
    TEST_ASSERT(mstateen.has_value() && *mstateen == kStateenSuperEnable);
    TEST_ASSERT(
        cpu.csr_file.write(simrv::core::Csr::Sstateen0, simrv::xlen::CsrValue{1}).has_value());
    auto sstateen = cpu.csr_file.read(simrv::core::Csr::Sstateen0);
    // SE0 (the top bit) gates access to sstateen0 in mstateen0/hstateen0; it is
    // reserved in sstateen0 and therefore reads as zero and ignores writes.
    TEST_ASSERT(sstateen.has_value() && *sstateen == 0);

    cpu.state().mstateen[0] = 0;
    cpu.state().priv = simrv::xlen::PrivilegeLevel::Supervisor;
    sstateen = cpu.csr_file.read(simrv::core::Csr::Sstateen0);
    TEST_ASSERT(!sstateen.has_value());
    TEST_ASSERT(
        !cpu.csr_file.write(simrv::core::Csr::Sstateen0, simrv::xlen::CsrValue{1}).has_value());
    TEST_ASSERT(cpu.state().sstateen[0] == 0);

    const auto hstateen_csr = simrv::core::csr_num(simrv::core::Csr::Hstateen0);
    TEST_ASSERT(!simrv::core::TrapController::can_access_csr(cpu.state().priv, cpu.state().misa,
                                                             hstateen_csr, false));
    const auto hcounteren_csr = simrv::core::csr_num(simrv::core::Csr::Hcounteren);
    TEST_ASSERT(!simrv::core::TrapController::can_access_csr(cpu.state().priv, cpu.state().misa,
                                                             hcounteren_csr, true));
    cpu.state().misa |= simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::H);
    TEST_ASSERT(simrv::core::TrapController::can_access_csr(cpu.state().priv, cpu.state().misa,
                                                            hstateen_csr, false));
    TEST_ASSERT(simrv::core::TrapController::can_access_csr(cpu.state().priv, cpu.state().misa,
                                                            hcounteren_csr, true));
    cpu.state().mstateen[0] = kStateenSuperEnable;
    cpu.state().hstateen[0] = 0;
    cpu.state().virt = true;
    auto guest_sstateen = cpu.csr_file.read(simrv::core::Csr::Sstateen0);
    TEST_ASSERT(!guest_sstateen.has_value() &&
                guest_sstateen.error() == simrv::isa::ExceptionCode::VirtualInstruction);
    auto guest_sstateen_write =
        cpu.csr_file.write(simrv::core::Csr::Sstateen0, simrv::xlen::CsrValue{1});
    TEST_ASSERT(!guest_sstateen_write.has_value() &&
                guest_sstateen_write.error() == simrv::isa::ExceptionCode::VirtualInstruction);
    cpu.state().hstateen[0] = kStateenSuperEnable;
    guest_sstateen = cpu.csr_file.read(simrv::core::Csr::Sstateen0);
    TEST_ASSERT(guest_sstateen.has_value() && *guest_sstateen == 0);
    cpu.state().virt = false;
    constexpr simrv::xlen::CsrValue kGuestHpmEnableBits =
        (simrv::xlen::CsrValue{1} << 3U) | (simrv::xlen::CsrValue{1} << 31U);
    TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Hcounteren, kGuestHpmEnableBits).has_value());
    const auto hcounteren = cpu.csr_file.read(simrv::core::Csr::Hcounteren);
    TEST_ASSERT(hcounteren.has_value() && *hcounteren == kGuestHpmEnableBits);

#if SIMRV_XLEN == 64
    cpu.state().mstatus |= simrv::core::MstatusBit::Gva;
    TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Mstatus, 0).has_value());
    const auto mstatus = cpu.csr_file.read(simrv::core::Csr::Mstatus);
    TEST_ASSERT(mstatus.has_value() && (*mstatus & simrv::core::kMstatusGva) != 0);
#endif
    const auto medeleg = cpu.csr_file.read(simrv::core::Csr::Medeleg);
    constexpr simrv::xlen::CsrValue kHDelegatedToHs =
        (simrv::xlen::CsrValue{1} << 20U) | (simrv::xlen::CsrValue{1} << 21U) |
        (simrv::xlen::CsrValue{1} << 22U) | (simrv::xlen::CsrValue{1} << 23U);
    TEST_ASSERT(medeleg.has_value() && (*medeleg & kHDelegatedToHs) == kHDelegatedToHs);
    TEST_ASSERT(medeleg.has_value() && (*medeleg & (simrv::xlen::CsrValue{1} << 9U)) == 0);

#if SIMRV_XLEN == 64
    constexpr simrv::xlen::CsrValue kHstatusSpvSpvp =
        (simrv::xlen::CsrValue{1} << 7U) | (simrv::xlen::CsrValue{1} << 8U) |
        (simrv::xlen::CsrValue{1} << 9U) | (simrv::xlen::CsrValue{1} << 20U) |
        (simrv::xlen::CsrValue{1} << 21U) | (simrv::xlen::CsrValue{1} << 22U);
    constexpr simrv::xlen::CsrValue kHstatusHupmm = simrv::xlen::CsrValue{2} << 48U;
    TEST_ASSERT(
        cpu.csr_file.write(simrv::core::Csr::Hstatus, kHstatusSpvSpvp | kHstatusHupmm).has_value());
    const auto hstatus = cpu.csr_file.read(simrv::core::Csr::Hstatus);
    TEST_ASSERT(hstatus.has_value() && (*hstatus & kHstatusSpvSpvp) == kHstatusSpvSpvp);
    TEST_ASSERT((*hstatus & kHstatusHupmm) == kHstatusHupmm);
    TEST_ASSERT((*hstatus & (simrv::xlen::CsrValue{0x3} << 32U)) ==
                (simrv::xlen::CsrValue{2} << 32U));
    constexpr simrv::xlen::CsrValue kHenvcfgPmm = simrv::xlen::CsrValue{2} << 32U;
    cpu.state().mstateen[0] |= uint64_t{1} << 62U;
    TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Henvcfg, kHenvcfgPmm).has_value());
    const auto henvcfg = cpu.csr_file.read(simrv::core::Csr::Henvcfg);
    TEST_ASSERT(henvcfg.has_value() && (*henvcfg & kHenvcfgPmm) == kHenvcfgPmm);
#endif

#if SIMRV_XLEN == 64
    constexpr uint64_t kHgatpSv39x4 = (uint64_t{8} << 60U) | 4U;
    TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Hgatp, kHgatpSv39x4).has_value());
    auto hgatp = cpu.csr_file.read(simrv::core::Csr::Hgatp);
    TEST_ASSERT(hgatp.has_value() && *hgatp == kHgatpSv39x4);
    TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Hgatp, kHgatpSv39x4 | 1U).has_value());
    hgatp = cpu.csr_file.read(simrv::core::Csr::Hgatp);
    TEST_ASSERT(hgatp.has_value() && *hgatp == kHgatpSv39x4);

    const auto vsatp_csr = simrv::core::csr_num(simrv::core::Csr::Vsatp);
    TEST_ASSERT(simrv::core::TrapController::can_access_csr(cpu.state().priv, cpu.state().misa,
                                                            vsatp_csr, true));
    TEST_ASSERT(!simrv::core::TrapController::can_access_csr(cpu.state().priv, cpu.state().misa,
                                                             vsatp_csr, true, true));
    cpu.state().virt = true;
    cpu.state().priv = simrv::xlen::PrivilegeLevel::Supervisor;
    TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Stvec, 0x1237).has_value());
    auto stvec = cpu.csr_file.read(simrv::core::Csr::Stvec);
    TEST_ASSERT(stvec.has_value() && *stvec == 0x1235);
    auto vstvec = cpu.csr_file.read(simrv::core::Csr::Vstvec);
    TEST_ASSERT(vstvec.has_value() && *vstvec == *stvec);
    constexpr simrv::xlen::CsrValue kVsStatusValue =
        simrv::core::MstatusBit::Spie | simrv::core::MstatusBit::Spp;
    TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Sstatus, kVsStatusValue).has_value());
    const auto sstatus = cpu.csr_file.read(simrv::core::Csr::Sstatus);
    TEST_ASSERT(sstatus.has_value() && (*sstatus & kVsStatusValue) == kVsStatusValue);
    constexpr uint64_t kVsSatpSv39 = (uint64_t{8} << 60U) | 0x123U;
    TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Satp, kVsSatpSv39).has_value());
    const auto vsatp = cpu.csr_file.read(simrv::core::Csr::Vsatp);
    TEST_ASSERT(vsatp.has_value() && *vsatp == kVsSatpSv39);
    cpu.state().virt = false;

    cpu.state().priv = simrv::xlen::PrivilegeLevel::Machine;
    cpu.state().mstateen[0] = kStateenSuperEnable;
    TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Hstateen0, kStateenSuperEnable).has_value());
    auto hstateen = cpu.csr_file.read(simrv::core::Csr::Hstateen0);
    TEST_ASSERT(hstateen.has_value() && *hstateen == kStateenSuperEnable);
    cpu.state().mstateen[0] = 0;
    cpu.state().priv = simrv::xlen::PrivilegeLevel::Supervisor;
    hstateen = cpu.csr_file.read(simrv::core::Csr::Hstateen0);
    TEST_ASSERT(!hstateen.has_value());
    TEST_ASSERT(!cpu.csr_file.write(simrv::core::Csr::Hstateen0, 0).has_value());
#endif
}

void test_sha_guest_extension_state() {
    using simrv::core::Csr;
    using simrv::core::CsrFile;
    using simrv::core::MstatusBit;
    using simrv::xlen::PrivilegeLevel;

    simrv::core::CPU cpu;
    auto& state = cpu.state();
    state.misa |= simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::H) |
                  simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::F) |
                  simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::V);
    state.priv = PrivilegeLevel::Supervisor;
    state.virt = true;
    state.mstatus = (state.mstatus & ~(MstatusBit::Fs | MstatusBit::Vs)) |
                    (simrv::xlen::CsrValue{1} << 13U) |
                    (simrv::xlen::CsrValue{1} << 9U);  // FS=Clean, VS=Clean.
    state.vsstatus = (simrv::xlen::CsrValue{1} << 13U) |
                     (simrv::xlen::CsrValue{1} << 9U);  // FS=Clean, VS=Clean.

    CsrFile csrs(cpu);
    const auto fcsr_write = csrs.write(simrv::core::csr_num(Csr::Fcsr), 1);
    TEST_ASSERT(fcsr_write.has_value());
    TEST_ASSERT((state.vsstatus & MstatusBit::Fs) == MstatusBit::Fs);
    TEST_ASSERT((state.mstatus & MstatusBit::Fs) == MstatusBit::Fs);

    const auto vxsat_write = csrs.write(simrv::core::csr_num(Csr::Vxsat), 1);
    TEST_ASSERT(vxsat_write.has_value());
    TEST_ASSERT((state.vsstatus & MstatusBit::Vs) == MstatusBit::Vs);
    TEST_ASSERT((state.mstatus & MstatusBit::Vs) == MstatusBit::Vs);
}

void test_sha_hideleg_warl_mask() {
    simrv::core::CPU cpu;
    cpu.state().misa |= simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::H);
    constexpr simrv::xlen::CsrValue kHstatusGva = simrv::xlen::CsrValue{1} << 6U;
    constexpr simrv::xlen::CsrValue kStatusXsHighBit = simrv::xlen::CsrValue{1} << 15U;
    TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Hstatus, kHstatusGva).has_value());
    auto hstatus = cpu.csr_file.read(simrv::core::Csr::Hstatus);
    TEST_ASSERT(hstatus.has_value() && (*hstatus & kHstatusGva) != 0);
    constexpr simrv::xlen::CsrValue kHstatusVgein = simrv::xlen::CsrValue{0x2A} << 12U;
    TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Hstatus, kHstatusVgein).has_value());
    hstatus = cpu.csr_file.read(simrv::core::Csr::Hstatus);
    TEST_ASSERT(hstatus.has_value() &&
                (*hstatus & (simrv::xlen::CsrValue{0x3F} << 12U)) == kHstatusVgein);
    constexpr simrv::xlen::CsrValue kHgeie = ~simrv::xlen::CsrValue{1};
    TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Hgeie, ~simrv::xlen::CsrValue{0}).has_value());
    const auto hgeie = cpu.csr_file.read(simrv::core::Csr::Hgeie);
    TEST_ASSERT(hgeie.has_value() && *hgeie == kHgeie);
    TEST_ASSERT(
        cpu.csr_file.write(simrv::core::Csr::Henvcfg, ~simrv::xlen::CsrValue{0}).has_value());
    const auto henvcfg = cpu.csr_file.read(simrv::core::Csr::Henvcfg);
    TEST_ASSERT(henvcfg.has_value() && *henvcfg == 0xF1U);
    constexpr auto kPbmte = uint64_t{1} << 62U;
    if constexpr (simrv::xlen::kIsXLen64) {
        TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Menvcfg, kPbmte).has_value());
        TEST_ASSERT(
            cpu.csr_file.write(simrv::core::Csr::Henvcfg, ~simrv::xlen::CsrValue{0}).has_value());
        const auto henvcfg_pbmte_enabled = cpu.csr_file.read(simrv::core::Csr::Henvcfg);
        TEST_ASSERT(henvcfg_pbmte_enabled.has_value() && (*henvcfg_pbmte_enabled & kPbmte) != 0);
        TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Menvcfg, 0).has_value());
        const auto henvcfg_pbmte_disabled = cpu.csr_file.read(simrv::core::Csr::Henvcfg);
        TEST_ASSERT(henvcfg_pbmte_disabled.has_value() && (*henvcfg_pbmte_disabled & kPbmte) == 0);
        cpu.state().priv = simrv::xlen::PrivilegeLevel::Supervisor;
        TEST_ASSERT(cpu.csr_file.read(simrv::core::csr_num(simrv::core::Csr::Vstimecmp)).error() ==
                    simrv::isa::ExceptionCode::IllegalInstruction);
        const auto vstimecmp_write_stce_disabled =
            cpu.csr_file.write(simrv::core::Csr::Vstimecmp, 100);
        TEST_ASSERT(!vstimecmp_write_stce_disabled.has_value() &&
                    vstimecmp_write_stce_disabled.error() ==
                        simrv::isa::ExceptionCode::IllegalInstruction);
        TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Menvcfg, uint64_t{1} << 63U).has_value());
        const auto vstimecmp_disabled =
            cpu.csr_file.read(simrv::core::csr_num(simrv::core::Csr::Vstimecmp));
        TEST_ASSERT(!vstimecmp_disabled.has_value() &&
                    vstimecmp_disabled.error() == simrv::isa::ExceptionCode::IllegalInstruction);
        const auto vstimecmp_write_tm_disabled =
            cpu.csr_file.write(simrv::core::Csr::Vstimecmp, 100);
        TEST_ASSERT(!vstimecmp_write_tm_disabled.has_value() &&
                    vstimecmp_write_tm_disabled.error() ==
                        simrv::isa::ExceptionCode::IllegalInstruction);
        TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Mcounteren, 1U << 1U).has_value());
        TEST_ASSERT(
            cpu.csr_file.read(simrv::core::csr_num(simrv::core::Csr::Vstimecmp)).has_value());
        TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Vstimecmp, 100).has_value());
    }
    TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Vsstatus, kStatusXsHighBit).has_value());
    const auto vsstatus = cpu.csr_file.read(simrv::core::Csr::Vsstatus);
    TEST_ASSERT(vsstatus.has_value() && (*vsstatus & kStatusXsHighBit) == 0);

    constexpr simrv::xlen::CsrValue kExpected = (simrv::xlen::CsrValue{1} << 2U) |
                                                (simrv::xlen::CsrValue{1} << 6U) |
                                                (simrv::xlen::CsrValue{1} << 10U);
    TEST_ASSERT(
        cpu.csr_file.write(simrv::core::Csr::Hideleg, ~simrv::xlen::CsrValue{0}).has_value());
    const auto hideleg = cpu.csr_file.read(simrv::core::Csr::Hideleg);
    TEST_ASSERT(hideleg.has_value() && *hideleg == kExpected);
    TEST_ASSERT(cpu.state().hideleg == kExpected);
    TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Mideleg, 0).has_value());
    const auto mideleg = cpu.csr_file.read(simrv::core::Csr::Mideleg);
    TEST_ASSERT(mideleg.has_value() && *mideleg == (kExpected | (simrv::xlen::CsrValue{1} << 12U)));

    cpu.state().virt = true;
    constexpr simrv::xlen::CsrValue kSupervisorInterrupts = (simrv::xlen::CsrValue{1} << 1U) |
                                                            (simrv::xlen::CsrValue{1} << 5U) |
                                                            (simrv::xlen::CsrValue{1} << 9U);
    TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Sie, kSupervisorInterrupts).has_value());
    const auto vsie = cpu.csr_file.read(simrv::core::Csr::Sie);
    TEST_ASSERT(vsie.has_value() && *vsie == kSupervisorInterrupts);
    TEST_ASSERT(cpu.csr_file.write(simrv::core::Csr::Sip, kSupervisorInterrupts).has_value());
    const auto vsip = cpu.csr_file.read(simrv::core::Csr::Sip);
    TEST_ASSERT(vsip.has_value() && *vsip == (simrv::xlen::CsrValue{1} << 1U));
}

void test_sha_hfence_instructions() {
    constexpr uint32_t kHfenceVvma = (0x11U << 25U) | 0x73U;
    constexpr uint32_t kHfenceGvma = (0x31U << 25U) | 0x73U;
    TEST_ASSERT(simrv::pipeline::decoder(kHfenceVvma) == simrv::isa::OperationId::HFENCE_VVMA);
    TEST_ASSERT(simrv::pipeline::decoder(kHfenceGvma) == simrv::isa::OperationId::HFENCE_GVMA);

    const auto h_bit = simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::H);
    const auto vvma = static_cast<simrv::xlen::Instruction>(simrv::isa::Funct7Priv::HfenceVvma);
    const auto gvma = static_cast<simrv::xlen::Instruction>(simrv::isa::Funct7Priv::HfenceGvma);
    TEST_ASSERT(simrv::core::TrapController::can_execute_privileged_instruction(
        simrv::xlen::PrivilegeLevel::Supervisor, h_bit, 0,
        static_cast<simrv::xlen::Instruction>(simrv::isa::Funct12Priv::Ecall), vvma, 0, false));
    TEST_ASSERT(simrv::core::TrapController::can_execute_privileged_instruction(
        simrv::xlen::PrivilegeLevel::Supervisor, h_bit, 0,
        static_cast<simrv::xlen::Instruction>(simrv::isa::Funct12Priv::Ecall), gvma, 0, false));
    TEST_ASSERT(!simrv::core::TrapController::can_execute_privileged_instruction(
        simrv::xlen::PrivilegeLevel::Supervisor, h_bit, 0,
        static_cast<simrv::xlen::Instruction>(simrv::isa::Funct12Priv::Ecall), vvma, 0, true));
    TEST_ASSERT(!simrv::core::TrapController::can_execute_privileged_instruction(
        simrv::xlen::PrivilegeLevel::Supervisor, 0, 0,
        static_cast<simrv::xlen::Instruction>(simrv::isa::Funct12Priv::Ecall), gvma, 0, false));
}

void test_sha_hypervisor_virtual_load_store() {
    using simrv::isa::OperationId;
    using simrv::xlen::Address;
    using simrv::xlen::Byte;
    using simrv::xlen::CsrValue;
    using simrv::xlen::Register;

    const auto decode = [](uint32_t selector, uint32_t rd = 1, uint32_t rs1 = 2, uint32_t rs2 = 0) {
        return (selector << 20U) | (rs2 << 20U) | (rs1 << 15U) | (4U << 12U) | (rd << 7U) | 0x73U;
    };
    TEST_ASSERT(simrv::pipeline::decoder(decode(0x600)) == OperationId::HLV_B);
    TEST_ASSERT(simrv::pipeline::decoder(decode(0x601)) == OperationId::HLV_BU);
    TEST_ASSERT(simrv::pipeline::decoder(decode(0x640)) == OperationId::HLV_H);
    TEST_ASSERT(simrv::pipeline::decoder(decode(0x641)) == OperationId::HLV_HU);
    TEST_ASSERT(simrv::pipeline::decoder(decode(0x643)) == OperationId::HLVX_HU);
    TEST_ASSERT(simrv::pipeline::decoder(decode(0x680)) == OperationId::HLV_W);
    TEST_ASSERT(simrv::pipeline::decoder(decode(0x681)) == OperationId::HLV_WU);
    TEST_ASSERT(simrv::pipeline::decoder(decode(0x683)) == OperationId::HLVX_WU);
    TEST_ASSERT(simrv::pipeline::decoder(decode(0x6C0)) == OperationId::HLV_D);

    // The selector field includes rs2, so use the actual HSV encoding's funct7 and rd=0.
    const auto hsv = [](uint32_t funct7, uint32_t rs1, uint32_t rs2) {
        return (funct7 << 25U) | (rs2 << 20U) | (rs1 << 15U) | (4U << 12U) | 0x73U;
    };
    TEST_ASSERT(simrv::pipeline::decoder(hsv(0x31, 2, 3)) == OperationId::HSV_B);
    TEST_ASSERT(simrv::pipeline::decoder(hsv(0x33, 2, 3)) == OperationId::HSV_H);
    TEST_ASSERT(simrv::pipeline::decoder(hsv(0x35, 2, 3)) == OperationId::HSV_W);
    TEST_ASSERT(simrv::pipeline::decoder(hsv(0x37, 2, 3)) == OperationId::HSV_D);
    const auto hlv_traits = simrv::pipeline::operation::make_dependency_traits(
        OperationId::HLV_W, simrv::isa::Opcode::System, simrv::xlen::RegId::T0, 0);
    const auto hsv_traits = simrv::pipeline::operation::make_dependency_traits(
        OperationId::HSV_W, simrv::isa::Opcode::System, simrv::xlen::RegId::Zero, 0);
    TEST_ASSERT(hlv_traits.is_mem_load && hlv_traits.reads_rs1_int && hlv_traits.writes_int &&
                !hlv_traits.is_serializing);
    TEST_ASSERT(hsv_traits.is_mem_store && hsv_traits.reads_rs1_int && hsv_traits.reads_rs2_int &&
                !hsv_traits.is_serializing);

#if SIMRV_XLEN == 64
    {
        simrv::core::Machine machine;
        std::vector<Byte> ram(0x20000, Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.memory().initialize_mmu();
        auto& cpu = machine.primary_hart();
        cpu.reset();

        const Address base = machine.memory_geometry().dram_base.raw();
        constexpr Address root = 0x1000;
        constexpr Address middle = 0x2000;
        constexpr Address leaf = 0x3000;
        const Address g_root = base + 0x4000;
        const Address g_middle = base + 0x5000;
        constexpr Address virtual_address = 0x1234;
        constexpr Address guest_physical_page = 0x9000;
        constexpr uint64_t valid_table = std::to_underlying(simrv::PteFlag::V);
        const auto put_pte = [&](Address address, uint64_t value) {
            std::memcpy(ram.data() + (address - base), &value, sizeof(value));
        };
        const auto put_leaf = [&](uint64_t flags) {
            put_pte(base + root, (uint64_t{2} << 10U) | valid_table);
            put_pte(base + middle, (uint64_t{3} << 10U) | valid_table);
            put_pte(base + leaf + 8U, ((guest_physical_page >> 12U) << 10U) | flags);
        };
        const uint64_t g_leaf_flags =
            std::to_underlying(simrv::PteFlag::V) | std::to_underlying(simrv::PteFlag::R) |
            std::to_underlying(simrv::PteFlag::W) | std::to_underlying(simrv::PteFlag::X) |
            std::to_underlying(simrv::PteFlag::U) | std::to_underlying(simrv::PteFlag::A) |
            std::to_underlying(simrv::PteFlag::D);
        put_pte(g_root, ((g_middle >> 12U) << 10U) | valid_table);
        put_pte(g_middle, ((base >> 12U) << 10U) | g_leaf_flags);
        constexpr uint64_t v_a =
            std::to_underlying(simrv::PteFlag::V) | std::to_underlying(simrv::PteFlag::A);
        put_leaf(v_a | std::to_underlying(simrv::PteFlag::R) |
                 std::to_underlying(simrv::PteFlag::W) | std::to_underlying(simrv::PteFlag::D));
        constexpr Register payload = 0x09ABCDEFU;
        std::memcpy(ram.data() + (base + guest_physical_page + 0x234 - base), &payload,
                    sizeof(payload));

        cpu.state().misa |= simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::H);
        cpu.state().priv = simrv::xlen::PrivilegeLevel::Supervisor;
        cpu.state().virt = false;
        cpu.state().vsatp = (uint64_t{8} << 60U) | (root >> 12U);
        cpu.state().satp = 0;  // HS satp is deliberately Bare.
        cpu.state().hgatp = (uint64_t{8} << 60U) | (g_root >> 12U);
        cpu.state().hstatus |= CsrValue{1} << 8U;  // SPVP=VS.

        auto& ctx = cpu.active_context();
        ctx.pending_exception.reset();
        ctx.opcode = simrv::isa::Opcode::System;
        ctx.op_id = OperationId::HLV_W;
        const auto loaded = simrv::memory::MemoryAccess::target_read(
            machine.memory(), cpu, simrv::xlen::VirtAddr{virtual_address}, simrv::isa::Funct3::Lw);
        TEST_ASSERT(!ctx.pending_exception.has_value());
        TEST_ASSERT(static_cast<Register>(loaded) == payload);
        TEST_ASSERT(!cpu.state().virt);

        cpu.state().henvcfg = CsrValue{2} << 32U;
        ctx.op_id = OperationId::HLV_W;
        ctx.pending_exception.reset();
        const auto pointer_masked = simrv::memory::MemoryAccess::target_read(
            machine.memory(), cpu, simrv::xlen::VirtAddr{0x5400000000001234ULL},
            simrv::isa::Funct3::Lw);
        TEST_ASSERT(!ctx.pending_exception.has_value());
        TEST_ASSERT(static_cast<Register>(pointer_masked) == payload);

        // If VS translation is Bare, an HLV operand is a GPA even when the
        // G stage is active. PMM must clear the top tag bits without
        // canonicalizing bit 56 as a virtual address.
        cpu.state().vsatp = 0;
        ctx.op_id = OperationId::HLV_W;
        ctx.pending_exception.reset();
        (void)simrv::memory::MemoryAccess::target_read(machine.memory(), cpu,
                                                       simrv::xlen::VirtAddr{0xFF00000080011000ULL},
                                                       simrv::isa::Funct3::Lw);
        TEST_ASSERT(ctx.pending_exception == simrv::isa::ExceptionCode::GuestLoadPageFault);
        TEST_ASSERT(ctx.pending_tval == 0x0100000080011000ULL);
        cpu.state().vsatp = (uint64_t{8} << 60U) | (root >> 12U);
        ctx.pending_exception.reset();
        cpu.state().henvcfg = 0;

        constexpr Register store_payload = 0x13579BDFU;
        ctx.op_id = OperationId::HSV_W;
        simrv::memory::MemoryAccess::target_write(machine.memory(), cpu,
                                                  simrv::xlen::VirtAddr{virtual_address},
                                                  store_payload, simrv::isa::Funct3::Sw);
        TEST_ASSERT(!ctx.pending_exception.has_value());
        Register stored = 0;
        std::memcpy(&stored, ram.data() + guest_physical_page + 0x234, sizeof(stored));
        TEST_ASSERT(stored == store_payload);

        // HLVX requires X rather than R in both translation stages and still performs a load.
        put_leaf(v_a | std::to_underlying(simrv::PteFlag::X));
        ctx.op_id = OperationId::HLVX_WU;
        ctx.pending_exception.reset();
        const auto execute_only = simrv::memory::MemoryAccess::target_read(
            machine.memory(), cpu, simrv::xlen::VirtAddr{virtual_address}, simrv::isa::Funct3::Lwu);
        TEST_ASSERT(!ctx.pending_exception.has_value());
        TEST_ASSERT(static_cast<Register>(execute_only) == store_payload);

        put_leaf(v_a | std::to_underlying(simrv::PteFlag::R));
        ctx.pending_exception.reset();
        (void)simrv::memory::MemoryAccess::target_read(
            machine.memory(), cpu, simrv::xlen::VirtAddr{virtual_address}, simrv::isa::Funct3::Lwu);
        TEST_ASSERT(ctx.pending_exception == simrv::isa::ExceptionCode::LoadPageFault);
        const bool hlvx_fault_marker = ctx.hypervisor_memory_access_fault;
        TEST_ASSERT(hlvx_fault_marker);

        // Exercise the real cached/fallback instruction path, including decode, retirement, and
        // writeback, rather than only calling the memory access helpers directly.
        put_leaf(v_a | std::to_underlying(simrv::PteFlag::R) |
                 std::to_underlying(simrv::PteFlag::W) | std::to_underlying(simrv::PteFlag::D));
        const uint32_t hlv_w = (0x680U << 20U) | (6U << 15U) | (4U << 12U) | (5U << 7U) | 0x73U;
        const uint32_t hsv_w = (0x35U << 25U) | (7U << 20U) | (6U << 15U) | (4U << 12U) | 0x73U;
        std::memcpy(ram.data() + 0x8000, &hlv_w, sizeof(hlv_w));
        std::memcpy(ram.data() + 0x8004, &hsv_w, sizeof(hsv_w));
        cpu.state().pc = simrv::xlen::VirtAddr{base + 0x8000};
        cpu.state().regs.write(simrv::xlen::RegId::T1, virtual_address);
        cpu.state().regs.write(simrv::xlen::RegId::T2, 0x2468ACE0U);
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::InstructionFast;
        cpu.run_cycle(machine);
        TEST_ASSERT(cpu.state().regs.read(simrv::xlen::RegId::T0) == store_payload);
        cpu.run_cycle(machine);
        std::memcpy(&stored, ram.data() + guest_physical_page + 0x234, sizeof(stored));
        TEST_ASSERT(stored == 0x2468ACE0U);

        cpu.state().medeleg |= CsrValue{1} << 13U;
        ctx.hypervisor_memory_access_fault = hlvx_fault_marker;
        simrv::core::TrapController::raise_exception(
            cpu, std::to_underlying(simrv::isa::ExceptionCode::LoadPageFault), virtual_address);
        constexpr CsrValue kSpv = CsrValue{1} << 7U;
        constexpr CsrValue kSpvp = CsrValue{1} << 8U;
        constexpr CsrValue kGva = CsrValue{1} << 6U;
        TEST_ASSERT((cpu.state().hstatus & kSpv) == 0);
        TEST_ASSERT((cpu.state().hstatus & kSpvp) != 0);
        TEST_ASSERT((cpu.state().hstatus & kGva) != 0);
    }
#endif
}

[[maybe_unused]] void test_sha_guest_return_transitions() {
    using simrv::core::MstatusBit;
    using simrv::xlen::PrivilegeLevel;
    const auto h_bit = simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::H);

    simrv::core::ArchState machine_state;
    machine_state.misa |= h_bit;
    machine_state.mstatus = simrv::core::mstatus_with_mpp(machine_state.mstatus | MstatusBit::Mpv,
                                                          PrivilegeLevel::Supervisor);
    machine_state.mepc = 0x8000;
    simrv::core::TrapController::mret(machine_state);
    TEST_ASSERT(machine_state.virt);
    TEST_ASSERT(machine_state.priv == PrivilegeLevel::Supervisor);
    TEST_ASSERT(machine_state.pc == 0x8000);
    TEST_ASSERT((machine_state.mstatus & MstatusBit::Mpv) == 0);

    simrv::core::ArchState hs_state;
    hs_state.misa |= h_bit;
    hs_state.priv = PrivilegeLevel::Supervisor;
    hs_state.hstatus = (simrv::xlen::CsrValue{1} << 7U) | (simrv::xlen::CsrValue{1} << 8U);
    hs_state.mstatus = simrv::core::mstatus_with_spp(hs_state.mstatus, PrivilegeLevel::Supervisor);
    hs_state.sepc = 0x9000;
    hs_state.vsepc = 0xA000;
    simrv::core::TrapController::sret(hs_state);
    TEST_ASSERT(hs_state.virt);
    TEST_ASSERT(hs_state.priv == PrivilegeLevel::Supervisor);
    TEST_ASSERT(hs_state.pc == 0x9000);
    TEST_ASSERT((hs_state.hstatus & (simrv::xlen::CsrValue{1} << 7U)) == 0);

    // HS SRET uses HS sstatus.SPP for the return privilege. SPVP records the guest
    // privilege for HLV/HSV and must not override the SRET destination.
    hs_state.mstatus = simrv::core::mstatus_with_spp(hs_state.mstatus, PrivilegeLevel::User);
    hs_state.vsepc = 0xB000;
    simrv::core::TrapController::sret(hs_state);
    TEST_ASSERT(hs_state.virt);
    TEST_ASSERT(hs_state.priv == PrivilegeLevel::User);
    TEST_ASSERT(hs_state.pc == 0xB000);
}

void test_sha_guest_exception_routing() {
    const auto h_bit = simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::H);
    {
        simrv::core::CPU cpu;
        auto& state = cpu.state();
        state.misa |= h_bit;
        state.virt = true;
        state.priv = simrv::xlen::PrivilegeLevel::Supervisor;
        state.pc = simrv::xlen::VirtAddr{0x4000};
        state.medeleg = simrv::xlen::CsrValue{1}
                        << std::to_underlying(simrv::isa::ExceptionCode::Breakpoint);
        state.hedeleg = state.medeleg;
        state.vstvec = 0x2000;
        simrv::core::TrapController::raise_exception(
            cpu, simrv::isa::to_trap_cause(simrv::isa::ExceptionCode::Breakpoint), 0x1234);
        TEST_ASSERT(state.virt);
        TEST_ASSERT(state.vscause == simrv::isa::ExceptionCode::Breakpoint);
        TEST_ASSERT(state.vsepc == 0x4000);
        TEST_ASSERT(state.vstval == 0x4000);
        TEST_ASSERT(state.pc == 0x2000);
    }
    {
        simrv::core::CPU cpu;
        auto& state = cpu.state();
        state.misa |= h_bit;
        state.virt = true;
        state.priv = simrv::xlen::PrivilegeLevel::Supervisor;
        state.pc = simrv::xlen::VirtAddr{0x5000};
        state.stvec = 0x3000;
        simrv::core::TrapController::raise_exception(
            cpu, simrv::isa::to_trap_cause(simrv::isa::ExceptionCode::VirtualInstruction),
            0xDEADBEEF);
        TEST_ASSERT(!state.virt);
        TEST_ASSERT(state.priv == simrv::xlen::PrivilegeLevel::Supervisor);
        TEST_ASSERT(state.scause == simrv::isa::ExceptionCode::VirtualInstruction);
        TEST_ASSERT(state.sepc == 0x5000);
        TEST_ASSERT((state.hstatus & (simrv::xlen::CsrValue{1} << 7U)) != 0);
        TEST_ASSERT((state.hstatus & (simrv::xlen::CsrValue{1} << 8U)) != 0);
        TEST_ASSERT(state.pc == 0x3000);
    }
    {
        simrv::core::CPU cpu;
        auto& state = cpu.state();
        state.misa |= h_bit;
        state.virt = true;
        state.priv = simrv::xlen::PrivilegeLevel::Supervisor;
        state.pc = simrv::xlen::VirtAddr{0x6000};
        state.mideleg |= simrv::xlen::CsrValue{1} << 2U;
        state.hideleg |= simrv::xlen::CsrValue{1} << 2U;
        state.vsstatus |= simrv::core::MstatusBit::Sie;
        state.vstvec = 0x7000;
        simrv::core::TrapController::raise_exception(cpu, simrv::isa::kInterruptCauseBit | 2U, 0);
        TEST_ASSERT(state.virt);
        TEST_ASSERT(state.vscause == (simrv::isa::kInterruptCauseBit | 1U));
        TEST_ASSERT((state.vsstatus & simrv::core::MstatusBit::Sie) == 0);
        TEST_ASSERT((state.vsstatus & simrv::core::MstatusBit::Spie) != 0);
        TEST_ASSERT(state.pc == 0x7000);
    }
    {
        simrv::core::CPU cpu;
        auto& state = cpu.state();
        state.misa |= h_bit;
        state.virt = true;
        state.priv = simrv::xlen::PrivilegeLevel::Supervisor;
        state.pc = simrv::xlen::VirtAddr{0x8000};
        state.medeleg = simrv::xlen::CsrValue{1} << 21U;
        state.stvec = 0x9000;
        cpu.active_context().pending_htval = 0x12345678U >> 2U;
        simrv::core::TrapController::raise_exception(
            cpu, simrv::isa::to_trap_cause(simrv::isa::ExceptionCode::GuestLoadPageFault),
            0xABCDEF);
        TEST_ASSERT(!state.virt);
        TEST_ASSERT(state.htval == (0x12345678U >> 2U));
    }
}

void test_sha_shtvala_guest_physical_fault_value() {
    if constexpr (simrv::xlen::kIsXLen64) {
        simrv::core::Machine machine;
        std::vector<simrv::xlen::Byte> ram(0x10000, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.memory().initialize_mmu();
        auto& cpu = machine.primary_hart();
        cpu.state().misa |= simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::H);
        cpu.state().virt = true;
        cpu.state().priv = simrv::xlen::PrivilegeLevel::Supervisor;
        cpu.state().satp = 0;  // VS Bare still receives the required G-stage translation.
        constexpr simrv::xlen::Address kRoot = 0x1000;
        constexpr simrv::xlen::Address kGuestPhysical = 0x4000;
        cpu.state().hgatp = (uint64_t{8} << 60U) | (kRoot >> 12U);

        simrv::memory::PageTableWalker walker(machine.ram_view());
        const auto bare_walk = walker.begin_walk(
            simrv::xlen::VirtAddr{kGuestPhysical}, simrv::PteAccess::Read,
            simrv::xlen::PrivilegeLevel::Supervisor, 0, 0, 64, true, &cpu.state());
        TEST_ASSERT(bare_walk.status == simrv::memory::PageWalkStatus::Fault);
        TEST_ASSERT(bare_walk.physical_address == simrv::xlen::PhysAddr{});
        TEST_ASSERT(bare_walk.guest_fault_address.raw() == kGuestPhysical);

        const auto vs_satp = static_cast<simrv::xlen::Word>((uint64_t{8} << 60U) | 2U);
        const auto vs_walk = walker.begin_walk(
            simrv::xlen::VirtAddr{kGuestPhysical}, simrv::PteAccess::Read,
            simrv::xlen::PrivilegeLevel::Supervisor, 0, vs_satp, 64, true, &cpu.state());
        TEST_ASSERT(vs_walk.status == simrv::memory::PageWalkStatus::Fault);
        TEST_ASSERT(vs_walk.pte_guest_address.raw() == 0x2000);
        TEST_ASSERT(vs_walk.pte_address == simrv::xlen::PhysAddr{});

        auto translated = cpu.translate_stage_address(
            machine, simrv::xlen::VirtAddr{kGuestPhysical}, simrv::PteAccess::Read,
            simrv::xlen::PrivilegeLevel::Supervisor, 64, simrv::memory::TlPort::Data,
            cpu.ca_state.data_walk);
        TEST_ASSERT(translated.has_value() && !translated->has_value());
        TEST_ASSERT(translated->error() ==
                    std::to_underlying(simrv::isa::ExceptionCode::GuestLoadPageFault));
        TEST_ASSERT(cpu.active_context().pending_htval == (kGuestPhysical >> 2U));

        cpu.state().mepc = 0;
        cpu.state().medeleg = simrv::xlen::CsrValue{1} << 21U;
        cpu.state().stvec = 0x8000;
        simrv::core::TrapController::raise_exception(cpu, translated->error(), kGuestPhysical);
        TEST_ASSERT(cpu.state().htval == (kGuestPhysical >> 2U));
    }
}

void test_sha_two_stage_cpu_translation_uses_vsatp() {
    if constexpr (simrv::xlen::kIsXLen64) {
        simrv::core::Machine machine;
        std::vector<simrv::xlen::Byte> ram(0x400000, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.memory().initialize_mmu();
        auto& cpu = machine.primary_hart();
        constexpr simrv::xlen::Address kVsRoot = 0x1000;
        constexpr simrv::xlen::Address kVsMiddle = 0x2000;
        constexpr simrv::xlen::Address kVsLeaf = 0x3000;
        constexpr simrv::xlen::Address kGuestVa = 0x1234;
        constexpr simrv::xlen::Address kGuestPa = 0x6000;
        const simrv::xlen::Address kDramBase = machine.memory_geometry().dram_base.raw();
        const simrv::xlen::Address kGRoot = kDramBase + 0x4000;
        const simrv::xlen::Address kGMiddle = kDramBase + 0x5000;
        const simrv::xlen::Address kHostBase = kDramBase + 0x200000;
        constexpr uint64_t kValidTable = std::to_underlying(simrv::PteFlag::V);
        constexpr uint64_t kLeafFlags =
            std::to_underlying(simrv::PteFlag::V) | std::to_underlying(simrv::PteFlag::R) |
            std::to_underlying(simrv::PteFlag::W) | std::to_underlying(simrv::PteFlag::X) |
            std::to_underlying(simrv::PteFlag::U) | std::to_underlying(simrv::PteFlag::A) |
            std::to_underlying(simrv::PteFlag::D);
        constexpr uint64_t kVsLeafFlags = kLeafFlags & ~std::to_underlying(simrv::PteFlag::U);
        const auto put = [&](simrv::xlen::Address address, uint64_t pte) {
            std::memcpy(ram.data() + (address - kDramBase), &pte, sizeof(pte));
        };

        // G-stage maps the first 2 MiB of GPA space to a distinct HPA range.
        put(kGRoot, ((kGMiddle >> 12U) << 10U) | kValidTable);
        put(kGMiddle, ((kHostBase >> 12U) << 10U) | kLeafFlags);
        // VS-stage page tables live in GPA space and map the test VA to a different GPA.
        put(kHostBase + kVsRoot, (uint64_t{2} << 10U) | kValidTable);
        put(kHostBase + kVsMiddle, (uint64_t{3} << 10U) | kValidTable);
        // VS-stage S-mode leaves must have U=0; the G-stage guest mapping above
        // deliberately has U=1 to grant the virtualized access.
        put(kHostBase + kVsLeaf + 8U, ((kGuestPa >> 12U) << 10U) | kVsLeafFlags);

        cpu.state().misa |= simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::H);
        cpu.state().virt = true;
        cpu.state().priv = simrv::xlen::PrivilegeLevel::Supervisor;
        cpu.state().satp = 0;
        cpu.state().vsatp = (uint64_t{8} << 60U) | (kVsRoot >> 12U);
        cpu.state().hgatp = (uint64_t{8} << 60U) | (kGRoot >> 12U);

        auto translated = cpu.translate_stage_address(
            machine, simrv::xlen::VirtAddr{kGuestVa}, simrv::PteAccess::Read,
            simrv::xlen::PrivilegeLevel::Supervisor, 64, simrv::memory::TlPort::Data,
            cpu.ca_state.data_walk);
        TEST_ASSERT(translated.has_value() && translated->has_value());
        TEST_ASSERT(translated->value().physical_address.raw() ==
                    kHostBase + kGuestPa + (kGuestVa & 0xFFFU));

        constexpr auto kPbmtEnable = uint64_t{1} << 62U;
        constexpr auto kPbmtNc = uint64_t{1} << 61U;
        constexpr auto kPbmtIo = uint64_t{2} << 61U;
        cpu.state().menvcfg |= kPbmtEnable;
        put(kGMiddle, ((kHostBase >> 12U) << 10U) | kLeafFlags | kPbmtNc);
        translated = cpu.translate_stage_address(
            machine, simrv::xlen::VirtAddr{kGuestVa}, simrv::PteAccess::Read,
            simrv::xlen::PrivilegeLevel::Supervisor, 64, simrv::memory::TlPort::Data,
            cpu.ca_state.data_walk);
        TEST_ASSERT(translated.has_value() && translated->has_value());
        TEST_ASSERT(translated->value().pbmt == simrv::PtePbmt::NonCacheable);

        // A non-default VS-stage PBMT overrides the intermediate G-stage type.
        cpu.state().henvcfg |= kPbmtEnable;
        put(kHostBase + kVsLeaf + 8U, ((kGuestPa >> 12U) << 10U) | kVsLeafFlags | kPbmtIo);
        translated = cpu.translate_stage_address(
            machine, simrv::xlen::VirtAddr{kGuestVa}, simrv::PteAccess::Read,
            simrv::xlen::PrivilegeLevel::Supervisor, 64, simrv::memory::TlPort::Data,
            cpu.ca_state.data_walk);
        TEST_ASSERT(translated.has_value() && translated->has_value());
        TEST_ASSERT(translated->value().pbmt == simrv::PtePbmt::Io);

        // G-stage PBMT is a page fault when menvcfg.PBMTE is clear.
        cpu.state().menvcfg &= ~kPbmtEnable;
        cpu.state().henvcfg &= ~kPbmtEnable;
        put(kHostBase + kVsLeaf + 8U, ((kGuestPa >> 12U) << 10U) | kVsLeafFlags);
        translated = cpu.translate_stage_address(
            machine, simrv::xlen::VirtAddr{kGuestVa}, simrv::PteAccess::Read,
            simrv::xlen::PrivilegeLevel::Supervisor, 64, simrv::memory::TlPort::Data,
            cpu.ca_state.data_walk);
        TEST_ASSERT(translated.has_value() && !translated->has_value());
        TEST_ASSERT(translated->error() ==
                    std::to_underlying(simrv::isa::ExceptionCode::GuestLoadPageFault));
    }
}

void test_zicntr_base_counter_values_and_access() {
    simrv::core::Machine machine;
    auto& cpu = machine.primary_hart();
    constexpr simrv::xlen::CsrNumber kCycle{0xC00};
    constexpr simrv::xlen::CsrNumber kTime{0xC01};
    constexpr simrv::xlen::CsrNumber kInstret{0xC02};
    constexpr simrv::xlen::CsrValue kCycleEnable = 1U;

    cpu.clint_mmio.mcycle = 0x1234;
    cpu.clint_mmio.mtime = 0x5678;
    cpu.state().minstret = 0x9ABC;
    const auto cycle = cpu.csr_file.read(kCycle);
    const auto time = cpu.csr_file.read(kTime);
    const auto instret = cpu.csr_file.read(kInstret);
    TEST_ASSERT(cycle.has_value() && *cycle == 0x1234);
    TEST_ASSERT(time.has_value() && *time == 0x5678);
    TEST_ASSERT(instret.has_value() && *instret == 0x9ABC);

    const auto decode_cycle_read = [&]() {
        auto& context = cpu.active_context();
        context = {};
        context.ir = (static_cast<uint32_t>(kCycle.value()) << 20U) | (2U << 12U) | (5U << 7U) |
                     0x73U;  // csrrs x5, cycle, x0
        context.ir_org = context.ir;
        context.op_id = simrv::isa::OperationId::ILLEGAL;
        cpu.run_decode_stage(machine);
    };

    cpu.state().priv = simrv::xlen::PrivilegeLevel::User;
    cpu.state().mcounteren = 0;
    cpu.state().scounteren = 0;
    decode_cycle_read();
    TEST_ASSERT(cpu.active_context().pending_exception ==
                simrv::isa::ExceptionCode::IllegalInstruction);

    cpu.state().mcounteren = kCycleEnable;
    decode_cycle_read();
    TEST_ASSERT(cpu.active_context().pending_exception ==
                simrv::isa::ExceptionCode::IllegalInstruction);

    cpu.state().scounteren = kCycleEnable;
    decode_cycle_read();
    TEST_ASSERT(!cpu.active_context().pending_exception.has_value());
    TEST_ASSERT(cpu.active_context().rcsr == 0x1234);

    cpu.state().priv = simrv::xlen::PrivilegeLevel::Supervisor;
    cpu.state().scounteren = 0;
    decode_cycle_read();
    TEST_ASSERT(!cpu.active_context().pending_exception.has_value());
    TEST_ASSERT(cpu.active_context().rcsr == 0x1234);
}

void test_fast_time_csr_polling_advances_between_batches() {
    if constexpr (simrv::xlen::kIsXLen64) {
        simrv::core::Machine machine;
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.memory().initialize_mmu();

        constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress.raw();
        constexpr uint32_t read_time_x5 = (0xC01U << 20U) | (2U << 12U) | (5U << 7U) | 0x73U;
        constexpr uint32_t read_time_x6 = (0xC01U << 20U) | (2U << 12U) | (6U << 7U) | 0x73U;
        constexpr std::array<uint32_t, 6> program = {
            read_time_x5,  // Initial time value.
            read_time_x6,  // Poll until the platform time changes.
            0x00138393U,   // addi x7, x7, 1 (bound the guest-side polling count).
            0xFE628CE3U,   // beq x5, x6, -8
            0x00100413U,   // addi x8, x0, 1 (observed time change).
            0x0000006FU,   // jal x0, 0
        };
        std::memcpy(ram.data(), program.data(), sizeof(program));

        auto& cpu = machine.primary_hart();
        cpu.state().pc = simrv::xlen::VirtAddr{pc};
        for (unsigned batch = 0; batch < 32 && cpu.state().regs.read(simrv::xlen::RegId::S0) == 0;
             ++batch) {
            TEST_ASSERT(machine.execute_fast_batch_for_testing(65536));
        }

        TEST_ASSERT(cpu.state().regs.read(simrv::xlen::RegId::S0) == 1);
        TEST_ASSERT(cpu.state().regs.read(simrv::xlen::RegId::T2) < 100);
    }
}

void test_fast_cycle_csr_polling_advances_between_batches() {
    if constexpr (simrv::xlen::kIsXLen64) {
        const auto run_polling_guest = [](bool appmode, bool collect_instruction_mix) {
            simrv::core::MachineConfig config{};
            config.execution.appmode = appmode;
            simrv::core::Machine machine(config);
            std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
            machine.set_ram_for_testing(ram.data(), ram.size());
            machine.memory().initialize_mmu();
            machine.set_instruction_mix_enabled(collect_instruction_mix);

            constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress.raw();
            constexpr uint32_t read_cycle_x5 = (0xC00U << 20U) | (2U << 12U) | (5U << 7U) | 0x73U;
            constexpr uint32_t read_cycle_x6 = (0xC00U << 20U) | (2U << 12U) | (6U << 7U) | 0x73U;
            constexpr std::array<uint32_t, 6> program = {
                read_cycle_x5,  // Initial cycle value.
                read_cycle_x6,  // Poll until batch-accounted cycles advance.
                0x00138393U,    // addi x7, x7, 1 (bound the guest-side polling count).
                0xFE628CE3U,    // beq x5, x6, -8
                0x00100413U,    // addi x8, x0, 1 (observed cycle change).
                0x0000006FU,    // jal x0, 0
            };
            std::memcpy(ram.data(), program.data(), sizeof(program));

            auto& cpu = machine.primary_hart();
            cpu.state().pc = simrv::xlen::VirtAddr{pc};
            for (unsigned batch = 0;
                 batch < 32 && cpu.state().regs.read(simrv::xlen::RegId::S0) == 0; ++batch) {
                TEST_ASSERT(machine.execute_fast_batch_for_testing(65536));
            }

            TEST_ASSERT(cpu.state().regs.read(simrv::xlen::RegId::S0) == 1);
            TEST_ASSERT(cpu.state().regs.read(simrv::xlen::RegId::T2) < 5);
        };

        run_polling_guest(/*appmode=*/true, /*collect_instruction_mix=*/false);
        // OsRunner's instrumented path executes cached instructions without the
        // ordinary CSR fallback, so verify that it flushes pending cycle state.
        run_polling_guest(/*appmode=*/false, /*collect_instruction_mix=*/true);
    }
}

void test_fast_mcountinhibit_matches_instruction_execution() {
    constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress.raw();
    constexpr std::array<uint32_t, 4> program = {
        0x00100293U,  // addi t0, zero, 1 (inhibit CY)
        0x32029073U,  // csrrw zero, mcountinhibit, t0
        0xB0002373U,  // csrrs t1, mcycle, zero
        0x0000006FU,  // jal zero, 0
    };
    const auto run_reference = [&]() {
        simrv::core::MachineConfig config{};
        config.execution.appmode = true;
        simrv::core::Machine machine(config);
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.memory().initialize_mmu();
        std::memcpy(ram.data(), program.data(), sizeof(program));
        auto& cpu = machine.primary_hart();
        cpu.clint_mmio.mcycle = 0;
        cpu.state().priv = simrv::xlen::PrivilegeLevel::Machine;
        cpu.state().pc = simrv::xlen::VirtAddr{pc};
        for (unsigned instruction = 0; instruction < 3; ++instruction) {
            machine.execute_cycle_for_testing();
        }
        return std::array<uint64_t, 2>{cpu.clint_mmio.mcycle,
                                       cpu.state().regs.read(simrv::xlen::RegId::T1)};
    };
    const auto reference = run_reference();
    TEST_ASSERT(reference[0] == 1 && reference[1] == 1);

    const auto run_guest = [&](bool appmode, bool collect_instruction_mix) {
        simrv::core::MachineConfig config{};
        config.execution.appmode = appmode;
        simrv::core::Machine machine(config);
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.memory().initialize_mmu();
        machine.set_instruction_mix_enabled(collect_instruction_mix);

        std::memcpy(ram.data(), program.data(), sizeof(program));

        auto& cpu = machine.primary_hart();
        cpu.clint_mmio.mcycle = 0;
        cpu.state().priv = simrv::xlen::PrivilegeLevel::Machine;
        cpu.state().pc = simrv::xlen::VirtAddr{pc};
        for (unsigned batch = 0; batch < 3; ++batch) {
            TEST_ASSERT(machine.execute_fast_batch_for_testing(4));
        }

        TEST_ASSERT(cpu.clint_mmio.mcycle == reference[0]);
        TEST_ASSERT(cpu.state().regs.read(simrv::xlen::RegId::T1) == reference[1]);
    };

    run_guest(/*appmode=*/true, /*collect_instruction_mix=*/false);
    run_guest(/*appmode=*/true, /*collect_instruction_mix=*/true);
}

void test_fast_mtime_mmio_matches_instruction_execution() {
    if constexpr (!simrv::xlen::kIsXLen64) return;
    constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress.raw();
    constexpr size_t kProgramSize = 24;
    const auto run = [&](bool fast_batch, bool collect_instruction_mix, bool guest_xlen64) {
        std::array<uint32_t, kProgramSize> program{};
        program[0] = 0x020082B7U;  // lui t0, 0x02008 (ACLINT mtime address after adjustment)
        program[1] = 0xFF828293U;  // addi t0, t0, -8
        const uint32_t load_mtime = guest_xlen64 ? 0x0002B303U : 0x0002A303U;
        program[2] = load_mtime;                                         // ld/lw t1, 0(t0)
        std::fill(program.begin() + 3, program.end() - 1, 0x00000013U);  // nop
        program.back() = guest_xlen64 ? 0x0002B403U : 0x0002A403U;       // ld/lw s0, 0(t0)

        simrv::core::MachineConfig config{};
        config.execution.appmode = true;
        simrv::core::Machine machine(config);
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.memory().initialize_mmu();
        machine.set_instruction_mix_enabled(collect_instruction_mix);
        simrv::device::AclintMtimer mtimer(&machine);
        TEST_ASSERT(machine.memory().system_bus().add_node(&mtimer));
        std::memcpy(ram.data(), program.data(), sizeof(program));

        auto& cpu = machine.primary_hart();
        cpu.state().regs.xlen = guest_xlen64 ? 64 : 32;
        cpu.state().priv = simrv::xlen::PrivilegeLevel::Machine;
        cpu.state().pc = simrv::xlen::VirtAddr{pc};
        for (size_t instruction = 0; instruction < program.size(); ++instruction) {
            machine.execute_cycle_for_testing();
        }
        simrv::xlen::Address decode_pc = pc;
        if constexpr (simrv::xlen::kIsXLen64) {
            if (!guest_xlen64) {
                decode_pc = static_cast<simrv::xlen::Address>(
                    static_cast<int64_t>(static_cast<int32_t>(pc)));
            }
        }
        TEST_ASSERT(cpu.decode_cache.lookup(simrv::xlen::VirtAddr{decode_pc}) != nullptr);
        cpu.state().regs.fill(0);
        cpu.e_icount = 0;
        cpu.e_ccount = 0;
        cpu.clint_mmio.mtime.store(1, std::memory_order_relaxed);
        cpu.clint_mmio.mcycle = 0;
        cpu.clint_mmio.rtc_divider = simrv::core::timing::kCyclesPerTimebaseTick - 2;
        cpu.state().priv = simrv::xlen::PrivilegeLevel::Machine;
        cpu.state().pc = simrv::xlen::VirtAddr{pc};
        if (fast_batch) {
            while (cpu.e_icount < program.size()) {
                const auto remaining = static_cast<uint32_t>(program.size() - cpu.e_icount);
                TEST_ASSERT(machine.execute_fast_batch_for_testing(remaining));
            }
        } else {
            for (size_t instruction = 0; instruction < program.size(); ++instruction) {
                machine.execute_cycle_for_testing();
            }
        }
        return std::array<uint64_t, 2>{cpu.state().regs.read(simrv::xlen::RegId::T1),
                                       cpu.state().regs.read(simrv::xlen::RegId::S0)};
    };

    const auto check_guest_width = [&](bool guest_xlen64) {
        const auto reference =
            run(/*fast_batch=*/false, /*collect_instruction_mix=*/false, guest_xlen64);
        const auto fast = run(/*fast_batch=*/true, /*collect_instruction_mix=*/false, guest_xlen64);
        const auto instrumented =
            run(/*fast_batch=*/true, /*collect_instruction_mix=*/true, guest_xlen64);
        TEST_ASSERT(reference[1] > reference[0]);
        TEST_ASSERT(fast == reference);
        TEST_ASSERT(instrumented == reference);
    };
    if constexpr (simrv::xlen::kIsXLen64) check_guest_width(true);
    check_guest_width(false);
}

void test_fast_vector_mtime_mmio_flushes_prior_cycles() {
    if constexpr (!simrv::xlen::kIsXLen64) return;

    constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress.raw();
    constexpr size_t kInstructionCount = 24;
    constexpr auto kVsetvliE64M1 = 0x0D807357U;  // vsetvli t1, zero, e64, m1, ta, ma
    constexpr auto kVle64V1T0 = 0x0202F087U;     // vle64.v v1, (t0)

    const auto run = [&](bool fast_batch, bool collect_instruction_mix) {
        simrv::core::MachineConfig config{};
        config.execution.appmode = true;
        simrv::core::Machine machine(config);
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.memory().initialize_mmu();
        machine.set_instruction_mix_enabled(collect_instruction_mix);
        simrv::device::AclintMtimer mtimer(&machine);
        TEST_ASSERT(machine.memory().system_bus().add_node(&mtimer));

        std::array<uint32_t, kInstructionCount> program{};
        program[0] = kVsetvliE64M1;
        program[1] = 0x020082B7U;  // lui t0, 0x02008
        program[2] = 0xFF828293U;  // addi t0, t0, -8 (ACLINT mtime)
        std::fill(program.begin() + 3, program.end() - 1, 0x00000013U);
        program.back() = kVle64V1T0;
        std::memcpy(ram.data(), program.data(), sizeof(program));

        auto& cpu = machine.primary_hart();
        cpu.state().misa = simrv::isa::misa_with_mxl(simrv::isa::kMisaDefault, 64);
        cpu.state().refresh_misa_status();
        cpu.state().mstatus |= simrv::core::MstatusBit::Vs;
        cpu.sync_decode_context();
        cpu.state().priv = simrv::xlen::PrivilegeLevel::Machine;
        cpu.state().pc = simrv::xlen::VirtAddr{pc};
        for (size_t i = 0; i < program.size(); ++i) machine.execute_cycle_for_testing();

        // Warm the decode cache, then rerun with mtime positioned one tick from rollover.
        cpu.state().regs.fill(0);
        cpu.state().regs.read_vector(simrv::xlen::VecRegId::V1).u64[0] = 0;
        cpu.e_icount = 0;
        cpu.e_ccount = 0;
        cpu.clint_mmio.mtime.store(1, std::memory_order_relaxed);
        cpu.clint_mmio.mcycle = 0;
        cpu.clint_mmio.rtc_divider = simrv::core::timing::kCyclesPerTimebaseTick - 2;
        cpu.state().priv = simrv::xlen::PrivilegeLevel::Machine;
        cpu.state().pc = simrv::xlen::VirtAddr{pc};

        if (fast_batch) {
            while (cpu.e_icount < program.size()) {
                TEST_ASSERT(machine.execute_fast_batch_for_testing(
                    static_cast<uint32_t>(program.size() - cpu.e_icount)));
            }
        } else {
            for (size_t i = 0; i < program.size(); ++i) machine.execute_cycle_for_testing();
        }
        return cpu.state().regs.read_vector(simrv::xlen::VecRegId::V1).u64[0];
    };

    const auto reference = run(/*fast_batch=*/false, /*collect_instruction_mix=*/false);
    const auto fast = run(/*fast_batch=*/true, /*collect_instruction_mix=*/false);
    const auto instrumented = run(/*fast_batch=*/true, /*collect_instruction_mix=*/true);
    TEST_ASSERT(reference > 1);
    TEST_ASSERT(fast == reference);
    TEST_ASSERT(instrumented == reference);
}

void test_zcb_decompression() {
    struct Encoding {
        uint16_t match;
        simrv::isa::OperationId operation;
    };
    constexpr std::array<Encoding, 12> kZcbInstructions{{
        {0x8000U, simrv::isa::OperationId::LBU},
        {0x8440U, simrv::isa::OperationId::LH},
        {0x8400U, simrv::isa::OperationId::LHU},
        {0x9c41U, simrv::isa::OperationId::MUL},
        {0x9c75U, simrv::isa::OperationId::XORI},
        {0x8800U, simrv::isa::OperationId::SB},
        {0x9c65U, simrv::isa::OperationId::SEXT_B},
        {0x9c6dU, simrv::isa::OperationId::SEXT_H},
        {0x8c00U, simrv::isa::OperationId::SH},
        {0x9c61U, simrv::isa::OperationId::ANDI},
        {0x9c69U, simrv::isa::OperationId::PACKW},
        {0x9c71U, simrv::isa::OperationId::ADD_UW},
    }};
    for (const auto& encoding : kZcbInstructions) {
        // Select x9 for rd'/rs1' and x10 for rs2'; immediate bits are checked below
        // on the byte/halfword memory encodings.
        uint16_t compressed = encoding.match | (1U << 7U);
        if (encoding.match == 0x8000U || encoding.match == 0x8440U || encoding.match == 0x8400U ||
            encoding.match == 0x8800U || encoding.match == 0x8c00U || encoding.match == 0x9c41U) {
            compressed |= 2U << 2U;
        }
        if (encoding.match == 0x8000U || encoding.match == 0x8800U) {
            compressed |= (1U << 5U) | (1U << 6U);
        } else if (encoding.match == 0x8440U || encoding.match == 0x8400U ||
                   encoding.match == 0x8c00U) {
            compressed |= 1U << 5U;
        }
        const auto expanded = simrv::pipeline::decompressInstruction(compressed, true);
        const auto decoded_operation = simrv::pipeline::decoder(expanded);
        if (decoded_operation != encoding.operation) {
            std::cerr << "Zcb encoding " << encoding.match << " expanded to " << expanded
                      << " and decoded as " << simrv::pipeline::operation_name(decoded_operation)
                      << std::endl;
        }
        TEST_ASSERT(decoded_operation == encoding.operation);
        const simrv::pipeline::Decoder decoded(expanded);
        if (encoding.match == 0x8000U || encoding.match == 0x8440U || encoding.match == 0x8400U) {
            TEST_ASSERT(decoded.rd() == simrv::xlen::RegId::A0);
            TEST_ASSERT(decoded.rs1() == simrv::xlen::RegId::S1);
        } else if (encoding.match == 0x8800U || encoding.match == 0x8c00U) {
            TEST_ASSERT(decoded.rs1() == simrv::xlen::RegId::S1);
            TEST_ASSERT(decoded.rs2() == simrv::xlen::RegId::A0);
        } else {
            TEST_ASSERT(decoded.rd() == simrv::xlen::RegId::S1);
            TEST_ASSERT(decoded.rs1() == simrv::xlen::RegId::S1);
        }
        if (encoding.match == 0x9c41U) {
            TEST_ASSERT(decoded.rs2() == simrv::xlen::RegId::A0);
        }
        if (encoding.match == 0x9c69U || encoding.match == 0x9c71U) {
            TEST_ASSERT(decoded.rs2() == simrv::xlen::RegId::Zero);
        }
        if (encoding.match == 0x8000U) {
            TEST_ASSERT(decoded.imm_i() == 3);
        }
        if (encoding.match == 0x8800U) TEST_ASSERT(decoded.imm_s() == 3);
        if (encoding.match == 0x8440U || encoding.match == 0x8400U) {
            TEST_ASSERT(decoded.imm_i() == 2);
        }
        if (encoding.match == 0x8c00U) {
            TEST_ASSERT(decoded.imm_s() == 2);
        }
    }

    // ZCB's C.ZEXT.W encoding is only defined for RV64.
    const auto rv32_zextw = simrv::pipeline::decompressInstruction(0x9c71U, false);
    TEST_ASSERT(rv32_zextw == 0U);
}

void test_zvbb_unary_decode() {
    using simrv::isa::OperationId;
    constexpr std::array<std::pair<uint32_t, OperationId>, 5> kUnaryInstructions{{
        {0x4b052057U, OperationId::VBREV_V},
        {0x4ad42057U, OperationId::VBREV8_V},
        {0x4b24a057U, OperationId::VREV8_V},
        {0x4be62057U, OperationId::VCLZ_V},
        {0x5696b1d7U, OperationId::VROR_VI},
    }};
    for (const auto& [instruction, expected] : kUnaryInstructions) {
        TEST_ASSERT(simrv::pipeline::decoder(instruction) == expected);
    }
}

void test_cbo_zero_execution() {
    using namespace simrv::isa;
    using namespace simrv::core;

    Machine machine;
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0xAA});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.memory().initialize_mmu();

    auto& cpu = machine.primary_hart();
    cpu.reset();

    constexpr size_t kLineBytes = simrv::cache::DCache::kLineBytes;
    constexpr simrv::xlen::PhysAddr target_paddr = simrv::memory::kDramBaseAddress + 0x1000;
    // Verify target buffer is filled with 0xAA
    const auto* ram_ptr = machine.ram_view().unchecked_ptr(target_paddr);
    for (size_t i = 0; i < kLineBytes; ++i) {
        TEST_ASSERT(ram_ptr[i] == static_cast<simrv::xlen::Byte>(0xAA));
    }

    // Execute cbo.zero on target_paddr
    simrv::memory::MemoryAccess::execute_cmo(machine.memory(), cpu,
                                             simrv::xlen::VirtAddr{(target_paddr + 16).raw()},
                                             OperationId::CBO_ZERO);

    // Check no pending exception
    TEST_ASSERT(!cpu.active_context().pending_exception.has_value());

    // Verify all bytes of cache block are zeroed
    for (size_t i = 0; i < kLineBytes; ++i) {
        TEST_ASSERT(ram_ptr[i] == static_cast<simrv::xlen::Byte>(0));
    }
    // Verify byte right after block is still 0xAA
    TEST_ASSERT(ram_ptr[kLineBytes] == static_cast<simrv::xlen::Byte>(0xAA));

    // Cache-block prefetches are non-faulting hints, including for inaccessible addresses.
    cpu.active_context().pending_exception.reset();
    constexpr auto kUnmappedAddress =
        std::numeric_limits<simrv::xlen::Address>::max() & ~simrv::xlen::Address{0xFFF};
    simrv::memory::MemoryAccess::execute_cmo(
        machine.memory(), cpu, simrv::xlen::VirtAddr{kUnmappedAddress}, OperationId::PREFETCH_R);
    TEST_ASSERT(!cpu.active_context().pending_exception.has_value());

    // A real cache-block operation to an inaccessible physical block raises a store access fault.
    simrv::memory::MemoryAccess::execute_cmo(
        machine.memory(), cpu, simrv::xlen::VirtAddr{kUnmappedAddress}, OperationId::CBO_ZERO);
    TEST_ASSERT(cpu.active_context().pending_exception == ExceptionCode::FaultStore);
    cpu.active_context().pending_exception.reset();

    // User pointer masking applies to cache-block operations as well as loads/stores.
    std::memset(machine.ram_view().unchecked_ptr(target_paddr), 0xAA, kLineBytes);
    cpu.state().priv = kPrivUser;
    cpu.state().senvcfg = (simrv::xlen::CsrValue{1} << 6U) | (simrv::xlen::CsrValue{1} << 7U);
#if SIMRV_XLEN == 64
    cpu.state().senvcfg |= simrv::xlen::CsrValue{2} << 32U;
#endif
    cpu.state().menvcfg |= (uint64_t{1} << 6U) | (uint64_t{1} << 7U);
#if SIMRV_XLEN == 64
    simrv::memory::MemoryAccess::execute_cmo(
        machine.memory(), cpu,
        simrv::xlen::VirtAddr{(target_paddr ^ (simrv::xlen::Address{1} << 63U)).raw()},
        OperationId::CBO_ZERO);
    TEST_ASSERT(!cpu.active_context().pending_exception.has_value());
    for (size_t i = 0; i < kLineBytes; ++i) {
        TEST_ASSERT(ram_ptr[i] == static_cast<simrv::xlen::Byte>(0));
    }
#endif

    // Test permission check: if menvcfg.CBZE = 0, supervisor cbo.zero must trap
    cpu.state().priv = kPrivSupervisor;
    cpu.state().menvcfg &= ~(1ULL << 7);  // Clear CBZE
    simrv::memory::MemoryAccess::execute_cmo(
        machine.memory(), cpu, simrv::xlen::VirtAddr{target_paddr.raw()}, OperationId::CBO_ZERO);
    TEST_ASSERT(cpu.active_context().pending_exception == ExceptionCode::IllegalInstruction);
}

void test_profile_engine() {
    using namespace simrv::isa;

    struct ProfileCase {
        ArchitectureProfile profile;
        std::string_view canonical;
        bool expect_b;
        bool expect_v;
    };
    constexpr std::array<ProfileCase, 10> kProfileCases = {{
        {.profile = ArchitectureProfile::Rvi20U32,
         .canonical = "rvi20u32",
         .expect_b = false,
         .expect_v = false},
        {.profile = ArchitectureProfile::Rvi20U64,
         .canonical = "rvi20u64",
         .expect_b = false,
         .expect_v = false},
        {.profile = ArchitectureProfile::Rva20U64,
         .canonical = "rva20u64",
         .expect_b = false,
         .expect_v = false},
        {.profile = ArchitectureProfile::Rva20S64,
         .canonical = "rva20s64",
         .expect_b = false,
         .expect_v = false},
        {.profile = ArchitectureProfile::Rva22U64,
         .canonical = "rva22u64",
         .expect_b = true,
         .expect_v = false},
        {.profile = ArchitectureProfile::Rva22S64,
         .canonical = "rva22s64",
         .expect_b = true,
         .expect_v = false},
        {.profile = ArchitectureProfile::Rva23U64,
         .canonical = "rva23u64",
         .expect_b = true,
         .expect_v = true},
        {.profile = ArchitectureProfile::Rva23S64,
         .canonical = "rva23s64",
         .expect_b = true,
         .expect_v = true},
        {.profile = ArchitectureProfile::Rvb23U64,
         .canonical = "rvb23u64",
         .expect_b = true,
         .expect_v = false},
        {.profile = ArchitectureProfile::Rvb23S64,
         .canonical = "rvb23s64",
         .expect_b = true,
         .expect_v = false},
    }};

    for (const auto& entry : kProfileCases) {
        TEST_ASSERT(to_profile_string(entry.profile) == entry.canonical);
        TEST_ASSERT(parse_architecture_profile(entry.canonical) == entry.profile);
        const auto* info = profile_info(entry.profile);
        TEST_ASSERT(info != nullptr && info->name == entry.canonical);
        const auto isa = parse_isa_spec(profile_default_isa_string(entry.profile));
        TEST_ASSERT(isa.has_value());
        TEST_ASSERT(isa->xlen == info->xlen);
        TEST_ASSERT(isa->has(IsaExtension::B) == entry.expect_b);
        TEST_ASSERT(isa->has(IsaExtension::V) == entry.expect_v);
    }

    TEST_ASSERT(parse_architecture_profile("rva23") == std::nullopt);
    TEST_ASSERT(parse_architecture_profile("rvb23") == std::nullopt);
    TEST_ASSERT(parse_architecture_profile("legacy") == std::nullopt);
    TEST_ASSERT(parse_architecture_profile("rv32gc") == std::nullopt);
    TEST_ASSERT(parse_architecture_profile("invalid_profile") == std::nullopt);
    TEST_ASSERT(!parse_isa_spec("rv64is").has_value());
    TEST_ASSERT(!parse_isa_spec("rv64iu").has_value());

    simrv::core::CPU misa_cpu;
    misa_cpu.reset();
    misa_cpu.state().supervisor_mode_supported = false;
    const auto misa_value = misa_cpu.csr_file.read(simrv::core::Csr::Misa);
    TEST_ASSERT(misa_value.has_value());
    TEST_ASSERT((*misa_value & misa_privilege_support_bit(MisaPrivilegeSupport::Supervisor)) == 0);
    TEST_ASSERT((*misa_value & misa_privilege_support_bit(MisaPrivilegeSupport::User)) != 0);

    if constexpr (simrv::xlen::kIsXLen64) {
        TEST_ASSERT(kDefaultArchitectureProfile == ArchitectureProfile::Rva23S64);
    } else {
        TEST_ASSERT(kDefaultArchitectureProfile == ArchitectureProfile::Rvi20U32);
    }

    // Verify ExtensionSet, aggregate shorthand expansion, and unified instruction_enabled() gating.
    ExtensionSet ext_set{};
    TEST_ASSERT(ext_set.empty());
    add_extension_to_set(ext_set, "zkn");
    TEST_ASSERT(!ext_set.empty());
    TEST_ASSERT(ext_set.has(MultiLetterExtension::Zbkb));
    TEST_ASSERT(ext_set.has(MultiLetterExtension::Zbkc));
    TEST_ASSERT(ext_set.has(MultiLetterExtension::Zknd));
    TEST_ASSERT(!ext_set.has(MultiLetterExtension::Zkr));

    const auto zfh_spec = parse_isa_spec("rv64gc_zfh_zvkn");
    TEST_ASSERT(zfh_spec.has_value());
    TEST_ASSERT(zfh_spec->has_extension(MultiLetterExtension::Zfh));
    TEST_ASSERT(zfh_spec->has_extension(MultiLetterExtension::Zfhmin));
    TEST_ASSERT(zfh_spec->has_extension(MultiLetterExtension::Zvkned));
    TEST_ASSERT(zfh_spec->has_extension(MultiLetterExtension::Zvknhb));
    TEST_ASSERT(zfh_spec->has_extension(MultiLetterExtension::Zvkb));
    TEST_ASSERT(zfh_spec->has_extension(MultiLetterExtension::Zvkt));

    const auto no_b_misa = parse_isa_spec("rv64gc")->misa_bits;
    TEST_ASSERT(!instruction_enabled(no_b_misa, ExtensionSet{}, OperationId::CLMUL));
    TEST_ASSERT(instruction_enabled(no_b_misa, ext_set, OperationId::CLMUL));
}

void test_track1a_zkr_svadu_sspm() {
    using simrv::core::Csr;
    using simrv::core::TrapController;
    using simrv::isa::ExceptionCode;
    using simrv::isa::MultiLetterExtension;

    simrv::core::Machine machine;
    auto& cpu = machine.primary_hart();
    auto& state = cpu.state();
    state.priv = simrv::xlen::PrivilegeLevel::Machine;
    state.virt = false;

    // 1. Zkr disabled -> IllegalInstruction
    state.enabled_extensions.clear(MultiLetterExtension::Zkr);
    TEST_ASSERT(TrapController::check_csr_access(state, simrv::xlen::CsrNumber{0x015}, true) ==
                ExceptionCode::IllegalInstruction);

    // 2. Zkr enabled -> read-only fails, read-write succeeds in M-mode, S/U obey mseccfg
    state.enabled_extensions.set(MultiLetterExtension::Zkr);
    TEST_ASSERT(TrapController::check_csr_access(state, simrv::xlen::CsrNumber{0x015}, false) ==
                ExceptionCode::IllegalInstruction);
    TEST_ASSERT(
        !TrapController::check_csr_access(state, simrv::xlen::CsrNumber{0x015}, true).has_value());

    const auto s1 = cpu.csr_file.read(Csr::Seed);
    TEST_ASSERT(s1.has_value());
    TEST_ASSERT(((*s1 >> 30U) & 0x3U) == 2U);  // OPST = ES16
    TEST_ASSERT(cpu.csr_file.write(Csr::Seed, 0).has_value());
    const auto s2 = cpu.csr_file.read(Csr::Seed);
    TEST_ASSERT(s2.has_value());
    TEST_ASSERT(*s2 != *s1);

    state.priv = simrv::xlen::PrivilegeLevel::Supervisor;
    state.mseccfg = 0;
    TEST_ASSERT(TrapController::check_csr_access(state, simrv::xlen::CsrNumber{0x015}, true) ==
                ExceptionCode::IllegalInstruction);
    state.mseccfg = static_cast<simrv::xlen::CsrValue>(1ULL << 9U);  // sseed
    TEST_ASSERT(
        !TrapController::check_csr_access(state, simrv::xlen::CsrNumber{0x015}, true).has_value());

    state.priv = simrv::xlen::PrivilegeLevel::User;
    TEST_ASSERT(TrapController::check_csr_access(state, simrv::xlen::CsrNumber{0x015}, true) ==
                ExceptionCode::IllegalInstruction);
    state.mseccfg = static_cast<simrv::xlen::CsrValue>(1ULL << 8U);  // useed
    TEST_ASSERT(
        !TrapController::check_csr_access(state, simrv::xlen::CsrNumber{0x015}, true).has_value());

    // 3. Svadu & Sspm menvcfg WARL behavior (RV64)
    state.priv = simrv::xlen::PrivilegeLevel::Machine;
    if constexpr (simrv::xlen::kIsXLen64) {
        const auto adue_bit = static_cast<simrv::xlen::CsrValue>(uint64_t{1} << 61U);
        const auto pmm_val = static_cast<simrv::xlen::CsrValue>(uint64_t{2} << 32U);
        state.enabled_extensions.clear(MultiLetterExtension::Svadu);
        state.enabled_extensions.clear(MultiLetterExtension::Sspm);
        TEST_ASSERT(cpu.csr_file.write(Csr::Menvcfg, adue_bit | pmm_val).has_value());
        TEST_ASSERT((*cpu.csr_file.read(Csr::Menvcfg) & (adue_bit | pmm_val)) == 0);

        state.enabled_extensions.set(MultiLetterExtension::Svadu);
        state.enabled_extensions.set(MultiLetterExtension::Sspm);
        TEST_ASSERT(cpu.csr_file.write(Csr::Menvcfg, adue_bit | pmm_val).has_value());
        TEST_ASSERT((*cpu.csr_file.read(Csr::Menvcfg) & (adue_bit | pmm_val)) ==
                    (adue_bit | pmm_val));
    }
}

[[maybe_unused]] void test_svpbmt_noncacheable_and_io_accesses() {
#if SIMRV_XLEN == 64
    {
        using simrv::isa::Funct3;
        using simrv::isa::Funct5Amo;
        using simrv::isa::Opcode;
        using simrv::xlen::PrivilegeLevel;

        simrv::core::Machine machine;
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.memory().initialize_mmu();
        auto& cpu = machine.primary_hart();
        cpu.reset();
        auto& state = cpu.state();
        state.initialize_lower_xlen_fields();
        state.menvcfg |= uint64_t{1} << 62U;  // Enable Svpbmt PTE memory-type interpretation.

        auto& amo_ctx = cpu.active_context();
        amo_ctx = {};
        amo_ctx.opcode = Opcode::Amo;
        amo_ctx.funct5 = Funct5Amo::Add;
        (void)simrv::memory::MemoryAccess::target_read(
            machine.memory(), cpu, simrv::xlen::VirtAddr{UINT64_C(0xFFFFFFFFFFFFF000)}, Funct3::Ld);
        TEST_ASSERT(amo_ctx.pending_exception == simrv::isa::ExceptionCode::FaultStore);

        constexpr simrv::xlen::Address kDram = simrv::memory::kDramBaseAddress.raw();
        constexpr simrv::xlen::Address kRoot = kDram + 0x1000;
        constexpr simrv::xlen::Address kLevel1 = kDram + 0x2000;
        constexpr simrv::xlen::Address kLevel0 = kDram + 0x3000;
        constexpr simrv::xlen::Address kPhysical = kDram + 0x8000;
        constexpr simrv::xlen::Address kVirtual = 0x40000;
        const auto write_pte = [&](simrv::xlen::Address address, uint64_t value) {
            std::memcpy(ram.data() + (address - kDram), &value, sizeof(value));
        };
        constexpr uint64_t kValid = 1U;
        constexpr uint64_t kLeaf = kValid | (uint64_t{1} << 1U) | (uint64_t{1} << 2U) |
                                   (uint64_t{1} << 6U) | (uint64_t{1} << 7U);
        write_pte(kRoot, ((kLevel1 >> 12U) << 10U) | kValid);
        write_pte(kLevel1, ((kLevel0 >> 12U) << 10U) | kValid);
        const auto leaf_address = kLevel0 + ((kVirtual >> 12U) & 0x1FFU) * 8U;
        const uint64_t leaf_base = ((kPhysical >> 12U) << 10U) | kLeaf;
        write_pte(leaf_address, leaf_base | (uint64_t{1} << 61U));  // PBMT=NC
        state.satp = (uint64_t{8} << 60U) | (kRoot >> 12U);
        state.priv = PrivilegeLevel::Supervisor;

        constexpr uint64_t kDirtyValue = UINT64_C(0xA5A5A5A5A5A5A5A5);
        std::array<simrv::xlen::Byte, simrv::cache::DCache::kLineBytes> dirty_line{};
        dirty_line.fill(simrv::xlen::Byte{0xA5});
        const auto line_base = kPhysical & ~(simrv::cache::DCache::kLineBytes - 1U);
        auto& bus = machine.memory().system_bus();
        simrv::memory::TlChannelA acquire{};
        acquire.opcode = simrv::memory::TlOpcodeA::AcquireBlock;
        acquire.grow = simrv::memory::TlGrow::NtoT;
        acquire.source = 1;
        acquire.hart = cpu.hart_id();
        acquire.address = simrv::xlen::PhysAddr{line_base};
        acquire.size = simrv::memory::kTlBlockSize;
        acquire.mask = simrv::memory::TlChannelA::compute_mask(acquire.size, acquire.address);
        simrv::memory::TlChannelD grant{};
        std::array<simrv::xlen::Byte, simrv::memory::CoherenceHub::kLineBytes> acquired_line{};
        TEST_ASSERT(bus.acquire_block(acquire, grant, acquired_line));
        bus.grant_ack(simrv::memory::TlChannelE{.sink = grant.sink});
        cpu.dcache.insert(simrv::xlen::PhysAddr{line_base}, acquired_line.data(),
                          simrv::memory::MesiState::Exclusive);
        for (size_t i = 0; i < dirty_line.size(); ++i) {
            TEST_ASSERT(cpu.dcache.write(simrv::xlen::PhysAddr{line_base + i}, 0xA5U, Funct3::Sb));
        }
        machine.memory().system_bus().mark_modified(simrv::xlen::PhysAddr{line_base},
                                                    cpu.hart_id());

        auto& ctx = cpu.active_context();
        ctx = {};
        ctx.opcode = Opcode::Load;
        const auto reads_before = machine.memory().system_bus().read_count();
        const auto misses_before = cpu.dcache.miss_count();
        const auto loaded = simrv::memory::MemoryAccess::target_read(
            machine.memory(), cpu, simrv::xlen::VirtAddr{kVirtual}, Funct3::Ld);
        TEST_ASSERT(!ctx.pending_exception.has_value());
        TEST_ASSERT(loaded == kDirtyValue);
        TEST_ASSERT(machine.memory().system_bus().read_count() > reads_before);
        TEST_ASSERT(cpu.dcache.miss_count() == misses_before);
        TEST_ASSERT(cpu.dcache.line_state(simrv::xlen::PhysAddr{line_base}) ==
                    simrv::memory::MesiState::Invalid);
        const auto* read_entry =
            cpu.tlb.lookup_data_r(simrv::xlen::VirtAddr{kVirtual}, 0, PrivilegeLevel::Supervisor);
        TEST_ASSERT(read_entry != nullptr && read_entry->pbmt == simrv::PtePbmt::NonCacheable);

        ctx = {};
        ctx.opcode = Opcode::Store;
        constexpr uint64_t kNcStore = UINT64_C(0x1122334455667788);
        const auto writes_before = machine.memory().system_bus().write_count();
        simrv::memory::MemoryAccess::target_write(
            machine.memory(), cpu, simrv::xlen::VirtAddr{kVirtual}, kNcStore, Funct3::Sd);
        TEST_ASSERT(!ctx.pending_exception.has_value());
        TEST_ASSERT(machine.memory().system_bus().write_count() > writes_before);
        TEST_ASSERT(cpu.dcache.line_state(simrv::xlen::PhysAddr{line_base}) ==
                    simrv::memory::MesiState::Invalid);
        uint64_t ram_value = 0;
        std::memcpy(&ram_value, ram.data() + (kPhysical - kDram), sizeof(ram_value));
        TEST_ASSERT(ram_value == kNcStore);

        ctx = {};
        ctx.opcode = Opcode::Amo;
        ctx.funct5 = Funct5Amo::Add;
        const auto nc_amo_reads_before = machine.memory().system_bus().read_count();
        (void)simrv::memory::MemoryAccess::target_read(machine.memory(), cpu,
                                                       simrv::xlen::VirtAddr{kVirtual}, Funct3::Ld);
        TEST_ASSERT(ctx.pending_exception == simrv::isa::ExceptionCode::FaultStore);
        TEST_ASSERT(machine.memory().system_bus().read_count() == nc_amo_reads_before);

        write_pte(leaf_address, leaf_base | (uint64_t{2} << 61U));  // PBMT=IO
        cpu.flush_data_tlb();
        ctx = {};
        ctx.opcode = Opcode::Load;
        const auto io_reads_before = machine.memory().system_bus().read_count();
        const auto io_loaded = simrv::memory::MemoryAccess::target_read(
            machine.memory(), cpu, simrv::xlen::VirtAddr{kVirtual}, Funct3::Ld);
        TEST_ASSERT(!ctx.pending_exception.has_value());
        TEST_ASSERT(io_loaded == kNcStore);
        TEST_ASSERT(machine.memory().system_bus().read_count() > io_reads_before);
        read_entry =
            cpu.tlb.lookup_data_r(simrv::xlen::VirtAddr{kVirtual}, 0, PrivilegeLevel::Supervisor);
        TEST_ASSERT(read_entry != nullptr && read_entry->pbmt == simrv::PtePbmt::Io);

        ctx = {};
        ctx.opcode = Opcode::Store;
        constexpr uint64_t kIoStore = UINT64_C(0x8877665544332211);
        const auto io_writes_before = machine.memory().system_bus().write_count();
        simrv::memory::MemoryAccess::target_write(
            machine.memory(), cpu, simrv::xlen::VirtAddr{kVirtual}, kIoStore, Funct3::Sd);
        TEST_ASSERT(!ctx.pending_exception.has_value());
        TEST_ASSERT(machine.memory().system_bus().write_count() > io_writes_before);
        std::memcpy(&ram_value, ram.data() + (kPhysical - kDram), sizeof(ram_value));
        TEST_ASSERT(ram_value == kIoStore);

        ctx = {};
        ctx.opcode = Opcode::Amo;
        ctx.funct5 = Funct5Amo::Lr;
        const auto io_amo_reads_before = machine.memory().system_bus().read_count();
        (void)simrv::memory::MemoryAccess::target_read(machine.memory(), cpu,
                                                       simrv::xlen::VirtAddr{kVirtual}, Funct3::Ld);
        TEST_ASSERT(ctx.pending_exception == simrv::isa::ExceptionCode::FaultLoad);
        TEST_ASSERT(machine.memory().system_bus().read_count() == io_amo_reads_before);

        ctx = {};
        ctx.opcode = Opcode::Amo;
        ctx.funct5 = Funct5Amo::Sc;
        const auto io_amo_writes_before = machine.memory().system_bus().write_count();
        simrv::memory::MemoryAccess::target_write(
            machine.memory(), cpu, simrv::xlen::VirtAddr{kVirtual}, kIoStore, Funct3::Sd);
        TEST_ASSERT(ctx.pending_exception == simrv::isa::ExceptionCode::FaultStore);
        TEST_ASSERT(machine.memory().system_bus().write_count() == io_amo_writes_before);
    }
#endif
}

[[maybe_unused]] void test_svpbmt_io_access_ordering() {
#if SIMRV_XLEN == 64
    {
        using simrv::xlen::PrivilegeLevel;

        simrv::core::Machine machine;
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.memory().initialize_mmu();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;

        auto& cpu = machine.primary_hart();
        cpu.reset();
        auto& state = cpu.state();
        state.initialize_lower_xlen_fields();
        state.priv = PrivilegeLevel::Supervisor;

        constexpr simrv::xlen::Address kDram = simrv::memory::kDramBaseAddress.raw();
        constexpr simrv::xlen::Address kCodeVirtual = 0x10000;
        constexpr simrv::xlen::Address kIoVirtual = 0x40000;
        constexpr simrv::xlen::Address kPmaVirtual = 0x50000;
        constexpr simrv::xlen::Address kIoPhysical = kDram + 0x8000;
        constexpr simrv::xlen::Address kPmaPhysical = kDram + 0x9000;
        state.satp = uint64_t{8} << 60U;
        state.pc = simrv::xlen::VirtAddr{kCodeVirtual};
        cpu.state().regs.write(simrv::xlen::RegId::Ra, kIoVirtual);
        cpu.state().regs.write(simrv::xlen::RegId::Sp, UINT64_C(0x1122334455667788));
        cpu.state().regs.write(simrv::xlen::RegId::Gp, UINT64_C(0x8877665544332211));
        cpu.state().regs.write(simrv::xlen::RegId::Tp, kPmaVirtual);
        cpu.state().regs.write(simrv::xlen::RegId::T0, UINT64_C(0x123456789ABCDEF0));

        constexpr std::array<uint32_t, 6> kProgram{
            0x0020B023U,  // sd sp, 0(ra)
            0x0FF0000FU,  // fence iorw, iorw
            0x00323023U,  // sd gp, 0(tp)
            0x0FF0000FU,  // fence iorw, iorw
            0x0050B823U,  // sd t0, 16(ra)
            0x0000006FU,  // jal zero, 0
        };
        std::memcpy(ram.data(), kProgram.data(), sizeof(kProgram));
        std::array<simrv::xlen::Byte, simrv::cache::ICache::kLineBytes> instruction_line{};
        std::memcpy(instruction_line.data(), kProgram.data(), sizeof(kProgram));
        cpu.icache.insert(simrv::xlen::PhysAddr{kDram}, instruction_line.data(),
                          simrv::memory::MesiState::Exclusive);

        cpu.tlb.insert_inst_r(simrv::xlen::VirtAddr{kCodeVirtual}, simrv::xlen::PhysAddr{kDram}, 0,
                              PrivilegeLevel::Supervisor);
        cpu.tlb.insert_data_w(simrv::xlen::VirtAddr{kIoVirtual}, simrv::xlen::PhysAddr{kIoPhysical},
                              0, PrivilegeLevel::Supervisor, false, simrv::memory::PtePbmt::Io);
        cpu.tlb.insert_data_w(simrv::xlen::VirtAddr{kPmaVirtual},
                              simrv::xlen::PhysAddr{kPmaPhysical}, 0, PrivilegeLevel::Supervisor);

        for (unsigned cycle = 0; cpu.e_icount < 5 && cycle < 256; ++cycle) {
            cpu.run_cycle(machine);
            machine.memory().system_bus().advance_cycle();
        }
        TEST_ASSERT(cpu.e_icount >= 5);

        size_t first_io_store = std::numeric_limits<size_t>::max();
        size_t pma_store = std::numeric_limits<size_t>::max();
        size_t second_io_store = std::numeric_limits<size_t>::max();
        size_t transaction_index = 0;
        for (const auto& transaction : machine.memory().system_bus().transaction_history()) {
            if (transaction.channel == TileLinkChannel::A) {
                if (transaction.opcode == "PutFullData" && transaction.address == kIoPhysical) {
                    first_io_store = std::min(first_io_store, transaction_index);
                } else if (transaction.opcode == "AcquireBlock" &&
                           transaction.address == kPmaPhysical) {
                    pma_store = std::min(pma_store, transaction_index);
                } else if (transaction.opcode == "PutFullData" &&
                           transaction.address == kIoPhysical + 16U) {
                    second_io_store = std::min(second_io_store, transaction_index);
                }
            }
            ++transaction_index;
        }
        TEST_ASSERT(first_io_store < pma_store);
        TEST_ASSERT(pma_store < second_io_store);
        TEST_ASSERT(
            cpu.tlb.lookup_data_w(simrv::xlen::VirtAddr{kIoVirtual}, 0, PrivilegeLevel::Supervisor)
                ->pbmt == simrv::memory::PtePbmt::Io);
    }
#endif
}

void test_sha_and_ssnpm_audit_regressions() {
#if SIMRV_XLEN == 64
    using simrv::core::Csr;
    using simrv::core::Machine;
    using simrv::core::MstatusBit;
    using simrv::isa::ExceptionCode;
    using simrv::isa::Funct3;
    using simrv::isa::IsaExtension;
    using simrv::isa::misa_extension_bit;
    using simrv::isa::OperationId;
    using simrv::xlen::Address;
    using simrv::xlen::Byte;
    using simrv::xlen::CsrValue;
    using simrv::xlen::PrivilegeLevel;
    using simrv::xlen::Register;

    // 1. hgatp WARL must reject unimplemented Sv48x4 (9) / Sv57x4 (10) modes.
    {
        Machine machine;
        auto& cpu = machine.primary_hart();
        cpu.reset();
        cpu.state().misa |= misa_extension_bit(IsaExtension::H);
        constexpr uint64_t kSv39x4 = (uint64_t{8} << 60U) | 4U;
        TEST_ASSERT(cpu.csr_file.write(Csr::Hgatp, kSv39x4).has_value());
        TEST_ASSERT(cpu.state().hgatp == kSv39x4);
        TEST_ASSERT(cpu.csr_file.write(Csr::Hgatp, (uint64_t{9} << 60U) | 5U).has_value());
        TEST_ASSERT(cpu.state().hgatp == kSv39x4);
        TEST_ASSERT(cpu.csr_file.write(Csr::Hgatp, (uint64_t{10} << 60U) | 6U).has_value());
        TEST_ASSERT(cpu.state().hgatp == kSv39x4);
    }

    // 2. VS-Bare + G-stage must keep translation active in CPU decode context and data helpers.
    {
        Machine machine;
        auto& cpu = machine.primary_hart();
        cpu.reset();
        cpu.state().misa |= misa_extension_bit(IsaExtension::H);
        cpu.state().hgatp = (uint64_t{8} << 60U) | 0x10U;
        cpu.state().vsatp = 0;
        cpu.state().satp = 0;
        cpu.state().priv = PrivilegeLevel::Supervisor;
        cpu.state().virt = false;
        TEST_ASSERT(!cpu.fetch_translation_active());
        TEST_ASSERT(!cpu.data_translation_active());

        cpu.state().virt = true;
        TEST_ASSERT(cpu.fetch_translation_active());
        TEST_ASSERT(cpu.data_translation_active());
        TEST_ASSERT((cpu.compute_decode_context() & 0x04U) != 0);

        cpu.state().virt = false;
        cpu.state().priv = PrivilegeLevel::Machine;
        cpu.state().mstatus = simrv::core::mstatus_with_mpp(MstatusBit::Mprv | MstatusBit::Mpv,
                                                            PrivilegeLevel::Supervisor);
        TEST_ASSERT(cpu.effective_data_virt());
        TEST_ASSERT(!cpu.fetch_translation_active());
        TEST_ASSERT(cpu.data_translation_active());
        TEST_ASSERT((cpu.compute_decode_context() & 0x04U) != 0);
    }

    // 3. VS-stage Svade A/D fault takes precedence over G-stage fault on the translated GPA.
    {
        Machine machine;
        std::vector<Byte> ram(0x40000, Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.memory().initialize_mmu();
        auto& cpu = machine.primary_hart();
        cpu.reset();
        cpu.state().misa |= misa_extension_bit(IsaExtension::H);
        const Address kBase = machine.memory_geometry().dram_base.raw();
        constexpr Address kVsRoot = 0x1000;
        constexpr Address kVsMid = 0x2000;
        constexpr Address kVsLeaf = 0x3000;
        const Address kGRoot = kBase + 0x4000;
        const Address kGMid = kBase + 0x5000;
        const auto put = [&](Address addr, uint64_t val) {
            std::memcpy(ram.data() + (addr - kBase), &val, sizeof(val));
        };
        constexpr uint64_t kValid = std::to_underlying(simrv::PteFlag::V);
        const uint64_t g_leaf =
            kValid | std::to_underlying(simrv::PteFlag::R) | std::to_underlying(simrv::PteFlag::W) |
            std::to_underlying(simrv::PteFlag::X) | std::to_underlying(simrv::PteFlag::U) |
            std::to_underlying(simrv::PteFlag::A) | std::to_underlying(simrv::PteFlag::D);
        put(kGRoot, ((kGMid >> 12U) << 10U) | kValid);
        put(kGMid, ((kBase >> 12U) << 10U) | g_leaf);
        // VS-stage leaf maps VA 0x1000 to an unmapped GPA with A=0 (Svade fault).
        put(kBase + kVsRoot, (uint64_t{2} << 10U) | kValid);
        put(kBase + kVsMid, (uint64_t{3} << 10U) | kValid);
        put(kBase + kVsLeaf + 8U,
            (uint64_t{0x90000} << 10U) | kValid | std::to_underlying(simrv::PteFlag::R));

        cpu.state().virt = true;
        cpu.state().priv = PrivilegeLevel::Supervisor;
        cpu.state().vsatp = (uint64_t{8} << 60U) | (kVsRoot >> 12U);
        cpu.state().hgatp = (uint64_t{8} << 60U) | (kGRoot >> 12U);
        simrv::memory::GuestPhysAddr fault_gpa{};
        const auto res = machine.memory().mmu()->translate(
            simrv::xlen::VirtAddr{0x1000}, simrv::PteAccess::Read, PrivilegeLevel::Supervisor, 0,
            cpu.state().vsatp, 64, false, &cpu.state(), &fault_gpa);
        TEST_ASSERT(!res.has_value());
        TEST_ASSERT(res.error() == std::to_underlying(ExceptionCode::LoadPageFault));
    }

    // 4. H-mode CMO permission checks raise VirtualInstruction for henvcfg/VS senvcfg.
    {
        Machine machine;
        std::vector<Byte> ram(0x20000, Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.memory().initialize_mmu();
        auto& cpu = machine.primary_hart();
        cpu.reset();
        cpu.state().misa |= misa_extension_bit(IsaExtension::H);
        const Address target = machine.memory_geometry().dram_base.raw() + 0x1000;
        cpu.state().virt = true;
        cpu.state().priv = PrivilegeLevel::Supervisor;
        cpu.state().menvcfg = (CsrValue{1} << 7U) | (CsrValue{1} << 6U) | (CsrValue{3} << 4U);
        cpu.state().henvcfg = 0;
        cpu.state().senvcfg = (CsrValue{1} << 7U) | (CsrValue{1} << 6U) | (CsrValue{3} << 4U);

        for (const auto op :
             {OperationId::CBO_ZERO, OperationId::CBO_CLEAN, OperationId::CBO_INVAL}) {
            cpu.active_context().pending_exception.reset();
            simrv::memory::MemoryAccess::execute_cmo(machine.memory(), cpu,
                                                     simrv::xlen::VirtAddr{target}, op);
            TEST_ASSERT(cpu.active_context().pending_exception ==
                        ExceptionCode::VirtualInstruction);
        }

        cpu.state().henvcfg = cpu.state().menvcfg;
        cpu.state().senvcfg = 0;
        cpu.state().priv = PrivilegeLevel::User;
        cpu.active_context().pending_exception.reset();
        simrv::memory::MemoryAccess::execute_cmo(
            machine.memory(), cpu, simrv::xlen::VirtAddr{target}, OperationId::CBO_ZERO);
        TEST_ASSERT(cpu.active_context().pending_exception == ExceptionCode::VirtualInstruction);

        cpu.state().menvcfg = 0;
        cpu.active_context().pending_exception.reset();
        simrv::memory::MemoryAccess::execute_cmo(
            machine.memory(), cpu, simrv::xlen::VirtAddr{target}, OperationId::CBO_ZERO);
        TEST_ASSERT(cpu.active_context().pending_exception == ExceptionCode::IllegalInstruction);
    }

    // 5. VS-mode henvcfg.PMM=2 pointer masking works even with stale HLV op_id and respects MXR.
    {
        Machine machine;
        std::vector<Byte> ram(0x20000, Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.memory().initialize_mmu();
        auto& cpu = machine.primary_hart();
        cpu.reset();
        cpu.state().misa |= misa_extension_bit(IsaExtension::H);
        const Address kBase = machine.memory_geometry().dram_base.raw();
        const Address kDataAddr = kBase + 0x2000;
        const Address kCodeAddr = kBase + 0x1000;
        constexpr uint64_t kSentinel = UINT64_C(0x1122334455667788);
        std::memcpy(ram.data() + 0x2000, &kSentinel, sizeof(kSentinel));
        const uint32_t ld_t0_a0 = (0x000U << 20U) | (10U << 15U) | (3U << 12U) | (5U << 7U) | 0x03U;
        std::memcpy(ram.data() + 0x1000, &ld_t0_a0, sizeof(ld_t0_a0));

        cpu.state().virt = true;
        cpu.state().priv = PrivilegeLevel::Supervisor;
        cpu.state().henvcfg = CsrValue{2} << 32U;
        cpu.state().pc = simrv::xlen::VirtAddr{kCodeAddr};
        cpu.state().regs.write(simrv::xlen::RegId::A0, kDataAddr | (uint64_t{0x2A} << 57U));
        cpu.active_context().op_id = OperationId::HLV_W;  // Stale HLV op from an earlier fallback.
        TEST_ASSERT(cpu.data_pointer_masking_active());

        machine.runtime_profile.engine = simrv::core::ExecutionEngine::InstructionFast;
        cpu.run_cycle(machine);
        TEST_ASSERT(cpu.state().regs.read(simrv::xlen::RegId::T0) == kSentinel);

        cpu.state().vsstatus |= MstatusBit::Mxr;
        cpu.active_context().pending_exception.reset();
        cpu.active_context().op_id = OperationId::LD;
        (void)simrv::memory::MemoryAccess::target_read(
            machine.memory(), cpu, simrv::xlen::VirtAddr{kDataAddr | (uint64_t{0x2A} << 57U)},
            Funct3::Ld);
        TEST_ASSERT(cpu.active_context().pending_exception == ExceptionCode::FaultLoad);
    }
#endif
}

void test_track1b_zacas_zabha() {
    using namespace simrv::isa;
    using namespace simrv::core;
    using simrv::xlen::CsrValue;
    auto encode_amo = [](uint32_t funct5, uint32_t f3, uint32_t rs2, uint32_t rs1,
                         uint32_t rd) -> uint32_t {
        return (funct5 << 27U) | (rs2 << 20U) | (rs1 << 15U) | (f3 << 12U) | (rd << 7U) | 0x2FU;
    };

    // 1. Decode & instruction_enabled gating
    {
        const uint32_t amoadd_b = encode_amo(0x00, 0, 11, 10, 12);
        const uint32_t amoswap_h = encode_amo(0x01, 1, 11, 10, 12);
        const uint32_t amocas_b = encode_amo(0x05, 0, 11, 10, 12);
        const uint32_t amocas_w = encode_amo(0x05, 2, 11, 10, 12);
        const uint32_t amocas_d = encode_amo(0x05, 3, 12, 10, 14);
        const uint32_t amocas_q = encode_amo(0x05, 4, 12, 10, 14);

        TEST_ASSERT(simrv::pipeline::decoder(amoadd_b) == OperationId::AMOADD_B);
        TEST_ASSERT(simrv::pipeline::decoder(amoswap_h) == OperationId::AMOSWAP_H);
        TEST_ASSERT(simrv::pipeline::decoder(amocas_b) == OperationId::AMOCAS_B);
        TEST_ASSERT(simrv::pipeline::decoder(amocas_w) == OperationId::AMOCAS_W);
        TEST_ASSERT(simrv::pipeline::decoder(amocas_d) == OperationId::AMOCAS_D);
        if constexpr (simrv::xlen::kIsXLen64) {
            TEST_ASSERT(simrv::pipeline::decoder(amocas_q) == OperationId::AMOCAS_Q);
            // Odd register pair must decode as ILLEGAL for amocas.q
            TEST_ASSERT(simrv::pipeline::decoder(encode_amo(0x05, 4, 13, 10, 14)) ==
                        OperationId::ILLEGAL);
            TEST_ASSERT(simrv::pipeline::decoder(encode_amo(0x05, 4, 12, 10, 15)) ==
                        OperationId::ILLEGAL);
        } else {
            TEST_ASSERT(simrv::pipeline::decoder(amocas_q) == OperationId::ILLEGAL);
            // Odd register pair must decode as ILLEGAL for amocas.d on RV32
            TEST_ASSERT(simrv::pipeline::decoder(encode_amo(0x05, 3, 13, 10, 14)) ==
                        OperationId::ILLEGAL);
            TEST_ASSERT(simrv::pipeline::decoder(encode_amo(0x05, 3, 12, 10, 15)) ==
                        OperationId::ILLEGAL);
        }

        const auto misa_a =
            misa_extension_bit(IsaExtension::I) | misa_extension_bit(IsaExtension::A);
        ExtensionSet none{};
        ExtensionSet zabha_only{};
        zabha_only.set(MultiLetterExtension::Zabha);
        ExtensionSet zacas_only{};
        zacas_only.set(MultiLetterExtension::Zacas);
        ExtensionSet both{};
        both.set(MultiLetterExtension::Zabha);
        both.set(MultiLetterExtension::Zacas);

        TEST_ASSERT(!instruction_enabled(misa_a, none, OperationId::AMOADD_B));
        TEST_ASSERT(instruction_enabled(misa_a, zabha_only, OperationId::AMOADD_B));
        TEST_ASSERT(!instruction_enabled(misa_a, none, OperationId::AMOCAS_W));
        TEST_ASSERT(instruction_enabled(misa_a, zacas_only, OperationId::AMOCAS_W));
        TEST_ASSERT(!instruction_enabled(misa_a, zabha_only, OperationId::AMOCAS_B));
        TEST_ASSERT(!instruction_enabled(misa_a, zacas_only, OperationId::AMOCAS_B));
        TEST_ASSERT(instruction_enabled(misa_a, both, OperationId::AMOCAS_B));
    }

    // 2. Execution positive + negative tests
    {
        constexpr simrv::xlen::Address kCodeAddr = 0x80001000ULL;
        constexpr simrv::xlen::Address kDataAddr = 0x80002000ULL;

        std::array<simrv::xlen::Byte, 0x4000> ram{};
        Machine machine;
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.memory().initialize_mmu();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::InstructionFast;

        auto& cpu = machine.primary_hart();
        cpu.state().priv = simrv::xlen::PrivilegeLevel::Machine;
        cpu.state().misa |= misa_extension_bit(IsaExtension::A);
        cpu.state().mtvec = 0x80003000ULL;

        // Negative: amoadd.b with Zabha disabled -> IllegalInstruction
        const uint32_t amoadd_b = encode_amo(0x00, 0, 11, 10, 12);
        std::memcpy(ram.data() + 0x1000, &amoadd_b, sizeof(amoadd_b));
        cpu.state().pc = simrv::xlen::VirtAddr{kCodeAddr};
        cpu.state().regs.write(simrv::xlen::RegId::A0, kDataAddr);
        cpu.state().regs.write(simrv::xlen::RegId::A1, 5);
        cpu.state().enabled_extensions = ExtensionSet{};
        cpu.run_cycle(machine);
        TEST_ASSERT(cpu.state().mcause == static_cast<CsrValue>(ExceptionCode::IllegalInstruction));

        // Negative: amocas.w with Zacas disabled -> IllegalInstruction
        const uint32_t amocas_w = encode_amo(0x05, 2, 11, 10, 12);
        std::memcpy(ram.data() + 0x1004, &amocas_w, sizeof(amocas_w));
        cpu.state().pc = simrv::xlen::VirtAddr{kCodeAddr + 4};
        cpu.state().mcause = 0;
        cpu.run_cycle(machine);
        TEST_ASSERT(cpu.state().mcause == static_cast<CsrValue>(ExceptionCode::IllegalInstruction));

        // Enable both Zabha and Zacas
        cpu.state().enabled_extensions.set(MultiLetterExtension::Zabha);
        cpu.state().enabled_extensions.set(MultiLetterExtension::Zacas);

        // Zabha: amoadd.b (mem = 0xFE -> -2, add 5 -> mem = 0x03, rd = sign_ext(-2))
        ram[0x2000] = std::byte{0xFEU};
        std::memcpy(ram.data() + 0x1008, &amoadd_b, sizeof(amoadd_b));
        cpu.state().pc = simrv::xlen::VirtAddr{kCodeAddr + 8};
        cpu.state().regs.write(simrv::xlen::RegId::A0, kDataAddr);
        cpu.state().regs.write(simrv::xlen::RegId::A1, 5);
        cpu.state().regs.write(simrv::xlen::RegId::A2, 0);
        cpu.run_cycle(machine);
        TEST_ASSERT(ram[0x2000] == std::byte{0x03U});
        TEST_ASSERT(static_cast<simrv::xlen::SignedWord>(
                        cpu.state().regs.read(simrv::xlen::RegId::A2)) == -2);

        // Zabha: amomin.h (mem = 0xFFF0 (-16), rs2 = 0x0010 (+16) -> mem = 0xFFF0, rd = -16)
        const uint32_t amomin_h = encode_amo(0x10, 1, 11, 10, 12);
        const uint16_t init_h = 0xFFF0U;
        std::memcpy(ram.data() + 0x2002, &init_h, sizeof(init_h));
        std::memcpy(ram.data() + 0x100C, &amomin_h, sizeof(amomin_h));
        cpu.state().pc = simrv::xlen::VirtAddr{kCodeAddr + 0x0C};
        cpu.state().regs.write(simrv::xlen::RegId::A0, kDataAddr + 2);
        cpu.state().regs.write(simrv::xlen::RegId::A1, 0x0010U);
        cpu.run_cycle(machine);
        uint16_t out_h = 0;
        std::memcpy(&out_h, ram.data() + 0x2002, sizeof(out_h));
        TEST_ASSERT(out_h == 0xFFF0U);
        TEST_ASSERT(static_cast<simrv::xlen::SignedWord>(
                        cpu.state().regs.read(simrv::xlen::RegId::A2)) == -16);

        // Zacas: amocas.w match & mismatch
        const uint32_t init_w = 0x80000010U;
        std::memcpy(ram.data() + 0x2004, &init_w, sizeof(init_w));
        std::memcpy(ram.data() + 0x1010, &amocas_w, sizeof(amocas_w));
        // Mismatch first (rd = 0x1234 != 0x80000010)
        cpu.state().pc = simrv::xlen::VirtAddr{kCodeAddr + 0x10};
        cpu.state().regs.write(simrv::xlen::RegId::A0, kDataAddr + 4);
        cpu.state().regs.write(simrv::xlen::RegId::A1, 0xCAFEBABEU);
        cpu.state().regs.write(simrv::xlen::RegId::A2, 0x1234U);
        cpu.run_cycle(machine);
        uint32_t out_w = 0;
        std::memcpy(&out_w, ram.data() + 0x2004, sizeof(out_w));
        TEST_ASSERT(out_w == init_w);
        TEST_ASSERT(static_cast<uint32_t>(cpu.state().regs.read(simrv::xlen::RegId::A2)) == init_w);

        // Match now (rd == init_w)
        cpu.state().pc = simrv::xlen::VirtAddr{kCodeAddr + 0x10};
        cpu.run_cycle(machine);
        std::memcpy(&out_w, ram.data() + 0x2004, sizeof(out_w));
        TEST_ASSERT(out_w == 0xCAFEBABEU);
        TEST_ASSERT(static_cast<uint32_t>(cpu.state().regs.read(simrv::xlen::RegId::A2)) == init_w);

        // Zacas + Zabha: amocas.b match
        const uint32_t amocas_b = encode_amo(0x05, 0, 11, 10, 12);
        ram[0x2008] = std::byte{0xAAU};
        std::memcpy(ram.data() + 0x1014, &amocas_b, sizeof(amocas_b));
        cpu.state().pc = simrv::xlen::VirtAddr{kCodeAddr + 0x14};
        cpu.state().regs.write(simrv::xlen::RegId::A0, kDataAddr + 8);
        cpu.state().regs.write(simrv::xlen::RegId::A1, 0x55U);
        cpu.state().regs.write(simrv::xlen::RegId::A2, 0xAAU);
        cpu.run_cycle(machine);
        TEST_ASSERT(ram[0x2008] == std::byte{0x55U});
        TEST_ASSERT(static_cast<simrv::xlen::SignedWord>(cpu.state().regs.read(
                        simrv::xlen::RegId::A2)) == static_cast<int8_t>(0xAAU));

        if constexpr (simrv::xlen::kIsXLen64) {
            // Zacas RV64: amocas.q (128-bit pair a2:a3 vs mem, swap with a4:a5)
            const uint32_t amocas_q = encode_amo(0x05, 4, 14, 10, 12);
            const uint64_t init_lo = UINT64_C(0x1122334455667788);
            const uint64_t init_hi = UINT64_C(0x99AABBCCDDEEFF00);
            std::memcpy(ram.data() + 0x2010, &init_lo, sizeof(init_lo));
            std::memcpy(ram.data() + 0x2018, &init_hi, sizeof(init_hi));
            std::memcpy(ram.data() + 0x1018, &amocas_q, sizeof(amocas_q));

            cpu.state().pc = simrv::xlen::VirtAddr{kCodeAddr + 0x18};
            cpu.state().regs.write(simrv::xlen::RegId::A0, kDataAddr + 0x10);
            cpu.state().regs.write(simrv::xlen::RegId::A2, init_lo);
            cpu.state().regs.write(simrv::xlen::RegId::A3, init_hi);
            cpu.state().regs.write(simrv::xlen::RegId::A4, UINT64_C(0x0102030405060708));
            cpu.state().regs.write(simrv::xlen::RegId::A5, UINT64_C(0xF1F2F3F4F5F6F7F8));
            cpu.run_cycle(machine);
            uint64_t out_lo = 0;
            uint64_t out_hi = 0;
            std::memcpy(&out_lo, ram.data() + 0x2010, sizeof(out_lo));
            std::memcpy(&out_hi, ram.data() + 0x2018, sizeof(out_hi));
            TEST_ASSERT(out_lo == UINT64_C(0x0102030405060708));
            TEST_ASSERT(out_hi == UINT64_C(0xF1F2F3F4F5F6F7F8));
            TEST_ASSERT(cpu.state().regs.read(simrv::xlen::RegId::A2) == init_lo);
            TEST_ASSERT(cpu.state().regs.read(simrv::xlen::RegId::A3) == init_hi);
        } else {
            // Zacas RV32: amocas.d (64-bit pair a2:a3 vs mem, swap with a4:a5)
            const uint32_t amocas_d = encode_amo(0x05, 3, 14, 10, 12);
            const uint64_t init_d = UINT64_C(0x1122334455667788);
            std::memcpy(ram.data() + 0x2010, &init_d, sizeof(init_d));
            std::memcpy(ram.data() + 0x101C, &amocas_d, sizeof(amocas_d));

            cpu.state().pc = simrv::xlen::VirtAddr{kCodeAddr + 0x1C};
            cpu.state().regs.write(simrv::xlen::RegId::A0, kDataAddr + 0x10);
            cpu.state().regs.write(simrv::xlen::RegId::A2, 0x55667788U);
            cpu.state().regs.write(simrv::xlen::RegId::A3, 0x11223344U);
            cpu.state().regs.write(simrv::xlen::RegId::A4, 0xAABBCCDDU);
            cpu.state().regs.write(simrv::xlen::RegId::A5, 0xDEADBEEFU);
            cpu.run_cycle(machine);
            uint64_t out_d = 0;
            std::memcpy(&out_d, ram.data() + 0x2010, sizeof(out_d));
            TEST_ASSERT(out_d == UINT64_C(0xDEADBEEFAABBCCDD));
            TEST_ASSERT(cpu.state().regs.read(simrv::xlen::RegId::A2) == 0x55667788U);
            TEST_ASSERT(cpu.state().regs.read(simrv::xlen::RegId::A3) == 0x11223344U);
        }
    }
}

void test_track1c_fp16_bf16() {
    using namespace simrv::isa;
    using namespace simrv::core;
    using simrv::xlen::CsrValue;

    auto encode_op_fp = [](uint32_t funct7, uint32_t rs2, uint32_t rs1, uint32_t rm, uint32_t rd) {
        return (funct7 << 25U) | (rs2 << 20U) | (rs1 << 15U) | (rm << 12U) | (rd << 7U) | 0x53U;
    };
    auto encode_fmadd = [](uint32_t rs3, uint32_t fmt, uint32_t rs2, uint32_t rs1, uint32_t rm,
                           uint32_t rd) {
        return (rs3 << 27U) | (fmt << 25U) | (rs2 << 20U) | (rs1 << 15U) | (rm << 12U) |
               (rd << 7U) | 0x43U;
    };
    auto encode_op_v = [](uint32_t funct6, uint32_t vm, uint32_t vs2, uint32_t vs1, uint32_t funct3,
                          uint32_t vd) {
        return (funct6 << 26U) | (vm << 25U) | (vs2 << 20U) | (vs1 << 15U) | (funct3 << 12U) |
               (vd << 7U) | 0x57U;
    };

    const uint32_t fadd_h = encode_op_fp(0x02U, 2U, 1U, 0U, 3U);
    const uint32_t fmadd_h = encode_fmadd(4U, 2U, 2U, 1U, 0U, 3U);
    const uint32_t fmin_h = encode_op_fp(0x16U, 2U, 1U, 0U, 3U);
    const uint32_t feq_h = encode_op_fp(0x52U, 2U, 1U, 2U, 10U);
    const uint32_t fclass_h = encode_op_fp(0x72U, 0U, 1U, 1U, 10U);
    const uint32_t fcvt_w_h = encode_op_fp(0x62U, 0U, 1U, 1U, 10U);
    const uint32_t fcvt_h_w = encode_op_fp(0x6AU, 0U, 10U, 0U, 3U);
    const uint32_t fcvt_bf16_s = encode_op_fp(0x22U, 8U, 1U, 0U, 3U);
    const uint32_t fcvt_s_bf16 = encode_op_fp(0x20U, 6U, 1U, 0U, 3U);
    const uint32_t vfadd_vv = encode_op_v(0x00U, 1U, 2U, 1U, 1U, 4U);
    const uint32_t vfwcvtbf16 = encode_op_v(0x12U, 1U, 2U, 13U, 1U, 4U);
    const uint32_t vfncvtbf16 = encode_op_v(0x12U, 1U, 2U, 29U, 1U, 4U);
    const uint32_t vfwmaccbf16_vv = encode_op_v(0x3BU, 1U, 2U, 1U, 1U, 4U);
    const uint32_t vfwmaccbf16_vf = encode_op_v(0x3BU, 1U, 2U, 1U, 5U, 4U);

    // 1. Decode checks
    TEST_ASSERT(simrv::pipeline::decoder(fadd_h) == OperationId::FADD_H);
    TEST_ASSERT(simrv::pipeline::decoder(fmadd_h) == OperationId::FMADD_H);
    TEST_ASSERT(simrv::pipeline::decoder(fmin_h) == OperationId::FMIN_H);
    TEST_ASSERT(simrv::pipeline::decoder(feq_h) == OperationId::FEQ_H);
    TEST_ASSERT(simrv::pipeline::decoder(fclass_h) == OperationId::FCLASS_H);
    TEST_ASSERT(simrv::pipeline::decoder(fcvt_w_h) == OperationId::FCVT_W_H);
    TEST_ASSERT(simrv::pipeline::decoder(fcvt_h_w) == OperationId::FCVT_H_W);
    TEST_ASSERT(simrv::pipeline::decoder(fcvt_bf16_s) == OperationId::FCVT_BF16_S);
    TEST_ASSERT(simrv::pipeline::decoder(fcvt_s_bf16) == OperationId::FCVT_S_BF16);
    TEST_ASSERT(simrv::pipeline::decoder(vfwcvtbf16) == OperationId::VFWCVTBF16_F_F_V);
    TEST_ASSERT(simrv::pipeline::decoder(vfncvtbf16) == OperationId::VFNCVTBF16_F_F_W);
    TEST_ASSERT(simrv::pipeline::decoder(vfwmaccbf16_vv) == OperationId::VFWMACCBF16_VV);
    TEST_ASSERT(simrv::pipeline::decoder(vfwmaccbf16_vf) == OperationId::VFWMACCBF16_VF);

    // 2. Execution & gating tests
    constexpr simrv::xlen::Address kCodeAddr = 0x80001000ULL;
    std::array<simrv::xlen::Byte, 0x4000> ram{};
    Machine machine;
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.memory().initialize_mmu();
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::InstructionFast;

    auto& cpu = machine.primary_hart();
    cpu.state().priv = simrv::xlen::PrivilegeLevel::Machine;
    cpu.state().misa = kMisaDefault | misa_extension_bit(IsaExtension::F) |
                       misa_extension_bit(IsaExtension::D) | misa_extension_bit(IsaExtension::V);
    cpu.state().mstatus |= MstatusBit::Fs | MstatusBit::Vs;
    cpu.state().mtvec = 0x80003000ULL;

    auto fp_reg = [](uint32_t idx) { return static_cast<simrv::xlen::FpRegId>(idx); };
    auto run_insn = [&](uint32_t offset, uint32_t insn) {
        std::memcpy(ram.data() + 0x1000 + offset, &insn, sizeof(insn));
        cpu.state().pc = simrv::xlen::VirtAddr{kCodeAddr + offset};
        cpu.state().mcause = 0;
        cpu.run_cycle(machine);
    };

    // Negative Zfh: disabled -> IllegalInstruction
    cpu.state().enabled_extensions = ExtensionSet{};
    run_insn(0x00, fadd_h);
    TEST_ASSERT(cpu.state().mcause == static_cast<CsrValue>(ExceptionCode::IllegalInstruction));

    // Positive Zfh: f1 = 1.5h (0x3E00), f2 = 2.0h (0x4000), f4 = 0.5h (0x3800)
    ExtensionSet zfh_ext{};
    zfh_ext.set(MultiLetterExtension::Zfhmin);
    zfh_ext.set(MultiLetterExtension::Zfh);
    cpu.state().enabled_extensions = zfh_ext;
    cpu.state().regs.write_fp(fp_reg(1), UINT64_C(0xFFFFFFFFFFFF3E00));
    cpu.state().regs.write_fp(fp_reg(2), UINT64_C(0xFFFFFFFFFFFF4000));
    cpu.state().regs.write_fp(fp_reg(4), UINT64_C(0xFFFFFFFFFFFF3800));

    run_insn(0x04, fadd_h);
    TEST_ASSERT(cpu.state().mcause == 0);
    TEST_ASSERT((cpu.state().regs.read_fp(fp_reg(3)) & 0xFFFFU) == 0x4300U);  // 3.5h

    run_insn(0x08, fmadd_h);
    TEST_ASSERT(cpu.state().mcause == 0);
    TEST_ASSERT((cpu.state().regs.read_fp(fp_reg(3)) & 0xFFFFU) ==
                0x4300U);  // 1.5*2.0 + 0.5 = 3.5h

    run_insn(0x0C, fmin_h);
    TEST_ASSERT((cpu.state().regs.read_fp(fp_reg(3)) & 0xFFFFU) ==
                0x3E00U);  // min(1.5, 2.0) = 1.5h

    run_insn(0x10, feq_h);
    TEST_ASSERT(cpu.state().regs.read(simrv::xlen::RegId::A0) == 0U);

    run_insn(0x14, fclass_h);
    TEST_ASSERT(cpu.state().regs.read(simrv::xlen::RegId::A0) == (1U << 6U));  // +normal

    run_insn(0x18, fcvt_w_h);
    TEST_ASSERT(cpu.state().regs.read(simrv::xlen::RegId::A0) == 1U);  // trunc(1.5) = 1

    cpu.state().regs.write(simrv::xlen::RegId::A0, 3);
    run_insn(0x1C, fcvt_h_w);
    TEST_ASSERT((cpu.state().regs.read_fp(fp_reg(3)) & 0xFFFFU) == 0x4200U);  // 3.0h

    // Negative & Positive Zfbfmin
    cpu.state().enabled_extensions = ExtensionSet{};
    run_insn(0x20, fcvt_bf16_s);
    TEST_ASSERT(cpu.state().mcause == static_cast<CsrValue>(ExceptionCode::IllegalInstruction));

    ExtensionSet bf_ext{};
    bf_ext.set(MultiLetterExtension::Zfbfmin);
    cpu.state().enabled_extensions = bf_ext;
    cpu.state().regs.write_fp(fp_reg(1), UINT64_C(0xFFFFFFFF3FC00000));  // 1.5f
    run_insn(0x24, fcvt_bf16_s);
    TEST_ASSERT(cpu.state().mcause == 0);
    TEST_ASSERT((cpu.state().regs.read_fp(fp_reg(3)) & 0xFFFFU) == 0x3FC0U);  // 1.5 bf16

    cpu.state().regs.write_fp(fp_reg(1), UINT64_C(0xFFFFFFFFFFFF3FC0));
    run_insn(0x28, fcvt_s_bf16);
    TEST_ASSERT(cpu.state().mcause == 0);
    TEST_ASSERT(static_cast<uint32_t>(cpu.state().regs.read_fp(fp_reg(3))) == 0x3FC00000U);

    // Vector SEW=16 setup
    cpu.state().vtype = 1U << 3U;  // SEW=16, LMUL=1
    cpu.state().vl = 2;
    cpu.state().vstart = 0;

    // Negative Zvfh: Zvfhmin alone must trap on vfadd.vv
    ExtensionSet zvfhmin_only{};
    zvfhmin_only.set(MultiLetterExtension::Zvfhmin);
    cpu.state().enabled_extensions = zvfhmin_only;
    run_insn(0x2C, vfadd_vv);
    TEST_ASSERT(cpu.state().mcause == static_cast<CsrValue>(ExceptionCode::IllegalInstruction));

    // Positive Zvfh: vfadd.vv (1.5h + 2.0h = 3.5h)
    ExtensionSet zvfh_ext{};
    zvfh_ext.set(MultiLetterExtension::Zvfhmin);
    zvfh_ext.set(MultiLetterExtension::Zvfh);
    cpu.state().enabled_extensions = zvfh_ext;
    cpu.state()
        .regs.read_vector(simrv::xlen::to_vec_reg_id(static_cast<simrv::xlen::RegId>(2)))
        .u16[0] = 0x3E00U;
    cpu.state()
        .regs.read_vector(simrv::xlen::to_vec_reg_id(static_cast<simrv::xlen::RegId>(1)))
        .u16[0] = 0x4000U;
    run_insn(0x30, vfadd_vv);
    TEST_ASSERT(cpu.state().mcause == 0);
    TEST_ASSERT(
        cpu.state()
            .regs.read_vector(simrv::xlen::to_vec_reg_id(static_cast<simrv::xlen::RegId>(4)))
            .u16[0] == 0x4300U);

    // Negative Zvfbfmin -> IllegalInstruction
    cpu.state().enabled_extensions = ExtensionSet{};
    run_insn(0x34, vfwcvtbf16);
    TEST_ASSERT(cpu.state().mcause == static_cast<CsrValue>(ExceptionCode::IllegalInstruction));

    // Positive Zvfbfmin: vfwcvtbf16.f.f.v & vfncvtbf16.f.f.w
    ExtensionSet zvfbfmin_ext{};
    zvfbfmin_ext.set(MultiLetterExtension::Zvfbfmin);
    cpu.state().enabled_extensions = zvfbfmin_ext;
    cpu.state()
        .regs.read_vector(simrv::xlen::to_vec_reg_id(static_cast<simrv::xlen::RegId>(2)))
        .u16[0] = 0x3FC0U;  // 1.5 bf16
    cpu.state()
        .regs.read_vector(simrv::xlen::to_vec_reg_id(static_cast<simrv::xlen::RegId>(2)))
        .u16[1] = 0x4000U;  // 2.0 bf16
    run_insn(0x38, vfwcvtbf16);
    TEST_ASSERT(cpu.state().mcause == 0);
    TEST_ASSERT(
        cpu.state()
            .regs.read_vector(simrv::xlen::to_vec_reg_id(static_cast<simrv::xlen::RegId>(4)))
            .u32[0] == 0x3FC00000U);
    TEST_ASSERT(
        cpu.state()
            .regs.read_vector(simrv::xlen::to_vec_reg_id(static_cast<simrv::xlen::RegId>(4)))
            .u32[1] == 0x40000000U);

    cpu.state()
        .regs.read_vector(simrv::xlen::to_vec_reg_id(static_cast<simrv::xlen::RegId>(2)))
        .u32[0] = 0x3FC00000U;
    cpu.state()
        .regs.read_vector(simrv::xlen::to_vec_reg_id(static_cast<simrv::xlen::RegId>(2)))
        .u32[1] = 0x40000000U;
    run_insn(0x3C, vfncvtbf16);
    TEST_ASSERT(cpu.state().mcause == 0);
    TEST_ASSERT(
        cpu.state()
            .regs.read_vector(simrv::xlen::to_vec_reg_id(static_cast<simrv::xlen::RegId>(4)))
            .u16[0] == 0x3FC0U);
    TEST_ASSERT(
        cpu.state()
            .regs.read_vector(simrv::xlen::to_vec_reg_id(static_cast<simrv::xlen::RegId>(4)))
            .u16[1] == 0x4000U);

    // Negative Zvfbfwma -> IllegalInstruction
    run_insn(0x40, vfwmaccbf16_vv);
    TEST_ASSERT(cpu.state().mcause == static_cast<CsrValue>(ExceptionCode::IllegalInstruction));

    // Positive Zvfbfwma: vfwmaccbf16.vv & vfwmaccbf16.vf
    zvfbfmin_ext.set(MultiLetterExtension::Zvfbfwma);
    cpu.state().enabled_extensions = zvfbfmin_ext;
    cpu.state()
        .regs.read_vector(simrv::xlen::to_vec_reg_id(static_cast<simrv::xlen::RegId>(2)))
        .u16[0] = 0x3FC0U;  // 1.5 bf16
    cpu.state()
        .regs.read_vector(simrv::xlen::to_vec_reg_id(static_cast<simrv::xlen::RegId>(1)))
        .u16[0] = 0x4000U;  // 2.0 bf16
    cpu.state()
        .regs.read_vector(simrv::xlen::to_vec_reg_id(static_cast<simrv::xlen::RegId>(4)))
        .u32[0] = 0x3F800000U;  // 1.0f
    run_insn(0x44, vfwmaccbf16_vv);
    TEST_ASSERT(cpu.state().mcause == 0);
    TEST_ASSERT(
        cpu.state()
            .regs.read_vector(simrv::xlen::to_vec_reg_id(static_cast<simrv::xlen::RegId>(4)))
            .u32[0] == 0x40800000U);  // 1.5*2.0 + 1.0 = 4.0f

    cpu.state().regs.write_fp(fp_reg(1), UINT64_C(0xFFFFFFFFFFFF4000));  // 2.0 bf16
    cpu.state()
        .regs.read_vector(simrv::xlen::to_vec_reg_id(static_cast<simrv::xlen::RegId>(4)))
        .u32[0] = 0x3F800000U;  // 1.0f
    run_insn(0x48, vfwmaccbf16_vf);
    TEST_ASSERT(cpu.state().mcause == 0);
    TEST_ASSERT(
        cpu.state()
            .regs.read_vector(simrv::xlen::to_vec_reg_id(static_cast<simrv::xlen::RegId>(4)))
            .u32[0] == 0x40800000U);  // 4.0f
}

void test_track2_aggregate_isa_expansion() {
    using namespace simrv::isa;

    // =========================================================================
    // 1. Aggregate Shorthand Expansion: zkn (Scalar NIST Cryptography)
    //    zkn = Zbkb + Zbkc + Zbkx + Zkne + Zknd + Zknh
    // =========================================================================
    {
        const auto zkn_64 = parse_isa_spec("rv64gc_zkn");
        TEST_ASSERT(zkn_64.has_value());
        TEST_ASSERT(zkn_64->has_extension(MultiLetterExtension::Zbkb));
        TEST_ASSERT(zkn_64->has_extension(MultiLetterExtension::Zbkc));
        TEST_ASSERT(zkn_64->has_extension(MultiLetterExtension::Zbkx));
        TEST_ASSERT(zkn_64->has_extension(MultiLetterExtension::Zkne));
        TEST_ASSERT(zkn_64->has_extension(MultiLetterExtension::Zknd));
        TEST_ASSERT(zkn_64->has_extension(MultiLetterExtension::Zknh));
        // Must NOT expand into ShangMi or entropy seed extensions
        TEST_ASSERT(!zkn_64->has_extension(MultiLetterExtension::Zksed));
        TEST_ASSERT(!zkn_64->has_extension(MultiLetterExtension::Zksh));
        TEST_ASSERT(!zkn_64->has_extension(MultiLetterExtension::Zkr));
        TEST_ASSERT(!zkn_64->has_extension(MultiLetterExtension::Zkt));

        const auto zkn_32 = parse_isa_spec("rv32gc_zkn");
        TEST_ASSERT(zkn_32.has_value());
        TEST_ASSERT(zkn_32->xlen == 32);
        TEST_ASSERT(zkn_32->has_extension(MultiLetterExtension::Zbkb));
        TEST_ASSERT(zkn_32->has_extension(MultiLetterExtension::Zbkc));
        TEST_ASSERT(zkn_32->has_extension(MultiLetterExtension::Zbkx));
        TEST_ASSERT(zkn_32->has_extension(MultiLetterExtension::Zkne));
        TEST_ASSERT(zkn_32->has_extension(MultiLetterExtension::Zknd));
        TEST_ASSERT(zkn_32->has_extension(MultiLetterExtension::Zknh));
        TEST_ASSERT(!zkn_32->has_extension(MultiLetterExtension::Zksed));
        TEST_ASSERT(!zkn_32->has_extension(MultiLetterExtension::Zksh));
    }

    // =========================================================================
    // 2. Aggregate Shorthand Expansion: zks (Scalar ShangMi Cryptography)
    //    zks = Zbkb + Zbkc + Zbkx + Zksed + Zksh
    // =========================================================================
    {
        const auto zks_64 = parse_isa_spec("rv64gc_zks");
        TEST_ASSERT(zks_64.has_value());
        TEST_ASSERT(zks_64->has_extension(MultiLetterExtension::Zbkb));
        TEST_ASSERT(zks_64->has_extension(MultiLetterExtension::Zbkc));
        TEST_ASSERT(zks_64->has_extension(MultiLetterExtension::Zbkx));
        TEST_ASSERT(zks_64->has_extension(MultiLetterExtension::Zksed));
        TEST_ASSERT(zks_64->has_extension(MultiLetterExtension::Zksh));
        // Must NOT expand into NIST cipher/hash extensions
        TEST_ASSERT(!zks_64->has_extension(MultiLetterExtension::Zknd));
        TEST_ASSERT(!zks_64->has_extension(MultiLetterExtension::Zkne));
        TEST_ASSERT(!zks_64->has_extension(MultiLetterExtension::Zknh));

        const auto zks_32 = parse_isa_spec("rv32gc_zks");
        TEST_ASSERT(zks_32.has_value());
        TEST_ASSERT(zks_32->xlen == 32);
        TEST_ASSERT(zks_32->has_extension(MultiLetterExtension::Zbkb));
        TEST_ASSERT(zks_32->has_extension(MultiLetterExtension::Zbkc));
        TEST_ASSERT(zks_32->has_extension(MultiLetterExtension::Zbkx));
        TEST_ASSERT(zks_32->has_extension(MultiLetterExtension::Zksed));
        TEST_ASSERT(zks_32->has_extension(MultiLetterExtension::Zksh));
        TEST_ASSERT(!zks_32->has_extension(MultiLetterExtension::Zknd));
        TEST_ASSERT(!zks_32->has_extension(MultiLetterExtension::Zkne));
        TEST_ASSERT(!zks_32->has_extension(MultiLetterExtension::Zknh));
    }

    // =========================================================================
    // 3. Aggregate Shorthand Expansion: zvkn (Vector NIST Cryptography)
    //    zvkn = Zvkned + Zvknhb + Zvkb + Zvkt
    // =========================================================================
    {
        const auto zvkn_64 = parse_isa_spec("rv64gc_zvkn");
        TEST_ASSERT(zvkn_64.has_value());
        TEST_ASSERT(zvkn_64->has_extension(MultiLetterExtension::Zvkned));
        TEST_ASSERT(zvkn_64->has_extension(MultiLetterExtension::Zvknhb));
        TEST_ASSERT(zvkn_64->has_extension(MultiLetterExtension::Zvkb));
        TEST_ASSERT(zvkn_64->has_extension(MultiLetterExtension::Zvkt));
        TEST_ASSERT(!zvkn_64->has_extension(MultiLetterExtension::Zvksed));
        TEST_ASSERT(!zvkn_64->has_extension(MultiLetterExtension::Zvksh));

        const auto zvkn_32 = parse_isa_spec("rv32gc_zvkn");
        TEST_ASSERT(zvkn_32.has_value());
        TEST_ASSERT(zvkn_32->xlen == 32);
        TEST_ASSERT(zvkn_32->has_extension(MultiLetterExtension::Zvkned));
        TEST_ASSERT(zvkn_32->has_extension(MultiLetterExtension::Zvknhb));
        TEST_ASSERT(zvkn_32->has_extension(MultiLetterExtension::Zvkb));
        TEST_ASSERT(zvkn_32->has_extension(MultiLetterExtension::Zvkt));
        TEST_ASSERT(!zvkn_32->has_extension(MultiLetterExtension::Zvksed));
        TEST_ASSERT(!zvkn_32->has_extension(MultiLetterExtension::Zvksh));
    }

    // =========================================================================
    // 4. Aggregate Shorthand Expansion: zvks (Vector ShangMi Cryptography)
    //    zvks = Zvksed + Zvksh + Zvkb + Zvkt
    // =========================================================================
    {
        const auto zvks_64 = parse_isa_spec("rv64gc_zvks");
        TEST_ASSERT(zvks_64.has_value());
        TEST_ASSERT(zvks_64->has_extension(MultiLetterExtension::Zvksed));
        TEST_ASSERT(zvks_64->has_extension(MultiLetterExtension::Zvksh));
        TEST_ASSERT(zvks_64->has_extension(MultiLetterExtension::Zvkb));
        TEST_ASSERT(zvks_64->has_extension(MultiLetterExtension::Zvkt));
        TEST_ASSERT(!zvks_64->has_extension(MultiLetterExtension::Zvkned));
        TEST_ASSERT(!zvks_64->has_extension(MultiLetterExtension::Zvknhb));

        const auto zvks_32 = parse_isa_spec("rv32gc_zvks");
        TEST_ASSERT(zvks_32.has_value());
        TEST_ASSERT(zvks_32->xlen == 32);
        TEST_ASSERT(zvks_32->has_extension(MultiLetterExtension::Zvksed));
        TEST_ASSERT(zvks_32->has_extension(MultiLetterExtension::Zvksh));
        TEST_ASSERT(zvks_32->has_extension(MultiLetterExtension::Zvkb));
        TEST_ASSERT(zvks_32->has_extension(MultiLetterExtension::Zvkt));
        TEST_ASSERT(!zvks_32->has_extension(MultiLetterExtension::Zvkned));
        TEST_ASSERT(!zvks_32->has_extension(MultiLetterExtension::Zvknhb));
    }

    // =========================================================================
    // 5. Aggregate Shorthand Expansion: zk (Full Standard Scalar Cryptography)
    //    zk = zkn + zkr + zkt
    // =========================================================================
    {
        const auto zk_64 = parse_isa_spec("rv64gc_zk");
        TEST_ASSERT(zk_64.has_value());
        TEST_ASSERT(zk_64->has_extension(MultiLetterExtension::Zbkb));
        TEST_ASSERT(zk_64->has_extension(MultiLetterExtension::Zbkc));
        TEST_ASSERT(zk_64->has_extension(MultiLetterExtension::Zbkx));
        TEST_ASSERT(zk_64->has_extension(MultiLetterExtension::Zkne));
        TEST_ASSERT(zk_64->has_extension(MultiLetterExtension::Zknd));
        TEST_ASSERT(zk_64->has_extension(MultiLetterExtension::Zknh));
        TEST_ASSERT(zk_64->has_extension(MultiLetterExtension::Zkr));
        TEST_ASSERT(zk_64->has_extension(MultiLetterExtension::Zkt));

        const auto zk_32 = parse_isa_spec("rv32gc_zk");
        TEST_ASSERT(zk_32.has_value());
        TEST_ASSERT(zk_32->xlen == 32);
        TEST_ASSERT(zk_32->has_extension(MultiLetterExtension::Zbkb));
        TEST_ASSERT(zk_32->has_extension(MultiLetterExtension::Zkr));
        TEST_ASSERT(zk_32->has_extension(MultiLetterExtension::Zkt));
    }

    // =========================================================================
    // 6. Individual Wave 2 Extension Parsing via parse_isa_spec
    // =========================================================================
    {
        // Track 2C: Code-Size Extensions
        const auto zcmp_spec = parse_isa_spec("rv64gc_zcmp");
        TEST_ASSERT(zcmp_spec.has_value());
        TEST_ASSERT(zcmp_spec->has_extension(MultiLetterExtension::Zcmp));
        TEST_ASSERT(!zcmp_spec->has_extension(MultiLetterExtension::Zcmt));

        const auto zcmt_spec = parse_isa_spec("rv64gc_zcmt");
        TEST_ASSERT(zcmt_spec.has_value());
        TEST_ASSERT(zcmt_spec->has_extension(MultiLetterExtension::Zcmt));
        TEST_ASSERT(!zcmt_spec->has_extension(MultiLetterExtension::Zcmp));

        // Track 2A: Vector Cryptography Individual Extensions
        const auto zvkned_spec = parse_isa_spec("rv64gc_zvkned");
        TEST_ASSERT(zvkned_spec.has_value() &&
                    zvkned_spec->has_extension(MultiLetterExtension::Zvkned));
        TEST_ASSERT(!zvkned_spec->has_extension(MultiLetterExtension::Zvknha));

        const auto zvknhb_spec = parse_isa_spec("rv64gc_zvknhb");
        TEST_ASSERT(zvknhb_spec.has_value() &&
                    zvknhb_spec->has_extension(MultiLetterExtension::Zvknhb));

        const auto zvknha_spec = parse_isa_spec("rv64gc_zvknha");
        TEST_ASSERT(zvknha_spec.has_value() &&
                    zvknha_spec->has_extension(MultiLetterExtension::Zvknha));

        const auto zvkg_spec = parse_isa_spec("rv64gc_zvkg");
        TEST_ASSERT(zvkg_spec.has_value() && zvkg_spec->has_extension(MultiLetterExtension::Zvkg));

        const auto zvksed_spec = parse_isa_spec("rv64gc_zvksed");
        TEST_ASSERT(zvksed_spec.has_value() &&
                    zvksed_spec->has_extension(MultiLetterExtension::Zvksed));

        const auto zvksh_spec = parse_isa_spec("rv64gc_zvksh");
        TEST_ASSERT(zvksh_spec.has_value() &&
                    zvksh_spec->has_extension(MultiLetterExtension::Zvksh));

        // Track 2B: Scalar Cryptography Individual Extensions
        const auto zknd_spec = parse_isa_spec("rv64gc_zknd");
        TEST_ASSERT(zknd_spec.has_value() && zknd_spec->has_extension(MultiLetterExtension::Zknd));

        const auto zkne_spec = parse_isa_spec("rv64gc_zkne");
        TEST_ASSERT(zkne_spec.has_value() && zkne_spec->has_extension(MultiLetterExtension::Zkne));

        const auto zknh_spec = parse_isa_spec("rv64gc_zknh");
        TEST_ASSERT(zknh_spec.has_value() && zknh_spec->has_extension(MultiLetterExtension::Zknh));

        const auto zksed_spec = parse_isa_spec("rv64gc_zksed");
        TEST_ASSERT(zksed_spec.has_value() &&
                    zksed_spec->has_extension(MultiLetterExtension::Zksed));

        const auto zksh_spec = parse_isa_spec("rv64gc_zksh");
        TEST_ASSERT(zksh_spec.has_value() && zksh_spec->has_extension(MultiLetterExtension::Zksh));

        const auto zbkb_spec = parse_isa_spec("rv64gc_zbkb");
        TEST_ASSERT(zbkb_spec.has_value() && zbkb_spec->has_extension(MultiLetterExtension::Zbkb));

        const auto zbkc_spec = parse_isa_spec("rv64gc_zbkc");
        TEST_ASSERT(zbkc_spec.has_value() && zbkc_spec->has_extension(MultiLetterExtension::Zbkc));

        const auto zbkx_spec = parse_isa_spec("rv64gc_zbkx");
        TEST_ASSERT(zbkx_spec.has_value() && zbkx_spec->has_extension(MultiLetterExtension::Zbkx));

        // RV32 target variants
        const auto zcmp_32 = parse_isa_spec("rv32gc_zcmp");
        TEST_ASSERT(zcmp_32.has_value() && zcmp_32->has_extension(MultiLetterExtension::Zcmp));

        const auto zcmt_32 = parse_isa_spec("rv32gc_zcmt");
        TEST_ASSERT(zcmt_32.has_value() && zcmt_32->has_extension(MultiLetterExtension::Zcmt));

        const auto zknd_32 = parse_isa_spec("rv32imac_zknd_zkne_zknh");
        TEST_ASSERT(zknd_32.has_value());
        TEST_ASSERT(zknd_32->has_extension(MultiLetterExtension::Zknd));
        TEST_ASSERT(zknd_32->has_extension(MultiLetterExtension::Zkne));
        TEST_ASSERT(zknd_32->has_extension(MultiLetterExtension::Zknh));
    }

    // =========================================================================
    // 7. Compound Combinations & Case Insensitivity
    // =========================================================================
    {
        // Combined Track 2A + 2B + 2C
        const auto combo1 = parse_isa_spec("rv64gc_zkn_zvkn_zcmp_zcmt");
        TEST_ASSERT(combo1.has_value());
        TEST_ASSERT(combo1->has_extension(MultiLetterExtension::Zbkb));
        TEST_ASSERT(combo1->has_extension(MultiLetterExtension::Zbkc));
        TEST_ASSERT(combo1->has_extension(MultiLetterExtension::Zbkx));
        TEST_ASSERT(combo1->has_extension(MultiLetterExtension::Zkne));
        TEST_ASSERT(combo1->has_extension(MultiLetterExtension::Zknd));
        TEST_ASSERT(combo1->has_extension(MultiLetterExtension::Zknh));
        TEST_ASSERT(combo1->has_extension(MultiLetterExtension::Zvkned));
        TEST_ASSERT(combo1->has_extension(MultiLetterExtension::Zvknhb));
        TEST_ASSERT(combo1->has_extension(MultiLetterExtension::Zvkb));
        TEST_ASSERT(combo1->has_extension(MultiLetterExtension::Zvkt));
        TEST_ASSERT(combo1->has_extension(MultiLetterExtension::Zcmp));
        TEST_ASSERT(combo1->has_extension(MultiLetterExtension::Zcmt));

        const auto combo2 = parse_isa_spec("rv64gc_zks_zvks_zvkg_zvknha");
        TEST_ASSERT(combo2.has_value());
        TEST_ASSERT(combo2->has_extension(MultiLetterExtension::Zksed));
        TEST_ASSERT(combo2->has_extension(MultiLetterExtension::Zksh));
        TEST_ASSERT(combo2->has_extension(MultiLetterExtension::Zvksed));
        TEST_ASSERT(combo2->has_extension(MultiLetterExtension::Zvksh));
        TEST_ASSERT(combo2->has_extension(MultiLetterExtension::Zvkg));
        TEST_ASSERT(combo2->has_extension(MultiLetterExtension::Zvknha));

        // Mixed and upper casing
        const auto upper1 = parse_isa_spec("RV64GC_ZKN");
        TEST_ASSERT(upper1.has_value() && upper1->has_extension(MultiLetterExtension::Zknd));

        const auto mixed1 = parse_isa_spec("rv64gc_ZkS");
        TEST_ASSERT(mixed1.has_value() && mixed1->has_extension(MultiLetterExtension::Zksed));

        const auto mixed2 = parse_isa_spec("Rv64Gc_ZvKn");
        TEST_ASSERT(mixed2.has_value() && mixed2->has_extension(MultiLetterExtension::Zvkned));

        const auto upper2 = parse_isa_spec("RV32GC_ZCMP_ZCMT");
        TEST_ASSERT(upper2.has_value());
        TEST_ASSERT(upper2->has_extension(MultiLetterExtension::Zcmp));
        TEST_ASSERT(upper2->has_extension(MultiLetterExtension::Zcmt));

        // Malformed strings & edge cases
        const auto trailing = parse_isa_spec("rv64i_");
        TEST_ASSERT(trailing.has_value() && trailing->extension_set.empty());
        TEST_ASSERT(!parse_isa_spec("rv64gc_zkn__zks").has_value());
        TEST_ASSERT(!parse_isa_spec("invalid_arch").has_value());
    }

    // =========================================================================
    // 8. Direct parse_multi_letter_extension Unit Mapping
    // =========================================================================
    {
        TEST_ASSERT(parse_multi_letter_extension("zcmp") == MultiLetterExtension::Zcmp);
        TEST_ASSERT(parse_multi_letter_extension("zcmt") == MultiLetterExtension::Zcmt);
        TEST_ASSERT(parse_multi_letter_extension("zvkned") == MultiLetterExtension::Zvkned);
        TEST_ASSERT(parse_multi_letter_extension("zvknhb") == MultiLetterExtension::Zvknhb);
        TEST_ASSERT(parse_multi_letter_extension("zvknha") == MultiLetterExtension::Zvknha);
        TEST_ASSERT(parse_multi_letter_extension("zvkg") == MultiLetterExtension::Zvkg);
        TEST_ASSERT(parse_multi_letter_extension("zvksed") == MultiLetterExtension::Zvksed);
        TEST_ASSERT(parse_multi_letter_extension("zvksh") == MultiLetterExtension::Zvksh);
        TEST_ASSERT(parse_multi_letter_extension("zbkb") == MultiLetterExtension::Zbkb);
        TEST_ASSERT(parse_multi_letter_extension("zbkc") == MultiLetterExtension::Zbkc);
        TEST_ASSERT(parse_multi_letter_extension("zbkx") == MultiLetterExtension::Zbkx);
        TEST_ASSERT(parse_multi_letter_extension("zknd") == MultiLetterExtension::Zknd);
        TEST_ASSERT(parse_multi_letter_extension("zkne") == MultiLetterExtension::Zkne);
        TEST_ASSERT(parse_multi_letter_extension("zknh") == MultiLetterExtension::Zknh);
        TEST_ASSERT(parse_multi_letter_extension("zksed") == MultiLetterExtension::Zksed);
        TEST_ASSERT(parse_multi_letter_extension("zksh") == MultiLetterExtension::Zksh);

        // Negative cases
        TEST_ASSERT(parse_multi_letter_extension("zcmpx") == std::nullopt);
        TEST_ASSERT(parse_multi_letter_extension("zvkn") == std::nullopt);  // aggregate, not single
        TEST_ASSERT(parse_multi_letter_extension("zks") == std::nullopt);
        TEST_ASSERT(parse_multi_letter_extension("invalid_name") == std::nullopt);
        TEST_ASSERT(parse_multi_letter_extension("") == std::nullopt);
    }

    // =========================================================================
    // 9. Direct ExtensionSet & add_extension_to_set Unit Operations
    // =========================================================================
    {
        ExtensionSet s{};
        TEST_ASSERT(s.empty());

        add_extension_to_set(s, "zkn");
        TEST_ASSERT(!s.empty());
        TEST_ASSERT(s.has(MultiLetterExtension::Zbkb));
        TEST_ASSERT(s.has(MultiLetterExtension::Zbkc));
        TEST_ASSERT(s.has(MultiLetterExtension::Zbkx));
        TEST_ASSERT(s.has(MultiLetterExtension::Zkne));
        TEST_ASSERT(s.has(MultiLetterExtension::Zknd));
        TEST_ASSERT(s.has(MultiLetterExtension::Zknh));

        s.clear(MultiLetterExtension::Zknd);
        TEST_ASSERT(!s.has(MultiLetterExtension::Zknd));
        TEST_ASSERT(s.has(MultiLetterExtension::Zkne));

        s.set(MultiLetterExtension::Zcmp);
        s.set(MultiLetterExtension::Zcmt);
        TEST_ASSERT(s.has(MultiLetterExtension::Zcmp));
        TEST_ASSERT(s.has(MultiLetterExtension::Zcmt));

        ExtensionSet s2{};
        add_extension_to_set(s2, "zvks");
        TEST_ASSERT(s2.has(MultiLetterExtension::Zvksed));
        TEST_ASSERT(s2.has(MultiLetterExtension::Zvksh));
        TEST_ASSERT(!s2.has(MultiLetterExtension::Zcmp));

        s.merge(s2);
        TEST_ASSERT(s.has(MultiLetterExtension::Zcmp));
        TEST_ASSERT(s.has(MultiLetterExtension::Zvksed));
        TEST_ASSERT(s.has(MultiLetterExtension::Zvksh));
    }

    // =========================================================================
    // 10. is_zk_extension & add_zk_extension Integration
    // =========================================================================
    {
        TEST_ASSERT(is_zk_extension("zk"));
        TEST_ASSERT(is_zk_extension("zkn"));
        TEST_ASSERT(is_zk_extension("zkr"));
        TEST_ASSERT(is_zk_extension("zks"));
        TEST_ASSERT(is_zk_extension("zkt"));
        TEST_ASSERT(is_zk_extension("zknd"));
        TEST_ASSERT(is_zk_extension("zkne"));
        TEST_ASSERT(is_zk_extension("zknh"));
        TEST_ASSERT(is_zk_extension("zksed"));
        TEST_ASSERT(is_zk_extension("zksh"));
        TEST_ASSERT(is_zk_extension("ZKND"));
        TEST_ASSERT(!is_zk_extension("zfoo"));
        TEST_ASSERT(!is_zk_extension("zvkn"));

        std::vector<std::string> ext_list;
        add_zk_extension(ext_list, "zk");
        TEST_ASSERT(std::ranges::find(ext_list, "zkn") != ext_list.end());
        TEST_ASSERT(std::ranges::find(ext_list, "zkr") != ext_list.end());
        TEST_ASSERT(std::ranges::find(ext_list, "zkt") != ext_list.end());

        // Deduplication test
        const auto size_before = ext_list.size();
        add_zk_extension(ext_list, "zkn");
        TEST_ASSERT(ext_list.size() == size_before);
    }
}

void test_track2_crypto_bitmanip_gating() {
    using namespace simrv::isa;

    // Zbkb and Zbkc provide scalar cryptographic bit-manipulation primitives:
    // CLMUL, CLMULH, CLMULR, PACK, PACKW.
    // They are constituent components of both Zkn and Zks aggregates.
    const auto no_b_misa = parse_isa_spec("rv64gc")->misa_bits;
    const auto b_misa = parse_isa_spec("rv64gcb")->misa_bits;

    // 1. When MISA has no B, empty ExtensionSet disables all crypto bitmanip
    const ExtensionSet empty_set{};
    TEST_ASSERT(!instruction_enabled(no_b_misa, empty_set, OperationId::CLMUL));
    TEST_ASSERT(!instruction_enabled(no_b_misa, empty_set, OperationId::CLMULH));
    TEST_ASSERT(!instruction_enabled(no_b_misa, empty_set, OperationId::CLMULR));
    TEST_ASSERT(!instruction_enabled(no_b_misa, empty_set, OperationId::PACK));
    TEST_ASSERT(!instruction_enabled(no_b_misa, empty_set, OperationId::PACKW));

    // 2. Aggregate zkn enables Zbkb and Zbkc -> enables all crypto bitmanip
    ExtensionSet zkn_set{};
    add_extension_to_set(zkn_set, "zkn");
    TEST_ASSERT(instruction_enabled(no_b_misa, zkn_set, OperationId::CLMUL));
    TEST_ASSERT(instruction_enabled(no_b_misa, zkn_set, OperationId::CLMULH));
    TEST_ASSERT(instruction_enabled(no_b_misa, zkn_set, OperationId::CLMULR));
    TEST_ASSERT(instruction_enabled(no_b_misa, zkn_set, OperationId::PACK));
    TEST_ASSERT(instruction_enabled(no_b_misa, zkn_set, OperationId::PACKW));

    // 3. Aggregate zks enables Zbkb and Zbkc -> enables all crypto bitmanip
    ExtensionSet zks_set{};
    add_extension_to_set(zks_set, "zks");
    TEST_ASSERT(instruction_enabled(no_b_misa, zks_set, OperationId::CLMUL));
    TEST_ASSERT(instruction_enabled(no_b_misa, zks_set, OperationId::CLMULH));
    TEST_ASSERT(instruction_enabled(no_b_misa, zks_set, OperationId::CLMULR));
    TEST_ASSERT(instruction_enabled(no_b_misa, zks_set, OperationId::PACK));
    TEST_ASSERT(instruction_enabled(no_b_misa, zks_set, OperationId::PACKW));

    // 4. Individual Zbkb enables pack and clmul
    ExtensionSet zbkb_set{};
    zbkb_set.set(MultiLetterExtension::Zbkb);
    TEST_ASSERT(instruction_enabled(no_b_misa, zbkb_set, OperationId::CLMUL));
    TEST_ASSERT(instruction_enabled(no_b_misa, zbkb_set, OperationId::PACK));

    // 5. Individual Zbkc enables clmul
    ExtensionSet zbkc_set{};
    zbkc_set.set(MultiLetterExtension::Zbkc);
    TEST_ASSERT(instruction_enabled(no_b_misa, zbkc_set, OperationId::CLMUL));
    TEST_ASSERT(instruction_enabled(no_b_misa, zbkc_set, OperationId::CLMULH));

    // 6. Non-crypto extensions do NOT enable crypto bitmanip when B is absent
    ExtensionSet other_set{};
    other_set.set(MultiLetterExtension::Zba);
    other_set.set(MultiLetterExtension::Zbs);
    other_set.set(MultiLetterExtension::Zicond);
    other_set.set(MultiLetterExtension::Zcmp);
    other_set.set(MultiLetterExtension::Zcmt);
    TEST_ASSERT(!instruction_enabled(no_b_misa, other_set, OperationId::CLMUL));
    TEST_ASSERT(!instruction_enabled(no_b_misa, other_set, OperationId::CLMULH));
    TEST_ASSERT(!instruction_enabled(no_b_misa, other_set, OperationId::PACK));

    // 7. When MISA has B, instruction_enabled returns true regardless of ExtensionSet
    TEST_ASSERT(instruction_enabled(b_misa, empty_set, OperationId::CLMUL));
    TEST_ASSERT(instruction_enabled(b_misa, empty_set, OperationId::PACK));

    // 8. Check classification helpers
    TEST_ASSERT(is_zb_crypto_operation(OperationId::CLMUL));
    TEST_ASSERT(is_zb_crypto_operation(OperationId::CLMULH));
    TEST_ASSERT(is_zb_crypto_operation(OperationId::CLMULR));
    TEST_ASSERT(is_zb_crypto_operation(OperationId::PACK));
    TEST_ASSERT(is_zb_crypto_operation(OperationId::PACKW));
    TEST_ASSERT(!is_zb_crypto_operation(OperationId::ADD));
    TEST_ASSERT(!is_zb_crypto_operation(OperationId::SUB));
    TEST_ASSERT(!is_zb_crypto_operation(OperationId::AND));

    TEST_ASSERT(is_zkn_operation(OperationId::CLMUL));
    TEST_ASSERT(is_zkn_operation(OperationId::PACK));
    TEST_ASSERT(!is_zkn_operation(OperationId::ADD));
}

void test_track2_mutual_exclusion_and_spec_separation() {
    using namespace simrv::isa;

    // Track 2C (Zcmp and Zcmt) specifies compressed multi-push/pop, double move,
    // and jump table dereference. These instructions repurpose Quadrant 2 encodings
    // that are mutually exclusive with Zcd (or standard C with D: C.FSWSP, C.FLDSP, C.FSDSP).
    //
    // Verify that IsaSpec accurately separates Zcd, Zcmp, and Zcmt across architectures.

    // 1. Standard rv64gc has C and D enabled (which implies Zcd), but neither Zcmp nor Zcmt
    const auto rv64gc = parse_isa_spec("rv64gc");
    TEST_ASSERT(rv64gc.has_value());
    TEST_ASSERT(rv64gc->has(IsaExtension::C));
    TEST_ASSERT(rv64gc->has(IsaExtension::D));
    TEST_ASSERT(!rv64gc->has_extension(MultiLetterExtension::Zcmp));
    TEST_ASSERT(!rv64gc->has_extension(MultiLetterExtension::Zcmt));

    // 2. rv64g_zcmp has D but not C, and has Zcmp
    const auto rv64g_zcmp = parse_isa_spec("rv64g_zcmp");
    TEST_ASSERT(rv64g_zcmp.has_value());
    TEST_ASSERT(!rv64g_zcmp->has(IsaExtension::C));
    TEST_ASSERT(rv64g_zcmp->has(IsaExtension::D));
    TEST_ASSERT(rv64g_zcmp->has_extension(MultiLetterExtension::Zcmp));
    TEST_ASSERT(!rv64g_zcmp->has_extension(MultiLetterExtension::Zcmt));

    // 3. rv64g_zcmt has D but not C, and has Zcmt
    const auto rv64g_zcmt = parse_isa_spec("rv64g_zcmt");
    TEST_ASSERT(rv64g_zcmt.has_value());
    TEST_ASSERT(!rv64g_zcmt->has(IsaExtension::C));
    TEST_ASSERT(rv64g_zcmt->has_extension(MultiLetterExtension::Zcmt));
    TEST_ASSERT(!rv64g_zcmt->has_extension(MultiLetterExtension::Zcmp));

    // 4. rv64g_zcmp_zcmt has both Zcmp and Zcmt
    const auto rv64g_both = parse_isa_spec("rv64g_zcmp_zcmt");
    TEST_ASSERT(rv64g_both.has_value());
    TEST_ASSERT(rv64g_both->has_extension(MultiLetterExtension::Zcmp));
    TEST_ASSERT(rv64g_both->has_extension(MultiLetterExtension::Zcmt));

    // 5. Dual-architecture RV32 parity
    const auto rv32gc = parse_isa_spec("rv32gc");
    TEST_ASSERT(rv32gc.has_value());
    TEST_ASSERT(rv32gc->has(IsaExtension::C));
    TEST_ASSERT(rv32gc->has(IsaExtension::D));
    TEST_ASSERT(!rv32gc->has_extension(MultiLetterExtension::Zcmp));
    TEST_ASSERT(!rv32gc->has_extension(MultiLetterExtension::Zcmt));

    const auto rv32g_both = parse_isa_spec("rv32g_zcmp_zcmt");
    TEST_ASSERT(rv32g_both.has_value());
    TEST_ASSERT(rv32g_both->has_extension(MultiLetterExtension::Zcmp));
    TEST_ASSERT(rv32g_both->has_extension(MultiLetterExtension::Zcmt));

    // 6. ExtensionSet independence between Zcd, Zcmp, and Zcmt
    ExtensionSet ext{};
    ext.set(MultiLetterExtension::Zcmp);
    TEST_ASSERT(ext.has(MultiLetterExtension::Zcmp));
    TEST_ASSERT(!ext.has(MultiLetterExtension::Zcmt));
    TEST_ASSERT(!ext.has(MultiLetterExtension::Zcd));

    ext.clear(MultiLetterExtension::Zcmp);
    ext.set(MultiLetterExtension::Zcmt);
    TEST_ASSERT(!ext.has(MultiLetterExtension::Zcmp));
    TEST_ASSERT(ext.has(MultiLetterExtension::Zcmt));
    TEST_ASSERT(!ext.has(MultiLetterExtension::Zcd));

    ext.set(MultiLetterExtension::Zcd);
    TEST_ASSERT(ext.has(MultiLetterExtension::Zcd));
    TEST_ASSERT(ext.has(MultiLetterExtension::Zcmt));
    TEST_ASSERT(!ext.has(MultiLetterExtension::Zcmp));
}

void test_track2b_scalar_crypto_gating() {
    using namespace simrv::isa;
    using enum OperationId;

    const auto misa = parse_isa_spec("rv64gc")->misa_bits;
    const ExtensionSet empty_set{};

    // All 29 scalar crypto operations must be disabled when ExtensionSet is empty
    const std::array<OperationId, 29> kAllZkOps = {
        AES32DSI,    AES32DSMI,   AES32ESI,    AES32ESMI,   AES64DS,     AES64DSM,
        AES64ES,     AES64ESM,    AES64IM,     AES64KS1I,   AES64KS2,    SHA256SIG0,
        SHA256SIG1,  SHA256SUM0,  SHA256SUM1,  SHA512SIG0,  SHA512SIG1,  SHA512SUM0,
        SHA512SUM1,  SHA512SIG0L, SHA512SIG0H, SHA512SIG1L, SHA512SIG1H, SHA512SUM0R,
        SHA512SUM1R, SM4ED,       SM4KS,       SM3P0,       SM3P1};

    for (const auto op : kAllZkOps) {
        TEST_ASSERT(!instruction_enabled(misa, empty_set, op));
    }

    // 1. Zkne enables encrypt and key schedule
    ExtensionSet zkne_set{};
    zkne_set.set(MultiLetterExtension::Zkne);
    if constexpr (simrv::xlen::kIsXLen64) {
        TEST_ASSERT(!instruction_enabled(misa, zkne_set, AES32ESI));
        TEST_ASSERT(!instruction_enabled(misa, zkne_set, AES32ESMI));
        TEST_ASSERT(instruction_enabled(misa, zkne_set, AES64ES));
        TEST_ASSERT(instruction_enabled(misa, zkne_set, AES64ESM));
        TEST_ASSERT(instruction_enabled(misa, zkne_set, AES64KS1I));
        TEST_ASSERT(instruction_enabled(misa, zkne_set, AES64KS2));
    } else {
        TEST_ASSERT(instruction_enabled(misa, zkne_set, AES32ESI));
        TEST_ASSERT(instruction_enabled(misa, zkne_set, AES32ESMI));
        TEST_ASSERT(!instruction_enabled(misa, zkne_set, AES64ES));
        TEST_ASSERT(!instruction_enabled(misa, zkne_set, AES64ESM));
        TEST_ASSERT(!instruction_enabled(misa, zkne_set, AES64KS1I));
        TEST_ASSERT(!instruction_enabled(misa, zkne_set, AES64KS2));
    }
    TEST_ASSERT(!instruction_enabled(misa, zkne_set, AES64DS));
    TEST_ASSERT(!instruction_enabled(misa, zkne_set, AES64IM));
    TEST_ASSERT(!instruction_enabled(misa, zkne_set, SHA256SIG0));

    // 2. Zknd enables decrypt and key schedule
    ExtensionSet zknd_set{};
    zknd_set.set(MultiLetterExtension::Zknd);
    if constexpr (simrv::xlen::kIsXLen64) {
        TEST_ASSERT(!instruction_enabled(misa, zknd_set, AES32DSI));
        TEST_ASSERT(!instruction_enabled(misa, zknd_set, AES32DSMI));
        TEST_ASSERT(instruction_enabled(misa, zknd_set, AES64DS));
        TEST_ASSERT(instruction_enabled(misa, zknd_set, AES64DSM));
        TEST_ASSERT(instruction_enabled(misa, zknd_set, AES64IM));
        TEST_ASSERT(instruction_enabled(misa, zknd_set, AES64KS1I));
        TEST_ASSERT(instruction_enabled(misa, zknd_set, AES64KS2));
    } else {
        TEST_ASSERT(instruction_enabled(misa, zknd_set, AES32DSI));
        TEST_ASSERT(instruction_enabled(misa, zknd_set, AES32DSMI));
        TEST_ASSERT(!instruction_enabled(misa, zknd_set, AES64DS));
        TEST_ASSERT(!instruction_enabled(misa, zknd_set, AES64DSM));
        TEST_ASSERT(!instruction_enabled(misa, zknd_set, AES64IM));
        TEST_ASSERT(!instruction_enabled(misa, zknd_set, AES64KS1I));
        TEST_ASSERT(!instruction_enabled(misa, zknd_set, AES64KS2));
    }
    TEST_ASSERT(!instruction_enabled(misa, zknd_set, AES64ES));

    // 3. Zknh enables SHA-256 and SHA-512
    ExtensionSet zknh_set{};
    zknh_set.set(MultiLetterExtension::Zknh);
    TEST_ASSERT(instruction_enabled(misa, zknh_set, SHA256SIG0));
    TEST_ASSERT(instruction_enabled(misa, zknh_set, SHA256SIG1));
    TEST_ASSERT(instruction_enabled(misa, zknh_set, SHA256SUM0));
    TEST_ASSERT(instruction_enabled(misa, zknh_set, SHA256SUM1));
    if constexpr (simrv::xlen::kIsXLen64) {
        TEST_ASSERT(instruction_enabled(misa, zknh_set, SHA512SIG0));
        TEST_ASSERT(instruction_enabled(misa, zknh_set, SHA512SIG1));
        TEST_ASSERT(instruction_enabled(misa, zknh_set, SHA512SUM0));
        TEST_ASSERT(instruction_enabled(misa, zknh_set, SHA512SUM1));
        TEST_ASSERT(!instruction_enabled(misa, zknh_set, SHA512SIG0L));
        TEST_ASSERT(!instruction_enabled(misa, zknh_set, SHA512SIG0H));
    } else {
        TEST_ASSERT(!instruction_enabled(misa, zknh_set, SHA512SIG0));
        TEST_ASSERT(!instruction_enabled(misa, zknh_set, SHA512SIG1));
        TEST_ASSERT(!instruction_enabled(misa, zknh_set, SHA512SUM0));
        TEST_ASSERT(!instruction_enabled(misa, zknh_set, SHA512SUM1));
        TEST_ASSERT(instruction_enabled(misa, zknh_set, SHA512SIG0L));
        TEST_ASSERT(instruction_enabled(misa, zknh_set, SHA512SIG0H));
    }
    TEST_ASSERT(!instruction_enabled(misa, zknh_set, SM4ED));

    // 4. Zksed enables SM4
    ExtensionSet zksed_set{};
    zksed_set.set(MultiLetterExtension::Zksed);
    TEST_ASSERT(instruction_enabled(misa, zksed_set, SM4ED));
    TEST_ASSERT(instruction_enabled(misa, zksed_set, SM4KS));
    TEST_ASSERT(!instruction_enabled(misa, zksed_set, SM3P0));

    // 5. Zksh enables SM3
    ExtensionSet zksh_set{};
    zksh_set.set(MultiLetterExtension::Zksh);
    TEST_ASSERT(instruction_enabled(misa, zksh_set, SM3P0));
    TEST_ASSERT(instruction_enabled(misa, zksh_set, SM3P1));
    TEST_ASSERT(!instruction_enabled(misa, zksh_set, SM4ED));

    // 6. Compound Zkn enables Zkne, Zknd, Zknh
    ExtensionSet zkn_set{};
    add_extension_to_set(zkn_set, "zkn");
    if constexpr (simrv::xlen::kIsXLen64) {
        TEST_ASSERT(instruction_enabled(misa, zkn_set, AES64ES));
        TEST_ASSERT(instruction_enabled(misa, zkn_set, AES64DS));
    } else {
        TEST_ASSERT(instruction_enabled(misa, zkn_set, AES32ESI));
        TEST_ASSERT(instruction_enabled(misa, zkn_set, AES32DSI));
    }
    TEST_ASSERT(instruction_enabled(misa, zkn_set, SHA256SIG0));
    TEST_ASSERT(!instruction_enabled(misa, zkn_set, SM4ED));
    TEST_ASSERT(!instruction_enabled(misa, zkn_set, SM3P0));

    // 7. Compound Zks enables Zksed, Zksh
    ExtensionSet zks_set{};
    add_extension_to_set(zks_set, "zks");
    TEST_ASSERT(instruction_enabled(misa, zks_set, SM4ED));
    TEST_ASSERT(instruction_enabled(misa, zks_set, SM3P0));
    TEST_ASSERT(!instruction_enabled(misa, zks_set, AES64ES));

    // 8. Compound Zk enables Zkn + Zkr + Zkt (not Zks)
    ExtensionSet zk_set{};
    add_extension_to_set(zk_set, "zk");
    if constexpr (simrv::xlen::kIsXLen64) {
        TEST_ASSERT(instruction_enabled(misa, zk_set, AES64ES));
        TEST_ASSERT(instruction_enabled(misa, zk_set, AES64DS));
    } else {
        TEST_ASSERT(instruction_enabled(misa, zk_set, AES32ESI));
        TEST_ASSERT(instruction_enabled(misa, zk_set, AES32DSI));
    }
    TEST_ASSERT(instruction_enabled(misa, zk_set, SHA256SIG0));
    TEST_ASSERT(!instruction_enabled(misa, zk_set, SM4ED));
    TEST_ASSERT(!instruction_enabled(misa, zk_set, SM3P0));

    // 9. Check width requirements (requires_rv64 and requires_rv32)
    namespace op_traits = simrv::pipeline::operation;

    const std::array<OperationId, 11> kRv64OnlyOps = {AES64ES,    AES64ESM,   AES64DS,   AES64DSM,
                                                      AES64IM,    AES64KS1I,  AES64KS2,  SHA512SIG0,
                                                      SHA512SIG1, SHA512SUM0, SHA512SUM1};
    for (const auto op : kRv64OnlyOps) {
        TEST_ASSERT(op_traits::requires_rv64(op));
        TEST_ASSERT(!op_traits::requires_rv32(op));
    }

    const std::array<OperationId, 10> kRv32OnlyOps = {
        AES32ESI,    AES32ESMI,   AES32DSI,    AES32DSMI,   SHA512SIG0L,
        SHA512SIG0H, SHA512SIG1L, SHA512SIG1H, SHA512SUM0R, SHA512SUM1R};
    for (const auto op : kRv32OnlyOps) {
        TEST_ASSERT(op_traits::requires_rv32(op));
        TEST_ASSERT(!op_traits::requires_rv64(op));
    }

    const std::array<OperationId, 8> kDualWidthOps = {
        SHA256SIG0, SHA256SIG1, SHA256SUM0, SHA256SUM1, SM4ED, SM4KS, SM3P0, SM3P1};
    for (const auto op : kDualWidthOps) {
        TEST_ASSERT(!op_traits::requires_rv64(op));
        TEST_ASSERT(!op_traits::requires_rv32(op));
    }
}

void test_track2a_vector_crypto() {
    using namespace simrv::isa;
    using namespace simrv::core;
    using simrv::xlen::CsrValue;

    auto encode_opp = [](uint32_t funct6, uint32_t vm, uint32_t vs2, uint32_t vs1, uint32_t vd) {
        return (funct6 << 26U) | (vm << 25U) | (vs2 << 20U) | (vs1 << 15U) | (2U << 12U) |
               (vd << 7U) | 0x77U;
    };

    // 1. Decode checks for all 21 Track 2A instructions + vm=0 ILLEGAL check
    TEST_ASSERT(simrv::pipeline::decoder(encode_opp(0x28U, 1U, 2U, 0x00U, 4U)) ==
                OperationId::VAESDM_VV);
    TEST_ASSERT(simrv::pipeline::decoder(encode_opp(0x28U, 1U, 2U, 0x01U, 4U)) ==
                OperationId::VAESDF_VV);
    TEST_ASSERT(simrv::pipeline::decoder(encode_opp(0x28U, 1U, 2U, 0x02U, 4U)) ==
                OperationId::VAESEM_VV);
    TEST_ASSERT(simrv::pipeline::decoder(encode_opp(0x28U, 1U, 2U, 0x03U, 4U)) ==
                OperationId::VAESEF_VV);
    TEST_ASSERT(simrv::pipeline::decoder(encode_opp(0x28U, 1U, 2U, 0x10U, 4U)) ==
                OperationId::VSM4R_VV);
    TEST_ASSERT(simrv::pipeline::decoder(encode_opp(0x28U, 1U, 2U, 0x11U, 4U)) ==
                OperationId::VGMUL_VV);
    TEST_ASSERT(simrv::pipeline::decoder(encode_opp(0x29U, 1U, 2U, 0x00U, 4U)) ==
                OperationId::VAESDM_VS);
    TEST_ASSERT(simrv::pipeline::decoder(encode_opp(0x29U, 1U, 2U, 0x01U, 4U)) ==
                OperationId::VAESDF_VS);
    TEST_ASSERT(simrv::pipeline::decoder(encode_opp(0x29U, 1U, 2U, 0x02U, 4U)) ==
                OperationId::VAESEM_VS);
    TEST_ASSERT(simrv::pipeline::decoder(encode_opp(0x29U, 1U, 2U, 0x03U, 4U)) ==
                OperationId::VAESEF_VS);
    TEST_ASSERT(simrv::pipeline::decoder(encode_opp(0x29U, 1U, 2U, 0x07U, 4U)) ==
                OperationId::VAESZ_VS);
    TEST_ASSERT(simrv::pipeline::decoder(encode_opp(0x29U, 1U, 2U, 0x10U, 4U)) ==
                OperationId::VSM4R_VS);
    TEST_ASSERT(simrv::pipeline::decoder(encode_opp(0x22U, 1U, 2U, 1U, 4U)) ==
                OperationId::VAESKF1_VI);
    TEST_ASSERT(simrv::pipeline::decoder(encode_opp(0x2AU, 1U, 2U, 2U, 4U)) ==
                OperationId::VAESKF2_VI);
    TEST_ASSERT(simrv::pipeline::decoder(encode_opp(0x2CU, 1U, 2U, 1U, 4U)) ==
                OperationId::VGHSH_VV);
    TEST_ASSERT(simrv::pipeline::decoder(encode_opp(0x2DU, 1U, 2U, 1U, 4U)) ==
                OperationId::VSHA2MS_VV);
    TEST_ASSERT(simrv::pipeline::decoder(encode_opp(0x2EU, 1U, 2U, 1U, 4U)) ==
                OperationId::VSHA2CH_VV);
    TEST_ASSERT(simrv::pipeline::decoder(encode_opp(0x2FU, 1U, 2U, 1U, 4U)) ==
                OperationId::VSHA2CL_VV);
    TEST_ASSERT(simrv::pipeline::decoder(encode_opp(0x20U, 1U, 2U, 1U, 4U)) ==
                OperationId::VSM3ME_VV);
    TEST_ASSERT(simrv::pipeline::decoder(encode_opp(0x2BU, 1U, 2U, 0U, 4U)) ==
                OperationId::VSM3C_VI);
    TEST_ASSERT(simrv::pipeline::decoder(encode_opp(0x21U, 1U, 2U, 0U, 4U)) ==
                OperationId::VSM4K_VI);
    TEST_ASSERT(simrv::pipeline::decoder(encode_opp(0x28U, 0U, 2U, 0x02U, 4U)) ==
                OperationId::ILLEGAL);

    // 2. Execution & gating tests
    constexpr simrv::xlen::Address kCodeAddr = 0x80001000ULL;
    std::array<simrv::xlen::Byte, 0x4000> ram{};
    Machine machine;
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.memory().initialize_mmu();
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::InstructionFast;

    auto& cpu = machine.primary_hart();
    cpu.state().priv = simrv::xlen::PrivilegeLevel::Machine;
    cpu.state().misa = kMisaDefault | misa_extension_bit(IsaExtension::V);
    cpu.state().mstatus |= MstatusBit::Vs;
    cpu.state().mtvec = 0x80003000ULL;
    cpu.state().vtype = 2U << 3U;  // SEW=32, LMUL=1
    cpu.state().vl = 4;
    cpu.state().vstart = 0;

    uint32_t pc_off = 0;
    auto run_insn = [&](uint32_t insn) {
        std::memcpy(ram.data() + 0x1000 + pc_off, &insn, sizeof(insn));
        cpu.state().pc = simrv::xlen::VirtAddr{kCodeAddr + pc_off};
        pc_off += 4U;
        cpu.state().mcause = 0;
        cpu.run_cycle(machine);
    };
    auto vec_reg = [&](uint32_t idx) -> auto& {
        return cpu.state().regs.read_vector(
            simrv::xlen::to_vec_reg_id(static_cast<simrv::xlen::RegId>(idx)));
    };

    // Negative gating: disabled Zvkned -> IllegalInstruction
    cpu.state().enabled_extensions = ExtensionSet{};
    run_insn(encode_opp(0x28U, 1U, 2U, 0x02U, 4U));
    TEST_ASSERT(cpu.state().mcause == static_cast<CsrValue>(ExceptionCode::IllegalInstruction));

    // Positive Zvkned: FIPS-197 Appendix B AES-128 full 10 rounds
    ExtensionSet zvk_ext{};
    zvk_ext.set(MultiLetterExtension::Zvkned);
    zvk_ext.set(MultiLetterExtension::Zvknhb);
    zvk_ext.set(MultiLetterExtension::Zvkg);
    zvk_ext.set(MultiLetterExtension::Zvksed);
    zvk_ext.set(MultiLetterExtension::Zvksh);
    zvk_ext.set(MultiLetterExtension::Zvkb);
    zvk_ext.set(MultiLetterExtension::Zvkt);
    cpu.state().enabled_extensions = zvk_ext;

    // Key: 2b7e1516 28aed2a6 abf71588 09cf4f3c (little-endian words)
    vec_reg(2).u32[0] = 0x16157e2bU;
    vec_reg(2).u32[1] = 0xa6d2ae28U;
    vec_reg(2).u32[2] = 0x8815f7abU;
    vec_reg(2).u32[3] = 0x3c4fcf09U;
    // Plaintext: 3243f6a8 885a308d 313198a2 e0370734
    vec_reg(4).u32[0] = 0xa8f64332U;
    vec_reg(4).u32[1] = 0x8d305a88U;
    vec_reg(4).u32[2] = 0xa2983131U;
    vec_reg(4).u32[3] = 0x340737e0U;

    run_insn(encode_opp(0x29U, 1U, 2U, 0x07U, 4U));  // vaesz.vs v4, v2
    TEST_ASSERT(cpu.state().mcause == 0);
    for (uint32_t r = 1U; r <= 10U; ++r) {
        run_insn(encode_opp(0x22U, 1U, 2U, r, 2U));  // vaeskf1.vi v2, v2, r
        TEST_ASSERT(cpu.state().mcause == 0);
        if (r < 10U) {
            run_insn(encode_opp(0x29U, 1U, 2U, 0x02U, 4U));  // vaesem.vs v4, v2
        } else {
            run_insn(encode_opp(0x29U, 1U, 2U, 0x03U, 4U));  // vaesef.vs v4, v2
        }
        TEST_ASSERT(cpu.state().mcause == 0);
    }
    // Expected FIPS-197 Appendix B ciphertext: 3925841d 02dc09fb dc118597 196a0b32
    TEST_ASSERT(vec_reg(4).u32[0] == 0x1d842539U);
    TEST_ASSERT(vec_reg(4).u32[1] == 0xfb09dc02U);
    TEST_ASSERT(vec_reg(4).u32[2] == 0x978511dcU);
    TEST_ASSERT(vec_reg(4).u32[3] == 0x320b6a19U);

    // Zvkg: vgmul.vv & vghsh.vv with GCM multiplicative identity H = 1 (byte 0 = 0x80)
    vec_reg(2).u32[0] = 0x00000080U;
    vec_reg(2).u32[1] = 0U;
    vec_reg(2).u32[2] = 0U;
    vec_reg(2).u32[3] = 0U;
    vec_reg(4).u32[0] = 0x11223344U;
    vec_reg(4).u32[1] = 0x55667788U;
    vec_reg(4).u32[2] = 0x99aabbccU;
    vec_reg(4).u32[3] = 0xddeeff00U;
    run_insn(encode_opp(0x28U, 1U, 2U, 0x11U, 4U));  // vgmul.vv v4, v2
    TEST_ASSERT(cpu.state().mcause == 0);
    TEST_ASSERT(vec_reg(4).u32[0] == 0x11223344U);
    TEST_ASSERT(vec_reg(4).u32[3] == 0xddeeff00U);

    vec_reg(1).u32[0] = 0x00000001U;
    vec_reg(1).u32[1] = 0U;
    vec_reg(1).u32[2] = 0U;
    vec_reg(1).u32[3] = 0U;
    run_insn(encode_opp(0x2CU, 1U, 2U, 1U, 4U));  // vghsh.vv v4, v2, v1
    TEST_ASSERT(cpu.state().mcause == 0);
    TEST_ASSERT(vec_reg(4).u32[0] == 0x11223345U);

    // Zvknha vs Zvknhb at SEW=64 (LMUL=2 -> 256-bit EGW)
    ExtensionSet zvknha_only{};
    zvknha_only.set(MultiLetterExtension::Zvknha);
    cpu.state().enabled_extensions = zvknha_only;
    cpu.state().vtype = (3U << 3U) | 1U;  // SEW=64, LMUL=2
    cpu.state().vl = 4;
    run_insn(encode_opp(0x2DU, 1U, 2U, 6U, 4U));  // vsha2ms.vv v4, v2, v6
    TEST_ASSERT(cpu.state().mcause == static_cast<CsrValue>(ExceptionCode::IllegalInstruction));

    cpu.state().enabled_extensions = zvk_ext;  // includes Zvknhb
    run_insn(encode_opp(0x2DU, 1U, 2U, 6U, 4U));
    TEST_ASSERT(cpu.state().mcause == 0);

    // Zvksh: vsm3me.vv requires EGW=256 (LMUL*VLEN >= 256 -> LMUL=1 at VLEN=128 must trap)
    cpu.state().vtype = (2U << 3U) | 0U;  // SEW=32, LMUL=1
    cpu.state().vl = 4;
    run_insn(encode_opp(0x20U, 1U, 2U, 6U, 4U));
    TEST_ASSERT(cpu.state().mcause == static_cast<CsrValue>(ExceptionCode::IllegalInstruction));

    cpu.state().vtype = (2U << 3U) | 1U;  // SEW=32, LMUL=2
    cpu.state().vl = 8;
    run_insn(encode_opp(0x20U, 1U, 2U, 6U, 4U));
    TEST_ASSERT(cpu.state().mcause == 0);

    // Zvksed: SM4 32-round GB/T 32907-2016 Appendix A.1 test vector
    cpu.state().vtype = (2U << 3U) | 0U;  // SEW=32, LMUL=1
    cpu.state().vl = 4;
    vec_reg(2).u32[0] = 0x01234567U ^ 0xa3b1bac6U;
    vec_reg(2).u32[1] = 0x89abcdefU ^ 0x56aa3350U;
    vec_reg(2).u32[2] = 0xfedcba98U ^ 0x677d9197U;
    vec_reg(2).u32[3] = 0x76543210U ^ 0xb27022dcU;
    vec_reg(4).u32[0] = 0x01234567U;
    vec_reg(4).u32[1] = 0x89abcdefU;
    vec_reg(4).u32[2] = 0xfedcba98U;
    vec_reg(4).u32[3] = 0x76543210U;
    for (uint32_t rnd = 0; rnd < 8U; ++rnd) {
        run_insn(encode_opp(0x21U, 1U, 2U, rnd, 2U));  // vsm4k.vi v2, v2, rnd
        TEST_ASSERT(cpu.state().mcause == 0);
        run_insn(encode_opp(0x29U, 1U, 2U, 0x10U, 4U));  // vsm4r.vs v4, v2
        TEST_ASSERT(cpu.state().mcause == 0);
    }
    // GB/T 32907-2016 Appendix A.1 ciphertext: 681edf34 d206965e 86b3e94f 536e4246
    TEST_ASSERT(vec_reg(4).u32[3] == 0x681edf34U);
    TEST_ASSERT(vec_reg(4).u32[2] == 0xd206965eU);
    TEST_ASSERT(vec_reg(4).u32[1] == 0x86b3e94fU);
    TEST_ASSERT(vec_reg(4).u32[0] == 0x536e4246U);
}

void test_track2c_zcmp_zcmt() {
    using namespace simrv::isa;
    using namespace simrv::core;
    using simrv::xlen::Byte;
    using simrv::xlen::CsrValue;
    using simrv::xlen::RegId;
    using simrv::xlen::Word;

    // 1. Direct compressed decoder checks
    TEST_ASSERT(simrv::pipeline::decode_compressed_op(0xB862U) == CM_PUSH);
    TEST_ASSERT(simrv::pipeline::decode_compressed_op(0xBA62U) == CM_POP);
    TEST_ASSERT(simrv::pipeline::decode_compressed_op(0xBC62U) == CM_POPRETZ);
    TEST_ASSERT(simrv::pipeline::decode_compressed_op(0xBE62U) == CM_POPRET);
    TEST_ASSERT(simrv::pipeline::decode_compressed_op(0xAC66U) == CM_MVA01S);
    TEST_ASSERT(simrv::pipeline::decode_compressed_op(0xAC26U) == CM_MVSA01);
    TEST_ASSERT(simrv::pipeline::decode_compressed_op(0xAC22U) == ILLEGAL);  // r1s == r2s
    TEST_ASSERT(simrv::pipeline::decode_compressed_op(0xB832U) == ILLEGAL);  // rlist < 4
    TEST_ASSERT(simrv::pipeline::decode_compressed_op(0xA002U) == CM_JT);
    TEST_ASSERT(simrv::pipeline::decode_compressed_op(0xA082U) == CM_JALT);

    // 2. Machine execution of Zcmp + Zcmt + jvt CSR
    constexpr Word kDramBase = 0x80000000ULL;
    Machine machine;
    std::array<Byte, 65536> backing{};
    machine.set_ram_for_testing(backing.data(), backing.size());
    machine.memory().initialize_mmu();
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::InstructionFast;
    auto& cpu = machine.primary_hart();
    cpu.state().priv = kPrivMachine;
    cpu.state().mtvec = kDramBase + 0x4000U;

    Word next_off = 0x1000U;
    const auto run_16 = [&](uint16_t insn16, Word custom_off = 0U) {
        const Word off = (custom_off != 0U) ? custom_off : next_off;
        if (custom_off == 0U) next_off += 4U;
        std::memcpy(backing.data() + off, &insn16, sizeof(insn16));
        cpu.state().pc = simrv::xlen::VirtAddr{kDramBase + off};
        cpu.state().mcause = 0;
        cpu.run_cycle(machine);
    };
    const auto run_32 = [&](uint32_t insn32, Word custom_off = 0U) {
        const Word off = (custom_off != 0U) ? custom_off : next_off;
        if (custom_off == 0U) next_off += 4U;
        std::memcpy(backing.data() + off, &insn32, sizeof(insn32));
        cpu.state().pc = simrv::xlen::VirtAddr{kDramBase + off};
        cpu.state().mcause = 0;
        cpu.run_cycle(machine);
    };

    // Negative gating: Zcmp/Zcmt disabled (with D disabled) -> IllegalInstruction
    cpu.state().misa &= ~(static_cast<CsrValue>(1ULL << ('D' - 'A')));
    cpu.state().enabled_extensions = ExtensionSet{};
    run_16(0xAC66U);  // cm.mva01s s0, s1
    TEST_ASSERT(cpu.state().mcause == static_cast<CsrValue>(ExceptionCode::IllegalInstruction));

    // Enable Zcmp + Zcmt
    ExtensionSet zc_ext{};
    zc_ext.set(MultiLetterExtension::Zcmp);
    zc_ext.set(MultiLetterExtension::Zcmt);
    cpu.state().enabled_extensions = zc_ext;

    // cm.mva01s s0, s1 (r1s=0, r2s=1 -> 0xAC66)
    cpu.state().regs.write(RegId::S0, 0x11223344U);
    cpu.state().regs.write(RegId::S1, 0x55667788U);
    run_16(0xAC66U);
    TEST_ASSERT(cpu.state().mcause == 0);
    TEST_ASSERT(cpu.state().regs.read(RegId::A0) == 0x11223344U);
    TEST_ASSERT(cpu.state().regs.read(RegId::A1) == 0x55667788U);

    // cm.mvsa01 s0, s1 (r1s=0, r2s=1 -> 0xAC26)
    cpu.state().regs.write(RegId::A0, 0xAABBCCDDU);
    cpu.state().regs.write(RegId::A1, 0x99887766U);
    run_16(0xAC26U);
    TEST_ASSERT(cpu.state().mcause == 0);
    TEST_ASSERT(cpu.state().regs.read(RegId::S0) == 0xAABBCCDDU);
    TEST_ASSERT(cpu.state().regs.read(RegId::S1) == 0x99887766U);

    // cm.push {ra, s0, s1}, -16 (rlist=6, spimm=0 -> 0xB862) and cm.popretz
    cpu.state().regs.write(RegId::Sp, kDramBase + 0x8000U);
    cpu.state().regs.write(RegId::Ra, kDramBase + 0x2200U);
    cpu.state().regs.write(RegId::S0, 0x12345678U);
    cpu.state().regs.write(RegId::S1, 0x87654321U);
    run_16(0xB862U);
    TEST_ASSERT(cpu.state().mcause == 0);
    TEST_ASSERT(cpu.state().regs.read(RegId::Sp) < kDramBase + 0x8000U);

    // Clobber registers then cm.popretz {ra, s0, s1} (0xBC62)
    cpu.state().regs.write(RegId::Ra, 0U);
    cpu.state().regs.write(RegId::S0, 0U);
    cpu.state().regs.write(RegId::S1, 0U);
    cpu.state().regs.write(RegId::A0, 0xDEADBEEFU);
    run_16(0xBC62U);
    TEST_ASSERT(cpu.state().mcause == 0);
    TEST_ASSERT(cpu.state().regs.read(RegId::Sp) == kDramBase + 0x8000U);
    TEST_ASSERT(cpu.state().regs.read(RegId::Ra) == kDramBase + 0x2200U);
    TEST_ASSERT(cpu.state().regs.read(RegId::S0) == 0x12345678U);
    TEST_ASSERT(cpu.state().regs.read(RegId::S1) == 0x87654321U);
    TEST_ASSERT(cpu.state().regs.read(RegId::A0) == 0U);
    TEST_ASSERT(cpu.state().pc.raw() == kDramBase + 0x2200U);

    // jvt CSR (0x017) WARL alignment + cm.jalt 32 (0xA082)
    cpu.state().regs.write(RegId::T0, kDramBase + 0x4005U);
    // csrrw x0, jvt(0x017), t0(x5) -> (0x017<<20)|(5<<15)|(1<<12)|0x73
    run_32((0x017U << 20U) | (5U << 15U) | (1U << 12U) | 0x73U);
    TEST_ASSERT(cpu.state().mcause == 0);
    TEST_ASSERT(cpu.state().jvt == kDramBase + 0x4000U);  // low 6 bits cleared (WARL)

    const uint32_t elem_bytes = cpu.state().regs.xlen / 8U;
    const Word target_addr = kDramBase + 0x3450U;
    std::memcpy(backing.data() + 0x4000U + (32U * elem_bytes), &target_addr, elem_bytes);
    run_16(0xA082U, 0x1100U);  // cm.jalt 32
    TEST_ASSERT(cpu.state().mcause == 0);
    TEST_ASSERT(cpu.state().regs.read(RegId::Ra) == kDramBase + 0x1102U);
    TEST_ASSERT(cpu.state().pc.raw() == target_addr);
}

void test_track3a_zfinx_zdinx_zhinx() {
    using namespace simrv::isa;
    using namespace simrv::core;
    using simrv::xlen::Byte;
    using simrv::xlen::CsrValue;
    using simrv::xlen::RegId;
    using simrv::xlen::Word;

    constexpr Word kDramBase = 0x80000000ULL;
    Machine machine;
    std::array<Byte, 65536> backing{};
    machine.set_ram_for_testing(backing.data(), backing.size());
    machine.memory().initialize_mmu();
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::InstructionFast;

    auto& cpu = machine.primary_hart();
    cpu.state().priv = kPrivMachine;
    cpu.state().mtvec = kDramBase + 0x4000U;
    // Clear F and D in misa and clear mstatus.FS
    cpu.state().misa &=
        ~(static_cast<CsrValue>(1ULL << ('F' - 'A')) | static_cast<CsrValue>(1ULL << ('D' - 'A')));
    cpu.state().mstatus &= ~MstatusBit::Fs;

    ExtensionSet zinx_ext{};
    zinx_ext.set(MultiLetterExtension::Zfinx);
    zinx_ext.set(MultiLetterExtension::Zdinx);
    zinx_ext.set(MultiLetterExtension::Zhinx);
    cpu.state().enabled_extensions = zinx_ext;

    Word next_off = 0x1000U;
    const auto run_32 = [&](uint32_t insn32) {
        const Word off = next_off;
        next_off += 4U;
        std::memcpy(backing.data() + off, &insn32, sizeof(insn32));
        cpu.state().pc = simrv::xlen::VirtAddr{kDramBase + off};
        cpu.state().mcause = 0;
        cpu.run_cycle(machine);
    };

    // 1. Zfinx fadd.s x12, x10, x11 (1.5f + 2.25f = 3.75f -> 0x40700000)
    // fadd.s: funct7=0x00, rm=0, opcode=0x53
    cpu.state().regs.write(RegId::A0, 0x3FC00000U);  // 1.5f
    cpu.state().regs.write(RegId::A1, 0x40100000U);  // 2.25f
    run_32((0x00U << 25U) | (11U << 20U) | (10U << 15U) | (0U << 12U) | (12U << 7U) | 0x53U);
    TEST_ASSERT(cpu.state().mcause == 0);
    TEST_ASSERT((cpu.state().regs.read(RegId::A2) & 0xFFFFFFFFU) == 0x40700000U);

    // 2. Sign-extension on RV64 for negative float (-1.0f = 0xBF800000) + x0 (0.0f)
    cpu.state().regs.write(RegId::A0, 0xBF800000U);
    run_32((0x00U << 25U) | (0U << 20U) | (10U << 15U) | (0U << 12U) | (12U << 7U) | 0x53U);
    TEST_ASSERT(cpu.state().mcause == 0);
    if constexpr (simrv::xlen::kIsXLen64) {
        TEST_ASSERT(cpu.state().regs.read(RegId::A2) == 0xFFFFFFFFBF800000ULL);
    } else {
        TEST_ASSERT(cpu.state().regs.read(RegId::A2) == 0xBF800000U);
    }

    // 3. FPR-only instructions (flw / fmv.x.w) must trap under Zfinx
    run_32((10U << 15U) | (2U << 12U) | (12U << 7U) | 0x07U);  // flw f12, 0(a0)
    TEST_ASSERT(cpu.state().mcause == static_cast<CsrValue>(ExceptionCode::IllegalInstruction));
    run_32((0x70U << 25U) | (10U << 15U) | (12U << 7U) | 0x53U);  // fmv.x.w a2, f10
    TEST_ASSERT(cpu.state().mcause == static_cast<CsrValue>(ExceptionCode::IllegalInstruction));

    // 4. Zhinx fadd.h x12, x10, x11 (1.5 + 2.5 = 4.0 -> 0x4400)
    cpu.state().regs.write(RegId::A0, 0x3E00U);  // 1.5h
    cpu.state().regs.write(RegId::A1, 0x4100U);  // 2.5h
    run_32((0x02U << 25U) | (11U << 20U) | (10U << 15U) | (0U << 12U) | (12U << 7U) | 0x53U);
    TEST_ASSERT(cpu.state().mcause == 0);
    TEST_ASSERT((cpu.state().regs.read(RegId::A2) & 0xFFFFU) == 0x4400U);

    // 5. Zdinx fadd.d (1.5 + 2.5 = 4.0 -> 0x4010000000000000ULL)
    if constexpr (simrv::xlen::kIsXLen64) {
        cpu.state().regs.write(RegId::A0, 0x3FF8000000000000ULL);  // 1.5
        cpu.state().regs.write(RegId::A1, 0x4004000000000000ULL);  // 2.5
        run_32((0x01U << 25U) | (11U << 20U) | (10U << 15U) | (0U << 12U) | (12U << 7U) | 0x53U);
        TEST_ASSERT(cpu.state().mcause == 0);
        TEST_ASSERT(cpu.state().regs.read(RegId::A2) == 0x4010000000000000ULL);
    } else {
        // RV32 even/odd GPR pair: (x10,x11) + (x12,x13) -> (x14,x15)
        cpu.state().regs.write(RegId::A0, 0x00000000U);
        cpu.state().regs.write(RegId::A1, 0x3FF80000U);  // 1.5
        cpu.state().regs.write(RegId::A2, 0x00000000U);
        cpu.state().regs.write(RegId::A3, 0x40040000U);  // 2.5
        run_32((0x01U << 25U) | (12U << 20U) | (10U << 15U) | (0U << 12U) | (14U << 7U) | 0x53U);
        TEST_ASSERT(cpu.state().mcause == 0);
        TEST_ASSERT(cpu.state().regs.read(RegId::A4) == 0x00000000U);
        TEST_ASSERT(cpu.state().regs.read(RegId::A5) == 0x40100000U);

        // Odd GPR index (x11) must raise IllegalInstruction on RV32 Zdinx
        run_32((0x01U << 25U) | (12U << 20U) | (11U << 15U) | (0U << 12U) | (14U << 7U) | 0x53U);
        TEST_ASSERT(cpu.state().mcause == static_cast<CsrValue>(ExceptionCode::IllegalInstruction));
    }
}

void test_track3b_priv_completeness() {
    using namespace simrv::isa;
    using namespace simrv::core;
    using simrv::xlen::Byte;
    using simrv::xlen::CsrValue;
    using simrv::xlen::PrivilegeLevel;
    using simrv::xlen::RegId;
    using simrv::xlen::Word;

    // 1. Smcsrind / Sscsrind indirect CSR access
    {
        Machine machine;
        std::array<Byte, 4096> ram{};
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.memory().initialize_mmu();
        CPU& cpu = machine.primary_hart();
        cpu.state().enabled_extensions.set(MultiLetterExtension::Smcsrind);
        cpu.state().enabled_extensions.set(MultiLetterExtension::Sscsrind);
        cpu.state().priv = PrivilegeLevel::Machine;

        auto res = cpu.write_csr(csr_num(Csr::Miselect), 0x30U);
        TEST_ASSERT(res.has_value());
        TEST_ASSERT(cpu.write_csr(csr_num(Csr::Mireg), 0x1234U).has_value());
        TEST_ASSERT(cpu.write_csr(csr_num(Csr::Mireg6), 0xABCDU).has_value());
        TEST_ASSERT(cpu.write_csr(csr_num(Csr::Miselect), 0x31U).has_value());
        TEST_ASSERT(cpu.write_csr(csr_num(Csr::Mireg), 0x5678U).has_value());
        TEST_ASSERT(cpu.write_csr(csr_num(Csr::Miselect), 0x30U).has_value());
        auto r1 = cpu.read_csr(csr_num(Csr::Mireg));
        auto r6 = cpu.read_csr(csr_num(Csr::Mireg6));
        TEST_ASSERT(r1.has_value() && *r1 == 0x1234U);
        TEST_ASSERT(r6.has_value() && *r6 == 0xABCDU);

        TEST_ASSERT(cpu.write_csr(csr_num(Csr::Siselect), 0x10U).has_value());
        TEST_ASSERT(cpu.write_csr(csr_num(Csr::Sireg2), 0x9999U).has_value());
        auto sr2 = cpu.read_csr(csr_num(Csr::Sireg2));
        TEST_ASSERT(sr2.has_value() && *sr2 == 0x9999U);
    }

    // 2. Ssdbltrp / Smdbltrp double-trap escalation and MRET/SRET clearing
    {
        Machine machine;
        std::array<Byte, 4096> ram{};
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.memory().initialize_mmu();
        CPU& cpu = machine.primary_hart();
        cpu.state().refresh_misa_status();
        cpu.state().enabled_extensions.set(MultiLetterExtension::Ssdbltrp);
        cpu.state().enabled_extensions.set(MultiLetterExtension::Smdbltrp);
        cpu.state().medeleg = (CsrValue{1} << 2U);  // Delegate IllegalInstruction to S-mode
        cpu.state().stvec = 0x100U;
        cpu.state().mtvec = 0x200U;
        if constexpr (simrv::xlen::kIsXLen64) {
            TEST_ASSERT(
                cpu.write_csr(csr_num(Csr::Menvcfg), static_cast<CsrValue>(uint64_t{1} << 59U))
                    .has_value());
        } else {
            TEST_ASSERT(cpu.write_csr(csr_num(Csr::Menvcfgh), CsrValue{1} << 27U).has_value());
        }

        cpu.state().priv = PrivilegeLevel::Supervisor;
        cpu.state().pc = simrv::xlen::VirtAddr{0x800U};
        cpu.raise_exception(static_cast<simrv::xlen::TrapCause>(ExceptionCode::IllegalInstruction),
                            0);
        TEST_ASSERT(cpu.state().priv == PrivilegeLevel::Supervisor);
        TEST_ASSERT(cpu.state().scause ==
                    static_cast<simrv::xlen::TrapCause>(ExceptionCode::IllegalInstruction));
        TEST_ASSERT((cpu.state().mstatus & MstatusBit::Sdt) != 0);

        // Second trap while SDT==1 escalates to M-mode with mcause=16 (DoubleTrap) and sets MDT=1
        cpu.raise_exception(static_cast<simrv::xlen::TrapCause>(ExceptionCode::IllegalInstruction),
                            0);
        TEST_ASSERT(cpu.state().priv == PrivilegeLevel::Machine);
        TEST_ASSERT(cpu.state().mcause ==
                    static_cast<simrv::xlen::TrapCause>(ExceptionCode::DoubleTrap));
        if constexpr (simrv::xlen::kIsXLen64) {
            TEST_ASSERT((cpu.state().mstatus & MstatusBit::Mdt) != 0);
        } else {
            TEST_ASSERT((cpu.state().mstatush & (CsrValue{1} << 10U)) != 0);
        }

        // MRET clears MDT
        cpu.mret();
        if constexpr (simrv::xlen::kIsXLen64) {
            TEST_ASSERT((cpu.state().mstatus & MstatusBit::Mdt) == 0);
        } else {
            TEST_ASSERT((cpu.state().mstatush & (CsrValue{1} << 10U)) == 0);
        }
    }

    // 3. Smrnmi CSRs and MNRET instruction
    {
        constexpr Word kDramBase = 0x80000000ULL;
        Machine machine;
        std::array<Byte, 65536> ram{};
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.memory().initialize_mmu();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::InstructionFast;
        CPU& cpu = machine.primary_hart();
        cpu.state().enabled_extensions.set(MultiLetterExtension::Smrnmi);
        cpu.state().priv = PrivilegeLevel::Machine;
        cpu.state().pc = simrv::xlen::VirtAddr{kDramBase + 0x1000U};

        const uint32_t mnret_insn = 0x70200073U;  // MNRET
        std::memcpy(ram.data() + 0x1000U, &mnret_insn, sizeof(mnret_insn));

        TEST_ASSERT(cpu.write_csr(csr_num(Csr::Mnscratch), 0xCAFEBABEU).has_value());
        TEST_ASSERT(cpu.write_csr(csr_num(Csr::Mnepc), kDramBase + 0x400U).has_value());
        TEST_ASSERT(cpu.write_csr(csr_num(Csr::Mncause), 0x10U).has_value());
        // MNPP = Supervisor (1 << 11)
        TEST_ASSERT(cpu.write_csr(csr_num(Csr::Mnstatus), CsrValue{1} << 11U).has_value());

        cpu.run_cycle(machine);
        TEST_ASSERT(cpu.state().pc.raw() == kDramBase + 0x400U);
        TEST_ASSERT(cpu.state().priv == PrivilegeLevel::Supervisor);
        TEST_ASSERT((cpu.state().mnstatus & (CsrValue{1} << 3U)) != 0);
    }
}

void test_rva23_profile_completeness_audit() {
    using namespace simrv::isa;
    using namespace simrv::core;

    const auto u64_spec = parse_isa_spec(profile_default_isa_string(ArchitectureProfile::Rva23U64));
    TEST_ASSERT(u64_spec.has_value());
    for (const auto ext : {
             MultiLetterExtension::Zicsr,       MultiLetterExtension::Zicntr,
             MultiLetterExtension::Zihpm,       MultiLetterExtension::Ziccif,
             MultiLetterExtension::Ziccrse,     MultiLetterExtension::Ziccamoa,
             MultiLetterExtension::Zicclsm,     MultiLetterExtension::Za64rs,
             MultiLetterExtension::Zihintpause, MultiLetterExtension::Zihintntl,
             MultiLetterExtension::Zic64b,      MultiLetterExtension::Zicbom,
             MultiLetterExtension::Zicbop,      MultiLetterExtension::Zicboz,
             MultiLetterExtension::Zicond,      MultiLetterExtension::Zimop,
             MultiLetterExtension::Zcmop,       MultiLetterExtension::Zcb,
             MultiLetterExtension::Zca,         MultiLetterExtension::Zcd,
             MultiLetterExtension::Zfa,         MultiLetterExtension::Zfhmin,
             MultiLetterExtension::Zawrs,       MultiLetterExtension::Zkt,
             MultiLetterExtension::Zvfhmin,     MultiLetterExtension::Zvbb,
             MultiLetterExtension::Zvkb,        MultiLetterExtension::Zvkt,
             MultiLetterExtension::Zmmul,       MultiLetterExtension::Zaamo,
             MultiLetterExtension::Zalrsc,      MultiLetterExtension::Zba,
             MultiLetterExtension::Zbb,         MultiLetterExtension::Zbs,
             MultiLetterExtension::Supm,
         }) {
        TEST_ASSERT(u64_spec->has_extension(ext));
    }
    TEST_ASSERT(!u64_spec->has_extension(MultiLetterExtension::Sstc));
    TEST_ASSERT(!u64_spec->has_extension(MultiLetterExtension::Svpbmt));
    TEST_ASSERT(!u64_spec->has_extension(MultiLetterExtension::Ssstateen));

    const auto s64_spec = parse_isa_spec(profile_default_isa_string(ArchitectureProfile::Rva23S64));
    TEST_ASSERT(s64_spec.has_value());
    for (const auto ext : {
             MultiLetterExtension::Zifencei,     MultiLetterExtension::Ss1p13,
             MultiLetterExtension::Svbare,       MultiLetterExtension::Sv39,
             MultiLetterExtension::Svade,        MultiLetterExtension::Ssccptr,
             MultiLetterExtension::Sstvecd,      MultiLetterExtension::Sstvala,
             MultiLetterExtension::Sscounterenw, MultiLetterExtension::Svpbmt,
             MultiLetterExtension::Svinval,      MultiLetterExtension::Svnapot,
             MultiLetterExtension::Sstc,         MultiLetterExtension::Sscofpmf,
             MultiLetterExtension::Ssnpm,        MultiLetterExtension::Ssu64xl,
             MultiLetterExtension::Ssstateen,    MultiLetterExtension::Shcounterenw,
             MultiLetterExtension::Shvstvala,    MultiLetterExtension::Shtvala,
             MultiLetterExtension::Shvstvecd,    MultiLetterExtension::Shvsatpa,
             MultiLetterExtension::Shgatpa,      MultiLetterExtension::Zcb,
             MultiLetterExtension::Zvbb,         MultiLetterExtension::Supm,
         }) {
        TEST_ASSERT(s64_spec->has_extension(ext));
    }

    if constexpr (simrv::xlen::kIsXLen64) {
        Machine machine;
        std::array<simrv::xlen::Byte, 65536> backing{};
        machine.set_ram_for_testing(backing.data(), backing.size());
        TEST_ASSERT(machine.initialize().has_value());
        const auto& cpu = machine.primary_hart();
        TEST_ASSERT(misa_has_extension(cpu.state().misa, IsaExtension::H));
        TEST_ASSERT(cpu.state().enabled_extensions.has(MultiLetterExtension::Zcb));
        TEST_ASSERT(cpu.state().enabled_extensions.has(MultiLetterExtension::Zvbb));
        TEST_ASSERT(cpu.state().enabled_extensions.has(MultiLetterExtension::Ssstateen));
        TEST_ASSERT(cpu.state().enabled_extensions.has(MultiLetterExtension::Sstc));
    }
}

void test_spec_pass2_smcntrpmf_stateen_aia_cfi() {
    using namespace simrv;
    using namespace simrv::core;
    using namespace simrv::isa;
    using namespace simrv::xlen;

    auto write_word_at = [](auto& buf, uint64_t offset, uint32_t word) {
        std::memcpy(buf.data() + offset, &word, sizeof(word));
    };

    auto make_machine = [](std::string_view isa_str) {
        MachineConfig cfg{};
        const auto spec = parse_isa_spec(isa_str).value();
        cfg.isa.isa_preset = spec.misa_bits;
        cfg.isa.enabled_extensions = spec.extension_set;
        cfg.isa.misa_override = true;
        auto machine = std::make_unique<Machine>(cfg);
        machine->primary_hart().state().misa = misa_with_mxl(spec.misa_bits, SIMRV_XLEN);
        machine->primary_hart().state().refresh_misa_status();
        machine->primary_hart().state().enabled_extensions = spec.extension_set;
        return machine;
    };

    // 1. Smcntrpmf: mcyclecfg / minstretcfg privilege-mode filtering
    {
        auto machine = make_machine((SIMRV_XLEN == 64) ? "rv64gc" : "rv32gc");
        auto& cpu = machine->primary_hart();
        TEST_ASSERT(TrapController::check_csr_access(cpu.state(),
                                                     CsrNumber{csr_addr(Csr::Mcyclecfg)},
                                                     true) == ExceptionCode::IllegalInstruction);
        TEST_ASSERT(TrapController::check_csr_access(cpu.state(),
                                                     CsrNumber{csr_addr(Csr::Minstretcfg)},
                                                     true) == ExceptionCode::IllegalInstruction);
    }
    {
        auto machine = make_machine((SIMRV_XLEN == 64) ? "rv64gc_smcntrpmf" : "rv32gc_smcntrpmf");
        std::array<simrv::xlen::Byte, 65536> backing{};
        machine->set_ram_for_testing(backing.data(), backing.size());
        machine->memory().initialize_mmu();
        auto& cpu = machine->primary_hart();
        cpu.state().priv = PrivilegeLevel::Machine;
        write_word_at(backing, 0x1000, 0x00000013U);  // nop
        write_word_at(backing, 0x1004, 0x00000013U);  // nop
        const uint64_t minh = uint64_t{1} << 62U;
        if constexpr (SIMRV_XLEN == 64) {
            TEST_ASSERT(cpu.csr_file.write(Csr::Mcyclecfg, minh).has_value());
            TEST_ASSERT(cpu.csr_file.write(Csr::Minstretcfg, minh).has_value());
        } else {
            TEST_ASSERT(cpu.csr_file.write(Csr::Mcyclecfgh, 1U << 30U).has_value());
            TEST_ASSERT(cpu.csr_file.write(Csr::Minstretcfgh, 1U << 30U).has_value());
        }
        const auto cyc0 = cpu.clint_mmio.mcycle;
        const auto inst0 = cpu.state().minstret;
        const uint64_t kDramBase = simrv::memory::kDramBaseAddress.raw();
        cpu.state().pc = VirtAddr{kDramBase + 0x1000};
        cpu.run_cycle(*machine);
        TEST_ASSERT(cpu.clint_mmio.mcycle == cyc0);
        TEST_ASSERT(cpu.state().minstret == inst0);

        if constexpr (SIMRV_XLEN == 64) {
            TEST_ASSERT(cpu.csr_file.write(Csr::Mcyclecfg, 0).has_value());
            TEST_ASSERT(cpu.csr_file.write(Csr::Minstretcfg, 0).has_value());
        } else {
            TEST_ASSERT(cpu.csr_file.write(Csr::Mcyclecfgh, 0).has_value());
            TEST_ASSERT(cpu.csr_file.write(Csr::Minstretcfgh, 0).has_value());
        }
        cpu.state().pc = VirtAddr{kDramBase + 0x1000};
        cpu.run_cycle(*machine);
        TEST_ASSERT(cpu.clint_mmio.mcycle > cyc0);
        TEST_ASSERT(cpu.state().minstret == inst0 + 1);
    }

    // 2. Smstateen/Ssstateen bit-level gating (JVT, CONTEXT, CSRIND)
    {
        auto machine = make_machine((SIMRV_XLEN == 64)
                                        ? "rv64imafdch_zcmt_sdtrig_ssstateen_smcsrind_sscsrind"
                                        : "rv32imafdch_zcmt_sdtrig_ssstateen_smcsrind_sscsrind");
        auto& cpu = machine->primary_hart();
        cpu.state().priv = PrivilegeLevel::Supervisor;
        cpu.state().virt = false;
        cpu.state().mstateen[0] = 0;
        TEST_ASSERT(TrapController::check_csr_access(cpu.state(), CsrNumber{csr_addr(Csr::Jvt)},
                                                     false) == ExceptionCode::IllegalInstruction);
        TEST_ASSERT(TrapController::check_csr_access(cpu.state(),
                                                     CsrNumber{csr_addr(Csr::Scontext)},
                                                     false) == ExceptionCode::IllegalInstruction);
        TEST_ASSERT(TrapController::check_csr_access(cpu.state(),
                                                     CsrNumber{csr_addr(Csr::Siselect)},
                                                     false) == ExceptionCode::IllegalInstruction);

        cpu.state().mstateen[0] = (uint64_t{1} << 2U) | (uint64_t{1} << 57U) | (uint64_t{1} << 60U);
        TEST_ASSERT(TrapController::check_csr_access(cpu.state(), CsrNumber{csr_addr(Csr::Jvt)},
                                                     false) == std::nullopt);
        TEST_ASSERT(TrapController::check_csr_access(
                        cpu.state(), CsrNumber{csr_addr(Csr::Scontext)}, false) == std::nullopt);
        TEST_ASSERT(TrapController::check_csr_access(
                        cpu.state(), CsrNumber{csr_addr(Csr::Siselect)}, false) == std::nullopt);

        cpu.state().virt = true;
        cpu.state().hstateen[0] = 0;
        TEST_ASSERT(TrapController::check_csr_access(cpu.state(), CsrNumber{csr_addr(Csr::Jvt)},
                                                     false) == ExceptionCode::VirtualInstruction);
        TEST_ASSERT(TrapController::check_csr_access(cpu.state(),
                                                     CsrNumber{csr_addr(Csr::Scontext)},
                                                     false) == ExceptionCode::VirtualInstruction);
        TEST_ASSERT(TrapController::check_csr_access(cpu.state(),
                                                     CsrNumber{csr_addr(Csr::Siselect)},
                                                     false) == ExceptionCode::VirtualInstruction);
    }

    // 3. Smaia / Ssaia core CSRs
    {
        auto machine =
            make_machine((SIMRV_XLEN == 64) ? "rv64gch_smaia_ssaia" : "rv32gch_smaia_ssaia");
        auto& cpu = machine->primary_hart();
        TEST_ASSERT(cpu.state().enabled_extensions.has(MultiLetterExtension::Smaia));
        TEST_ASSERT(cpu.state().enabled_extensions.has(MultiLetterExtension::Ssaia));
        cpu.state().priv = PrivilegeLevel::Machine;
        TEST_ASSERT(cpu.csr_file.write(Csr::Mvien, 0x222U).has_value());
        TEST_ASSERT(cpu.csr_file.read(Csr::Mvien).value() == 0x222U);
        TEST_ASSERT(cpu.csr_file.write(Csr::Hvien, 0x444U).has_value());
        TEST_ASSERT(cpu.csr_file.read(Csr::Hvien).value() == 0x444U);
        cpu.state().mie = (1U << 11U);
        cpu.state().mip = (1U << 11U);
        const auto mtopi = cpu.csr_file.read(Csr::Mtopi).value();
        TEST_ASSERT((mtopi >> 16U) == 11U);
        TEST_ASSERT((mtopi & 0xFFU) == 1U);
    }

    // 4. Zicfilp + Zicfiss (CFI)
    {
        auto machine =
            make_machine((SIMRV_XLEN == 64) ? "rv64gc_zicfilp_zicfiss" : "rv32gc_zicfilp_zicfiss");
        std::array<simrv::xlen::Byte, 65536> backing{};
        machine->set_ram_for_testing(backing.data(), backing.size());
        machine->memory().initialize_mmu();
        auto& cpu = machine->primary_hart();
        cpu.state().priv = PrivilegeLevel::Supervisor;
        cpu.state().virt = false;
        // Enable LPE (bit 2) and SSE (bit 3) in menvcfg
        cpu.state().menvcfg |= (uint64_t{1} << 2U) | (uint64_t{1} << 3U);

        // Zicfilp: JALR x0, 0(x10) sets elp=true; LPAD with matching x7 clears elp
        const uint64_t kDramBase = simrv::memory::kDramBaseAddress.raw();
        const uint64_t code_off = 0x1000;
        const uint64_t target_off = 0x1020;
        const uint64_t code_base = kDramBase + code_off;
        const uint64_t target_pc = kDramBase + target_off;
        cpu.state().regs.write(RegId::A0, target_pc);
        cpu.state().regs.write(RegId::T2, 0x12345ULL << 12U);
        write_word_at(backing, code_off, 0x00050067U);                  // jalr x0, 0(x10)
        write_word_at(backing, target_off, (0x12345U << 12U) | 0x17U);  // lpad 0x12345
        write_word_at(backing, target_off + 4, 0x00000013U);            // nop
        cpu.state().pc = VirtAddr{code_base};
        cpu.run_cycle(*machine);  // jalr -> target_pc, elp = true
        TEST_ASSERT(cpu.state().pc.raw() == target_pc);
        TEST_ASSERT(cpu.state().elp);
        cpu.run_cycle(*machine);  // lpad -> elp = false
        TEST_ASSERT(!cpu.state().elp);

        // Zicfilp: ELP mismatch raises SoftwareCheck (18) with tval=2
        cpu.state().elp = true;
        cpu.state().mtvec = kDramBase + 0x2000;
        write_word_at(backing, 0x2000, 0x00000013U);
        cpu.run_cycle(*machine);
        TEST_ASSERT(cpu.state().priv == PrivilegeLevel::Machine);
        TEST_ASSERT(cpu.state().mcause ==
                    static_cast<xlen::TrapCause>(ExceptionCode::SoftwareCheck));
        TEST_ASSERT(cpu.state().mtval == 2U);
        TEST_ASSERT(!cpu.state().elp);
        if constexpr (SIMRV_XLEN == 64) {
            TEST_ASSERT((cpu.state().mstatus & MstatusBit::Mpelp) != 0);
        } else {
            TEST_ASSERT((cpu.state().mstatush & (1U << 9U)) != 0);
        }
        TrapController::mret(cpu.state());
        TEST_ASSERT(cpu.state().elp);
        cpu.state().elp = false;

        // Zicfiss: SSRDP, SSPUSH x1, SSPOPCHK x1, SSAMOSWAP.W
        cpu.state().priv = PrivilegeLevel::Supervisor;
        const uint64_t ssp_base = kDramBase + 0x4000;
        cpu.state().ssp = ssp_base;
        cpu.state().regs.write(RegId::Ra, 0xCAFEBABEU);
        const uint64_t ss_off = 0x3000;
        write_word_at(backing, ss_off + 0, 0xCDC04573U);   // ssrdp x10 (mop.r.28 rd=x10, rs1=x0)
        write_word_at(backing, ss_off + 4, 0xCE104073U);   // sspush x1 (mop.rr.7 rs2=x1)
        write_word_at(backing, ss_off + 8, 0xCDC0C073U);   // sspopchk x1 (mop.r.28 rs1=x1)
        write_word_at(backing, ss_off + 12, 0xCDC0C073U);  // sspopchk x1 (mismatch!)
        cpu.state().pc = VirtAddr{kDramBase + ss_off};
        cpu.run_cycle(*machine);  // ssrdp x10
        TEST_ASSERT(cpu.state().regs.read(RegId::A0) == ssp_base);
        cpu.run_cycle(*machine);  // sspush x1
        const uint64_t expected_ssp = (SIMRV_XLEN == 64) ? (ssp_base - 8U) : (ssp_base - 4U);
        TEST_ASSERT(cpu.state().ssp == expected_ssp);
        cpu.run_cycle(*machine);  // sspopchk x1 (match)
        TEST_ASSERT(cpu.state().ssp == ssp_base);
        cpu.state().regs.write(RegId::Ra, 0xDEADBEEFU);
        cpu.run_cycle(*machine);  // sspopchk x1 (mismatch -> SoftwareCheck tval=3)
        TEST_ASSERT(cpu.state().mcause ==
                    static_cast<xlen::TrapCause>(ExceptionCode::SoftwareCheck));
        TEST_ASSERT(cpu.state().mtval == 3U);

        // SSAMOSWAP.W
        cpu.state().priv = PrivilegeLevel::Supervisor;
        cpu.state().ssp = ssp_base;
        write_word_at(backing, 0x4000, 0x11223344U);
        cpu.state().regs.write(RegId::A0, ssp_base);
        cpu.state().regs.write(RegId::A1, 0x55667788U);
        // ssamoswap.w x12, x11, (x10): funct5=0x09 -> 0x48b5262f
        write_word_at(backing, 0x3100, 0x48b5262fU);
        cpu.state().pc = VirtAddr{kDramBase + 0x3100};
        cpu.run_cycle(*machine);
        TEST_ASSERT(static_cast<uint32_t>(cpu.state().regs.read(RegId::A2)) == 0x11223344U);
        uint32_t swapped = 0;
        std::memcpy(&swapped, backing.data() + 0x4000, 4);
        TEST_ASSERT(swapped == 0x55667788U);
    }
}

}  // namespace

int main() {
    std::cout << "[RUN] test_zihintntl_encodings" << std::endl;
    test_zihintntl_encodings();
    std::cout << "[PASS] test_zihintntl_encodings" << std::endl;
    std::cout << "[RUN] test_zihintpause_encoding" << std::endl;
    test_zihintpause_encoding();
    std::cout << "[PASS] test_zihintpause_encoding" << std::endl;
    std::cout << "[RUN] test_zifencei_flushes_local_instruction_state" << std::endl;
    test_zifencei_flushes_local_instruction_state();
    std::cout << "[PASS] test_zifencei_flushes_local_instruction_state" << std::endl;
    std::cout << "[RUN] test_svade_faults_on_clear_accessed_and_dirty_bits" << std::endl;
    test_svade_faults_on_clear_accessed_and_dirty_bits();
    std::cout << "[PASS] test_svade_faults_on_clear_accessed_and_dirty_bits" << std::endl;
    std::cout << "[RUN] test_sstvecd_and_sstvala_supervisor_traps" << std::endl;
    test_sstvecd_and_sstvala_supervisor_traps();
    std::cout << "[PASS] test_sstvecd_and_sstvala_supervisor_traps" << std::endl;

    std::cout << "[RUN] test_zicclsm_unaligned_main_memory" << std::endl;
    test_zicclsm_unaligned_main_memory();
    std::cout << "[PASS] test_zicclsm_unaligned_main_memory" << std::endl;

    std::cout << "[RUN] test_za64rs_reservation_set" << std::endl;
    test_za64rs_reservation_set();
    std::cout << "[PASS] test_za64rs_reservation_set" << std::endl;

    std::cout << "[RUN] test_ziccrse_eventual_lr_sc_success" << std::endl;
    test_ziccrse_eventual_lr_sc_success();
    std::cout << "[PASS] test_ziccrse_eventual_lr_sc_success" << std::endl;

    std::cout << "[RUN] test_ziccif_atomic_instruction_fetch_under_smp_store" << std::endl;
    test_ziccif_atomic_instruction_fetch_under_smp_store();
    std::cout << "[PASS] test_ziccif_atomic_instruction_fetch_under_smp_store" << std::endl;

    std::cout << "[RUN] test_track2c_zcmp_zcmt" << std::endl;
    test_track2c_zcmp_zcmt();
    std::cout << "[PASS] test_track2c_zcmp_zcmt" << std::endl;

    std::cout << "[RUN] test_track3a_zfinx_zdinx_zhinx" << std::endl;
    test_track3a_zfinx_zdinx_zhinx();
    std::cout << "[PASS] test_track3a_zfinx_zdinx_zhinx" << std::endl;

    std::cout << "[RUN] test_track3b_priv_completeness" << std::endl;
    test_track3b_priv_completeness();
    std::cout << "[PASS] test_track3b_priv_completeness" << std::endl;

    std::cout << "[RUN] test_rva23_profile_completeness_audit" << std::endl;
    test_rva23_profile_completeness_audit();
    std::cout << "[PASS] test_rva23_profile_completeness_audit" << std::endl;

    std::cout << "[RUN] test_spec_pass2_smcntrpmf_stateen_aia_cfi" << std::endl;
    test_spec_pass2_smcntrpmf_stateen_aia_cfi();
    std::cout << "[PASS] test_spec_pass2_smcntrpmf_stateen_aia_cfi" << std::endl;
#if SIMRV_XLEN == 64
    std::cout << "[RUN] test_svpbmt_noncacheable_and_io_accesses" << std::endl;
    test_svpbmt_noncacheable_and_io_accesses();
    std::cout << "[PASS] test_svpbmt_noncacheable_and_io_accesses" << std::endl;
    std::cout << "[RUN] test_svpbmt_io_access_ordering" << std::endl;
    test_svpbmt_io_access_ordering();
    std::cout << "[PASS] test_svpbmt_io_access_ordering" << std::endl;
#endif
    std::cout << "[RUN] test_ziccamoa_main_memory_atomics" << std::endl;
    test_ziccamoa_main_memory_atomics();
    std::cout << "[PASS] test_ziccamoa_main_memory_atomics" << std::endl;
    std::cout << "[RUN] test_zfa_scalar_instructions" << std::endl;
    test_zfa_scalar_instructions();
    std::cout << "[PASS] test_zfa_scalar_instructions" << std::endl;
    std::cout << "[RUN] test_zfhmin_scalar_instructions" << std::endl;
    test_zfhmin_scalar_instructions();
    std::cout << "[PASS] test_zfhmin_scalar_instructions" << std::endl;
    std::cout << "[RUN] test_binary16_narrowing_rounding_and_flags" << std::endl;
    test_binary16_narrowing_rounding_and_flags();
    std::cout << "[PASS] test_binary16_narrowing_rounding_and_flags" << std::endl;
    std::cout << "[RUN] test_scalar_rmm_rounding" << std::endl;
    test_scalar_rmm_rounding();
    std::cout << "[PASS] test_scalar_rmm_rounding" << std::endl;
    std::cout << "[RUN] test_zvfhmin_vector_conversions" << std::endl;
    test_zvfhmin_vector_conversions();
    std::cout << "[PASS] test_zvfhmin_vector_conversions" << std::endl;

    std::cout << "[RUN] test_zic64b_cache_geometry" << std::endl;
    test_zic64b_cache_geometry();
    std::cout << "[PASS] test_zic64b_cache_geometry" << std::endl;

    std::cout << "[RUN] test_zicond_semantics" << std::endl;
    test_zicond_semantics();
    std::cout << "[PASS] test_zicond_semantics" << std::endl;

    std::cout << "[RUN] test_cmo_decoder_and_predicates" << std::endl;
    test_cmo_decoder_and_predicates();
    std::cout << "[PASS] test_cmo_decoder_and_predicates" << std::endl;

    std::cout << "[RUN] test_svinval_decoder" << std::endl;
    test_svinval_decoder();
    std::cout << "[PASS] test_svinval_decoder" << std::endl;

    std::cout << "[RUN] test_zcmop_noop_decompression" << std::endl;
    test_zcmop_noop_decompression();
    std::cout << "[PASS] test_zcmop_noop_decompression" << std::endl;

    std::cout << "[RUN] test_zawrs_instructions" << std::endl;
    test_zawrs_instructions();
    std::cout << "[PASS] test_zawrs_instructions" << std::endl;

    std::cout << "[RUN] test_ssu64xl_user_xlen_csr" << std::endl;
    test_ssu64xl_user_xlen_csr();
    std::cout << "[PASS] test_ssu64xl_user_xlen_csr" << std::endl;

    std::cout << "[RUN] test_supm_user_data_pointer_masking" << std::endl;
    test_supm_user_data_pointer_masking();
    std::cout << "[PASS] test_supm_user_data_pointer_masking" << std::endl;

    std::cout << "[RUN] test_sscofpmf_counter_overflow_and_filtering" << std::endl;
    test_sscofpmf_counter_overflow_and_filtering();
    std::cout << "[PASS] test_sscofpmf_counter_overflow_and_filtering" << std::endl;
    std::cout << "[RUN] test_sscounterenw_allows_enabled_hpm_counters" << std::endl;
    test_sscounterenw_allows_enabled_hpm_counters();
    std::cout << "[PASS] test_sscounterenw_allows_enabled_hpm_counters" << std::endl;
    std::cout << "[RUN] test_sha_state_enable_csrs" << std::endl;
    test_sha_state_enable_csrs();
    std::cout << "[PASS] test_sha_state_enable_csrs" << std::endl;
    std::cout << "[RUN] test_sha_guest_extension_state" << std::endl;
    test_sha_guest_extension_state();
    std::cout << "[PASS] test_sha_guest_extension_state" << std::endl;
    std::cout << "[RUN] test_sha_hideleg_warl_mask" << std::endl;
    test_sha_hideleg_warl_mask();
    std::cout << "[PASS] test_sha_hideleg_warl_mask" << std::endl;
    std::cout << "[RUN] test_sha_hfence_instructions" << std::endl;
    test_sha_hfence_instructions();
    std::cout << "[PASS] test_sha_hfence_instructions" << std::endl;
    std::cout << "[RUN] test_sha_hypervisor_virtual_load_store" << std::endl;
    test_sha_hypervisor_virtual_load_store();
    std::cout << "[PASS] test_sha_hypervisor_virtual_load_store" << std::endl;
#if SIMRV_XLEN == 64
    std::cout << "[RUN] test_sha_guest_return_transitions" << std::endl;
    test_sha_guest_return_transitions();
    std::cout << "[PASS] test_sha_guest_return_transitions" << std::endl;
#endif
    std::cout << "[RUN] test_sha_guest_exception_routing" << std::endl;
    test_sha_guest_exception_routing();
    std::cout << "[PASS] test_sha_guest_exception_routing" << std::endl;
    std::cout << "[RUN] test_sha_shtvala_guest_physical_fault_value" << std::endl;
    test_sha_shtvala_guest_physical_fault_value();
    std::cout << "[PASS] test_sha_shtvala_guest_physical_fault_value" << std::endl;
    std::cout << "[RUN] test_sha_two_stage_cpu_translation_uses_vsatp" << std::endl;
    test_sha_two_stage_cpu_translation_uses_vsatp();
    std::cout << "[PASS] test_sha_two_stage_cpu_translation_uses_vsatp" << std::endl;
    std::cout << "[RUN] test_sha_and_ssnpm_audit_regressions" << std::endl;
    test_sha_and_ssnpm_audit_regressions();
    std::cout << "[PASS] test_sha_and_ssnpm_audit_regressions" << std::endl;
    std::cout << "[RUN] test_zicntr_base_counter_values_and_access" << std::endl;
    test_zicntr_base_counter_values_and_access();
    std::cout << "[PASS] test_zicntr_base_counter_values_and_access" << std::endl;

    std::cout << "[RUN] test_fast_time_csr_polling_advances_between_batches" << std::endl;
    test_fast_time_csr_polling_advances_between_batches();
    std::cout << "[PASS] test_fast_time_csr_polling_advances_between_batches" << std::endl;

    std::cout << "[RUN] test_fast_cycle_csr_polling_advances_between_batches" << std::endl;
    test_fast_cycle_csr_polling_advances_between_batches();
    std::cout << "[PASS] test_fast_cycle_csr_polling_advances_between_batches" << std::endl;

    std::cout << "[RUN] test_fast_mcountinhibit_matches_instruction_execution" << std::endl;
    test_fast_mcountinhibit_matches_instruction_execution();
    std::cout << "[PASS] test_fast_mcountinhibit_matches_instruction_execution" << std::endl;

    std::cout << "[RUN] test_fast_mtime_mmio_matches_instruction_execution" << std::endl;
    test_fast_mtime_mmio_matches_instruction_execution();
    std::cout << "[PASS] test_fast_mtime_mmio_matches_instruction_execution" << std::endl;

    std::cout << "[RUN] test_fast_vector_mtime_mmio_flushes_prior_cycles" << std::endl;
    test_fast_vector_mtime_mmio_flushes_prior_cycles();
    std::cout << "[PASS] test_fast_vector_mtime_mmio_flushes_prior_cycles" << std::endl;

    std::cout << "[RUN] test_zimop_decoder" << std::endl;
    test_zimop_decoder();
    std::cout << "[PASS] test_zimop_decoder" << std::endl;

    std::cout << "[RUN] test_zcb_decompression" << std::endl;
    test_zcb_decompression();
    std::cout << "[PASS] test_zcb_decompression" << std::endl;

    std::cout << "[RUN] test_zvbb_unary_decode" << std::endl;
    test_zvbb_unary_decode();
    std::cout << "[PASS] test_zvbb_unary_decode" << std::endl;

    std::cout << "[RUN] test_cbo_zero_execution" << std::endl;
    test_cbo_zero_execution();
    std::cout << "[PASS] test_cbo_zero_execution" << std::endl;

    std::cout << "[RUN] test_profile_engine" << std::endl;
    test_profile_engine();
    std::cout << "[PASS] test_profile_engine" << std::endl;

    std::cout << "[RUN] test_track1a_zkr_svadu_sspm" << std::endl;
    test_track1a_zkr_svadu_sspm();
    std::cout << "[PASS] test_track1a_zkr_svadu_sspm" << std::endl;

    std::cout << "[RUN] test_track1b_zacas_zabha" << std::endl;
    test_track1b_zacas_zabha();
    std::cout << "[PASS] test_track1b_zacas_zabha" << std::endl;

    std::cout << "[RUN] test_track1c_fp16_bf16" << std::endl;
    test_track1c_fp16_bf16();
    std::cout << "[PASS] test_track1c_fp16_bf16" << std::endl;

    std::cout << "[RUN] test_track2_aggregate_isa_expansion" << std::endl;
    test_track2_aggregate_isa_expansion();
    std::cout << "[PASS] test_track2_aggregate_isa_expansion" << std::endl;

    std::cout << "[RUN] test_track2_crypto_bitmanip_gating" << std::endl;
    test_track2_crypto_bitmanip_gating();
    std::cout << "[PASS] test_track2_crypto_bitmanip_gating" << std::endl;

    std::cout << "[RUN] test_track2_mutual_exclusion_and_spec_separation" << std::endl;
    test_track2_mutual_exclusion_and_spec_separation();
    std::cout << "[PASS] test_track2_mutual_exclusion_and_spec_separation" << std::endl;

    std::cout << "[RUN] test_track2a_vector_crypto" << std::endl;
    test_track2a_vector_crypto();
    std::cout << "[PASS] test_track2a_vector_crypto" << std::endl;

    std::cout << "[RUN] test_track2b_scalar_crypto_gating" << std::endl;
    test_track2b_scalar_crypto_gating();
    std::cout << "[PASS] test_track2b_scalar_crypto_gating" << std::endl;

    std::cout << "\nAll RVA23 Profile unit tests passed successfully!\n";
    return simrv::test::failures() == 0 ? 0 : 1;
}
