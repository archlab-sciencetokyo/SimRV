/**
 * @file Rva23ProfileTests.cpp
 * @brief Regression tests for RVA23 Profile: Zicond, CMO (Zicboz/Zicbom/Zicbop), Svinval, and
 * profile engine.
 */
#include <array>
#include <cassert>
#include <bit>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

#include "simrv/cache/DCache.hpp"
#include "simrv/cache/ICache.hpp"
#include "simrv/cache/L2Cache.hpp"
#include "simrv/cache/L3Cache.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/CsrFile.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/execute/ExecuteUnit.hpp"
#include "simrv/isa/Common.hpp"
#include "simrv/isa/IsaConfig.hpp"
#include "simrv/isa/FpConversions.hpp"
#include "simrv/isa/IsaTypes.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/ReservationTable.hpp"
#include "simrv/memory/TileLinkBus.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "simrv/pipeline/RetirementEffects.hpp"
#include "simrv/xlen/AddressTypes.hpp"
#include "simrv/xlen/RegisterTypes.hpp"

namespace {

#define TEST_ASSERT(cond)                                                                    \
    do {                                                                                     \
        if (!(cond)) {                                                                       \
            std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ << ": " #cond \
                      << std::endl;                                                          \
            std::exit(1);                                                                    \
        }                                                                                    \
    } while (false)

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

        constexpr simrv::xlen::Address kBase = simrv::memory::kDramBaseAddress;
        constexpr simrv::xlen::Register kValue = 0x0123456789ABCDEFULL;
        const auto check_unaligned = [&](simrv::xlen::Address offset) {
            cpu.active_context().pending_exception.reset();
            cpu.active_context().opcode = simrv::isa::Opcode::Store;
            simrv::memory::MemoryAccess::target_write(
                machine.memory(), cpu, simrv::xlen::VirtAddr{kBase + offset}, kValue,
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
    static_assert(ReservationTable::kReservationGranule == 64);

    ReservationTable table;
    constexpr simrv::xlen::Address kAddress = 0x8000103FULL;
    table.set_reservation(0, kAddress);
    TEST_ASSERT(table.check_reservation(0, 0x80001000ULL));
    TEST_ASSERT(table.check_reservation(0, 0x8000103FULL));
    TEST_ASSERT(!table.check_reservation(0, 0x80001040ULL));

    table.invalidate_matching(0x80001000ULL, 1);
    TEST_ASSERT(!table.check_reservation(0, kAddress));
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
    constexpr simrv::xlen::Address kAddress = simrv::memory::kDramBaseAddress + 0x200;

    const auto lr = [&](simrv::core::CPU& cpu) {
        auto& ctx = cpu.active_context();
        ctx = {};
        ctx.opcode = simrv::isa::Opcode::Amo;
        ctx.funct5 = simrv::isa::Funct5Amo::Lr;
        ctx.funct3 = simrv::isa::Funct3::Sw;
        ctx.mem_addr = kAddress;
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
        ctx.mem_addr = kAddress;
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
    constexpr simrv::xlen::Address kCode = simrv::memory::kDramBaseAddress + 0x100;
    const auto amo = [](uint32_t funct5, uint32_t rs2, uint32_t rd) {
        return (funct5 << 27U) | (rs2 << 20U) | (10U << 15U) | (2U << 12U) | (rd << 7U) | 0x2FU;
    };
    constexpr uint32_t kAddiT0T0One = 0x00128293U;
    constexpr uint32_t kBneT1ZeroMinus12 =
        0xFE031AE3U;  // bne t1, zero, -12
    const std::array<uint32_t, 4> loop = {amo(2, 0, 5), kAddiT0T0One, amo(3, 5, 6),
                                          kBneT1ZeroMinus12};
    std::memcpy(ram.data() + 0x100, loop.data(), sizeof(loop));
    constexpr uint32_t kLoopInitial = 0;
    std::memcpy(ram.data() + 0x204, &kLoopInitial, sizeof(kLoopInitial));
    hart0.state().pc = kCode;
    hart1.state().pc = kCode;
    hart0.state().regs.write(simrv::xlen::RegId::A0, kAddress + 4);
    hart1.state().regs.write(simrv::xlen::RegId::A0, kAddress + 4);
    const auto done = [kCode](const simrv::core::CPU& cpu) { return cpu.state().pc == kCode + 16; };
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

    constexpr simrv::xlen::Address kTarget = simrv::memory::kDramBaseAddress + 0x100;
    constexpr simrv::xlen::Address kWriter = simrv::memory::kDramBaseAddress + 0x300;
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
    writer.state().pc = kWriter;
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
    for (unsigned i = 0; i < 100'000 && !writer_done.load(std::memory_order_acquire); ++i) {
        TEST_ASSERT(fetcher.fetch_stage(machine, kTarget));
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
    constexpr simrv::xlen::Address kBase = simrv::memory::kDramBaseAddress;
    constexpr std::array<Funct5Amo, 9> kOps = {
        Funct5Amo::Swap, Funct5Amo::Add, Funct5Amo::Xor, Funct5Amo::And, Funct5Amo::Or,
        Funct5Amo::Min,  Funct5Amo::Max, Funct5Amo::Minu, Funct5Amo::Maxu};

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
            ctx.mem_addr = address;
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
        3, 0x80000004ULL, 0x80000002ULL, 1, 0x80000003ULL,
        0x80000001ULL, 3, 3, 0x80000001ULL};
    exercise(4, Funct3::Sw, 0x80000001ULL, 3, kExpectedW);

    if constexpr (simrv::xlen::kIsXLen64) {
        constexpr std::array<uint64_t, 9> kExpectedD = {
            3, 0x8000000000000004ULL, 0x8000000000000002ULL, 1,
            0x8000000000000003ULL, 0x8000000000000001ULL, 3, 3,
            0x8000000000000001ULL};
        exercise(8, Funct3::Sd, 0x8000000000000001ULL, 3, kExpectedD);
    }
}

void test_zfa_scalar_instructions() {
    using namespace simrv::isa;
    const auto encode = [](uint32_t funct7, uint32_t rs2, uint32_t rs1, uint32_t rm,
                           uint32_t rd) {
        return (funct7 << 25U) | (rs2 << 20U) | (rs1 << 15U) | (rm << 12U) | (rd << 7U) |
               0x53U;
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
    TEST_ASSERT(required_misa_extensions(OperationId::FLI_S) == misa_extension_bit(IsaExtension::F));
    TEST_ASSERT(required_misa_extensions(OperationId::FCVTMOD_W_D) ==
                (misa_extension_bit(IsaExtension::F) | misa_extension_bit(IsaExtension::D)));
    TEST_ASSERT(is_f_op(OperationId::FROUNDNX_S));
    TEST_ASSERT(is_d_op(OperationId::FCVTMOD_W_D));

    std::array<simrv::xlen::FloatingRegister, 32> fp{};
    simrv::xlen::CsrValue fcsr = 0;
    const auto execute = [&](uint32_t funct7, uint32_t rs2, uint32_t rs1, uint32_t rm,
                             uint32_t rs2_reg = 0) {
        const uint32_t encoded_rs2 = (funct7 == 0x78 || funct7 == 0x79 || funct7 == 0x20 ||
                                      funct7 == 0x21 || funct7 == 0x61)
                                         ? rs2
                                         : rs2_reg;
        return simrv::execute::ExecuteUnit::opFp(
            funct7, static_cast<Funct3>(rm), simrv::xlen::to_fp_reg_id(rs1),
            simrv::xlen::to_fp_reg_id(encoded_rs2), 0, fp.data(), fcsr);
    };
    auto result = execute(0x78, 1, 16, 0);
    TEST_ASSERT(result.fp_wb_enable && result.fp_wb_data ==
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
    TEST_ASSERT(result.fp_wb_enable && result.fp_wb_data ==
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

    fp[2] = static_cast<uint64_t>(simrv::xlen::kF32BoxerBits) | UINT32_C(0x40200000); // 2.5
    result = execute(0x20, 4, 2, 4);
    TEST_ASSERT(result.fp_wb_enable &&
                (result.fp_wb_data & UINT64_C(0xFFFFFFFF)) == UINT32_C(0x40400000));
    TEST_ASSERT((fcsr & (FflagsBit::Nv | FflagsBit::Nx)) == 0);
    fp[2] = static_cast<uint64_t>(simrv::xlen::kF32BoxerBits) | UINT32_C(0x40200000); // 2.5
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
        return (funct7 << 25U) | (rs2 << 20U) | (rs1 << 15U) | (rm << 12U) | (rd << 7U) |
               0x53U;
    };
    TEST_ASSERT(simrv::pipeline::decoder((1U << 12U) | (1U << 7U) | 0x07U) == OperationId::FLH);
    TEST_ASSERT(simrv::pipeline::decoder((2U << 20U) | (1U << 12U) | 0x27U) == OperationId::FSH);
    TEST_ASSERT(simrv::pipeline::decoder(encode_fp(0x72, 0, 2, 0, 1)) == OperationId::FMV_X_H);
    TEST_ASSERT(simrv::pipeline::decoder(encode_fp(0x7A, 0, 2, 0, 1)) == OperationId::FMV_H_X);
    TEST_ASSERT(simrv::pipeline::decoder(encode_fp(0x22, 2, 2, 0, 1)) == OperationId::FCVT_S_H);
    TEST_ASSERT(simrv::pipeline::decoder(encode_fp(0x26, 0, 2, 0, 1)) == OperationId::FCVT_H_S);
    TEST_ASSERT(simrv::pipeline::decoder(encode_fp(0x21, 2, 2, 0, 1)) == OperationId::FCVT_D_H);
    TEST_ASSERT(simrv::pipeline::decoder(encode_fp(0x25, 1, 2, 0, 1)) == OperationId::FCVT_H_D);
    TEST_ASSERT(simrv::pipeline::decoder(encode_fp(0x02, 0, 2, 0, 1)) == OperationId::ILLEGAL);

    std::array<simrv::xlen::FloatingRegister, 32> fp{};
    simrv::xlen::CsrValue fcsr = 0;
    const auto execute = [&](uint32_t funct7, uint32_t rm, uint32_t rs1, uint32_t rs2,
                             simrv::xlen::Register int_source = 0) {
        return simrv::execute::ExecuteUnit::opFp(
            funct7, static_cast<Funct3>(rm), simrv::xlen::to_fp_reg_id(rs1),
            simrv::xlen::to_fp_reg_id(rs2), int_source, fp.data(), fcsr);
    };
    fp[2] = ~simrv::xlen::FloatingRegister{0xFFFF} | UINT16_C(0x3C00);  // Half 1.0
    auto result = execute(0x22, 0, 2, 2);
    TEST_ASSERT(result.fp_wb_enable && result.fp_wb_data ==
                (static_cast<uint64_t>(simrv::xlen::kF32BoxerBits) | UINT32_C(0x3F800000)));

    fp[2] = static_cast<uint64_t>(simrv::xlen::kF32BoxerBits) | UINT32_C(0x3FC00000);  // 1.5
    result = execute(0x26, 0, 2, 0);
    TEST_ASSERT(result.fp_wb_enable &&
                (result.fp_wb_data & UINT64_C(0xFFFF)) == UINT16_C(0x3E00));

    fp[2] = static_cast<uint64_t>(simrv::xlen::kF32BoxerBits) | UINT32_C(0x3F801000);
    fcsr = 0;
    result = execute(0x26, 3, 2, 0);  // RUP
    TEST_ASSERT((result.fp_wb_data & UINT64_C(0xFFFF)) == UINT16_C(0x3C01));
    TEST_ASSERT((fcsr & simrv::isa::FflagsBit::Nx) != 0);

    const double half_midpoint = 1.00048828125;
    fp[2] = std::bit_cast<simrv::xlen::FloatingRegister>(
        std::nextafter(half_midpoint, INFINITY));
    fcsr = 0;
    result = execute(0x25, 0, 2, 1);
    TEST_ASSERT(result.fp_wb_enable &&
                (result.fp_wb_data & UINT64_C(0xFFFF)) == UINT16_C(0x3C01));
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
    const auto address = simrv::xlen::VirtAddr{simrv::memory::kDramBaseAddress + 0x200};
    simrv::memory::MemoryAccess::storeFp(machine.memory(), cpu, address, fp[2], Funct3::Fsh);
    TEST_ASSERT(!cpu.active_context().pending_exception.has_value());
    const auto loaded = simrv::memory::MemoryAccess::loadFp(machine.memory(), cpu, address,
                                                            Funct3::Flh);
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
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFWCVT_F_F_V, ir(vd, vs2));
    TEST_ASSERT(!cpu.active_context().pending_exception.has_value());
    TEST_ASSERT(dest.u32[0] == UINT32_C(0x3F800000));

    source.u32[0] = UINT32_C(0x3F801000);
    cpu.state().fcsr = 0;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFNCVT_F_F_W, ir(vd, vs2));
    TEST_ASSERT(!cpu.active_context().pending_exception.has_value());
    TEST_ASSERT(dest.u16[0] == UINT16_C(0x3C00));
    TEST_ASSERT((cpu.state().fcsr & simrv::isa::FflagsBit::Nx) != 0);

    cpu.state().fcsr = simrv::isa::RoundingMode::Rmm << 5U;
    simrv::execute::ExecuteUnit::execute_vector(
        cpu, machine.memory(), simrv::isa::OperationId::VFNCVT_F_F_W, ir(vd, vs2));
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
    cpu.icache.insert(kLineAddress, line.data());
    uint32_t cached_word = 0;
    TEST_ASSERT(cpu.icache.read(kLineAddress, cached_word));

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
    TEST_ASSERT(!cpu.icache.read(kLineAddress, instruction));
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
        const simrv::xlen::Address root = memory.dram_base + 0x1000;
        const simrv::xlen::Address level1 = memory.dram_base + 0x2000;
        const simrv::xlen::Address level0 = memory.dram_base + 0x3000;
        const simrv::xlen::Address physical = memory.dram_base + 0x5000;
        constexpr simrv::xlen::Address kVirtual = 0x4000;
        constexpr uint64_t kValid = std::to_underlying(simrv::PteFlag::V);
        constexpr uint64_t kPermissions = std::to_underlying(simrv::PteFlag::R) |
                                          std::to_underlying(simrv::PteFlag::W) |
                                          std::to_underlying(simrv::PteFlag::X);
        const auto write_pte = [&](simrv::xlen::Address address, uint64_t pte) {
            TEST_ASSERT(machine.memory().dma_write(address, &pte, sizeof(pte)));
        };
        const auto read_pte = [&](simrv::xlen::Address address) {
            uint64_t pte = 0;
            TEST_ASSERT(machine.memory().dma_read(address, &pte, sizeof(pte)));
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
        TEST_ASSERT(load->error() ==
                    std::to_underlying(simrv::isa::ExceptionCode::LoadPageFault));
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
        simrv::isa::ExceptionCode::MisalignedFetch,    simrv::isa::ExceptionCode::FaultFetch,
        simrv::isa::ExceptionCode::MisalignedLoad,     simrv::isa::ExceptionCode::FaultLoad,
        simrv::isa::ExceptionCode::MisalignedStore,    simrv::isa::ExceptionCode::FaultStore,
        simrv::isa::ExceptionCode::IllegalInstruction, simrv::isa::ExceptionCode::FetchPageFault,
        simrv::isa::ExceptionCode::LoadPageFault,      simrv::isa::ExceptionCode::StorePageFault,
    };
    simrv::xlen::CsrValue delegation = 0;
    for (const auto cause : causes) {
        delegation |= simrv::xlen::CsrValue{1} << std::to_underlying(cause);
    }

    for (const auto cause : causes) {
        auto& state = cpu.state();
        state.priv = simrv::xlen::PrivilegeLevel::User;
        state.pc = 0x2000;
        state.medeleg = delegation;
        state.stvec = kVectorBase;
        cpu.raise_exception(cause, kTrapValue);
        TEST_ASSERT(cpu.state().priv == simrv::xlen::PrivilegeLevel::Supervisor);
        TEST_ASSERT(cpu.state().pc == kVectorBase);
        TEST_ASSERT(cpu.state().stval == kTrapValue);
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
    constexpr std::array<uint16_t, 8> kCmop{
        0x6081U, 0x6181U, 0x6281U, 0x6381U, 0x6481U, 0x6581U, 0x6681U, 0x6781U};
    for (const uint16_t compressed : kCmop) {
        const auto expanded = simrv::pipeline::decompressInstruction(compressed, true);
        TEST_ASSERT(expanded == 0x00000013U);  // addi x0, x0, 0
        TEST_ASSERT(simrv::pipeline::decoder(expanded) == simrv::isa::OperationId::ADDI);
    }
}

void test_zimop_decoder() {
    constexpr std::array<uint32_t, 40> kMopMatches{
        0x81c04073U, 0x81d04073U, 0x81e04073U, 0x81f04073U, 0x85c04073U, 0x85d04073U,
        0x85e04073U, 0x85f04073U, 0x89c04073U, 0x89d04073U, 0x89e04073U, 0x89f04073U,
        0x8dc04073U, 0x8dd04073U, 0x8de04073U, 0x8df04073U, 0xc1c04073U, 0xc1d04073U,
        0xc1e04073U, 0xc1f04073U, 0xc5c04073U, 0xc5d04073U, 0xc5e04073U, 0xc5f04073U,
        0xc9c04073U, 0xc9d04073U, 0xc9e04073U, 0xc9f04073U, 0xcdc04073U, 0xcdd04073U,
        0xcde04073U, 0xcdf04073U, 0x82004073U, 0x86004073U, 0x8a004073U, 0x8e004073U,
        0xc2004073U, 0xc6004073U, 0xca004073U, 0xce004073U};
    for (size_t index = 0; index < kMopMatches.size(); ++index) {
        const uint32_t encoded = kMopMatches[index] | (17U << 7U) | (9U << 15U) |
                                 (index >= 32 ? (23U << 20U) : 0U);
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
    constexpr uint32_t kWrsNto = (0x09DU << 20U) | 0x73U;
    constexpr uint32_t kWrsSto = (0x01DU << 20U) | 0x73U;
    TEST_ASSERT(simrv::pipeline::decoder(kWrsNto) == OperationId::WRS_NTO);
    TEST_ASSERT(simrv::pipeline::decoder(kWrsSto) == OperationId::WRS_STO);
    TEST_ASSERT(required_misa_extensions(OperationId::WRS_NTO) == misa_extension_bit(IsaExtension::A));
    TEST_ASSERT(simrv::pipeline::decoder(kWrsNto | (1U << 15U)) == OperationId::ILLEGAL);
    TEST_ASSERT(simrv::pipeline::decoder(kWrsSto | (1U << 7U)) == OperationId::ILLEGAL);

    for (const auto priv : {simrv::xlen::PrivilegeLevel::User,
                            simrv::xlen::PrivilegeLevel::Supervisor,
                            simrv::xlen::PrivilegeLevel::Machine}) {
        TEST_ASSERT(simrv::core::TrapController::can_execute_privileged_instruction(
            priv, 0, 0, 0x09D, 0));
        TEST_ASSERT(simrv::core::TrapController::can_execute_privileged_instruction(
            priv, 0, 0, 0x01D, 0));
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
                           simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::I) |
                           simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::S) |
                           simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::U);
        cpu.state().initialize_lower_xlen_fields();

        constexpr uint64_t kUxlMask = uint64_t{0x3} << 32U;
        constexpr uint64_t kSxlMask = uint64_t{0x3} << 34U;
        const uint64_t requested =
            (static_cast<uint64_t>(cpu.state().mstatus) & ~(kUxlMask | kSxlMask)) |
            (uint64_t{2} << 32U) | (uint64_t{2} << 34U);
        const auto write = cpu.csr_file.write(
            simrv::xlen::CsrNumber{0x300}, static_cast<simrv::xlen::CsrValue>(requested));
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
        state.misa |= simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::S) |
                      simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::U);
        state.initialize_lower_xlen_fields();

        constexpr simrv::xlen::Address kBase = simrv::memory::kDramBaseAddress + 0x100;
        constexpr simrv::xlen::Register kValue = 0x0123456789ABCDEFULL;
        constexpr uint64_t kPmmMask = uint64_t{0x3} << 32U;
        constexpr uint64_t kPmmSeven = uint64_t{2} << 32U;
        const simrv::xlen::VirtAddr tagged{
            static_cast<simrv::xlen::Word>(kBase | (uint64_t{0x55} << 57U))};

        state.priv = simrv::xlen::PrivilegeLevel::Machine;
        cpu.active_context().opcode = simrv::isa::Opcode::Store;
        cpu.active_context().pending_exception.reset();
        simrv::memory::MemoryAccess::target_write(machine.memory(), cpu,
                                                  simrv::xlen::VirtAddr{kBase}, kValue,
                                                  simrv::isa::Funct3::Sd);
        TEST_ASSERT(!cpu.active_context().pending_exception.has_value());

        state.priv = simrv::xlen::PrivilegeLevel::Supervisor;
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
        const auto enable_pmm = cpu.csr_file.write(
            simrv::xlen::CsrNumber{0x10A}, static_cast<simrv::xlen::CsrValue>(kPmmSeven));
        TEST_ASSERT(enable_pmm.has_value());
        TEST_ASSERT((static_cast<uint64_t>(state.senvcfg) & kPmmMask) == kPmmSeven);

        state.priv = simrv::xlen::PrivilegeLevel::User;
        cpu.active_context().opcode = simrv::isa::Opcode::Load;
        cpu.active_context().pending_exception.reset();
        const auto loaded = simrv::memory::MemoryAccess::target_read(
            machine.memory(), cpu, tagged, simrv::isa::Funct3::Ld);
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

void test_zcb_decompression() {
    struct Encoding {
        uint16_t match;
        simrv::isa::OperationId operation;
    };
    constexpr std::array<Encoding, 12> kZcbInstructions{{
        {0x8000U, simrv::isa::OperationId::LBU},  {0x8440U, simrv::isa::OperationId::LH},
        {0x8400U, simrv::isa::OperationId::LHU},  {0x9c41U, simrv::isa::OperationId::MUL},
        {0x9c75U, simrv::isa::OperationId::XORI}, {0x8800U, simrv::isa::OperationId::SB},
        {0x9c65U, simrv::isa::OperationId::SEXT_B},
        {0x9c6dU, simrv::isa::OperationId::SEXT_H},
        {0x8c00U, simrv::isa::OperationId::SH},   {0x9c61U, simrv::isa::OperationId::ANDI},
        {0x9c69U, simrv::isa::OperationId::PACK}, {0x9c71U, simrv::isa::OperationId::ADD_UW},
    }};
    for (const auto& encoding : kZcbInstructions) {
        // Select x9 for rd'/rs1' and x10 for rs2'; immediate bits are checked below
        // on the byte/halfword memory encodings.
        uint16_t compressed = encoding.match | (1U << 7U);
        if (encoding.match == 0x8000U || encoding.match == 0x8440U ||
            encoding.match == 0x8400U || encoding.match == 0x8800U ||
            encoding.match == 0x8c00U || encoding.match == 0x9c41U) {
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
    constexpr simrv::xlen::Address target_paddr = simrv::memory::kDramBaseAddress + 0x1000;
    // Verify target buffer is filled with 0xAA
    const auto* ram_ptr = machine.ram_view().unchecked_ptr(target_paddr);
    for (size_t i = 0; i < kLineBytes; ++i) {
        TEST_ASSERT(ram_ptr[i] == static_cast<simrv::xlen::Byte>(0xAA));
    }

    // Execute cbo.zero on target_paddr
    simrv::memory::MemoryAccess::execute_cmo(
        machine.memory(), cpu, simrv::xlen::VirtAddr{target_paddr + 16}, OperationId::CBO_ZERO);

    // Check no pending exception
    TEST_ASSERT(!cpu.active_context().pending_exception.has_value());

    // Verify all bytes of cache block are zeroed
    for (size_t i = 0; i < kLineBytes; ++i) {
        TEST_ASSERT(ram_ptr[i] == static_cast<simrv::xlen::Byte>(0));
    }
    // Verify byte right after block is still 0xAA
    TEST_ASSERT(ram_ptr[kLineBytes] == static_cast<simrv::xlen::Byte>(0xAA));

    // Test permission check: if menvcfg.CBZE = 0, supervisor cbo.zero must trap
    cpu.state().priv = kPrivSupervisor;
    cpu.state().menvcfg &= ~(1ULL << 6);  // Clear CBZE
    simrv::memory::MemoryAccess::execute_cmo(
        machine.memory(), cpu, simrv::xlen::VirtAddr{target_paddr}, OperationId::CBO_ZERO);
    TEST_ASSERT(cpu.active_context().pending_exception == ExceptionCode::IllegalInstruction);
}

void test_profile_engine() {
    using namespace simrv::isa;

    // 1. String conversion
    TEST_ASSERT(to_profile_string(ArchitectureProfile::Rva23S64) == "rva23s64");
    TEST_ASSERT(to_profile_string(ArchitectureProfile::Rva23U64) == "rva23u64");
    TEST_ASSERT(to_profile_string(ArchitectureProfile::Legacy) == "legacy");

    // 2. String parsing
    TEST_ASSERT(parse_architecture_profile("rva23") == ArchitectureProfile::Rva23S64);
    TEST_ASSERT(parse_architecture_profile("rva23s64") == ArchitectureProfile::Rva23S64);
    TEST_ASSERT(parse_architecture_profile("rva23u64") == ArchitectureProfile::Rva23U64);
    TEST_ASSERT(parse_architecture_profile("rva22") == ArchitectureProfile::Rva22S64);
    TEST_ASSERT(parse_architecture_profile("legacy") == ArchitectureProfile::Legacy);
    TEST_ASSERT(parse_architecture_profile("rv64gc") == ArchitectureProfile::Legacy);
    TEST_ASSERT(parse_architecture_profile("invalid_profile") == std::nullopt);

    // 3. Default profile configuration
    if constexpr (simrv::xlen::kIsXLen64) {
        TEST_ASSERT(kDefaultArchitectureProfile == ArchitectureProfile::Rva23S64);
    } else {
        TEST_ASSERT(kDefaultArchitectureProfile == ArchitectureProfile::Legacy);
    }
}

void test_svpbmt_noncacheable_and_io_accesses() {
    if constexpr (simrv::xlen::kIsXLen64) {
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
        state.misa |= simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::S) |
                      simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::U);
        state.initialize_lower_xlen_fields();

        constexpr simrv::xlen::Address kDram = simrv::memory::kDramBaseAddress;
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
        acquire.address = line_base;
        acquire.size = simrv::memory::kTlBlockSize;
        simrv::memory::TlChannelD grant{};
        std::array<simrv::xlen::Byte, simrv::memory::CoherenceHub::kLineBytes> acquired_line{};
        TEST_ASSERT(bus.acquire_block(acquire, grant, acquired_line));
        bus.grant_ack(simrv::memory::TlChannelE{.sink = grant.sink});
        cpu.dcache.insert(line_base, acquired_line.data(), simrv::memory::MesiState::Exclusive);
        for (size_t i = 0; i < dirty_line.size(); ++i) {
            TEST_ASSERT(cpu.dcache.write(line_base + i, 0xA5U, Funct3::Sb));
        }
        machine.memory().system_bus().mark_modified(line_base, cpu.hart_id());

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
        TEST_ASSERT(cpu.dcache.line_state(line_base) == simrv::memory::MesiState::Invalid);
        const auto* read_entry = cpu.tlb.lookup_data_r(kVirtual, 0, PrivilegeLevel::Supervisor);
        TEST_ASSERT(read_entry != nullptr && read_entry->pbmt == simrv::PtePbmt::NonCacheable);

        ctx = {};
        ctx.opcode = Opcode::Store;
        constexpr uint64_t kNcStore = UINT64_C(0x1122334455667788);
        const auto writes_before = machine.memory().system_bus().write_count();
        simrv::memory::MemoryAccess::target_write(machine.memory(), cpu,
                                                  simrv::xlen::VirtAddr{kVirtual}, kNcStore,
                                                  Funct3::Sd);
        TEST_ASSERT(!ctx.pending_exception.has_value());
        TEST_ASSERT(machine.memory().system_bus().write_count() > writes_before);
        TEST_ASSERT(cpu.dcache.line_state(line_base) == simrv::memory::MesiState::Invalid);
        uint64_t ram_value = 0;
        std::memcpy(&ram_value, ram.data() + (kPhysical - kDram), sizeof(ram_value));
        TEST_ASSERT(ram_value == kNcStore);

        ctx = {};
        ctx.opcode = Opcode::Amo;
        ctx.funct5 = Funct5Amo::Add;
        const auto nc_amo_reads_before = machine.memory().system_bus().read_count();
        (void)simrv::memory::MemoryAccess::target_read(
            machine.memory(), cpu, simrv::xlen::VirtAddr{kVirtual}, Funct3::Ld);
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
        read_entry = cpu.tlb.lookup_data_r(kVirtual, 0, PrivilegeLevel::Supervisor);
        TEST_ASSERT(read_entry != nullptr && read_entry->pbmt == simrv::PtePbmt::Io);

        ctx = {};
        ctx.opcode = Opcode::Store;
        constexpr uint64_t kIoStore = UINT64_C(0x8877665544332211);
        const auto io_writes_before = machine.memory().system_bus().write_count();
        simrv::memory::MemoryAccess::target_write(machine.memory(), cpu,
                                                  simrv::xlen::VirtAddr{kVirtual}, kIoStore,
                                                  Funct3::Sd);
        TEST_ASSERT(!ctx.pending_exception.has_value());
        TEST_ASSERT(machine.memory().system_bus().write_count() > io_writes_before);
        std::memcpy(&ram_value, ram.data() + (kPhysical - kDram), sizeof(ram_value));
        TEST_ASSERT(ram_value == kIoStore);

        ctx = {};
        ctx.opcode = Opcode::Amo;
        ctx.funct5 = Funct5Amo::Lr;
        const auto io_amo_reads_before = machine.memory().system_bus().read_count();
        (void)simrv::memory::MemoryAccess::target_read(
            machine.memory(), cpu, simrv::xlen::VirtAddr{kVirtual}, Funct3::Ld);
        TEST_ASSERT(ctx.pending_exception == simrv::isa::ExceptionCode::FaultLoad);
        TEST_ASSERT(machine.memory().system_bus().read_count() == io_amo_reads_before);

        ctx = {};
        ctx.opcode = Opcode::Amo;
        ctx.funct5 = Funct5Amo::Sc;
        const auto io_amo_writes_before = machine.memory().system_bus().write_count();
        simrv::memory::MemoryAccess::target_write(machine.memory(), cpu,
                                                  simrv::xlen::VirtAddr{kVirtual}, kIoStore,
                                                  Funct3::Sd);
        TEST_ASSERT(ctx.pending_exception == simrv::isa::ExceptionCode::FaultStore);
        TEST_ASSERT(machine.memory().system_bus().write_count() == io_amo_writes_before);
    }
}

void test_svpbmt_io_access_ordering() {
    if constexpr (simrv::xlen::kIsXLen64) {
        using simrv::xlen::PrivilegeLevel;

        simrv::core::Machine machine;
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.memory().initialize_mmu();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;

        auto& cpu = machine.primary_hart();
        cpu.reset();
        auto& state = cpu.state();
        state.misa |= simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::S);
        state.initialize_lower_xlen_fields();
        state.priv = PrivilegeLevel::Supervisor;

        constexpr simrv::xlen::Address kDram = simrv::memory::kDramBaseAddress;
        constexpr simrv::xlen::Address kCodeVirtual = 0x10000;
        constexpr simrv::xlen::Address kIoVirtual = 0x40000;
        constexpr simrv::xlen::Address kPmaVirtual = 0x50000;
        constexpr simrv::xlen::Address kIoPhysical = kDram + 0x8000;
        constexpr simrv::xlen::Address kPmaPhysical = kDram + 0x9000;
        state.satp = uint64_t{8} << 60U;
        state.pc = kCodeVirtual;
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
        cpu.icache.insert(kDram, instruction_line.data(), simrv::memory::MesiState::Exclusive);

        cpu.tlb.insert_inst_r(kCodeVirtual, kDram, 0, PrivilegeLevel::Supervisor);
        cpu.tlb.insert_data_w(kIoVirtual, kIoPhysical, 0, PrivilegeLevel::Supervisor, false,
                              simrv::memory::PtePbmt::Io);
        cpu.tlb.insert_data_w(kPmaVirtual, kPmaPhysical, 0, PrivilegeLevel::Supervisor);

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
        TEST_ASSERT(cpu.tlb.lookup_data_w(kIoVirtual, 0, PrivilegeLevel::Supervisor)->pbmt ==
                    simrv::memory::PtePbmt::Io);
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
    std::cout << "[RUN] test_svpbmt_noncacheable_and_io_accesses" << std::endl;
    test_svpbmt_noncacheable_and_io_accesses();
    std::cout << "[PASS] test_svpbmt_noncacheable_and_io_accesses" << std::endl;
    std::cout << "[RUN] test_svpbmt_io_access_ordering" << std::endl;
    test_svpbmt_io_access_ordering();
    std::cout << "[PASS] test_svpbmt_io_access_ordering" << std::endl;
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
    std::cout << "[RUN] test_zicntr_base_counter_values_and_access" << std::endl;
    test_zicntr_base_counter_values_and_access();
    std::cout << "[PASS] test_zicntr_base_counter_values_and_access" << std::endl;

    std::cout << "[RUN] test_zimop_decoder" << std::endl;
    test_zimop_decoder();
    std::cout << "[PASS] test_zimop_decoder" << std::endl;

    std::cout << "[RUN] test_zcb_decompression" << std::endl;
    test_zcb_decompression();
    std::cout << "[PASS] test_zcb_decompression" << std::endl;

    std::cout << "[RUN] test_cbo_zero_execution" << std::endl;
    test_cbo_zero_execution();
    std::cout << "[PASS] test_cbo_zero_execution" << std::endl;

    std::cout << "[RUN] test_profile_engine" << std::endl;
    test_profile_engine();
    std::cout << "[PASS] test_profile_engine" << std::endl;

    std::cout << "\nAll RVA23 Profile unit tests passed successfully!\n";
    return 0;
}
