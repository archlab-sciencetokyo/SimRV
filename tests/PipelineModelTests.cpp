/**
 * @file PipelineModelTests.cpp
 * @brief Unit tests for the supported RISC-V 3-stage and 5-stage pipeline models.
 */
#include <cstdlib>
#include <iostream>

#include "simrv/core/RuntimeProfile.hpp"
#include "simrv/isa/Base.hpp"
#include "simrv/pipeline/OperationTraits.hpp"
#include "simrv/pipeline/PipelineConfig.hpp"
#include "simrv/pipeline/PipelineSim.hpp"
#include "simrv/pipeline/RetirementEffects.hpp"
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
using simrv::pipeline::PipelineType;

void test_operation_traits() {
    using namespace simrv::pipeline::operation;
    TEST_CHECK(is_multiply(OperationId::MULH));
    TEST_CHECK(is_divide_or_remainder(OperationId::REMW));
    TEST_CHECK(is_fp_divide_or_sqrt(OperationId::FSQRT_D));
    TEST_CHECK(is_fp_alu(OperationId::FCVT_D_LU));
    TEST_CHECK(!is_fp_alu(OperationId::FDIV_D));
    TEST_CHECK(is_load(Opcode::LoadFp));
    TEST_CHECK(is_store(Opcode::StoreFp));
    TEST_CHECK(reads_rs1(Opcode::Jalr));
    TEST_CHECK(!reads_rs2(Opcode::OpImm));
}

void test_writeback_effects() {
    simrv::pipeline::PipelineContext context{};
    context.opcode = Opcode::Load;
    context.rd = RegId::A0;
    context.mem_rdata = 0x1234;
    auto effects = simrv::pipeline::build_writeback_effects(context);
    TEST_CHECK(effects.increments_instruction_count);
    TEST_CHECK(effects.integer_write.enabled);
    TEST_CHECK(effects.integer_write.destination == RegId::A0);
    TEST_CHECK(effects.integer_write.value == 0x1234);

    context.pending_exception = ExceptionCode::FaultLoad;
    effects = simrv::pipeline::build_writeback_effects(context);
    TEST_CHECK(!effects.increments_instruction_count);
    TEST_CHECK(!effects.integer_write.enabled);

    context = {};
    context.opcode = Opcode::LoadFp;
    context.rd = static_cast<RegId>(FpRegId::Fa0);
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

void test_runtime_profile_policy() {
    using simrv::core::ExecutionEngine;
    using simrv::core::InteractionMode;
    using simrv::core::RuntimeProfile;

    RuntimeProfile profile{};
    TEST_CHECK(profile.engine == ExecutionEngine::InstructionFast);
    TEST_CHECK(profile.allows_fast_batch());
    TEST_CHECK(profile.execution_name() == "instruction-fast");

    profile.engine = ExecutionEngine::CycleFast;
    TEST_CHECK(!profile.allows_fast_batch());
    TEST_CHECK(profile.execution_name() == "cycle-fast");
    TEST_CHECK(!profile.records_cycle_history());

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
    TEST_CHECK(sim.f_reg().pc == 0x1010);
    TEST_CHECK(sim.d_reg().pc == 0x100c);
    TEST_CHECK(sim.e_reg().pc == 0x1008);
    TEST_CHECK(sim.m_reg().pc == 0x1004);
    TEST_CHECK(sim.w_reg().pc == 0x1000);
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
    TEST_CHECK(sim.e_reg().pc == 0x1008);
    TEST_CHECK(sim.cycle_history().size() == 1);

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
    TEST_CHECK(!fast.f_reg().valid);
    TEST_CHECK(observable.f_reg().pc == 0x2004);
    TEST_CHECK(fast.cycle_history().empty());
    TEST_CHECK(observable.cycle_history().size() == 1);

    std::cout << "  PipelineSim wrapper tests passed.\n";
}

}  // namespace

auto main() -> int {
    std::cout << "=== Running Pipeline Model Tests ===\n";
    test_operation_traits();
    test_writeback_effects();
    test_pipeline_factory_and_names();
    test_runtime_profile_policy();
    test_pipeline_sim_wrapper();
    std::cout << "=== All Pipeline Model Tests Passed Successfully ===\n";
    return 0;
}
