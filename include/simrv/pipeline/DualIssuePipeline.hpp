/**
 * @file DualIssuePipeline.hpp
 * @brief SweRV EH1-style dual-issue in-order superscalar pipeline model.
 */
#pragma once

#include <array>
#include <vector>

#include "simrv/pipeline/PipelineModel.hpp"
#include "simrv/pipeline/PipelineSim.hpp"

namespace simrv::pipeline {

/**
 * @class DualIssuePipeline
 * @brief Cycle-accurate simulation model for SweRV EH1-class dual-issue superscalar RISC-V cores.
 */
class DualIssuePipeline : public PipelineModel {
   public:
    explicit DualIssuePipeline(const CpuConfig& cfg);
    ~DualIssuePipeline() override = default;

    void reset() override;

    auto step_instruction(Register pc, isa::Opcode opcode, RegId rd, RegId rs1, RegId rs2,
                          isa::OperationId op_id, bool branched, bool is_branch, bool is_jump,
                          bool icache_miss, bool dcache_miss, bool tlb_miss, Register target_pc)
        -> uint32_t override;

    [[nodiscard]] auto get_stats() const -> PipelineStats override;
    [[nodiscard]] auto get_cycle_history() const -> std::vector<PipelineCycleSnapshot> override;

    [[nodiscard]] auto f_reg() const -> PipelineReg override { return f_reg_; }
    [[nodiscard]] auto d_reg() const -> PipelineReg override { return d_reg_; }
    [[nodiscard]] auto e_reg() const -> PipelineReg override { return e_reg_; }
    [[nodiscard]] auto m_reg() const -> PipelineReg override { return m_reg_; }
    [[nodiscard]] auto w_reg() const -> PipelineReg override { return w_reg_; }

    [[nodiscard]] auto e1_reg() const -> PipelineReg { return e1_reg_; }
    [[nodiscard]] auto w1_reg() const -> PipelineReg { return w1_reg_; }

    [[nodiscard]] auto div_busy_cycles_remaining() const -> uint32_t override {
        return div_busy_cycles_remaining_;
    }
    [[nodiscard]] auto icache_stall_remaining() const -> uint32_t override {
        return icache_stall_remaining_;
    }
    [[nodiscard]] auto dcache_stall_remaining() const -> uint32_t override {
        return dcache_stall_remaining_;
    }
    [[nodiscard]] auto tlb_stall_remaining() const -> uint32_t override {
        return tlb_stall_remaining_;
    }
    [[nodiscard]] auto control_bubble_remaining() const -> uint32_t override {
        return control_bubble_remaining_;
    }

    [[nodiscard]] auto get_bht_entry(Register pc) const -> uint8_t override;
    [[nodiscard]] auto get_btb_target(Register pc) const -> std::pair<bool, Register> override;

    [[nodiscard]] auto save_state() const -> PipelineSimState override;
    void restore_state(const PipelineSimState& state) override;

   private:
    [[nodiscard]] auto can_dual_issue(const PipelineReg& slot0, const PipelineReg& slot1) const
        -> bool;
    void init_execution_latency(PipelineReg& reg);
    auto check_forwarding_hazard(const PipelineReg& slot, const PipelineReg& stage) const -> bool;
    auto evaluate_branch_prediction(Register pc, Register target_pc, bool branched) -> bool;
    void push_cycle_snapshot(bool f_stalled, bool d_stalled);

    const CpuConfig& config_;

    PipelineReg f_reg_{};
    PipelineReg d_reg_{};
    PipelineReg e_reg_{};   // Slot 0 (Full ALU/LSU/BR/CSR)
    PipelineReg e1_reg_{};  // Slot 1 (ALU/MUL)
    PipelineReg m_reg_{};   // Slot 0 MEM
    PipelineReg w_reg_{};   // Slot 0 WB
    PipelineReg w1_reg_{};  // Slot 1 WB

    bool slot0_active_{false};
    PipelineReg pending_slot0_{};

    uint32_t control_bubble_remaining_{0};
    uint32_t tlb_stall_remaining_{0};
    uint32_t icache_stall_remaining_{0};
    uint32_t dcache_stall_remaining_{0};
    uint32_t div_busy_cycles_remaining_{0};

    uint64_t cycle_count_{0};
    uint64_t stall_cycles_{0};
    uint64_t bubble_cycles_{0};
    uint64_t icache_stalls_{0};
    uint64_t dcache_stalls_{0};
    uint64_t tlb_stalls_{0};
    uint64_t structural_stalls_{0};
    uint64_t data_hazard_stalls_{0};
    uint64_t control_hazard_bubbles_{0};
    uint64_t dual_issue_cycles_{0};
    uint64_t single_issue_cycles_{0};

    std::array<uint8_t, 256> branch_history_table_{};
    std::vector<BtbEntry> btb_;
    uint32_t gshare_history_{0};

    static constexpr size_t kHistoryCapacity = 16;
    std::array<PipelineCycleSnapshot, kHistoryCapacity> cycle_ring_buffer_{};
    size_t ring_head_{0};
    size_t ring_size_{0};
};

}  // namespace simrv::pipeline
