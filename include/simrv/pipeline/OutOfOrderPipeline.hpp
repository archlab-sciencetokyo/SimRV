#pragma once

#include <vector>
#include <array>
#include <deque>
#include <mutex>
#include <utility>
#include "simrv/pipeline/PipelineModel.hpp"
#include "simrv/pipeline/PipelineSim.hpp"

namespace simrv::pipeline {

struct RobEntry {
    Register pc = 0;
    isa::Opcode opcode = static_cast<isa::Opcode>(0);
    RegId rd = static_cast<RegId>(0);
    isa::OperationId op_id = isa::OperationId::UNKNOWN;
    bool is_ready = false;
    bool is_mispredicted = false;
    bool valid = false;
};

struct RsEntry {
    bool busy = false;
    isa::Opcode opcode = static_cast<isa::Opcode>(0);
    isa::OperationId op_id = isa::OperationId::UNKNOWN;
    RegId rd = static_cast<RegId>(0);
    
    bool src1_ready = true;
    int32_t src1_rob_tag = -1; // ROB index if waiting
    
    bool src2_ready = true;
    int32_t src2_rob_tag = -1; // ROB index if waiting
    
    int32_t rob_tag = -1; // ROB index of this instruction
    uint32_t remaining_latency = 0;
    bool executing = false;
    
    bool is_load_store = false;
    uint32_t memory_stall_remaining = 0;
};

class OutOfOrderPipeline : public PipelineModel {
public:
    explicit OutOfOrderPipeline(const CpuConfig& cfg);
    ~OutOfOrderPipeline() override = default;

    void reset() override;
    
    auto step_instruction(Register pc, isa::Opcode opcode, RegId rd, RegId rs1, RegId rs2,
                          isa::OperationId op_id, bool branched, bool is_branch, bool is_jump,
                          bool icache_miss, bool dcache_miss, bool tlb_miss, Register target_pc) -> uint32_t override;

    [[nodiscard]] auto get_stats() const -> PipelineStats override;
    [[nodiscard]] auto get_cycle_history() const -> std::vector<PipelineCycleSnapshot> override;

    // Stage registers for TUI visualization (mapped to ROB/RS states)
    [[nodiscard]] auto f_reg() const -> PipelineReg override;
    [[nodiscard]] auto d_reg() const -> PipelineReg override;
    [[nodiscard]] auto e_reg() const -> PipelineReg override;
    [[nodiscard]] auto m_reg() const -> PipelineReg override;
    [[nodiscard]] auto w_reg() const -> PipelineReg override;

    // Remaining stalls for TUI/hazard checks
    [[nodiscard]] auto div_busy_cycles_remaining() const -> uint32_t override;
    [[nodiscard]] auto icache_stall_remaining() const -> uint32_t override { return icache_stall_remaining_; }
    [[nodiscard]] auto dcache_stall_remaining() const -> uint32_t override;
    [[nodiscard]] auto tlb_stall_remaining() const -> uint32_t override { return tlb_stall_remaining_; }
    [[nodiscard]] auto control_bubble_remaining() const -> uint32_t override { return ooo_fetch_stall_remaining_; }

    // Predictor queries
    [[nodiscard]] auto get_bht_entry(Register pc) const -> uint8_t override;
    [[nodiscard]] auto get_btb_target(Register pc) const -> std::pair<bool, Register> override;

    // Out-of-order specific visual info
    [[nodiscard]] auto is_ooo() const -> bool override { return true; }
    [[nodiscard]] auto get_rob_size() const -> size_t override { return config.rob_size; }
    [[nodiscard]] auto get_rob_occupancy() const -> size_t override;
    [[nodiscard]] auto get_rob_entries() const -> std::vector<RobEntryInfo> override;
    [[nodiscard]] auto get_rs_occupancy() const -> size_t override;

private:
    void tick_pipeline_ooo();
    void record_cycle_snapshot_ooo();
    
    [[nodiscard]] auto count_rob_in_flight() const -> size_t;
    [[nodiscard]] auto count_rs_in_flight() const -> size_t;
    
    auto resolve_jump_ex(BtbEntry& btb_entry, Register pc, isa::Opcode opcode, Register target_pc) -> uint32_t;
    auto resolve_branch_ex(BtbEntry& btb_entry, Register pc, Register target_pc, bool branched) -> uint32_t;

    const CpuConfig& config;

    std::vector<RobEntry> rob_;
    std::vector<RsEntry> rs_;
    std::array<int32_t, 32> rat_gpr_{};
    std::array<int32_t, 32> rat_fpr_{};
    uint32_t rob_head_ = 0;
    uint32_t rob_tail_ = 0;
    
    std::array<uint8_t, 256> branch_history_table_{}; // 2-bit dynamic bimodal predictor (0-3)
    std::vector<BtbEntry> btb_;                       // Branch Target Buffer
    std::deque<PipelineCycleSnapshot> cycle_history_; // Cycle-by-cycle snapshot history
    mutable std::mutex history_mutex_;

    uint32_t ooo_fetch_stall_remaining_ = 0;
    uint32_t tlb_stall_remaining_ = 0;
    uint32_t icache_stall_remaining_ = 0;
    uint32_t gshare_history_ = 0;

    // Instruction queues for fetch buffer simulation
    struct QueuedInst {
        Register pc;
        isa::Opcode opcode;
        RegId rd;
        RegId rs1;
        RegId rs2;
        isa::OperationId op_id;
        bool branched;
        bool is_branch;
        bool is_jump;
        bool icache_miss;
        bool dcache_miss;
        bool tlb_miss;
        Register target_pc;
    };
    std::deque<QueuedInst> fetch_queue_;

    uint64_t cycle_count_ = 0;
    uint64_t stall_cycles_ = 0;
    uint64_t bubble_cycles_ = 0;
    uint64_t icache_stalls_ = 0;
    uint64_t dcache_stalls_ = 0;
    uint64_t tlb_stalls_ = 0;
    uint64_t structural_stalls_ = 0;
    uint64_t data_hazard_stalls_ = 0;
    uint64_t control_hazard_bubbles_ = 0;
};

} // namespace simrv::pipeline
