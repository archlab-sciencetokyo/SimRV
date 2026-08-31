/**
 * @file OperationTraitsTests.cpp
 * @brief Unit tests for decoded instruction metadata traits and scoreboard tracking.
 */
#include <cstdlib>
#include <iostream>

#include "simrv/isa/Common.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/pipeline/OperationInfo.hpp"
#include "simrv/pipeline/OperationTraits.hpp"
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
    TEST_CHECK(!sb.is_busy(RegBank::Integer, RegId::A0));
    const auto fa0 = static_cast<RegId>(simrv::xlen::FpRegId::Fa0);
    TEST_CHECK(!sb.is_busy(RegBank::Float, fa0));

    // Zero register x0 should never be busy
    sb.reserve(RegBank::Integer, RegId::Zero, PipelineStage::Execute, 2, false);
    TEST_CHECK(!sb.is_busy(RegBank::Integer, RegId::Zero));

    // Integer register reservation
    sb.reserve(RegBank::Integer, RegId::A0, PipelineStage::Execute, 1, true);
    TEST_CHECK(sb.is_busy(RegBank::Integer, RegId::A0));
    TEST_CHECK(sb.can_forward(RegBank::Integer, RegId::A0));
    TEST_CHECK(sb.get_stage(RegBank::Integer, RegId::A0) == PipelineStage::Execute);

    // Float register reservation
    sb.reserve(RegBank::Float, fa0, PipelineStage::Memory, 2, false);
    TEST_CHECK(sb.is_busy(RegBank::Float, fa0));
    TEST_CHECK(!sb.can_forward(RegBank::Float, fa0));
    TEST_CHECK(sb.get_stage(RegBank::Float, fa0) == PipelineStage::Memory);

    // Independent banks
    TEST_CHECK(sb.is_busy(RegBank::Integer, RegId::A0));
    sb.release(RegBank::Integer, RegId::A0);
    TEST_CHECK(!sb.is_busy(RegBank::Integer, RegId::A0));
    TEST_CHECK(sb.is_busy(RegBank::Float, fa0));

    // Flush from stage
    sb.reserve(RegBank::Integer, RegId::A1, PipelineStage::Execute);
    sb.reserve(RegBank::Integer, RegId::A2, PipelineStage::Memory);
    sb.flush_from_stage(PipelineStage::Execute);
    TEST_CHECK(!sb.is_busy(RegBank::Integer, RegId::A1));
    TEST_CHECK(sb.is_busy(RegBank::Integer, RegId::A2));

    // Reset
    sb.reset();
    TEST_CHECK(!sb.is_busy(RegBank::Integer, RegId::A2));
    TEST_CHECK(!sb.is_busy(RegBank::Float, fa0));
}

}  // namespace

int main() {
    test_base_integer_traits();
    test_floating_point_traits();
    test_common_isa_helpers();
    test_scoreboard();
    std::cout << "OperationTraitsTests passed successfully.\n";
    return 0;
}
