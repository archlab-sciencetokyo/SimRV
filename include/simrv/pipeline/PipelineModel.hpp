#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "simrv/Define.hpp"
#include "simrv/isa/Base.hpp"
#include "simrv/pipeline/PipelineSim.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::pipeline {

class PipelineModel {
   public:
    virtual ~PipelineModel() = default;

    virtual void reset() = 0;

    virtual auto step_instruction(Register pc, isa::Opcode opcode, RegId rd, RegId rs1, RegId rs2,
                                  isa::OperationId op_id, bool branched, bool is_branch,
                                  bool is_jump, bool icache_miss, bool dcache_miss, bool tlb_miss,
                                  Register target_pc) -> uint32_t = 0;

    [[nodiscard]] virtual auto get_stats() const -> PipelineStats = 0;
    [[nodiscard]] virtual auto get_cycle_history() const -> std::vector<PipelineCycleSnapshot> = 0;

    // Stage registers for TUI visualization
    [[nodiscard]] virtual auto f_reg() const -> PipelineReg = 0;
    [[nodiscard]] virtual auto d_reg() const -> PipelineReg = 0;
    [[nodiscard]] virtual auto e_reg() const -> PipelineReg = 0;
    [[nodiscard]] virtual auto m_reg() const -> PipelineReg = 0;
    [[nodiscard]] virtual auto w_reg() const -> PipelineReg = 0;

    // Remaining stalls for TUI/hazard checks
    [[nodiscard]] virtual auto div_busy_cycles_remaining() const -> uint32_t = 0;
    [[nodiscard]] virtual auto icache_stall_remaining() const -> uint32_t = 0;
    [[nodiscard]] virtual auto dcache_stall_remaining() const -> uint32_t = 0;
    [[nodiscard]] virtual auto tlb_stall_remaining() const -> uint32_t = 0;
    [[nodiscard]] virtual auto control_bubble_remaining() const -> uint32_t = 0;

    // Predictor queries
    [[nodiscard]] virtual auto get_bht_entry(Register pc) const -> uint8_t = 0;
    [[nodiscard]] virtual auto get_btb_target(Register pc) const -> std::pair<bool, Register> = 0;

    // Pipeline state checkpointing
    [[nodiscard]] virtual auto save_state() const -> PipelineSimState = 0;
    virtual void restore_state(const PipelineSimState& state) = 0;
};

}  // namespace simrv::pipeline
