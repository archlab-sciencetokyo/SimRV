#pragma once

#include <cstdint>
#include <vector>
#include <array>
#include <simrv/Define.hpp>
#include <simrv/xlen/Types.hpp>

namespace simrv::pipeline {

struct InFlightInstr {
    RegId rd = static_cast<RegId>(0);
    bool writes_reg = false;
    bool is_load = false;
    int remaining_latency = 0; // Cycles until value is ready for forwarding
};

class PipelineSim {
public:
    PipelineSim();

    /// Reset pipeline simulation state
    void reset();

    /**
     * @brief Process a single committed instruction and calculate its cycle latency
     * @return Number of simulated cycles spent for this instruction (1 base + stalls/bubbles)
     */
    auto step_instruction(Register pc, Opcode opcode, RegId rd, RegId rs1, RegId rs2,
                          OperationId op_id, bool branched, bool is_branch, bool is_jump,
                          bool icache_miss, bool dcache_miss) -> uint32_t;

    // Getters for statistics
    [[nodiscard]] auto cycle_count() const -> uint64_t { return cycle_count_; }
    [[nodiscard]] auto stall_cycles() const -> uint64_t { return stall_cycles_; }
    [[nodiscard]] auto bubble_cycles() const -> uint64_t { return bubble_cycles_; }
    [[nodiscard]] auto icache_stalls() const -> uint64_t { return icache_stalls_; }
    [[nodiscard]] auto dcache_stalls() const -> uint64_t { return dcache_stalls_; }
    [[nodiscard]] auto data_hazard_stalls() const -> uint64_t { return data_hazard_stalls_; }
    [[nodiscard]] auto control_hazard_bubbles() const -> uint64_t { return control_hazard_bubbles_; }

private:
    std::vector<InFlightInstr> in_flight_;
    std::array<bool, 256> branch_history_table_{}; // 1-bit dynamic branch predictor

    uint64_t cycle_count_ = 0;
    uint64_t stall_cycles_ = 0;
    uint64_t bubble_cycles_ = 0;
    uint64_t icache_stalls_ = 0;
    uint64_t dcache_stalls_ = 0;
    uint64_t data_hazard_stalls_ = 0;
    uint64_t control_hazard_bubbles_ = 0;
};

} // namespace simrv::pipeline
