#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include <deque>
#include <mutex>
#include <memory>
#include <simrv/Define.hpp>
#include <simrv/xlen/Types.hpp>
#include <simrv/isa/Base.hpp>

namespace simrv::pipeline {

struct BtbEntry {
    Register pc = 0;
    Register target = 0;
    bool valid = false;
};

struct PipelineReg {
    Register pc = 0;
    isa::Opcode opcode = static_cast<isa::Opcode>(0);
    RegId rd = static_cast<RegId>(0);
    RegId rs1 = static_cast<RegId>(0);
    RegId rs2 = static_cast<RegId>(0);
    isa::OperationId op_id = isa::OperationId::UNKNOWN;
    bool writes_reg = false;
    bool is_load = false;
    int remaining_latency = 0; // Cycles until value is ready for forwarding
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

struct CpuConfig {
    uint32_t icache_miss_penalty = 10;
    uint32_t dcache_miss_penalty = 15;
    uint32_t tlb_miss_penalty = 30;
    uint32_t mul_latency = 3;
    uint32_t div_latency = 20;
    uint32_t branch_mispredict_penalty = 2;
    bool enable_forwarding = true;
    BranchPredictorType bp_type = BranchPredictorType::TwoBitBimodal;
    uint32_t btb_entries = 128;
    uint32_t global_history_bits = 8;
    bool enable_ex_forwarding = true;
    bool enable_mem_forwarding = true;
};
 
struct PipelineCycleSnapshot {
    uint64_t cycle = 0;
    struct StageInfo {
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

class PipelineModel; // Forward declaration

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
                          bool icache_miss, bool dcache_miss, bool tlb_miss, Register target_pc) -> uint32_t;

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

    // Model state getters for TUI compatibility
    [[nodiscard]] auto f_reg() const -> PipelineReg;
    [[nodiscard]] auto d_reg() const -> PipelineReg;
    [[nodiscard]] auto e_reg() const -> PipelineReg;
    [[nodiscard]] auto m_reg() const -> PipelineReg;
    [[nodiscard]] auto w_reg() const -> PipelineReg;

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

} // namespace simrv::pipeline
