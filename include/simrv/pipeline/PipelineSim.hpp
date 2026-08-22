#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <simrv/Define.hpp>
#include <simrv/isa/Base.hpp>
#include <simrv/xlen/Types.hpp>
#include <vector>

namespace simrv::pipeline {

struct BtbEntry {
    Register pc = 0;
    Register target = 0;
    bool valid = false;
};

struct PipelineReg {
    uint64_t inst_id = 0;
    Register pc = 0;
    isa::Opcode opcode = static_cast<isa::Opcode>(0);
    RegId rd = static_cast<RegId>(0);
    RegId rs1 = static_cast<RegId>(0);
    RegId rs2 = static_cast<RegId>(0);
    isa::OperationId op_id = isa::OperationId::UNKNOWN;
    bool writes_reg = false;
    uint32_t rd_mask = 0;
    bool writes_fp_reg = false;
    uint32_t rd_fp_mask = 0;
    bool is_fp_op = false;
    bool is_load = false;
    int remaining_latency = 0;  // Cycles until value is ready for forwarding
    bool valid = false;
    bool tlb_miss = false;
    bool icache_miss = false;
    bool dcache_miss = false;
    bool is_branch = false;
    bool is_jump = false;
    bool branched = false;
    Register target_pc = 0;
    bool control_resolved = false;
};

enum class BranchPredictorType : uint8_t {
    StaticNotTaken,
    StaticTaken,
    OneBitBimodal,
    TwoBitBimodal,
    Gshare
};

enum class PipelineType : uint8_t {
    FiveStage = 0,   ///< Standard 5-stage RV64/32GC Rocket microarchitecture
    ThreeStage = 1,  ///< 3-stage lightweight embedded microcontroller (Ibex/E21)
    DualIssue = 2    ///< SweRV EH1-style dual-issue in-order superscalar pipeline
};

enum class CpuPreset : uint8_t {
    Rocket,    ///< Standard 5-stage RV64/32GC microarchitecture (Rocket Core)
    Embedded,  ///< 3-stage lightweight embedded microcontroller
    Fast       ///< Minimal latency profile for functional speed
};

struct CpuConfig {
    uint32_t icache_miss_penalty = 10;
    uint32_t dcache_miss_penalty = 15;
    uint32_t tlb_miss_penalty = 25;
    uint32_t mul_latency = 3;
    uint32_t div_latency = 18;
    uint32_t fp_alu_latency = 4;
    uint32_t fp_div_latency = 16;
    uint32_t csr_flush_penalty = 3;
    uint32_t fence_flush_penalty = 4;
    uint32_t branch_mispredict_penalty = 2;
    bool enable_forwarding = true;
    BranchPredictorType bp_type = BranchPredictorType::TwoBitBimodal;
    uint32_t btb_entries = 128;
    uint32_t global_history_bits = 8;
    bool enable_ex_forwarding = true;
    bool enable_mem_forwarding = true;
    bool record_snapshots = false;
    PipelineType pipeline_type = PipelineType::FiveStage;

    void apply_preset(CpuPreset preset) {
        switch (preset) {
            case CpuPreset::Rocket:
                pipeline_type = PipelineType::FiveStage;
                icache_miss_penalty = 10;
                dcache_miss_penalty = 15;
                tlb_miss_penalty = 25;
                mul_latency = 3;
                div_latency = 18;
                fp_alu_latency = 4;
                fp_div_latency = 16;
                csr_flush_penalty = 3;
                fence_flush_penalty = 4;
                branch_mispredict_penalty = 2;
                enable_forwarding = true;
                enable_ex_forwarding = true;
                enable_mem_forwarding = true;
                bp_type = BranchPredictorType::TwoBitBimodal;
                btb_entries = 128;
                break;
            case CpuPreset::Embedded:
                pipeline_type = PipelineType::ThreeStage;
                icache_miss_penalty = 6;
                dcache_miss_penalty = 8;
                tlb_miss_penalty = 15;
                mul_latency = 2;
                div_latency = 12;
                fp_alu_latency = 3;
                fp_div_latency = 12;
                csr_flush_penalty = 2;
                fence_flush_penalty = 2;
                branch_mispredict_penalty = 1;
                enable_forwarding = true;
                enable_ex_forwarding = true;
                enable_mem_forwarding = true;
                bp_type = BranchPredictorType::OneBitBimodal;
                btb_entries = 64;
                break;
            case CpuPreset::Fast:
                icache_miss_penalty = 2;
                dcache_miss_penalty = 2;
                tlb_miss_penalty = 4;
                mul_latency = 1;
                div_latency = 4;
                fp_alu_latency = 1;
                fp_div_latency = 4;
                csr_flush_penalty = 1;
                fence_flush_penalty = 1;
                branch_mispredict_penalty = 1;
                enable_forwarding = true;
                enable_ex_forwarding = true;
                enable_mem_forwarding = true;
                bp_type = BranchPredictorType::StaticNotTaken;
                btb_entries = 0;
                break;
        }
    }
};

struct PipelineCycleSnapshot {
    uint64_t cycle = 0;
    struct StageInfo {
        uint64_t inst_id = 0;
        Register pc = 0;
        isa::OperationId op_id = isa::OperationId::UNKNOWN;
        bool valid = false;
        bool stalled = false;
    };
    StageInfo f;
    StageInfo d;
    StageInfo e;
    StageInfo m;
    StageInfo w;
    StageInfo e1;  // Slot 1 EX stage for dual-issue superscalar
    StageInfo w1;  // Slot 1 WB stage for dual-issue superscalar
};

struct PipelineStats {
    uint64_t cycle_count = 0;
    uint64_t stall_cycles = 0;
    uint64_t bubble_cycles = 0;
    uint64_t icache_stalls = 0;
    uint64_t dcache_stalls = 0;
    uint64_t tlb_stalls = 0;
    uint64_t structural_stalls = 0;
    uint64_t data_hazard_stalls = 0;
    uint64_t control_hazard_bubbles = 0;
    uint64_t dual_issue_cycles = 0;
    uint64_t single_issue_cycles = 0;
};

struct PipelineSimState {
    PipelineReg f_reg;
    PipelineReg d_reg;
    PipelineReg e_reg;
    PipelineReg m_reg;
    PipelineReg w_reg;
    std::array<uint8_t, 256> branch_history_table{};
    std::vector<BtbEntry> btb;
    std::vector<PipelineCycleSnapshot> cycle_history;
    uint32_t control_bubble_remaining = 0;
    uint32_t tlb_stall_remaining = 0;
    uint32_t icache_stall_remaining = 0;
    uint32_t dcache_stall_remaining = 0;
    uint32_t div_busy_cycles_remaining = 0;
    PipelineStats stats;
    uint32_t gshare_history = 0;
};

class PipelineModel;  // Forward declaration

class PipelineSim {
   public:
    PipelineSim();
    ~PipelineSim();

    /// Reset pipeline simulation state
    void reset();

    CpuConfig config;

    /**
     * @brief Process a single committed instruction and calculate its cycle latency
     * @return Number of simulated cycles spent for this instruction (1 base + stalls/bubbles)
     */
    auto step_instruction(Register pc, isa::Opcode opcode, RegId rd, RegId rs1, RegId rs2,
                          isa::OperationId op_id, bool branched, bool is_branch, bool is_jump,
                          bool icache_miss, bool dcache_miss, bool tlb_miss, Register target_pc)
        -> uint32_t;

    // Getters for statistics
    [[nodiscard]] auto cycle_count() const -> uint64_t;
    [[nodiscard]] auto stall_cycles() const -> uint64_t;
    [[nodiscard]] auto bubble_cycles() const -> uint64_t;
    [[nodiscard]] auto icache_stalls() const -> uint64_t;
    [[nodiscard]] auto dcache_stalls() const -> uint64_t;
    [[nodiscard]] auto tlb_stalls() const -> uint64_t;
    [[nodiscard]] auto structural_stalls() const -> uint64_t;
    [[nodiscard]] auto data_hazard_stalls() const -> uint64_t;
    [[nodiscard]] auto control_hazard_bubbles() const -> uint64_t;
    [[nodiscard]] auto get_cycle_history_copy() const -> std::vector<PipelineCycleSnapshot>;
    [[nodiscard]] auto get_stats() const -> PipelineStats;

    // Model state getters for TUI compatibility
    [[nodiscard]] auto f_reg() const -> PipelineReg;
    [[nodiscard]] auto d_reg() const -> PipelineReg;
    [[nodiscard]] auto e_reg() const -> PipelineReg;
    [[nodiscard]] auto m_reg() const -> PipelineReg;
    [[nodiscard]] auto w_reg() const -> PipelineReg;
    [[nodiscard]] auto e1_reg() const -> PipelineReg;
    [[nodiscard]] auto w1_reg() const -> PipelineReg;

    [[nodiscard]] auto div_busy_cycles_remaining() const -> uint32_t;
    [[nodiscard]] auto icache_stall_remaining() const -> uint32_t;
    [[nodiscard]] auto dcache_stall_remaining() const -> uint32_t;
    [[nodiscard]] auto tlb_stall_remaining() const -> uint32_t;
    [[nodiscard]] auto control_bubble_remaining() const -> uint32_t;

    [[nodiscard]] auto get_model() const -> const PipelineModel* { return model_.get(); }
    [[nodiscard]] auto get_model() -> PipelineModel* { return model_.get(); }

    [[nodiscard]] auto save_state() const -> PipelineSimState;
    void restore_state(const PipelineSimState& state);

   private:
    void init_model();

    std::unique_ptr<PipelineModel> model_;
};

}  // namespace simrv::pipeline
