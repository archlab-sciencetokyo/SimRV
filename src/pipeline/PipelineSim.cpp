#include "simrv/pipeline/PipelineSim.hpp"
#include "simrv/pipeline/PipelineModel.hpp"
#include "simrv/pipeline/InOrderPipeline.hpp"

namespace simrv::pipeline {

PipelineSim::PipelineSim() {
    init_model();
}

PipelineSim::~PipelineSim() = default;

void PipelineSim::init_model() {
    model_ = std::make_unique<InOrderPipeline>(config);
}

void PipelineSim::reset() {
    // If model configuration changed, re-initialize model
    init_model();
    if (model_) {
        model_->reset();
    }
}

auto PipelineSim::step_instruction(Register pc, isa::Opcode opcode, RegId rd, RegId rs1, RegId rs2,
                                  isa::OperationId op_id, bool branched, bool is_branch, bool is_jump,
                                  bool icache_miss, bool dcache_miss, bool tlb_miss, Register target_pc) -> uint32_t {
    if (!model_) {
        init_model();
    }
    return model_->step_instruction(pc, opcode, rd, rs1, rs2, op_id, branched, is_branch, is_jump,
                                   icache_miss, dcache_miss, tlb_miss, target_pc);
}

// Getters for statistics
auto PipelineSim::cycle_count() const -> uint64_t { return model_ ? model_->get_stats().cycle_count : 0; }
auto PipelineSim::stall_cycles() const -> uint64_t { return model_ ? model_->get_stats().stall_cycles : 0; }
auto PipelineSim::bubble_cycles() const -> uint64_t { return model_ ? model_->get_stats().bubble_cycles : 0; }
auto PipelineSim::icache_stalls() const -> uint64_t { return model_ ? model_->get_stats().icache_stalls : 0; }
auto PipelineSim::dcache_stalls() const -> uint64_t { return model_ ? model_->get_stats().dcache_stalls : 0; }
auto PipelineSim::tlb_stalls() const -> uint64_t { return model_ ? model_->get_stats().tlb_stalls : 0; }
auto PipelineSim::structural_stalls() const -> uint64_t { return model_ ? model_->get_stats().structural_stalls : 0; }
auto PipelineSim::data_hazard_stalls() const -> uint64_t { return model_ ? model_->get_stats().data_hazard_stalls : 0; }
auto PipelineSim::control_hazard_bubbles() const -> uint64_t { return model_ ? model_->get_stats().control_hazard_bubbles : 0; }
auto PipelineSim::get_cycle_history_copy() const -> std::vector<PipelineCycleSnapshot> {
    return model_ ? model_->get_cycle_history() : std::vector<PipelineCycleSnapshot>{};
}

// Model state getters for TUI compatibility
auto PipelineSim::f_reg() const -> PipelineReg { return model_ ? model_->f_reg() : PipelineReg{}; }
auto PipelineSim::d_reg() const -> PipelineReg { return model_ ? model_->d_reg() : PipelineReg{}; }
auto PipelineSim::e_reg() const -> PipelineReg { return model_ ? model_->e_reg() : PipelineReg{}; }
auto PipelineSim::m_reg() const -> PipelineReg { return model_ ? model_->m_reg() : PipelineReg{}; }
auto PipelineSim::w_reg() const -> PipelineReg { return model_ ? model_->w_reg() : PipelineReg{}; }

auto PipelineSim::div_busy_cycles_remaining() const -> uint32_t { return model_ ? model_->div_busy_cycles_remaining() : 0; }
auto PipelineSim::icache_stall_remaining() const -> uint32_t { return model_ ? model_->icache_stall_remaining() : 0; }
auto PipelineSim::dcache_stall_remaining() const -> uint32_t { return model_ ? model_->dcache_stall_remaining() : 0; }
auto PipelineSim::tlb_stall_remaining() const -> uint32_t { return model_ ? model_->tlb_stall_remaining() : 0; }
auto PipelineSim::control_bubble_remaining() const -> uint32_t { return model_ ? model_->control_bubble_remaining() : 0; }

} // namespace simrv::pipeline
