/**
 * @file PipelineHazardTests.cpp
 * @brief Regression tests for integer forwarding, FP conservative hazard stalls, FMA rs3
 * dependencies, and cross-bank conversions.
 */
#include <cstdlib>
#include <iostream>

#include "simrv/cache/ICache.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/RuntimeProfile.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/pipeline/OperationTraits.hpp"
#include "simrv/pipeline/PipelineConfig.hpp"
#include "simrv/pipeline/PipelineSim.hpp"
#include "simrv/pipeline/Scoreboard.hpp"
#include "simrv/xlen/Types.hpp"

#define TEST_CHECK(expr)                                                                     \
    do {                                                                                     \
        if (!(expr)) {                                                                       \
            std::cerr << "Assertion failed: " #expr " at " __FILE__ ":" << __LINE__ << "\n"; \
            std::abort();                                                                    \
        }                                                                                    \
    } while (0)

namespace {

using simrv::isa::OperationId;
using simrv::pipeline::PipelineStage;
using simrv::pipeline::Scoreboard;
using namespace simrv::pipeline::operation;

void test_integer_forwarding_hazards() {
    Scoreboard sb{};
    // Producer: ADD x1, x2, x3 in Execute stage with forwarding enabled
    sb.reserve(RegBank::Integer, RegId::Ra, PipelineStage::Execute, 0, true);
    TEST_CHECK(sb.is_busy(RegBank::Integer, RegId::Ra));
    TEST_CHECK(sb.can_forward(RegBank::Integer, RegId::Ra));

    // Consumer: ADD x4, x1, x5 reads x1 as rs1
    TEST_CHECK(is_rs1_int(OperationId::ADD));
    TEST_CHECK(sb.is_busy(RegBank::Integer, RegId::Ra));
    // Since can_forward is true, forwarding resolves the hazard without stall
    TEST_CHECK(sb.can_forward(RegBank::Integer, RegId::Ra));

    // After Writeback, release
    sb.release(RegBank::Integer, RegId::Ra);
    TEST_CHECK(!sb.is_busy(RegBank::Integer, RegId::Ra));
}

void test_load_use_hazard() {
    Scoreboard sb{};
    // Producer: LW x1, 0(x2) in Execute stage, latency = 1 (cannot forward yet)
    sb.reserve(RegBank::Integer, RegId::Ra, PipelineStage::Execute, 1, false);
    TEST_CHECK(sb.is_busy(RegBank::Integer, RegId::Ra));
    TEST_CHECK(!sb.can_forward(RegBank::Integer, RegId::Ra));

    // Consumer: ADD x3, x1, x4 in Decode stage
    TEST_CHECK(is_rs1_int(OperationId::ADD));
    // Hazard stall required!
    TEST_CHECK(!sb.can_forward(RegBank::Integer, RegId::Ra));

    // Next cycle: LW advances to Memory stage, result becomes forwardable
    sb.reserve(RegBank::Integer, RegId::Ra, PipelineStage::Memory, 0, true);
    TEST_CHECK(sb.can_forward(RegBank::Integer, RegId::Ra));
}

void test_fp_conservative_stall() {
    Scoreboard sb{};
    const auto fa0 = static_cast<RegId>(simrv::xlen::FpRegId::Fa0);
    // Producer: FADD.S f1, f2, f3
    TEST_CHECK(writes_float(OperationId::FADD_S));
    sb.reserve(RegBank::Float, fa0, PipelineStage::Execute, 3, false);
    TEST_CHECK(sb.is_busy(RegBank::Float, fa0));
    TEST_CHECK(!sb.can_forward(RegBank::Float, fa0));

    // Consumer: FMUL.S f4, f1, f5
    TEST_CHECK(is_rs1_fp(OperationId::FMUL_S));
    // Conservative rule: FP hazards remain stalled until retirement/release
    TEST_CHECK(sb.is_busy(RegBank::Float, fa0));
    TEST_CHECK(!sb.can_forward(RegBank::Float, fa0));

    // Released at retirement
    sb.release(RegBank::Float, fa0);
    TEST_CHECK(!sb.is_busy(RegBank::Float, fa0));
}

void test_fma_rs3_hazard() {
    Scoreboard sb{};
    const auto fa2 = static_cast<RegId>(simrv::xlen::FpRegId::Fa2);
    // Producer: FADD.D f3, f4, f5
    sb.reserve(RegBank::Float, fa2, PipelineStage::Execute, 2, false);

    // Consumer: FMADD.D f1, f2, f6, f3 (where f3 is rs3)
    TEST_CHECK(reads_rs3(OperationId::FMADD_D));
    TEST_CHECK(is_rs3_fp(OperationId::FMADD_D));
    TEST_CHECK(sb.is_busy(RegBank::Float, fa2));

    // Release producer
    sb.release(RegBank::Float, fa2);
    TEST_CHECK(!sb.is_busy(RegBank::Float, fa2));
}

void test_cross_bank_conversion_hazards() {
    Scoreboard sb{};
    const auto fa0 = static_cast<RegId>(simrv::xlen::FpRegId::Fa0);

    // 1. FP to Integer conversion: FCVT.W.S x1, f1
    TEST_CHECK(is_rs1_fp(OperationId::FCVT_W_S));
    TEST_CHECK(writes_integer(OperationId::FCVT_W_S));
    TEST_CHECK(!writes_float(OperationId::FCVT_W_S));

    // Reserve FP source f1
    sb.reserve(RegBank::Float, fa0, PipelineStage::Execute, 2, false);
    TEST_CHECK(sb.is_busy(RegBank::Float, fa0));
    sb.release(RegBank::Float, fa0);

    // FCVT now reserves integer destination x1
    sb.reserve(RegBank::Integer, RegId::Ra, PipelineStage::Execute, 1, false);
    TEST_CHECK(sb.is_busy(RegBank::Integer, RegId::Ra));
    TEST_CHECK(!sb.is_busy(RegBank::Float, fa0));
    sb.release(RegBank::Integer, RegId::Ra);

    // 2. Integer to FP conversion: FCVT.S.W f1, x1
    TEST_CHECK(is_rs1_int(OperationId::FCVT_S_W));
    TEST_CHECK(writes_float(OperationId::FCVT_S_W));
    TEST_CHECK(!writes_integer(OperationId::FCVT_S_W));

    // 3. FP Store: FSW f2, 0(x1)
    TEST_CHECK(is_store(OperationId::FSW));
    TEST_CHECK(is_rs1_int(OperationId::FSW));
    TEST_CHECK(is_rs2_fp(OperationId::FSW));
    TEST_CHECK(!writes_rd(OperationId::FSW));
}

void test_pipeline_sim_hazard_events() {
    simrv::pipeline::PipelineSim sim;
    sim.config.record_snapshots = true;
    sim.reset();

    // Cycle 1: Dispatch with data hazard stall
    sim.advance_cycle({
        .fetch = {.instruction = {.pc = 0x1008, .op_id = OperationId::ADD}, .valid = true},
        .decode = {.instruction = {.pc = 0x1004, .op_id = OperationId::ADD},
                   .valid = true,
                   .stalled = true},
        .execute = {.instruction = {.pc = 0x1000, .op_id = OperationId::LW}, .valid = true},
        .retired = false,
        .data_hazard_stall = true,
    });

    TEST_CHECK(sim.stall_cycles() == 1);
    TEST_CHECK(sim.data_hazard_stalls() == 1);
    TEST_CHECK(sim.cycle_count() == 1);
}

void test_cycle_kernel_bit_for_bit_hazard_stall_counters() {
    // Verifies bit-for-bit cycle counts and data hazard stall counts across
    // 3-stage and 5-stage cycle-accurate pipeline models under integer RAW dependencies.
    // Program:
    // 1: addi x1, x0, 10
    // 2: addi x2, x1, 5   (depends on x1)
    // 3: add  x3, x2, x1  (depends on x2 and x1)
    // 4: jal  x0, 0
    constexpr std::array<Instruction, 4> program = {
        0x00a00093,  // addi x1, x0, 10
        0x00508113,  // addi x2, x1, 5
        0x001101b3,  // add  x3, x2, x1
        0x0000006f,  // jal  x0, 0
    };

    const auto run_model = [&](simrv::pipeline::PipelineType type, bool forwarding) {
        simrv::core::Machine machine;
        const Address pc = machine.memory_geometry().dram_base;
        std::vector<Byte> ram(1024 * 1024, Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        auto& cpu = machine.primary_hart();
        cpu.machine_ = &machine;
        cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        cpu.pipeline_sim.config.pipeline_type = type;
        cpu.pipeline_sim.config.enable_forwarding = forwarding;

        std::array<Byte, simrv::cache::ICache::kLineBytes> line{};
        std::memcpy(line.data(), program.data(), sizeof(program));
        std::memcpy(ram.data(), program.data(), sizeof(program));
        cpu.icache.insert(pc, line.data(), simrv::memory::MesiState::Exclusive);
        cpu.state().pc = pc;

        uint32_t cycles = 0;
        while (cpu.e_icount < 3 && cycles < 64) {
            cpu.run_cycle(machine);
            machine.memory().system_bus().advance_cycle();
            ++cycles;
        }
        TEST_CHECK(cpu.e_icount == 3);
        TEST_CHECK(cpu.state().regs.read(RegId::Ra) == 10);
        TEST_CHECK(cpu.state().regs.read(RegId::Sp) == 15);
        TEST_CHECK(cpu.state().regs.read(RegId::Gp) == 25);
        return std::make_pair(cycles, cpu.pipeline_sim.data_hazard_stalls());
    };

    // 5-stage with forwarding: resolved via forwarding paths (8 cycles, 0 data hazard stalls)
    const auto [cycles_5s_fwd, stalls_5s_fwd] =
        run_model(simrv::pipeline::PipelineType::FiveStage, true);
    TEST_CHECK(cycles_5s_fwd == 8);
    TEST_CHECK(stalls_5s_fwd == 0);

    // 5-stage without forwarding: RAW dependencies stall until Writeback (12 cycles, 4 stalls)
    const auto [cycles_5s_nofwd, stalls_5s_nofwd] =
        run_model(simrv::pipeline::PipelineType::FiveStage, false);
    TEST_CHECK(cycles_5s_nofwd == 12);
    TEST_CHECK(stalls_5s_nofwd == 4);

    // 3-stage with forwarding: (6 cycles, 0 stalls)
    const auto [cycles_3s_fwd, stalls_3s_fwd] =
        run_model(simrv::pipeline::PipelineType::ThreeStage, true);
    TEST_CHECK(cycles_3s_fwd == 6);
    TEST_CHECK(stalls_3s_fwd == 0);

    // 3-stage without forwarding: in this 3-stage model (Fetch -> Decode/Execute -> Writeback),
    // single-cycle ALU operations write back on cycle completion so subsequent instructions
    // in Decode observe committed architectural registers (6 cycles, 0 stalls)
    const auto [cycles_3s_nofwd, stalls_3s_nofwd] =
        run_model(simrv::pipeline::PipelineType::ThreeStage, false);
    TEST_CHECK(cycles_3s_nofwd == 6);
    TEST_CHECK(stalls_3s_nofwd == 0);
}

}  // namespace

int main() {
    std::cout << "=== Running Pipeline Hazard Tests ===\n";
    test_integer_forwarding_hazards();
    test_load_use_hazard();
    test_fp_conservative_stall();
    test_fma_rs3_hazard();
    test_cross_bank_conversion_hazards();
    test_pipeline_sim_hazard_events();
    test_cycle_kernel_bit_for_bit_hazard_stall_counters();
    std::cout << "=== All Pipeline Hazard Tests Passed Successfully ===\n";
    return 0;
}
