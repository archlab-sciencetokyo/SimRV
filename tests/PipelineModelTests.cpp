/**
 * @file PipelineModelTests.cpp
 * @brief Unit tests for RISC-V 3-Stage, 5-Stage Rocket, and Dual-Issue Superscalar pipeline models.
 */
#include <cstdlib>
#include <iostream>
#include <memory>

#include "simrv/isa/Base.hpp"
#include "simrv/pipeline/DualIssuePipeline.hpp"
#include "simrv/pipeline/InOrderPipeline.hpp"
#include "simrv/pipeline/PipelineFactory.hpp"
#include "simrv/pipeline/PipelineModel.hpp"
#include "simrv/pipeline/PipelineSim.hpp"
#include "simrv/pipeline/ThreeStagePipeline.hpp"
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
using simrv::pipeline::CpuConfig;
using simrv::pipeline::DualIssuePipeline;
using simrv::pipeline::InOrderPipeline;
using simrv::pipeline::PipelineModel;
using simrv::pipeline::PipelineType;
using simrv::pipeline::ThreeStagePipeline;

void test_pipeline_factory_and_names() {
    std::cout << "[Test] PipelineFactory parser and naming...\n";

    TEST_CHECK(simrv::pipeline::parse_pipeline_type("3") == PipelineType::ThreeStage);
    TEST_CHECK(simrv::pipeline::parse_pipeline_type("3stage") == PipelineType::ThreeStage);
    TEST_CHECK(simrv::pipeline::parse_pipeline_type("threestage") == PipelineType::ThreeStage);
    TEST_CHECK(simrv::pipeline::parse_pipeline_type("embedded") == PipelineType::ThreeStage);
    TEST_CHECK(simrv::pipeline::parse_pipeline_type("ibex") == PipelineType::ThreeStage);

    TEST_CHECK(simrv::pipeline::parse_pipeline_type("dual") == PipelineType::DualIssue);
    TEST_CHECK(simrv::pipeline::parse_pipeline_type("dualissue") == PipelineType::DualIssue);
    TEST_CHECK(simrv::pipeline::parse_pipeline_type("superscalar") == PipelineType::DualIssue);
    TEST_CHECK(simrv::pipeline::parse_pipeline_type("swerv") == PipelineType::DualIssue);

    TEST_CHECK(simrv::pipeline::parse_pipeline_type("5") == PipelineType::FiveStage);
    TEST_CHECK(simrv::pipeline::parse_pipeline_type("5stage") == PipelineType::FiveStage);
    TEST_CHECK(simrv::pipeline::parse_pipeline_type("rocket") == PipelineType::FiveStage);

    CpuConfig cfg;
    auto pipe3 = simrv::pipeline::create_pipeline(PipelineType::ThreeStage, cfg);
    TEST_CHECK(dynamic_cast<ThreeStagePipeline*>(pipe3.get()) != nullptr);

    auto pipe5 = simrv::pipeline::create_pipeline(PipelineType::FiveStage, cfg);
    TEST_CHECK(dynamic_cast<InOrderPipeline*>(pipe5.get()) != nullptr);

    auto pipe_dual = simrv::pipeline::create_pipeline(PipelineType::DualIssue, cfg);
    TEST_CHECK(dynamic_cast<DualIssuePipeline*>(pipe_dual.get()) != nullptr);

    std::cout << "  Factory creation passed.\n";
}

void test_three_stage_pipeline() {
    std::cout << "[Test] ThreeStagePipeline simulation...\n";
    CpuConfig cfg;
    cfg.record_snapshots = true;
    cfg.enable_forwarding = true;
    ThreeStagePipeline pipe(cfg);

    // Inst 1: addi x1, x0, 10 (pc=0x1000)
    uint32_t c1 =
        pipe.step_instruction(0x1000, Opcode::OpImm, RegId::Ra, RegId::Zero, RegId::Zero,
                              OperationId::ADD, false, false, false, false, false, false, 0);
    TEST_CHECK(c1 == 1);

    // Inst 2: addi x2, x1, 5 (pc=0x1004, dependent on x1 from w_reg_ with forwarding)
    uint32_t c2 =
        pipe.step_instruction(0x1004, Opcode::OpImm, RegId::Sp, RegId::Ra, RegId::Zero,
                              OperationId::ADD, false, false, false, false, false, false, 0);
    TEST_CHECK(c2 == 1);

    // Inst 3: lw x3, 0(x2) (pc=0x1008)
    uint32_t c3 =
        pipe.step_instruction(0x1008, Opcode::Load, RegId::Gp, RegId::Sp, RegId::Zero,
                              OperationId::ADD, false, false, false, false, false, false, 0);
    TEST_CHECK(c3 == 1);

    // Inst 4: addi x4, x3, 1 (pc=0x100C, Load-use hazard with x3 in e_reg_)
    uint32_t c4 =
        pipe.step_instruction(0x100C, Opcode::OpImm, RegId::Tp, RegId::Gp, RegId::Zero,
                              OperationId::ADD, false, false, false, false, false, false, 0);
    TEST_CHECK(c4 == 2);  // 1 stall cycle + 1 exec cycle

    auto stats = pipe.get_stats();
    TEST_CHECK(stats.data_hazard_stalls == 1);

    // Test branch mispredict (1 cycle penalty)
    uint32_t c5 =
        pipe.step_instruction(0x1010, Opcode::Branch, RegId::Zero, RegId::Tp, RegId::Sp,
                              OperationId::BEQ, true, true, false, false, false, false, 0x1020);
    TEST_CHECK(c5 >= 2);  // Branch penalty evaluated

    // Test Save / Restore state
    auto saved = pipe.save_state();
    pipe.reset();
    TEST_CHECK(pipe.get_stats().cycle_count == 0);
    pipe.restore_state(saved);
    TEST_CHECK(pipe.get_stats().cycle_count == saved.stats.cycle_count);

    std::cout << "  ThreeStagePipeline tests passed.\n";
}

void test_dual_issue_pipeline() {
    std::cout << "[Test] DualIssuePipeline simulation...\n";
    CpuConfig cfg;
    cfg.record_snapshots = true;
    cfg.enable_forwarding = true;
    DualIssuePipeline pipe(cfg);

    // Pair 1:
    // Slot 0: addi x1, x0, 10 (pc=0x2000)
    // Slot 1: addi x2, x0, 20 (pc=0x2004) -> Independent, co-issues in 1 cycle
    uint32_t c1 =
        pipe.step_instruction(0x2000, Opcode::OpImm, RegId::Ra, RegId::Zero, RegId::Zero,
                              OperationId::ADD, false, false, false, false, false, false, 0);
    TEST_CHECK(c1 == 1);

    uint32_t c2 =
        pipe.step_instruction(0x2004, Opcode::OpImm, RegId::Sp, RegId::Zero, RegId::Zero,
                              OperationId::ADD, false, false, false, false, false, false, 0);
    TEST_CHECK(c2 == 0);  // 0 additional cycles (co-issued!)

    auto stats1 = pipe.get_stats();
    TEST_CHECK(stats1.dual_issue_cycles == 1);

    // Pair 2 with Inter-Slot RAW Dependency:
    // Slot 0: addi x3, x0, 30 (pc=0x2008)
    // Slot 1: addi x4, x3, 40 (pc=0x200C) -> Reads x3 produced by Slot 0 -> cannot dual issue!
    uint32_t c3 =
        pipe.step_instruction(0x2008, Opcode::OpImm, RegId::Gp, RegId::Zero, RegId::Zero,
                              OperationId::ADD, false, false, false, false, false, false, 0);
    TEST_CHECK(c3 == 1);

    uint32_t c4 =
        pipe.step_instruction(0x200C, Opcode::OpImm, RegId::Tp, RegId::Gp, RegId::Zero,
                              OperationId::ADD, false, false, false, false, false, false, 0);
    TEST_CHECK(c4 >= 1);  // Fallback to serialized issue

    auto stats2 = pipe.get_stats();
    TEST_CHECK(stats2.single_issue_cycles >= 1);

    // Structural constraint: Load instruction in Slot 1 cannot co-issue
    // Slot 0: addi x5, x0, 50 (pc=0x2010)
    // Slot 1: lw x6, 0(x1) (pc=0x2014) -> Load not allowed in Slot 1
    pipe.step_instruction(0x2010, Opcode::OpImm, RegId::T0, RegId::Zero, RegId::Zero,
                          OperationId::ADD, false, false, false, false, false, false, 0);
    uint32_t c6 =
        pipe.step_instruction(0x2014, Opcode::Load, RegId::T1, RegId::Ra, RegId::Zero,
                              OperationId::ADD, false, false, false, false, false, false, 0);
    TEST_CHECK(c6 >= 1);

    // Test Save / Restore state
    auto saved = pipe.save_state();
    pipe.reset();
    TEST_CHECK(pipe.get_stats().cycle_count == 0);
    pipe.restore_state(saved);
    TEST_CHECK(pipe.get_stats().cycle_count == saved.stats.cycle_count);
    TEST_CHECK(pipe.get_stats().dual_issue_cycles == saved.stats.dual_issue_cycles);

    std::cout << "  DualIssuePipeline tests passed.\n";
}

void test_pipeline_sim_wrapper() {
    std::cout << "[Test] PipelineSim wrapper with pipeline switching...\n";
    simrv::pipeline::PipelineSim sim;

    // Default Rocket 5-stage
    sim.reset();
    TEST_CHECK(sim.config.pipeline_type == PipelineType::FiveStage);

    // Switch to ThreeStage
    sim.config.pipeline_type = PipelineType::ThreeStage;
    sim.reset();
    TEST_CHECK(sim.get_model() != nullptr);

    uint32_t c1 =
        sim.step_instruction(0x3000, Opcode::OpImm, RegId::Ra, RegId::Zero, RegId::Zero,
                             OperationId::ADD, false, false, false, false, false, false, 0);
    TEST_CHECK(c1 == 1);

    // Switch to DualIssue
    sim.config.pipeline_type = PipelineType::DualIssue;
    sim.reset();
    uint32_t d1 =
        sim.step_instruction(0x4000, Opcode::OpImm, RegId::Ra, RegId::Zero, RegId::Zero,
                             OperationId::ADD, false, false, false, false, false, false, 0);
    uint32_t d2 =
        sim.step_instruction(0x4004, Opcode::OpImm, RegId::Sp, RegId::Zero, RegId::Zero,
                             OperationId::ADD, false, false, false, false, false, false, 0);
    TEST_CHECK(d1 == 1);
    TEST_CHECK(d2 == 0);

    std::cout << "  PipelineSim wrapper tests passed.\n";
}

}  // namespace

auto main() -> int {
    std::cout << "=== Running Pipeline Model Tests ===\n";
    test_pipeline_factory_and_names();
    test_three_stage_pipeline();
    test_dual_issue_pipeline();
    test_pipeline_sim_wrapper();
    std::cout << "=== All Pipeline Model Tests Passed Successfully ===\n";
    return 0;
}
