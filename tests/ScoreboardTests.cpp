/**
 * @file ScoreboardTests.cpp
 * @brief Unit tests for register scoreboard tracking, reservations, flushes, and hazard detection.
 */
#include <cstdlib>
#include <iostream>

#include "simrv/isa/OperationId.hpp"
#include "simrv/pipeline/DecodedInstruction.hpp"
#include "simrv/pipeline/Decoder.hpp"
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

using simrv::isa::OperationId;
using simrv::pipeline::DecodedInstruction;
using simrv::pipeline::PipelineStage;
using simrv::pipeline::Scoreboard;
using namespace simrv::pipeline::operation;

void test_initial_state() {
    Scoreboard sb{};
    for (size_t i = 0; i < Scoreboard::kNumIntRegisters; ++i) {
        const auto reg = static_cast<RegId>(i);
        TEST_CHECK(!sb.is_busy(RegBank::Integer, reg));
        TEST_CHECK(!sb.can_forward(RegBank::Integer, reg));
        TEST_CHECK(sb.get_forwarded_value(RegBank::Integer, reg) == 0);
        TEST_CHECK(!sb.get_stage(RegBank::Integer, reg).has_value());
    }
    for (size_t i = 0; i < Scoreboard::kNumFpRegisters; ++i) {
        const auto reg = static_cast<RegId>(i);
        TEST_CHECK(!sb.is_busy(RegBank::Float, reg));
        TEST_CHECK(!sb.can_forward(RegBank::Float, reg));
    }
}

void test_integer_reservation_and_forwarding() {
    Scoreboard sb{};
    // Reserve x5 (T0) at Execute stage with forwarded value 42
    sb.reserve(RegBank::Integer, RegId::T0, PipelineStage::Execute, 1, true, 42);
    TEST_CHECK(sb.is_busy(RegBank::Integer, RegId::T0));
    TEST_CHECK(sb.can_forward(RegBank::Integer, RegId::T0));
    TEST_CHECK(sb.get_forwarded_value(RegBank::Integer, RegId::T0) == 42);
    TEST_CHECK(sb.get_stage(RegBank::Integer, RegId::T0) == PipelineStage::Execute);
    TEST_CHECK(sb.get_latency(RegBank::Integer, RegId::T0) == 1);

    // x0 (Zero) must never be reserved
    sb.reserve(RegBank::Integer, RegId::Zero, PipelineStage::Execute, 0, true, 100);
    TEST_CHECK(!sb.is_busy(RegBank::Integer, RegId::Zero));

    // Release x5
    sb.release(RegBank::Integer, RegId::T0);
    TEST_CHECK(!sb.is_busy(RegBank::Integer, RegId::T0));
    TEST_CHECK(!sb.can_forward(RegBank::Integer, RegId::T0));
}

void test_float_reservation_and_conservative_policy() {
    Scoreboard sb{};
    const auto fa0 = static_cast<RegId>(simrv::xlen::FpRegId::Fa0);

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
    const auto fa3 = static_cast<RegId>(simrv::xlen::FpRegId::Fa3);

    // Reserve fa3 in scoreboard
    sb.reserve(RegBank::Float, fa3, PipelineStage::Memory, 1, false);
    TEST_CHECK(sb.is_busy(RegBank::Float, fa3));

    // Consumer instruction with rs3 = fa3 (13)
    // fmadd.s f0, f1, f2, fa3 (rs3 is encoded in bits 31:27 = 13 = 0x0D)
    // fmadd.s rd=0, rs1=1, rs2=2, funct3=7, rs3=13:
    // (13 << 27) | (0 << 25) | (2 << 20) | (1 << 15) | (7 << 12) | (0 << 7) | 0x43
    constexpr Instruction fmadd_inst =
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
    sb.reserve(RegBank::Integer, RegId::T0, PipelineStage::Decode, 1, false);
    sb.reserve(RegBank::Integer, RegId::T1, PipelineStage::Execute, 1, true, 10);
    sb.reserve(RegBank::Integer, RegId::T2, PipelineStage::Writeback, 0, true, 20);

    // Flush Decode and younger
    sb.flush_from_stage(PipelineStage::Decode);
    TEST_CHECK(!sb.is_busy(RegBank::Integer, RegId::T0));
    TEST_CHECK(sb.is_busy(RegBank::Integer, RegId::T1));
    TEST_CHECK(sb.is_busy(RegBank::Integer, RegId::T2));

    // Reset clears everything
    sb.reset();
    TEST_CHECK(!sb.is_busy(RegBank::Integer, RegId::T1));
    TEST_CHECK(!sb.is_busy(RegBank::Integer, RegId::T2));
}

}  // namespace

int main() {
    std::cout << "=== Running Scoreboard Unit Tests ===\n";
    test_initial_state();
    test_integer_reservation_and_forwarding();
    test_float_reservation_and_conservative_policy();
    test_fma_rs3_hazard_detection();
    test_stage_flush();
    std::cout << "=== All Scoreboard Unit Tests Passed Successfully ===\n";
    return 0;
}
