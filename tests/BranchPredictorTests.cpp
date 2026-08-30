/**
 * @file BranchPredictorTests.cpp
 * @brief Unit tests for SimRV cycle-accurate branch prediction subsystem.
 */
#include <cstdlib>
#include <iostream>

#include "simrv/isa/Base.hpp"
#include "simrv/pipeline/BranchPredictor.hpp"
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
using simrv::pipeline::BranchFeedback;
using simrv::pipeline::BranchPrediction;
using simrv::pipeline::BranchPredictor;
using simrv::pipeline::BranchPredictorConfig;
using simrv::pipeline::BranchPredictorType;
using simrv::pipeline::DecodedInstruction;

void test_type_parsing() {
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

    const Address pc = 0x2000;

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

    const Address pc = 0x3000;

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
    call1.rd = RegId::Ra;
    call1.imm = 0x100;

    auto pred_call1 = bp.predict(0x80000000, call1);
    TEST_CHECK(pred_call1.is_call);
    TEST_CHECK(pred_call1.predicted_target == 0x80000100);
    TEST_CHECK(bp.ras_depth() == 1);
    TEST_CHECK(bp.ras_peek() == 0x80000004);

    // Nested call: JAL RA, 0x50
    DecodedInstruction call2{};
    call2.opcode = Opcode::Jal;
    call2.rd = RegId::Ra;
    call2.imm = 0x50;

    auto pred_call2 = bp.predict(0x80000120, call2);
    TEST_CHECK(pred_call2.is_call);
    TEST_CHECK(bp.ras_depth() == 2);
    TEST_CHECK(bp.ras_peek() == 0x80000124);

    // Return from nested call: JALR x0, RA, 0
    DecodedInstruction ret1{};
    ret1.opcode = Opcode::Jalr;
    ret1.rd = RegId::Zero;
    ret1.rs1 = RegId::Ra;
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
    jalr.rd = RegId::Zero;
    jalr.rs1 = RegId::A0;
    jalr.imm = 0;

    const Address pc = 0x4000;

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

}  // namespace

int main() {
    test_type_parsing();
    test_static_predictor();
    test_bimodal_transitions();
    test_gshare_and_history_restoration();
    test_ras_call_and_return();
    test_btb_indirect_jump_caching();
    test_telemetry_stats();
    std::cout << "All BranchPredictor tests passed successfully!\n";
    return 0;
}
