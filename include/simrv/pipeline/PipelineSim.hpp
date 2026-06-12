#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include <simrv/Define.hpp>
#include <simrv/xlen/Types.hpp>

namespace simrv::pipeline {

struct BtbEntry {
    Register pc = 0;
    Register target = 0;
    bool valid = false;
};

struct PipelineReg {
    Register pc = 0;
    Opcode opcode = static_cast<Opcode>(0);
    RegId rd = static_cast<RegId>(0);
    RegId rs1 = static_cast<RegId>(0);
    RegId rs2 = static_cast<RegId>(0);
    OperationId op_id = OperationId::UNKNOWN;
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

struct CpuConfig {
    uint32_t icache_miss_penalty = 10;
    uint32_t dcache_miss_penalty = 15;
    uint32_t tlb_miss_penalty = 30;
    uint32_t mul_latency = 3;
    uint32_t div_latency = 20;
    uint32_t branch_mispredict_penalty = 2;
};

class PipelineSim {
public:
    PipelineSim();

    /// Reset pipeline simulation state
    void reset();

    CpuConfig config;

    /**
     * @brief Process a single committed instruction and calculate its cycle latency
     * @return Number of simulated cycles spent for this instruction (1 base + stalls/bubbles)
     */
    auto step_instruction(Register pc, Opcode opcode, RegId rd, RegId rs1, RegId rs2,
                          OperationId op_id, bool branched, bool is_branch, bool is_jump,
                          bool icache_miss, bool dcache_miss, bool tlb_miss, Register target_pc) -> uint32_t;

    // Getters for statistics
    [[nodiscard]] auto cycle_count() const -> uint64_t { return cycle_count_; }
    [[nodiscard]] auto stall_cycles() const -> uint64_t { return stall_cycles_; }
    [[nodiscard]] auto bubble_cycles() const -> uint64_t { return bubble_cycles_; }
    [[nodiscard]] auto icache_stalls() const -> uint64_t { return icache_stalls_; }
    [[nodiscard]] auto dcache_stalls() const -> uint64_t { return dcache_stalls_; }
    [[nodiscard]] auto tlb_stalls() const -> uint64_t { return tlb_stalls_; }
    [[nodiscard]] auto structural_stalls() const -> uint64_t { return structural_stalls_; }
    [[nodiscard]] auto data_hazard_stalls() const -> uint64_t { return data_hazard_stalls_; }
    [[nodiscard]] auto control_hazard_bubbles() const -> uint64_t { return control_hazard_bubbles_; }

    // Exposed pipeline stage registers and predictor tables for TUI visualizer
    PipelineReg f_reg_; // IF stage
    PipelineReg d_reg_; // ID stage
    PipelineReg e_reg_; // EX stage
    PipelineReg m_reg_; // MEM stage
    PipelineReg w_reg_; // WB stage

    std::array<uint8_t, 256> branch_history_table_{}; // 2-bit dynamic bimodal predictor (0-3)
    std::array<BtbEntry, 128> btb_{};                 // Branch Target Buffer

    uint32_t control_bubble_remaining_ = 0;
    uint32_t tlb_stall_remaining_ = 0;
    uint32_t icache_stall_remaining_ = 0;
    uint32_t dcache_stall_remaining_ = 0;
    uint32_t div_busy_cycles_remaining_ = 0;

private:
    void tick_pipeline();

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
