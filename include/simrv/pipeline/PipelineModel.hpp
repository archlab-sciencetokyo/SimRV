#pragma once

#include <cstdint>
#include <vector>
#include <utility>
#include "simrv/Define.hpp"
#include "simrv/xlen/Types.hpp"
#include "simrv/isa/Base.hpp"

namespace simrv::pipeline {

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

// Forward declaration of PipelineCycleSnapshot and PipelineReg since they are defined in PipelineSim.hpp
struct PipelineCycleSnapshot;
struct PipelineReg;

class PipelineModel {
public:
    virtual ~PipelineModel() = default;

    virtual void reset() = 0;
    
    virtual auto step_instruction(Register pc, isa::Opcode opcode, RegId rd, RegId rs1, RegId rs2,
                                  isa::OperationId op_id, bool branched, bool is_branch, bool is_jump,
                                  bool icache_miss, bool dcache_miss, bool tlb_miss, Register target_pc) -> uint32_t = 0;

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

    // Out-of-order specific visual info (returns empty/false if in-order)
    [[nodiscard]] virtual auto is_ooo() const -> bool { return false; }
    [[nodiscard]] virtual auto get_rob_size() const -> size_t { return 0; }
    [[nodiscard]] virtual auto get_rob_occupancy() const -> size_t { return 0; }
    struct RobEntryInfo {
        Register pc;
        isa::OperationId op_id;
        bool ready;
        bool head;
        bool tail;
    };
    [[nodiscard]] virtual auto get_rob_entries() const -> std::vector<RobEntryInfo> { return {}; }
    [[nodiscard]] virtual auto get_rs_occupancy() const -> size_t { return 0; }
};

} // namespace simrv::pipeline
