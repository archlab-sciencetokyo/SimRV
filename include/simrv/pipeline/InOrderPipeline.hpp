#pragma once

#include <vector>
#include <array>
#include <deque>
#include <mutex>
#include "simrv/pipeline/PipelineModel.hpp"
#include "simrv/pipeline/PipelineSim.hpp"

namespace simrv::pipeline {

class InOrderPipeline : public PipelineModel {
public:
    explicit InOrderPipeline(const CpuConfig& cfg);
    ~InOrderPipeline() override = default;

    void reset() override;
    
    auto step_instruction(Register pc, isa::Opcode opcode, RegId rd, RegId rs1, RegId rs2,
                          isa::OperationId op_id, bool branched, bool is_branch, bool is_jump,
                          bool icache_miss, bool dcache_miss, bool tlb_miss, Register target_pc) -> uint32_t override;

    [[nodiscard]] auto get_stats() const -> PipelineStats override;
    [[nodiscard]] auto get_cycle_history() const -> std::vector<PipelineCycleSnapshot> override;

    // Stage registers for TUI visualization
    [[nodiscard]] auto f_reg() const -> PipelineReg override { return f_reg_; }
    [[nodiscard]] auto d_reg() const -> PipelineReg override { return d_reg_; }
    [[nodiscard]] auto e_reg() const -> PipelineReg override { return e_reg_; }
    [[nodiscard]] auto m_reg() const -> PipelineReg override { return m_reg_; }
    [[nodiscard]] auto w_reg() const -> PipelineReg override { return w_reg_; }

    // Remaining stalls for TUI/hazard checks
    [[nodiscard]] auto div_busy_cycles_remaining() const -> uint32_t override { return div_busy_cycles_remaining_; }
    [[nodiscard]] auto icache_stall_remaining() const -> uint32_t override { return icache_stall_remaining_; }
    [[nodiscard]] auto dcache_stall_remaining() const -> uint32_t override { return dcache_stall_remaining_; }
    [[nodiscard]] auto tlb_stall_remaining() const -> uint32_t override { return tlb_stall_remaining_; }
    [[nodiscard]] auto control_bubble_remaining() const -> uint32_t override { return control_bubble_remaining_; }

    // Predictor queries
    [[nodiscard]] auto get_bht_entry(Register pc) const -> uint8_t override;
    [[nodiscard]] auto get_btb_target(Register pc) const -> std::pair<bool, Register> override;

    // Pipeline state checkpointing
    [[nodiscard]] auto save_state() const -> PipelineSimState override;
    void restore_state(const PipelineSimState& state) override;

private:
    void tick_pipeline();
    void record_cycle_snapshot();
    void init_execution_latency(PipelineReg& reg);
    [[nodiscard]] auto check_stall_mem() const -> bool;
    [[nodiscard]] auto check_stall_ex() const -> bool;
    [[nodiscard]] auto check_stall_id() const -> bool;
    [[nodiscard]] auto check_stall_if() const -> bool;
    auto resolve_branches_ex() -> bool;

    [[nodiscard]] auto check_hazard_with_stage(const PipelineReg& stage_reg, bool reads_rs1, bool reads_rs2) const -> bool;
    auto resolve_jump_ex(BtbEntry& btb_entry, Register pc, isa::Opcode opcode, Register target_pc) -> uint32_t;
    auto resolve_branch_ex(BtbEntry& btb_entry, Register pc, Register target_pc, bool branched) -> uint32_t;
    void update_stall_stats(bool stall_mem, bool stall_ex, bool stall_id, bool stall_if);
    void decrement_latencies();
    void stage_register_transfers(bool MEM_stalled, bool EX_stalled, bool ID_stalled, bool IF_stalled);

    const CpuConfig& config;

    PipelineReg f_reg_; // IF stage
    PipelineReg d_reg_; // ID stage
    PipelineReg e_reg_; // EX stage
    PipelineReg m_reg_; // MEM stage
    PipelineReg w_reg_; // WB stage

    std::array<uint8_t, 256> branch_history_table_{}; // 2-bit dynamic bimodal predictor (0-3)
    std::vector<BtbEntry> btb_;                       // Branch Target Buffer
    std::deque<PipelineCycleSnapshot> cycle_history_; // Cycle-by-cycle pipeline stage snapshot history
    mutable std::mutex history_mutex_;                // Mutex protecting cycle history access

    uint32_t control_bubble_remaining_ = 0;
    uint32_t tlb_stall_remaining_ = 0;
    uint32_t icache_stall_remaining_ = 0;
    uint32_t dcache_stall_remaining_ = 0;
    uint32_t div_busy_cycles_remaining_ = 0;

    uint64_t cycle_count_ = 0;
    uint64_t next_inst_id_ = 0;
    uint64_t stall_cycles_ = 0;
    uint64_t bubble_cycles_ = 0;
    uint64_t icache_stalls_ = 0;
    uint64_t dcache_stalls_ = 0;
    uint64_t tlb_stalls_ = 0;
    uint64_t structural_stalls_ = 0;
    uint64_t data_hazard_stalls_ = 0;
    uint64_t control_hazard_bubbles_ = 0;
    uint32_t gshare_history_ = 0;
};

} // namespace simrv::pipeline
