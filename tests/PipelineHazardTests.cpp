/**
 * @file PipelineHazardTests.cpp
 * @brief Regression tests for integer forwarding, FP conservative hazard stalls, FMA rs3
 * dependencies, and cross-bank conversions.
 */
#include <cstdlib>
#include <iostream>

#include "simrv/isa/OperationId.hpp"
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

}  // namespace

int main() {
    std::cout << "=== Running Pipeline Hazard Tests ===\n";
    test_integer_forwarding_hazards();
    test_load_use_hazard();
    test_fp_conservative_stall();
    test_fma_rs3_hazard();
    test_cross_bank_conversion_hazards();
    test_pipeline_sim_hazard_events();
    std::cout << "=== All Pipeline Hazard Tests Passed Successfully ===\n";
    return 0;
}
