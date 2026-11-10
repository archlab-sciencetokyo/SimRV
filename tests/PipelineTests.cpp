// Consolidated test sections; each keeps its original checks and exit status.
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

#include "simrv/cache/BaseCache.hpp"
#include "simrv/cache/ICache.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/RuntimeProfile.hpp"
#include "simrv/isa/Common.hpp"
#include "simrv/isa/InstructionFields.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/pipeline/BranchPredictor.hpp"
#include "simrv/pipeline/CpuModel.hpp"
#include "simrv/pipeline/DecodedInstruction.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "simrv/pipeline/OperationInfo.hpp"
#include "simrv/pipeline/OperationTraits.hpp"
#include "simrv/pipeline/PipelineConfig.hpp"
#include "simrv/pipeline/PipelineSim.hpp"
#include "simrv/pipeline/RetirementEffects.hpp"
#include "simrv/pipeline/Scoreboard.hpp"
#include "simrv/xlen/RegisterTypes.hpp"

namespace section_0 {
/**
 * @file OperationTraitsTests.cpp
 * @brief Unit tests for decoded instruction metadata traits and scoreboard tracking.
 */

#define TEST_CHECK(expr)                                                                     \
    do {                                                                                     \
        if (!(expr)) {                                                                       \
            std::cerr << "Assertion failed: " #expr " at " __FILE__ ":" << __LINE__ << "\n"; \
            std::abort();                                                                    \
        }                                                                                    \
    } while (0)

namespace {

using simrv::isa::Opcode;
using simrv::isa::OperationId;
using namespace simrv::pipeline::operation;

void test_base_integer_traits() {
    // Upper immediate
    TEST_CHECK(writes_integer(OperationId::LUI));
    TEST_CHECK(!reads_rs1(OperationId::LUI));
    TEST_CHECK(!reads_rs2(OperationId::LUI));
    TEST_CHECK(!reads_rs3(OperationId::LUI));

    TEST_CHECK(writes_integer(OperationId::AUIPC));
    TEST_CHECK(!reads_rs1(OperationId::AUIPC));

    // Control flow
    TEST_CHECK(is_control(OperationId::JAL));
    TEST_CHECK(is_jump(OperationId::JAL));
    TEST_CHECK(writes_integer(OperationId::JAL));
    TEST_CHECK(!reads_rs1(OperationId::JAL));

    TEST_CHECK(is_control(OperationId::JALR));
    TEST_CHECK(is_jump(OperationId::JALR));
    TEST_CHECK(writes_integer(OperationId::JALR));
    TEST_CHECK(is_rs1_int(OperationId::JALR));
    TEST_CHECK(!reads_rs2(OperationId::JALR));

    TEST_CHECK(is_control(OperationId::BEQ));
    TEST_CHECK(is_branch(OperationId::BEQ));
    TEST_CHECK(!writes_rd(OperationId::BEQ));
    TEST_CHECK(is_rs1_int(OperationId::BEQ));
    TEST_CHECK(is_rs2_int(OperationId::BEQ));

    // Integer ALU
    TEST_CHECK(writes_integer(OperationId::ADD));
    TEST_CHECK(is_rs1_int(OperationId::ADD));
    TEST_CHECK(is_rs2_int(OperationId::ADD));
    TEST_CHECK(!reads_rs3(OperationId::ADD));

    TEST_CHECK(writes_integer(OperationId::ADDI));
    TEST_CHECK(is_rs1_int(OperationId::ADDI));
    TEST_CHECK(!reads_rs2(OperationId::ADDI));

    // Memory
    TEST_CHECK(is_load(OperationId::LW));
    TEST_CHECK(is_memory(OperationId::LW));
    TEST_CHECK(writes_integer(OperationId::LW));
    TEST_CHECK(is_rs1_int(OperationId::LW));
    TEST_CHECK(!reads_rs2(OperationId::LW));

    TEST_CHECK(is_store(OperationId::SW));
    TEST_CHECK(is_memory(OperationId::SW));
    TEST_CHECK(!writes_rd(OperationId::SW));
    TEST_CHECK(is_rs1_int(OperationId::SW));
    TEST_CHECK(is_rs2_int(OperationId::SW));

    // System & CSR
    TEST_CHECK(is_serializing(OperationId::CSRRW));
    TEST_CHECK(writes_integer(OperationId::CSRRW));
    TEST_CHECK(is_rs1_int(OperationId::CSRRW));

    TEST_CHECK(is_serializing(OperationId::CSRRWI));
    TEST_CHECK(writes_integer(OperationId::CSRRWI));
    TEST_CHECK(!reads_rs1(OperationId::CSRRWI));

    TEST_CHECK(is_serializing(OperationId::FENCE));
    TEST_CHECK(!writes_rd(OperationId::FENCE));
    TEST_CHECK(has_side_effects(OperationId::ECALL));
}

void test_floating_point_traits() {
    // Loads and Stores
    TEST_CHECK(is_load(OperationId::FLW));
    TEST_CHECK(writes_float(OperationId::FLW));
    TEST_CHECK(is_rs1_int(OperationId::FLW));
    TEST_CHECK(!reads_rs2(OperationId::FLW));

    TEST_CHECK(is_store(OperationId::FSW));
    TEST_CHECK(!writes_rd(OperationId::FSW));
    TEST_CHECK(is_rs1_int(OperationId::FSW));
    TEST_CHECK(is_rs2_fp(OperationId::FSW));

    TEST_CHECK(is_load(OperationId::FLD));
    TEST_CHECK(writes_float(OperationId::FLD));
    TEST_CHECK(is_store(OperationId::FSD));
    TEST_CHECK(is_rs2_fp(OperationId::FSD));

    // FP ALU binary
    TEST_CHECK(is_fp_alu(OperationId::FADD_S));
    TEST_CHECK(writes_float(OperationId::FADD_S));
    TEST_CHECK(is_rs1_fp(OperationId::FADD_S));
    TEST_CHECK(is_rs2_fp(OperationId::FADD_S));
    TEST_CHECK(!reads_rs3(OperationId::FADD_S));

    // FMA (3 sources)
    TEST_CHECK(is_fp_alu(OperationId::FMADD_S));
    TEST_CHECK(writes_float(OperationId::FMADD_S));
    TEST_CHECK(is_rs1_fp(OperationId::FMADD_S));
    TEST_CHECK(is_rs2_fp(OperationId::FMADD_S));
    TEST_CHECK(is_rs3_fp(OperationId::FMADD_S));

    TEST_CHECK(is_fp_alu(OperationId::FNMSUB_D));
    TEST_CHECK(writes_float(OperationId::FNMSUB_D));
    TEST_CHECK(is_rs1_fp(OperationId::FNMSUB_D));
    TEST_CHECK(is_rs2_fp(OperationId::FNMSUB_D));
    TEST_CHECK(is_rs3_fp(OperationId::FNMSUB_D));

    // FP Divide & Sqrt
    TEST_CHECK(is_fp_divide_or_sqrt(OperationId::FDIV_S));
    TEST_CHECK(writes_float(OperationId::FDIV_S));
    TEST_CHECK(is_rs1_fp(OperationId::FDIV_S));
    TEST_CHECK(is_rs2_fp(OperationId::FDIV_S));

    TEST_CHECK(is_fp_divide_or_sqrt(OperationId::FSQRT_D));
    TEST_CHECK(writes_float(OperationId::FSQRT_D));
    TEST_CHECK(is_rs1_fp(OperationId::FSQRT_D));
    TEST_CHECK(!reads_rs2(OperationId::FSQRT_D));

    // FP Comparison (Float -> Integer)
    TEST_CHECK(writes_integer(OperationId::FEQ_S));
    TEST_CHECK(!writes_float(OperationId::FEQ_S));
    TEST_CHECK(is_rs1_fp(OperationId::FEQ_S));
    TEST_CHECK(is_rs2_fp(OperationId::FEQ_S));

    TEST_CHECK(writes_integer(OperationId::FLT_D));
    TEST_CHECK(is_rs1_fp(OperationId::FLT_D));
    TEST_CHECK(is_rs2_fp(OperationId::FLT_D));

    // FP Classify / Move to Integer
    TEST_CHECK(writes_integer(OperationId::FCLASS_S));
    TEST_CHECK(is_rs1_fp(OperationId::FCLASS_S));
    TEST_CHECK(writes_integer(OperationId::FMV_X_W));
    TEST_CHECK(is_rs1_fp(OperationId::FMV_X_W));
    TEST_CHECK(!reads_rs2(OperationId::FMV_X_W));

    // Move to Float from Integer
    TEST_CHECK(writes_float(OperationId::FMV_W_X));
    TEST_CHECK(is_rs1_int(OperationId::FMV_W_X));
    TEST_CHECK(!is_rs1_fp(OperationId::FMV_W_X));

    // Conversions
    TEST_CHECK(writes_integer(OperationId::FCVT_W_S));
    TEST_CHECK(is_rs1_fp(OperationId::FCVT_W_S));

    TEST_CHECK(writes_float(OperationId::FCVT_S_W));
    TEST_CHECK(is_rs1_int(OperationId::FCVT_S_W));

    TEST_CHECK(writes_float(OperationId::FCVT_S_D));
    TEST_CHECK(is_rs1_fp(OperationId::FCVT_S_D));
}

void test_common_isa_helpers() {
    using namespace simrv::isa;
    TEST_CHECK(is_destination_fp(Opcode::LoadFp, OperationId::FLW));
    TEST_CHECK(is_destination_fp(Opcode::OpFp, OperationId::FADD_S));
    TEST_CHECK(!is_destination_fp(Opcode::OpFp, OperationId::FEQ_S));
    TEST_CHECK(!is_destination_fp(Opcode::OpFp, OperationId::FCVT_W_S));
    TEST_CHECK(!is_destination_fp(Opcode::Op, OperationId::ADD));

    TEST_CHECK(is_rs1_fp(Opcode::OpFp, OperationId::FADD_S));
    TEST_CHECK(!is_rs1_fp(Opcode::OpFp, OperationId::FCVT_S_W));
    TEST_CHECK(!is_rs1_fp(Opcode::OpFp, OperationId::FMV_W_X));
    TEST_CHECK(!is_rs1_fp(Opcode::LoadFp, OperationId::FLW));

    TEST_CHECK(is_rs2_fp(Opcode::StoreFp, OperationId::FSW));
    TEST_CHECK(is_rs2_fp(Opcode::OpFp, OperationId::FADD_S));
    TEST_CHECK(!is_rs2_fp(Opcode::Op, OperationId::ADD));
}

void test_scoreboard() {
    using simrv::pipeline::PipelineStage;
    using simrv::pipeline::Scoreboard;

    Scoreboard sb{};
    TEST_CHECK(!sb.is_busy(RegBank::Integer, simrv::xlen::RegId::A0));
    const auto fa0 = static_cast<simrv::xlen::RegId>(simrv::xlen::FpRegId::Fa0);
    TEST_CHECK(!sb.is_busy(RegBank::Float, fa0));

    // Zero register x0 should never be busy
    sb.reserve(RegBank::Integer, simrv::xlen::RegId::Zero, PipelineStage::Execute, 2, false);
    TEST_CHECK(!sb.is_busy(RegBank::Integer, simrv::xlen::RegId::Zero));

    // Integer register reservation
    sb.reserve(RegBank::Integer, simrv::xlen::RegId::A0, PipelineStage::Execute, 1, true);
    TEST_CHECK(sb.is_busy(RegBank::Integer, simrv::xlen::RegId::A0));
    TEST_CHECK(sb.can_forward(RegBank::Integer, simrv::xlen::RegId::A0));
    TEST_CHECK(sb.get_stage(RegBank::Integer, simrv::xlen::RegId::A0) == PipelineStage::Execute);

    // Float register reservation
    sb.reserve(RegBank::Float, fa0, PipelineStage::Memory, 2, false);
    TEST_CHECK(sb.is_busy(RegBank::Float, fa0));
    TEST_CHECK(!sb.can_forward(RegBank::Float, fa0));
    TEST_CHECK(sb.get_stage(RegBank::Float, fa0) == PipelineStage::Memory);

    // Independent banks
    TEST_CHECK(sb.is_busy(RegBank::Integer, simrv::xlen::RegId::A0));
    sb.release(RegBank::Integer, simrv::xlen::RegId::A0);
    TEST_CHECK(!sb.is_busy(RegBank::Integer, simrv::xlen::RegId::A0));
    TEST_CHECK(sb.is_busy(RegBank::Float, fa0));

    // Flush from stage
    sb.reserve(RegBank::Integer, simrv::xlen::RegId::A1, PipelineStage::Execute);
    sb.reserve(RegBank::Integer, simrv::xlen::RegId::A2, PipelineStage::Memory);
    sb.flush_from_stage(PipelineStage::Execute);
    TEST_CHECK(!sb.is_busy(RegBank::Integer, simrv::xlen::RegId::A1));
    TEST_CHECK(sb.is_busy(RegBank::Integer, simrv::xlen::RegId::A2));

    // Reset
    sb.reset();
    TEST_CHECK(!sb.is_busy(RegBank::Integer, simrv::xlen::RegId::A2));
    TEST_CHECK(!sb.is_busy(RegBank::Float, fa0));
}

void test_vector_operand_traits() {
    for (const auto op : {OperationId::VSEXT_VF2, OperationId::VFSQRT_V, OperationId::VFWCVT_F_X_V,
                          OperationId::VFNCVT_F_F_W, OperationId::VIOTA_M, OperationId::VMV4R_V}) {
        TEST_CHECK(writes_vector(op));
        TEST_CHECK(!reads_rs1(op));
        TEST_CHECK(rs2_bank(op) == RegBank::Vector);
    }
    TEST_CHECK(writes_vector(OperationId::VID_V));
    TEST_CHECK(!reads_rs1(OperationId::VID_V));
    TEST_CHECK(!reads_rs2(OperationId::VID_V));
    TEST_CHECK(writes_integer(OperationId::VMV_X_S));
    TEST_CHECK(!reads_rs1(OperationId::VMV_X_S));
    TEST_CHECK(rs2_bank(OperationId::VMV_X_S) == RegBank::Vector);
    TEST_CHECK(writes_float(OperationId::VFMV_F_S));
    TEST_CHECK(!reads_rs1(OperationId::VFMV_F_S));
    TEST_CHECK(rs2_bank(OperationId::VFMV_F_S) == RegBank::Vector);
    TEST_CHECK(writes_vector(OperationId::VMV_S_X));
    TEST_CHECK(rs1_bank(OperationId::VMV_S_X) == RegBank::Integer);
    TEST_CHECK(!reads_rs2(OperationId::VMV_S_X));
}

}  // namespace

int run() {
    test_base_integer_traits();
    test_floating_point_traits();
    test_common_isa_helpers();
    test_scoreboard();
    test_vector_operand_traits();
    std::cout << "OperationTraitsTests passed successfully.\n";
    return 0;
}

}  // namespace section_0
#undef TEST_CHECK

namespace section_1 {
/**
 * @file ScoreboardTests.cpp
 * @brief Unit tests for register scoreboard tracking, reservations, flushes, and hazard detection.
 */

#define TEST_CHECK(expr)                                                                     \
    do {                                                                                     \
        if (!(expr)) {                                                                       \
            std::cerr << "Assertion failed: " #expr " at " __FILE__ ":" << __LINE__ << "\n"; \
            std::abort();                                                                    \
        }                                                                                    \
    } while (0)

namespace {

using simrv::isa::OperationId;
using simrv::pipeline::DecodedInstruction;
using simrv::pipeline::PipelineStage;
using simrv::pipeline::Scoreboard;
using namespace simrv::pipeline::operation;

void test_initial_state() {
    Scoreboard sb{};
    for (size_t i = 0; i < Scoreboard::kNumIntRegisters; ++i) {
        const auto reg = static_cast<simrv::xlen::RegId>(i);
        TEST_CHECK(!sb.is_busy(RegBank::Integer, reg));
        TEST_CHECK(!sb.can_forward(RegBank::Integer, reg));
        TEST_CHECK(sb.get_forwarded_value(RegBank::Integer, reg) == 0);
        TEST_CHECK(!sb.get_stage(RegBank::Integer, reg).has_value());
    }
    for (size_t i = 0; i < Scoreboard::kNumFpRegisters; ++i) {
        const auto reg = static_cast<simrv::xlen::RegId>(i);
        TEST_CHECK(!sb.is_busy(RegBank::Float, reg));
        TEST_CHECK(!sb.can_forward(RegBank::Float, reg));
    }
}

void test_integer_reservation_and_forwarding() {
    Scoreboard sb{};
    // Reserve x5 (T0) at Execute stage with forwarded value 42
    sb.reserve(RegBank::Integer, simrv::xlen::RegId::T0, PipelineStage::Execute, 1, true, 42);
    TEST_CHECK(sb.is_busy(RegBank::Integer, simrv::xlen::RegId::T0));
    TEST_CHECK(sb.can_forward(RegBank::Integer, simrv::xlen::RegId::T0));
    TEST_CHECK(sb.get_forwarded_value(RegBank::Integer, simrv::xlen::RegId::T0) == 42);
    TEST_CHECK(sb.get_stage(RegBank::Integer, simrv::xlen::RegId::T0) == PipelineStage::Execute);
    TEST_CHECK(sb.get_latency(RegBank::Integer, simrv::xlen::RegId::T0) == 1);

    // x0 (Zero) must never be reserved
    sb.reserve(RegBank::Integer, simrv::xlen::RegId::Zero, PipelineStage::Execute, 0, true, 100);
    TEST_CHECK(!sb.is_busy(RegBank::Integer, simrv::xlen::RegId::Zero));

    // Release x5
    sb.release(RegBank::Integer, simrv::xlen::RegId::T0);
    TEST_CHECK(!sb.is_busy(RegBank::Integer, simrv::xlen::RegId::T0));
    TEST_CHECK(!sb.can_forward(RegBank::Integer, simrv::xlen::RegId::T0));
}

void test_float_reservation_and_conservative_policy() {
    Scoreboard sb{};
    const auto fa0 = static_cast<simrv::xlen::RegId>(simrv::xlen::FpRegId::Fa0);

    // Reserve fa0 with conservative non-forwarding (can_forward = false)
    sb.reserve(RegBank::Float, fa0, PipelineStage::Execute, 2, false);
    TEST_CHECK(sb.is_busy(RegBank::Float, fa0));
    TEST_CHECK(!sb.can_forward(RegBank::Float, fa0));

    // Consumer instruction reading fa0 as rs1
    DecodedInstruction consumer{};
    // fmul.s f2, f10 (fa0), f1
    unpack_instruction(consumer, 0x10150153, OperationId::FMUL_S);
    TEST_CHECK(consumer.traits.reads_rs1_fp);
    TEST_CHECK(consumer.rs1 == fa0);

    // Must report hazard even if forwarding is enabled
    TEST_CHECK(sb.has_raw_hazard(consumer, true));
    TEST_CHECK(sb.has_raw_hazard(consumer, false));

    // Release at retirement
    sb.release(RegBank::Float, fa0);
    TEST_CHECK(!sb.is_busy(RegBank::Float, fa0));
    TEST_CHECK(!sb.has_raw_hazard(consumer, true));
}

void test_fma_rs3_hazard_detection() {
    Scoreboard sb{};
    const auto fa3 = static_cast<simrv::xlen::RegId>(simrv::xlen::FpRegId::Fa3);

    // Reserve fa3 in scoreboard
    sb.reserve(RegBank::Float, fa3, PipelineStage::Memory, 1, false);
    TEST_CHECK(sb.is_busy(RegBank::Float, fa3));

    // Consumer instruction with rs3 = fa3 (13)
    // fmadd.s f0, f1, f2, fa3 (rs3 is encoded in bits 31:27 = 13 = 0x0D)
    // fmadd.s rd=0, rs1=1, rs2=2, funct3=7, rs3=13:
    // (13 << 27) | (0 << 25) | (2 << 20) | (1 << 15) | (7 << 12) | (0 << 7) | 0x43
    constexpr simrv::xlen::Instruction fmadd_inst =
        (13U << 27U) | (2U << 20U) | (1U << 15U) | (7U << 12U) | 0x43U;
    DecodedInstruction consumer{};
    unpack_instruction(consumer, fmadd_inst, OperationId::FMADD_S);
    TEST_CHECK(consumer.traits.reads_rs3_fp);

    // Hazard must be detected on rs3
    TEST_CHECK(sb.has_raw_hazard(consumer, true));

    // Release fa3
    sb.release(RegBank::Float, fa3);
    TEST_CHECK(!sb.has_raw_hazard(consumer, true));
}

void test_stage_flush() {
    Scoreboard sb{};
    sb.reserve(RegBank::Integer, simrv::xlen::RegId::T0, PipelineStage::Decode, 1, false);
    sb.reserve(RegBank::Integer, simrv::xlen::RegId::T1, PipelineStage::Execute, 1, true, 10);
    sb.reserve(RegBank::Integer, simrv::xlen::RegId::T2, PipelineStage::Writeback, 0, true, 20);

    // Flush Decode and younger
    sb.flush_from_stage(PipelineStage::Decode);
    TEST_CHECK(!sb.is_busy(RegBank::Integer, simrv::xlen::RegId::T0));
    TEST_CHECK(sb.is_busy(RegBank::Integer, simrv::xlen::RegId::T1));
    TEST_CHECK(sb.is_busy(RegBank::Integer, simrv::xlen::RegId::T2));

    // Reset clears everything
    sb.reset();
    TEST_CHECK(!sb.is_busy(RegBank::Integer, simrv::xlen::RegId::T1));
    TEST_CHECK(!sb.is_busy(RegBank::Integer, simrv::xlen::RegId::T2));
}

void test_integer_raw_hazard_forwardable_mask() {
    Scoreboard sb{};
    // Reserve x5 (T0) with can_forward = true
    sb.reserve(RegBank::Integer, simrv::xlen::RegId::T0, PipelineStage::Execute, 0, true, 42);
    // Reserve x6 (T1) with can_forward = false (e.g. load in flight)
    sb.reserve(RegBank::Integer, simrv::xlen::RegId::T1, PipelineStage::Execute, 1, false, 0);

    DecodedInstruction consumer_t0{};
    // add a0, t0, zero (ir = 0x00028533: rd=10(a0), rs1=5(t0), rs2=0)
    unpack_instruction(consumer_t0, 0x00028533, OperationId::ADD);
    TEST_CHECK(consumer_t0.traits.reads_rs1_int);
    TEST_CHECK(consumer_t0.rs1 == simrv::xlen::RegId::T0);

    // When forwarding is enabled, t0 can forward -> no hazard
    TEST_CHECK(!sb.has_raw_hazard(consumer_t0, true));
    // When forwarding is disabled, t0 cannot forward -> hazard!
    TEST_CHECK(sb.has_raw_hazard(consumer_t0, false));

    DecodedInstruction consumer_t1{};
    // add a0, t1, zero (ir = 0x00030533: rd=10(a0), rs1=6(t1), rs2=0)
    unpack_instruction(consumer_t1, 0x00030533, OperationId::ADD);
    TEST_CHECK(consumer_t1.traits.reads_rs1_int);
    TEST_CHECK(consumer_t1.rs1 == simrv::xlen::RegId::T1);

    // t1 cannot forward -> hazard regardless of forwarding setting!
    TEST_CHECK(sb.has_raw_hazard(consumer_t1, true));
    TEST_CHECK(sb.has_raw_hazard(consumer_t1, false));

    // Release t1
    sb.release(RegBank::Integer, simrv::xlen::RegId::T1);
    TEST_CHECK(!sb.has_raw_hazard(consumer_t1, true));
    TEST_CHECK(!sb.has_raw_hazard(consumer_t1, false));

    // Release t0
    sb.release(RegBank::Integer, simrv::xlen::RegId::T0);
    TEST_CHECK(!sb.has_raw_hazard(consumer_t0, false));
}

}  // namespace

int run() {
    std::cout << "=== Running Scoreboard Unit Tests ===\n";
    test_initial_state();
    test_integer_reservation_and_forwarding();
    test_integer_raw_hazard_forwardable_mask();
    test_float_reservation_and_conservative_policy();
    test_fma_rs3_hazard_detection();
    test_stage_flush();
    std::cout << "=== All Scoreboard Unit Tests Passed Successfully ===\n";
    return 0;
}

}  // namespace section_1
#undef TEST_CHECK

namespace section_2 {
/**
 * @file BranchPredictorTests.cpp
 * @brief Unit tests for SimRV cycle-accurate branch prediction subsystem.
 */

#define TEST_CHECK(expr)                                                                     \
    do {                                                                                     \
        if (!(expr)) {                                                                       \
            std::cerr << "Assertion failed: " #expr " at " __FILE__ ":" << __LINE__ << "\n"; \
            std::abort();                                                                    \
        }                                                                                    \
    } while (0)

namespace {

using simrv::isa::Opcode;
using simrv::pipeline::BranchFeedback;
using simrv::pipeline::BranchPrediction;
using simrv::pipeline::BranchPredictor;
using simrv::pipeline::BranchPredictorConfig;
using simrv::pipeline::BranchPredictorType;
using simrv::pipeline::DecodedInstruction;

void test_type_parsing() {
    TEST_CHECK(simrv::pipeline::parse_branch_predictor_type("none") ==
               BranchPredictorType::Disabled);
    TEST_CHECK(simrv::pipeline::parse_branch_predictor_type("static") ==
               BranchPredictorType::Static);
    TEST_CHECK(simrv::pipeline::parse_branch_predictor_type("bimodal") ==
               BranchPredictorType::Bimodal);
    TEST_CHECK(simrv::pipeline::parse_branch_predictor_type("2bit") ==
               BranchPredictorType::Bimodal);
    TEST_CHECK(simrv::pipeline::parse_branch_predictor_type("gshare") ==
               BranchPredictorType::GShare);
    TEST_CHECK(simrv::pipeline::parse_branch_predictor_type("tournament") ==
               BranchPredictorType::Tournament);
    TEST_CHECK(simrv::pipeline::parse_branch_predictor_type("invalid") == std::nullopt);
}

void test_disabled_predictor() {
    BranchPredictorConfig config{.type = BranchPredictorType::Disabled};
    BranchPredictor bp(config);

    DecodedInstruction branch{};
    branch.opcode = Opcode::Branch;
    branch.imm = -16;
    const auto branch_prediction = bp.predict(0x100, branch);
    TEST_CHECK(branch_prediction.is_control);
    TEST_CHECK(!branch_prediction.predicted_taken);
    TEST_CHECK(branch_prediction.predicted_target == 0x104);

    DecodedInstruction jump{};
    jump.opcode = Opcode::Jal;
    jump.imm = 64;
    const auto jump_prediction = bp.predict(0x200, jump);
    TEST_CHECK(jump_prediction.is_control);
    TEST_CHECK(!jump_prediction.predicted_taken);
    TEST_CHECK(jump_prediction.predicted_target == 0x204);
}

void test_registered_btb_read_latency() {
    BranchPredictorConfig config{
        .type = BranchPredictorType::Bimodal,
        .bht_entries = 128,
        .btb_entries = 64,
        .ras_entries = 8,
        .pc_shift = 2,
        .untagged_btb = true,
        .registered_btb_read = true,
    };
    BranchPredictor bp(config);

    DecodedInstruction branch{};
    branch.opcode = Opcode::Branch;
    branch.imm = 12;
    constexpr simrv::xlen::Address pc = 0x1000;

    bp.latch_btb_read(pc);
    const auto initial = bp.predict(pc, branch);
    TEST_CHECK(!initial.predicted_taken);
    bp.update({.pc = pc,
               .actual_taken = true,
               .actual_target = pc + 12,
               .opcode = Opcode::Branch,
               .prediction = initial});

    // The registered output remains stale until the next modeled BRAM read edge.
    TEST_CHECK(!bp.predict(pc, branch).predicted_taken);
    bp.latch_btb_read(pc);
    const auto updated = bp.predict(pc, branch);
    TEST_CHECK(updated.predicted_taken);
    TEST_CHECK(updated.predicted_target == pc + 12);
}

void test_static_predictor() {
    BranchPredictorConfig config{
        .type = BranchPredictorType::Static,
        .bht_entries = 128,
        .btb_entries = 64,
        .ras_entries = 8,
    };
    BranchPredictor bp(config);

    DecodedInstruction fwd_branch{};
    fwd_branch.opcode = Opcode::Branch;
    fwd_branch.imm = 16;  // Forward jump

    DecodedInstruction bwd_branch{};
    bwd_branch.opcode = Opcode::Branch;
    bwd_branch.imm = -8;  // Backward jump (loop)

    auto pred_fwd = bp.predict(0x1000, fwd_branch);
    TEST_CHECK(!pred_fwd.predicted_taken);
    TEST_CHECK(pred_fwd.predicted_target == 0x1004);

    auto pred_bwd = bp.predict(0x1020, bwd_branch);
    TEST_CHECK(pred_bwd.predicted_taken);
    TEST_CHECK(pred_bwd.predicted_target == 0x1020 - 8);
}

void test_bimodal_transitions() {
    BranchPredictorConfig config{
        .type = BranchPredictorType::Bimodal,
        .bht_entries = 128,
        .btb_entries = 64,
        .ras_entries = 8,
    };
    BranchPredictor bp(config);

    DecodedInstruction branch{};
    branch.opcode = Opcode::Branch;
    branch.imm = 12;

    const simrv::xlen::Address pc = 0x2000;

    // Initial state is Weakly Not-Taken (1) -> predicts not-taken
    auto pred1 = bp.predict(pc, branch);
    TEST_CHECK(!pred1.predicted_taken);

    // Train Taken -> counter becomes Weakly Taken (2)
    BranchFeedback fb1{
        .pc = pc,
        .actual_taken = true,
        .actual_target = pc + 12,
        .opcode = Opcode::Branch,
        .prediction = pred1,
    };
    bp.update(fb1);

    auto pred2 = bp.predict(pc, branch);
    TEST_CHECK(pred2.predicted_taken);
    TEST_CHECK(pred2.predicted_target == pc + 12);

    // Train Taken again -> counter becomes Strongly Taken (3)
    BranchFeedback fb2{
        .pc = pc,
        .actual_taken = true,
        .actual_target = pc + 12,
        .opcode = Opcode::Branch,
        .prediction = pred2,
    };
    bp.update(fb2);

    auto pred3 = bp.predict(pc, branch);
    TEST_CHECK(pred3.predicted_taken);

    // Train Not-Taken -> counter becomes Weakly Taken (2) -> still predicts taken
    BranchFeedback fb3{
        .pc = pc,
        .actual_taken = false,
        .actual_target = pc + 4,
        .opcode = Opcode::Branch,
        .prediction = pred3,
    };
    bp.update(fb3);

    auto pred4 = bp.predict(pc, branch);
    TEST_CHECK(pred4.predicted_taken);

    // Train Not-Taken -> counter becomes Weakly Not-Taken (1) -> predicts not-taken
    BranchFeedback fb4{
        .pc = pc,
        .actual_taken = false,
        .actual_target = pc + 4,
        .opcode = Opcode::Branch,
        .prediction = pred4,
    };
    bp.update(fb4);

    auto pred5 = bp.predict(pc, branch);
    TEST_CHECK(!pred5.predicted_taken);
}

void test_gshare_and_history_restoration() {
    BranchPredictorConfig config{
        .type = BranchPredictorType::GShare,
        .bht_entries = 256,
        .btb_entries = 64,
        .ras_entries = 8,
        .ghr_bits = 8,
    };
    BranchPredictor bp(config);

    DecodedInstruction branch{};
    branch.opcode = Opcode::Branch;
    branch.imm = 8;

    const simrv::xlen::Address pc = 0x3000;

    // First prediction with GHR=0
    auto pred1 = bp.predict(pc, branch);
    TEST_CHECK(!pred1.predicted_taken);
    TEST_CHECK(bp.ghr() == 0b0);

    // Speculative prediction with predicted taken
    // We train the entry at (pc ^ 0) to be strongly taken
    BranchFeedback fb1{
        .pc = pc,
        .actual_taken = true,
        .actual_target = pc + 8,
        .opcode = Opcode::Branch,
        .prediction = pred1,
    };
    bp.update(fb1);
    bp.update(fb1);

    // Now with GHR=0, it predicts taken, speculatively shifting 1 into GHR
    auto pred2 = bp.predict(pc, branch);
    TEST_CHECK(pred2.predicted_taken);
    TEST_CHECK(bp.ghr() == 0b1);

    // If mispredicted, restoring speculation should restore GHR back to 0
    bp.restore_speculation(pred2);
    TEST_CHECK(bp.ghr() == 0b0);
}

void test_ras_call_and_return() {
    BranchPredictorConfig config{
        .type = BranchPredictorType::GShare,
        .bht_entries = 128,
        .btb_entries = 64,
        .ras_entries = 4,
        .enable_ras = true,
    };
    BranchPredictor bp(config);

    // Direct function call: JAL RA, 0x100
    DecodedInstruction call1{};
    call1.opcode = Opcode::Jal;
    call1.rd = simrv::xlen::RegId::Ra;
    call1.imm = 0x100;

    auto pred_call1 = bp.predict(0x80000000, call1);
    TEST_CHECK(pred_call1.is_call);
    TEST_CHECK(pred_call1.predicted_target == 0x80000100);
    TEST_CHECK(bp.ras_depth() == 1);
    TEST_CHECK(bp.ras_peek() == 0x80000004);

    // Nested call: JAL RA, 0x50
    DecodedInstruction call2{};
    call2.opcode = Opcode::Jal;
    call2.rd = simrv::xlen::RegId::Ra;
    call2.imm = 0x50;

    auto pred_call2 = bp.predict(0x80000120, call2);
    TEST_CHECK(pred_call2.is_call);
    TEST_CHECK(bp.ras_depth() == 2);
    TEST_CHECK(bp.ras_peek() == 0x80000124);

    // Return from nested call: JALR x0, RA, 0
    DecodedInstruction ret1{};
    ret1.opcode = Opcode::Jalr;
    ret1.rd = simrv::xlen::RegId::Zero;
    ret1.rs1 = simrv::xlen::RegId::Ra;
    ret1.imm = 0;

    auto pred_ret1 = bp.predict(0x80000170, ret1);
    TEST_CHECK(pred_ret1.is_return);
    TEST_CHECK(pred_ret1.ras_hit);
    TEST_CHECK(pred_ret1.predicted_target == 0x80000124);
    TEST_CHECK(bp.ras_depth() == 1);

    // Return from top call: JALR x0, RA, 0
    auto pred_ret2 = bp.predict(0x80000130, ret1);
    TEST_CHECK(pred_ret2.is_return);
    TEST_CHECK(pred_ret2.ras_hit);
    TEST_CHECK(pred_ret2.predicted_target == 0x80000004);
    TEST_CHECK(bp.ras_depth() == 0);
}

void test_btb_indirect_jump_caching() {
    BranchPredictorConfig config{
        .type = BranchPredictorType::GShare,
        .bht_entries = 128,
        .btb_entries = 64,
        .ras_entries = 4,
        .enable_btb = true,
        .enable_ras = false,
    };
    BranchPredictor bp(config);

    // Non-return JALR: JALR x0, a0, 0
    DecodedInstruction jalr{};
    jalr.opcode = Opcode::Jalr;
    jalr.rd = simrv::xlen::RegId::Zero;
    jalr.rs1 = simrv::xlen::RegId::A0;
    jalr.imm = 0;

    const simrv::xlen::Address pc = 0x4000;

    // First lookup: BTB miss
    auto pred1 = bp.predict(pc, jalr);
    TEST_CHECK(!pred1.btb_hit);

    // Train BTB with target 0x80001000
    BranchFeedback fb1{
        .pc = pc,
        .actual_taken = true,
        .actual_target = 0x80001000,
        .opcode = Opcode::Jalr,
        .prediction = pred1,
    };
    bp.update(fb1);

    // Second lookup: BTB hit
    auto pred2 = bp.predict(pc, jalr);
    TEST_CHECK(pred2.btb_hit);
    TEST_CHECK(pred2.predicted_target == 0x80001000);
}

void test_telemetry_stats() {
    BranchPredictorConfig config{
        .type = BranchPredictorType::Bimodal,
        .bht_entries = 128,
        .btb_entries = 64,
        .ras_entries = 8,
    };
    BranchPredictor bp(config);

    DecodedInstruction branch{};
    branch.opcode = Opcode::Branch;
    branch.imm = 4;

    auto pred = bp.predict(0x5000, branch);
    BranchFeedback fb{
        .pc = 0x5000,
        .actual_taken = false,
        .actual_target = 0x5004,
        .opcode = Opcode::Branch,
        .prediction = pred,
    };
    bp.update(fb);

    const auto& st = bp.stats();
    TEST_CHECK(st.total_branches == 1);
    TEST_CHECK(st.conditional_branches == 1);
    TEST_CHECK(st.direction_predictions == 1);
    TEST_CHECK(st.direction_hits == 1);
    TEST_CHECK(st.direction_misses == 0);
    TEST_CHECK(st.overall_accuracy() == 100.0);
}

void test_untagged_btb_false_control_alias() {
    BranchPredictorConfig config{
        .type = BranchPredictorType::Bimodal,
        .bht_entries = 16,
        .btb_entries = 16,
        .ras_entries = 4,
        .enable_btb = true,
        .enable_ras = false,
        .pc_shift = 2,
        .untagged_btb = true,
        .predict_non_control = true,
    };
    BranchPredictor bp(config);

    DecodedInstruction branch{};
    branch.opcode = Opcode::Branch;
    branch.imm = 12;
    constexpr simrv::xlen::Address branch_pc = 0x1000;
    auto cold = bp.predict(branch_pc, branch);
    bp.update({.pc = branch_pc,
               .actual_taken = true,
               .actual_target = branch_pc + 12,
               .opcode = Opcode::Branch,
               .prediction = cold});

    DecodedInstruction add{};
    add.opcode = Opcode::Op;
    constexpr simrv::xlen::Address alias_pc = branch_pc + (16 << 2);
    const auto alias = bp.predict(alias_pc, add);
    TEST_CHECK(alias.is_control);
    TEST_CHECK(alias.false_control_alias);
    TEST_CHECK(alias.predicted_taken);
    TEST_CHECK(alias.predicted_target == branch_pc + 12);

    bp.update({.pc = alias_pc,
               .actual_taken = false,
               .actual_target = alias_pc + 4,
               .opcode = Opcode::Op,
               .prediction = alias});
    TEST_CHECK(!bp.predict(alias_pc, add).predicted_taken);
}

}  // namespace

int run() {
    test_type_parsing();
    test_disabled_predictor();
    test_registered_btb_read_latency();
    test_static_predictor();
    test_bimodal_transitions();
    test_gshare_and_history_restoration();
    test_ras_call_and_return();
    test_btb_indirect_jump_caching();
    test_telemetry_stats();
    test_untagged_btb_false_control_alias();
    std::cout << "All BranchPredictor tests passed successfully!\n";
    return 0;
}

}  // namespace section_2
#undef TEST_CHECK

namespace section_3 {
/**
 * @file PipelineHazardTests.cpp
 * @brief Regression tests for integer forwarding, FP conservative hazard stalls, FMA rs3
 * dependencies, and cross-bank conversions.
 */

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
    sb.reserve(RegBank::Integer, simrv::xlen::RegId::Ra, PipelineStage::Execute, 0, true);
    TEST_CHECK(sb.is_busy(RegBank::Integer, simrv::xlen::RegId::Ra));
    TEST_CHECK(sb.can_forward(RegBank::Integer, simrv::xlen::RegId::Ra));

    // Consumer: ADD x4, x1, x5 reads x1 as rs1
    TEST_CHECK(is_rs1_int(OperationId::ADD));
    TEST_CHECK(sb.is_busy(RegBank::Integer, simrv::xlen::RegId::Ra));
    // Since can_forward is true, forwarding resolves the hazard without stall
    TEST_CHECK(sb.can_forward(RegBank::Integer, simrv::xlen::RegId::Ra));

    // After Writeback, release
    sb.release(RegBank::Integer, simrv::xlen::RegId::Ra);
    TEST_CHECK(!sb.is_busy(RegBank::Integer, simrv::xlen::RegId::Ra));
}

void test_load_use_hazard() {
    Scoreboard sb{};
    // Producer: LW x1, 0(x2) in Execute stage, latency = 1 (cannot forward yet)
    sb.reserve(RegBank::Integer, simrv::xlen::RegId::Ra, PipelineStage::Execute, 1, false);
    TEST_CHECK(sb.is_busy(RegBank::Integer, simrv::xlen::RegId::Ra));
    TEST_CHECK(!sb.can_forward(RegBank::Integer, simrv::xlen::RegId::Ra));

    // Consumer: ADD x3, x1, x4 in Decode stage
    TEST_CHECK(is_rs1_int(OperationId::ADD));
    // Hazard stall required!
    TEST_CHECK(!sb.can_forward(RegBank::Integer, simrv::xlen::RegId::Ra));

    // Next cycle: LW advances to Memory stage, result becomes forwardable
    sb.reserve(RegBank::Integer, simrv::xlen::RegId::Ra, PipelineStage::Memory, 0, true);
    TEST_CHECK(sb.can_forward(RegBank::Integer, simrv::xlen::RegId::Ra));
}

void test_fp_conservative_stall() {
    Scoreboard sb{};
    const auto fa0 = static_cast<simrv::xlen::RegId>(simrv::xlen::FpRegId::Fa0);
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
    const auto fa2 = static_cast<simrv::xlen::RegId>(simrv::xlen::FpRegId::Fa2);
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
    const auto fa0 = static_cast<simrv::xlen::RegId>(simrv::xlen::FpRegId::Fa0);

    // 1. FP to Integer conversion: FCVT.W.S x1, f1
    TEST_CHECK(is_rs1_fp(OperationId::FCVT_W_S));
    TEST_CHECK(writes_integer(OperationId::FCVT_W_S));
    TEST_CHECK(!writes_float(OperationId::FCVT_W_S));

    // Reserve FP source f1
    sb.reserve(RegBank::Float, fa0, PipelineStage::Execute, 2, false);
    TEST_CHECK(sb.is_busy(RegBank::Float, fa0));
    sb.release(RegBank::Float, fa0);

    // FCVT now reserves integer destination x1
    sb.reserve(RegBank::Integer, simrv::xlen::RegId::Ra, PipelineStage::Execute, 1, false);
    TEST_CHECK(sb.is_busy(RegBank::Integer, simrv::xlen::RegId::Ra));
    TEST_CHECK(!sb.is_busy(RegBank::Float, fa0));
    sb.release(RegBank::Integer, simrv::xlen::RegId::Ra);

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
    constexpr std::array<simrv::xlen::Instruction, 4> program = {
        0x00a00093,  // addi x1, x0, 10
        0x00508113,  // addi x2, x1, 5
        0x001101b3,  // add  x3, x2, x1
        0x0000006f,  // jal  x0, 0
    };

    const auto run_model = [&](simrv::pipeline::PipelineType type, bool forwarding) {
        simrv::core::Machine machine;
        const simrv::xlen::Address pc = machine.memory_geometry().dram_base;
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        auto& cpu = machine.primary_hart();
        cpu.machine_ = &machine;
        cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        cpu.pipeline_sim.config.pipeline_type = type;
        cpu.pipeline_sim.config.enable_forwarding = forwarding;

        std::array<simrv::xlen::Byte, simrv::cache::ICache::kLineBytes> line{};
        std::memcpy(line.data(), program.data(), sizeof(program));
        std::memcpy(ram.data(), program.data(), sizeof(program));
        cpu.icache.insert(simrv::xlen::PhysAddr{pc}, line.data(),
                          simrv::memory::MesiState::Exclusive);
        cpu.state().pc = pc;

        uint32_t cycles = 0;
        while (cpu.e_icount < 3 && cycles < 64) {
            cpu.run_cycle(machine);
            machine.memory().system_bus().advance_cycle();
            ++cycles;
        }
        TEST_CHECK(cpu.e_icount == 3);
        TEST_CHECK(cpu.state().regs.read(simrv::xlen::RegId::Ra) == 10);
        TEST_CHECK(cpu.state().regs.read(simrv::xlen::RegId::Sp) == 15);
        TEST_CHECK(cpu.state().regs.read(simrv::xlen::RegId::Gp) == 25);
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

void test_zkt_fixed_instruction_latency() {
    using simrv::pipeline::operation::ExecutionClass;
    constexpr std::array zkt_ops{
        OperationId::LUI,    OperationId::AUIPC,  OperationId::ADDI,  OperationId::SLTI,
        OperationId::SLTIU,  OperationId::XORI,   OperationId::ORI,   OperationId::ANDI,
        OperationId::SLLI,   OperationId::SRLI,   OperationId::SRAI,  OperationId::ADD,
        OperationId::SUB,    OperationId::SLL,    OperationId::SLT,   OperationId::SLTU,
        OperationId::XOR,    OperationId::SRL,    OperationId::SRA,   OperationId::OR,
        OperationId::AND,    OperationId::ADDIW,  OperationId::SLLIW, OperationId::SRLIW,
        OperationId::SRAIW,  OperationId::ADDW,   OperationId::SUBW,  OperationId::SLLW,
        OperationId::SRLW,   OperationId::SRAW,   OperationId::MUL,   OperationId::MULH,
        OperationId::MULHSU, OperationId::MULHU,  OperationId::MULW,  OperationId::ANDN,
        OperationId::ORN,    OperationId::XNOR,   OperationId::ROL,   OperationId::ROR,
        OperationId::RORI,   OperationId::ROLW,   OperationId::RORW,  OperationId::RORIW,
        OperationId::CLMUL,  OperationId::CLMULH, OperationId::PACK,  OperationId::PACKW,
        OperationId::REV8,
    };
    for (const auto op : zkt_ops) {
        const auto cls = simrv::pipeline::operation::info(op).execution_class;
        TEST_CHECK(cls == ExecutionClass::Default || cls == ExecutionClass::Multiply);
    }
    TEST_CHECK(simrv::pipeline::operation::info(OperationId::DIV).execution_class ==
               ExecutionClass::DivideOrRemainder);
    TEST_CHECK(simrv::pipeline::operation::info(OperationId::REM).execution_class ==
               ExecutionClass::DivideOrRemainder);

    constexpr simrv::xlen::Instruction add = 0x002081B3;  // add x3, x1, x2
    constexpr simrv::xlen::Instruction mul = 0x022081B3;  // mul x3, x1, x2
    const auto encode_r = [](uint32_t funct7, uint32_t funct3, uint32_t opcode = 0x33U) {
        constexpr uint32_t rd = 3;
        constexpr uint32_t rs1 = 1;
        constexpr uint32_t rs2 = 2;
        return static_cast<simrv::xlen::Instruction>((funct7 << 25U) | (rs2 << 20U) | (rs1 << 15U) |
                                                     (funct3 << 12U) | (rd << 7U) | opcode);
    };
    constexpr std::array<simrv::xlen::Register, 3> operand_patterns{
        0,
        ~simrv::xlen::Register{0},
        static_cast<simrv::xlen::Register>(UINT64_C(0x8000000000000001)),
    };
    const std::array zkt_bitmanip_ops{
        encode_r(0x05, 0x1),        // clmul
        encode_r(0x05, 0x3),        // clmulh
        encode_r(0x20, 0x7),        // andn
        encode_r(0x20, 0x6),        // orn
        encode_r(0x20, 0x4),        // xnor
        encode_r(0x30, 0x1),        // rol
        encode_r(0x30, 0x5),        // ror
        encode_r(0x04, 0x4),        // pack
        encode_r(0x04, 0x4, 0x3b),  // packw
    };
    const auto run_one = [&](simrv::xlen::Instruction instruction, simrv::xlen::Register lhs,
                             simrv::xlen::Register rhs, simrv::pipeline::PipelineType type) {
        simrv::core::Machine machine;
        constexpr size_t ram_size = 1024 * 1024;
        std::vector<simrv::xlen::Byte> ram(ram_size, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        auto& cpu = machine.primary_hart();
        cpu.machine_ = &machine;
        cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        cpu.pipeline_sim.config.pipeline_type = type;

        const auto pc = machine.memory_geometry().dram_base;
        const std::array<simrv::xlen::Instruction, 2> program = {instruction, 0x0000006F};
        std::memcpy(ram.data(), program.data(), sizeof(program));
        std::array<simrv::xlen::Byte, simrv::cache::ICache::kLineBytes> line{};
        std::memcpy(line.data(), program.data(), sizeof(program));
        cpu.icache.insert(simrv::xlen::PhysAddr{pc}, line.data(),
                          simrv::memory::MesiState::Exclusive);
        cpu.state().pc = pc;
        cpu.state().regs.write(simrv::xlen::RegId::Ra, lhs);
        cpu.state().regs.write(simrv::xlen::RegId::Sp, rhs);

        uint32_t cycles = 0;
        while (cpu.e_icount < 1 && cycles < 64) {
            cpu.run_cycle(machine);
            machine.memory().system_bus().advance_cycle();
            ++cycles;
        }
        TEST_CHECK(cpu.e_icount == 1);
        return cycles;
    };

    for (const auto type :
         {simrv::pipeline::PipelineType::ThreeStage, simrv::pipeline::PipelineType::FiveStage}) {
        TEST_CHECK(run_one(add, 0, 0, type) == run_one(add, ~simrv::xlen::Register{0},
                                                       simrv::xlen::Register{0x55555555U}, type));
        TEST_CHECK(run_one(mul, 0, 0, type) == run_one(mul, ~simrv::xlen::Register{0},
                                                       simrv::xlen::Register{0x55555555U}, type));
        for (const auto instruction : zkt_bitmanip_ops) {
            const auto expected_cycles =
                run_one(instruction, operand_patterns[0], operand_patterns[0], type);
            for (const auto lhs : operand_patterns) {
                for (const auto rhs : operand_patterns) {
                    TEST_CHECK(run_one(instruction, lhs, rhs, type) == expected_cycles);
                }
            }
        }
    }
}

void test_zvkt_vector_latency() {
    using simrv::pipeline::operation::ExecutionClass;
    constexpr std::array zvkt_ops{
        OperationId::VANDN_VV, OperationId::VANDN_VX, OperationId::VROL_VV,  OperationId::VROL_VX,
        OperationId::VROR_VV,  OperationId::VROR_VX,  OperationId::VROR_VI,  OperationId::VCLZ_V,
        OperationId::VCTZ_V,   OperationId::VCPOP_V,  OperationId::VBREV_V,  OperationId::VBREV8_V,
        OperationId::VREV8_V,  OperationId::VWSLL_VV, OperationId::VWSLL_VX, OperationId::VWSLL_VI,
    };
    for (const auto op : zvkt_ops) {
        TEST_CHECK(simrv::pipeline::operation::info(op).execution_class == ExecutionClass::Default);
    }

    constexpr simrv::xlen::Instruction kVandnVv =
        (1U << 26U) | (1U << 25U) | (6U << 20U) | (4U << 15U) | (2U << 7U) | 0x57U;
    constexpr simrv::xlen::Instruction kVclzV = (0x12U << 26U) | (1U << 25U) | (6U << 20U) |
                                                (12U << 15U) | (2U << 12U) | (2U << 7U) | 0x57U;
    TEST_CHECK(simrv::pipeline::decoder(kVandnVv) == OperationId::VANDN_VV);
    TEST_CHECK(simrv::pipeline::decoder(kVclzV) == OperationId::VCLZ_V);

    const auto run_vector = [&](simrv::xlen::Instruction instruction, uint8_t pattern,
                                simrv::pipeline::PipelineType type) {
        simrv::core::Machine machine;
        constexpr size_t ram_size = 1024 * 1024;
        std::vector<simrv::xlen::Byte> ram(ram_size, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        auto& cpu = machine.primary_hart();
        cpu.machine_ = &machine;
        cpu.reset();
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        cpu.pipeline_sim.config.pipeline_type = type;
        cpu.state().mstatus |= simrv::core::MstatusBit::Vs;
        cpu.state().vl = 1;
        cpu.state().vtype = 0;
        auto& vector = cpu.state().regs.read_vector(
            simrv::xlen::to_vec_reg_id(static_cast<simrv::xlen::RegId>(6)));
        vector.u8[0] = pattern;
        vector.u8[1] = static_cast<uint8_t>(~pattern);

        const auto pc = machine.memory_geometry().dram_base;
        const std::array<simrv::xlen::Instruction, 2> program = {instruction, 0x0000006F};
        std::memcpy(ram.data(), program.data(), sizeof(program));
        std::array<simrv::xlen::Byte, simrv::cache::ICache::kLineBytes> line{};
        std::memcpy(line.data(), program.data(), sizeof(program));
        cpu.icache.insert(simrv::xlen::PhysAddr{pc}, line.data(),
                          simrv::memory::MesiState::Exclusive);
        cpu.state().pc = pc;

        uint32_t cycles = 0;
        while (cpu.e_icount < 1 && cycles < 64) {
            cpu.run_cycle(machine);
            machine.memory().system_bus().advance_cycle();
            ++cycles;
        }
        TEST_CHECK(cpu.e_icount == 1);
        return cycles;
    };

    for (const auto type :
         {simrv::pipeline::PipelineType::ThreeStage, simrv::pipeline::PipelineType::FiveStage}) {
        TEST_CHECK(run_vector(kVandnVv, 0, type) == run_vector(kVandnVv, 0xA5, type));
        TEST_CHECK(run_vector(kVclzV, 0, type) == run_vector(kVclzV, 0xA5, type));
    }
}

void test_cycle_kernel_fp_hazard_stall_counters() {
    // Verifies FP conservative hazard stalls in cycle simulation:
    // 1: addi     x1, x0, 2
    // 2: fcvt.s.w f1, x1        (reads x1 int, produces f1)
    // 3: fadd.s   f2, f1, f1    (RAW on f1 - stalls until fcvt.s.w retires)
    // 4: fadd.s   f3, f1, f2    (RAW on f2 and f1 - stalls until fadd.s retires)
    // 5: jal      x0, 0
    constexpr std::array<simrv::xlen::Instruction, 5> program = {
        0x00200093,  // addi    x1, x0, 2
        0xd20080d3,  // fcvt.s.w f1, x1
        0x00108153,  // fadd.s  f2, f1, f1
        0x002081d3,  // fadd.s  f3, f1, f2
        0x0000006f,  // jal     x0, 0
    };

    const auto run_model = [&](simrv::pipeline::PipelineType type, bool forwarding) {
        simrv::core::Machine machine;
        const simrv::xlen::Address pc = machine.memory_geometry().dram_base;
        std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
        machine.set_ram_for_testing(ram.data(), ram.size());
        auto& cpu = machine.primary_hart();
        cpu.machine_ = &machine;
        cpu.reset();
        cpu.state().mstatus |= (3ULL << 13);  // FS = Dirty
        machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
        cpu.pipeline_sim.config.pipeline_type = type;
        cpu.pipeline_sim.config.enable_forwarding = forwarding;

        std::array<simrv::xlen::Byte, simrv::cache::ICache::kLineBytes> line{};
        std::memcpy(line.data(), program.data(), sizeof(program));
        std::memcpy(ram.data(), program.data(), sizeof(program));
        cpu.icache.insert(simrv::xlen::PhysAddr{pc}, line.data(),
                          simrv::memory::MesiState::Exclusive);
        cpu.state().pc = pc;

        uint32_t cycles = 0;
        while (cpu.e_icount < 4 && cycles < 64) {
            cpu.run_cycle(machine);
            machine.memory().system_bus().advance_cycle();
            ++cycles;
        }
        TEST_CHECK(cpu.e_icount == 4);
        return std::make_pair(cycles, cpu.pipeline_sim.data_hazard_stalls());
    };

    // 5-stage: FP RAW dependencies stall until Writeback retirement
    const auto [cycles_5s, stalls_5s] = run_model(simrv::pipeline::PipelineType::FiveStage, true);
    TEST_CHECK(cycles_5s > 0);
    TEST_CHECK(stalls_5s > 0);

    // 3-stage:
    const auto [cycles_3s, stalls_3s] = run_model(simrv::pipeline::PipelineType::ThreeStage, true);
    TEST_CHECK(cycles_3s > 0);
}

}  // namespace

int run() {
    std::cout << "=== Running Pipeline Hazard Tests ===\n";
    test_integer_forwarding_hazards();
    test_load_use_hazard();
    test_fp_conservative_stall();
    test_fma_rs3_hazard();
    test_cross_bank_conversion_hazards();
    test_pipeline_sim_hazard_events();
    test_cycle_kernel_bit_for_bit_hazard_stall_counters();
    test_zkt_fixed_instruction_latency();
    test_zvkt_vector_latency();
    test_cycle_kernel_fp_hazard_stall_counters();
    std::cout << "=== All Pipeline Hazard Tests Passed Successfully ===\n";
    return 0;
}

}  // namespace section_3
#undef TEST_CHECK

namespace section_4 {
/**
 * @file PipelineModelTests.cpp
 * @brief Unit tests for the supported RISC-V 3-stage and 5-stage pipeline models.
 */

#define TEST_CHECK(expr)                                                                     \
    do {                                                                                     \
        if (!(expr)) {                                                                       \
            std::cerr << "Assertion failed: " #expr " at " __FILE__ ":" << __LINE__ << "\n"; \
            std::abort();                                                                    \
        }                                                                                    \
    } while (0)

namespace {

using simrv::isa::Opcode;
using simrv::isa::OperationId;
using simrv::pipeline::CpuConfig;
using simrv::pipeline::PipelineStage;
using simrv::pipeline::PipelineType;

void test_operation_traits() {
    using namespace simrv::pipeline::operation;
    TEST_CHECK(is_multiply(OperationId::MULH));
    TEST_CHECK(is_divide_or_remainder(OperationId::REMW));
    TEST_CHECK(is_fp_divide_or_sqrt(OperationId::FSQRT_D));
    TEST_CHECK(is_fp_alu(OperationId::FCVT_D_LU));
    TEST_CHECK(!is_fp_alu(OperationId::FDIV_D));
    TEST_CHECK(info(OperationId::MUL).execution_class == ExecutionClass::Multiply);
    TEST_CHECK(info(OperationId::ADD).execution_class == ExecutionClass::Default);
    TEST_CHECK(simrv::pipeline::operation_name(OperationId::VADD_VV) == "VADD_VV");
    TEST_CHECK(simrv::pipeline::operation_name(static_cast<OperationId>(0xffff)) == "UNKNOWN");
    TEST_CHECK(is_load(Opcode::LoadFp));
    TEST_CHECK(is_store(Opcode::StoreFp));
    TEST_CHECK(reads_rs1(Opcode::Jalr));
    TEST_CHECK(!reads_rs2(Opcode::OpImm));
}

void test_writeback_effects() {
    simrv::pipeline::PipelineContext context{};
    context.opcode = Opcode::Load;
    context.rd = simrv::xlen::RegId::A0;
    context.mem_rdata = 0x1234;
    auto effects = simrv::pipeline::build_writeback_effects(context);
    TEST_CHECK(effects.increments_instruction_count);
    TEST_CHECK(effects.integer_write.enabled);
    TEST_CHECK(effects.integer_write.destination == simrv::xlen::RegId::A0);
    TEST_CHECK(effects.integer_write.value == 0x1234);

    context.pending_exception = ExceptionCode::FaultLoad;
    effects = simrv::pipeline::build_writeback_effects(context);
    TEST_CHECK(!effects.increments_instruction_count);
    TEST_CHECK(!effects.integer_write.enabled);

    context = {};
    context.opcode = Opcode::LoadFp;
    context.rd = static_cast<simrv::xlen::RegId>(simrv::xlen::FpRegId::Fa0);
    context.fp_mem_rdata = 0x3ff0000000000000ULL;
    effects = simrv::pipeline::build_writeback_effects(context);
    TEST_CHECK(effects.floating_write.enabled);
    TEST_CHECK(effects.marks_floating_point_dirty);
}

void test_pipeline_factory_and_names() {
    std::cout << "[Test] PipelineFactory parser and naming...\n";

    TEST_CHECK(simrv::pipeline::parse_pipeline_type("3stage") == PipelineType::ThreeStage);
    TEST_CHECK(simrv::pipeline::parse_pipeline_type("5stage") == PipelineType::FiveStage);
    TEST_CHECK(!simrv::pipeline::parse_pipeline_type("embedded").has_value());
    TEST_CHECK(!simrv::pipeline::parse_pipeline_type("dual").has_value());

    TEST_CHECK(simrv::pipeline::pipeline_type_name(PipelineType::ThreeStage) == "3-Stage In-Order");
}

void test_cpu_model_presets_and_validation() {
    using simrv::pipeline::CpuModelPreset;
    using simrv::pipeline::PipelineStage;
    using simrv::pipeline::PipelineType;
    const auto tiny = simrv::pipeline::make_cpu_model_preset(CpuModelPreset::Tiny);
    TEST_CHECK(tiny.validate().has_value());
    TEST_CHECK(tiny.pipeline.pipeline_type == PipelineType::ThreeStage);
    TEST_CHECK(!tiny.pipeline.enable_forwarding);
    TEST_CHECK(!tiny.pipeline.enable_instruction_prefetch);
    TEST_CHECK(tiny.instruction_cache.capacity_bytes == 2048);
    TEST_CHECK(tiny.instruction_cache.associativity == 1);

    const auto balanced = simrv::pipeline::make_cpu_model_preset(CpuModelPreset::Balanced);
    TEST_CHECK(balanced.validate().has_value());
    TEST_CHECK(balanced.pipeline.pipeline_type == PipelineType::FiveStage);
    TEST_CHECK(!balanced.pipeline.enable_instruction_prefetch);
    TEST_CHECK(balanced.instruction_cache.capacity_bytes == 4096);
    TEST_CHECK(balanced.instruction_cache.associativity == 2);

    const auto performance = simrv::pipeline::make_cpu_model_preset(CpuModelPreset::Performance);
    TEST_CHECK(performance.validate().has_value());
    TEST_CHECK(performance.pipeline.enable_instruction_prefetch);
    TEST_CHECK(performance.instruction_cache.capacity_bytes == 16384);
    TEST_CHECK(performance.instruction_cache.associativity == 4);

    auto invalid = balanced;
    invalid.data_cache.line_bytes = 16;
    TEST_CHECK(!invalid.validate().has_value());
    invalid = balanced;
    invalid.instruction_cache.capacity_bytes = 3072;
    TEST_CHECK(!invalid.validate().has_value());
    TEST_CHECK(simrv::pipeline::parse_cpu_model_preset("performance") ==
               CpuModelPreset::Performance);
    TEST_CHECK(!simrv::pipeline::parse_cpu_model_preset("superscalar").has_value());
}

void test_runtime_cache_geometry() {
    simrv::cache::BaseCache<256, 64, 8> cache;
    TEST_CHECK(cache.configure(2048, 1));
    TEST_CHECK(cache.capacity_bytes() == 2048);
    TEST_CHECK(cache.associativity() == 1);
    TEST_CHECK(cache.set_count() == 32);
    TEST_CHECK(cache.configure(16384, 4));
    TEST_CHECK(cache.capacity_bytes() == 16384);
    TEST_CHECK(cache.associativity() == 4);
    TEST_CHECK(cache.set_count() == 64);
    TEST_CHECK(!cache.configure(4096, 3));
    TEST_CHECK(!cache.configure(32768, 4));
}

void test_runtime_profile_policy() {
    using simrv::core::ExecutionEngine;
    using simrv::core::InteractionMode;
    using simrv::core::RuntimeProfile;

    TEST_CHECK(!simrv::core::is_cycle_engine(ExecutionEngine::InstructionFast));
    TEST_CHECK(simrv::core::is_cycle_engine(ExecutionEngine::CycleFast));
    TEST_CHECK(simrv::core::is_observable_engine(ExecutionEngine::CycleObservable));
    TEST_CHECK(simrv::core::is_fast_engine(ExecutionEngine::InstructionFast));

    RuntimeProfile profile{};
    TEST_CHECK(profile.engine == ExecutionEngine::InstructionFast);
    TEST_CHECK(profile.allows_fast_batch());
    TEST_CHECK(profile.execution_name() == "instruction-fast");

    profile.engine = ExecutionEngine::CycleFast;
    TEST_CHECK(!profile.allows_fast_batch());
    TEST_CHECK(profile.execution_name() == "cycle-fast");
    TEST_CHECK(!profile.records_cycle_history());

    profile.gdb = true;
    TEST_CHECK(profile.records_cycle_history());

    profile.engine = ExecutionEngine::CycleObservable;
    TEST_CHECK(profile.records_cycle_history());

    profile.engine = ExecutionEngine::InstructionObservable;
    profile.interaction = InteractionMode::Tui;
    TEST_CHECK(profile.allows_fast_batch());
    TEST_CHECK(profile.fast_batch_quantum() == 2048);
    profile.engine = ExecutionEngine::InstructionFast;
    profile.interaction = InteractionMode::Cli;
    TEST_CHECK(profile.fast_batch_quantum() == 65536);
    profile.tracing = true;
    TEST_CHECK(!profile.allows_fast_batch());

    TEST_CHECK(simrv::core::select_execution_engine(false, InteractionMode::Cli) ==
               ExecutionEngine::InstructionFast);
    TEST_CHECK(simrv::core::select_execution_engine(false, InteractionMode::Tui) ==
               ExecutionEngine::InstructionObservable);
    TEST_CHECK(simrv::core::select_execution_engine(true, InteractionMode::Cli) ==
               ExecutionEngine::CycleFast);
    TEST_CHECK(simrv::core::select_execution_engine(true, InteractionMode::Tui) ==
               ExecutionEngine::CycleObservable);
}

void test_pipeline_sim_wrapper() {
    std::cout << "[Test] PipelineSim full-cycle observer...\n";
    simrv::pipeline::PipelineSim sim;
    sim.config.record_snapshots = true;
    sim.reset();
    sim.advance_cycle({
        .fetch = {.instruction = {.pc = 0x1010, .op_id = OperationId::SUB}, .valid = true},
        .decode = {.instruction = {.pc = 0x100c, .op_id = OperationId::ADD}, .valid = true},
        .execute = {.instruction = {.pc = 0x1008, .op_id = OperationId::MUL},
                    .valid = true,
                    .stalled = true},
        .memory = {.instruction = {.pc = 0x1004, .op_id = OperationId::LW}, .valid = true},
        .writeback = {.instruction = {.pc = 0x1000, .op_id = OperationId::ADDI}, .valid = true},
        .retired = true,
        .data_hazard_stall = true,
    });
    TEST_CHECK(sim.stage_reg(PipelineStage::Fetch).pc == 0x1010);
    TEST_CHECK(sim.stage_reg(PipelineStage::Decode).pc == 0x100c);
    TEST_CHECK(sim.stage_reg(PipelineStage::Execute).pc == 0x1008);
    TEST_CHECK(sim.stage_reg(PipelineStage::Memory).pc == 0x1004);
    TEST_CHECK(sim.stage_reg(PipelineStage::Writeback).pc == 0x1000);
    TEST_CHECK(sim.cycle_count() == 1);
    TEST_CHECK(sim.stall_cycles() == 1);
    TEST_CHECK(sim.data_hazard_stalls() == 1);
    const auto history = sim.cycle_history();
    TEST_CHECK(history.size() == 1);
    TEST_CHECK(history.at(0).f.pc == 0x1010);
    TEST_CHECK(history.at(0).w.pc == 0x1000);

    auto saved = sim.save_state();
    sim.reset();
    sim.restore_state(saved);
    TEST_CHECK(sim.stage_reg(PipelineStage::Execute).pc == 0x1008);
    TEST_CHECK(sim.cycle_history().size() == 1);
    TEST_CHECK(sim.cycle_history().at(0).f.pc == simrv::xlen::VirtAddr{0x1010});

    simrv::pipeline::PipelineSim fast;
    fast.config.record_snapshots = false;
    simrv::pipeline::PipelineSim observable;
    observable.config.record_snapshots = true;
    const simrv::pipeline::PipelineCycleEvent transition{
        .fetch = {.instruction = {.pc = 0x2004, .op_id = OperationId::ADD}, .valid = true},
        .writeback = {.instruction = {.pc = 0x2000, .op_id = OperationId::SUB}, .valid = true},
        .retired = true,
        .control_flush = true,
    };
    fast.advance_cycle_fast({.control_flush = true});
    observable.advance_cycle(transition);
    TEST_CHECK(fast.get_stats().cycle_count == observable.get_stats().cycle_count);
    TEST_CHECK(fast.get_stats().bubble_cycles == observable.get_stats().bubble_cycles);
    TEST_CHECK(!fast.stage_reg(PipelineStage::Fetch).valid);
    TEST_CHECK(observable.stage_reg(PipelineStage::Fetch).pc == 0x2004);
    TEST_CHECK(fast.cycle_history().empty());
    TEST_CHECK(observable.cycle_history().size() == 1);

    std::cout << "  PipelineSim wrapper tests passed.\n";
}

}  // namespace

int run() {
    std::cout << "=== Running Pipeline Model Tests ===\n";
    test_operation_traits();
    test_writeback_effects();
    test_pipeline_factory_and_names();
    test_cpu_model_presets_and_validation();
    test_runtime_cache_geometry();
    test_runtime_profile_policy();
    test_pipeline_sim_wrapper();
    std::cout << "=== All Pipeline Model Tests Passed Successfully ===\n";
    return 0;
}

}  // namespace section_4
#undef TEST_CHECK

int main() {
    if (section_0::run() != 0) return 1;
    if (section_1::run() != 0) return 1;
    if (section_2::run() != 0) return 1;
    if (section_3::run() != 0) return 1;
    if (section_4::run() != 0) return 1;
    return 0;
}
