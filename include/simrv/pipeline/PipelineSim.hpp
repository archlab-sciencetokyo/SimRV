#pragma once

#include <array>
#include <cstdint>
#include <simrv/Define.hpp>
#include <simrv/isa/Base.hpp>
#include <simrv/pipeline/CycleTransition.hpp>
#include <simrv/xlen/Types.hpp>
#include <vector>

namespace simrv::pipeline {

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

/// Immutable event submitted by the architectural engine to the cycle model.
struct PipelineInstruction {
    Register pc = 0;
    isa::Opcode opcode = static_cast<isa::Opcode>(0);
    RegId rd = static_cast<RegId>(0);
    RegId rs1 = static_cast<RegId>(0);
    RegId rs2 = static_cast<RegId>(0);
    isa::OperationId op_id = isa::OperationId::UNKNOWN;
    bool branched = false;
    bool is_branch = false;
    bool is_jump = false;
    bool icache_miss = false;
    bool dcache_miss = false;
    bool tlb_miss = false;
    Register target_pc = 0;
};

struct PipelineStageEvent {
    PipelineInstruction instruction{};
    uint32_t remaining_latency = 0;
    bool valid = false;
    bool stalled = false;
};

struct PipelineCycleMetrics {
    bool fetch_stalled = false;
    bool decode_stalled = false;
    bool execute_stalled = false;
    bool memory_stalled = false;
    bool writeback_stalled = false;
    bool retired = false;
    bool icache_miss = false;
    bool dcache_miss = false;
    bool tlb_miss = false;
    bool data_hazard_stall = false;
    bool control_flush = false;
};

/// Complete immutable view of one authoritative CA transition.
struct PipelineCycleEvent {
    PipelineStageEvent fetch{};
    PipelineStageEvent decode{};
    PipelineStageEvent execute{};
    PipelineStageEvent memory{};
    PipelineStageEvent writeback{};
    bool retired = false;
    bool icache_miss = false;
    bool dcache_miss = false;
    bool tlb_miss = false;
    bool data_hazard_stall = false;
    bool control_flush = false;
};

enum class PipelineType : uint8_t { FiveStage = 0, ThreeStage = 1 };

struct CpuConfig {
    uint32_t mul_latency = 3;
    uint32_t div_latency = 18;
    uint32_t fp_alu_latency = 4;
    uint32_t fp_div_latency = 16;
    uint32_t csr_flush_penalty = 3;
    uint32_t fence_flush_penalty = 4;
    bool enable_forwarding = true;
    bool record_snapshots = false;
    PipelineType pipeline_type = PipelineType::FiveStage;
    BranchPredictorConfig branch_predictor{};
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

class PipelineHistoryView {
   public:
    constexpr PipelineHistoryView() noexcept = default;
    [[nodiscard]] constexpr auto empty() const noexcept -> bool { return size_ == 0; }
    [[nodiscard]] constexpr auto size() const noexcept -> size_t { return size_; }
    [[nodiscard]] auto at(size_t index) const -> const PipelineCycleSnapshot&;

   private:
    friend class PipelineSim;
    static constexpr size_t kCapacity = 4096;
    constexpr PipelineHistoryView(const std::array<PipelineCycleSnapshot, kCapacity>* history,
                                  size_t head, size_t size) noexcept
        : history_(history), head_(head), size_(size) {}
    const std::array<PipelineCycleSnapshot, kCapacity>* history_ = nullptr;
    size_t head_ = 0;
    size_t size_ = 0;
};

struct PipelineSimState {
    PipelineReg f_reg;
    PipelineReg d_reg;
    PipelineReg e_reg;
    PipelineReg m_reg;
    PipelineReg w_reg;
    std::vector<PipelineCycleSnapshot> cycle_history;
    PipelineStats stats;
    bool ca_kernel_active = false;
};

class PipelineSim {
   public:
    PipelineSim() = default;

    /// Reset pipeline simulation state
    void reset();

    CpuConfig config;

    /// Advance the authoritative CA observer state by exactly one global cycle.
    void advance_cycle(const PipelineCycleEvent& event);
    void advance_cycle_fast(const PipelineCycleMetrics& metrics) noexcept;

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
    [[nodiscard]] auto cycle_history() const noexcept -> PipelineHistoryView;
    [[nodiscard]] auto get_stats() const -> PipelineStats;

    // Model state getters for TUI compatibility
    [[nodiscard]] auto f_reg() const -> const PipelineReg&;
    [[nodiscard]] auto d_reg() const -> const PipelineReg&;
    [[nodiscard]] auto e_reg() const -> const PipelineReg&;
    [[nodiscard]] auto m_reg() const -> const PipelineReg&;
    [[nodiscard]] auto w_reg() const -> const PipelineReg&;

    [[nodiscard]] auto div_busy_cycles_remaining() const -> uint32_t;
    [[nodiscard]] auto icache_stall_remaining() const -> uint32_t;
    [[nodiscard]] auto dcache_stall_remaining() const -> uint32_t;
    [[nodiscard]] auto tlb_stall_remaining() const -> uint32_t;
    [[nodiscard]] auto control_bubble_remaining() const -> uint32_t;

    [[nodiscard]] auto save_state() const -> PipelineSimState;
    void restore_state(const PipelineSimState& state);

   private:
    void update_stats(const PipelineCycleMetrics& metrics) noexcept;
    bool ca_kernel_active_ = false;
    std::array<PipelineReg, 5> ca_regs_{};
    PipelineStats ca_stats_{};
    static constexpr size_t kCaHistoryCapacity = PipelineHistoryView::kCapacity;
    std::array<PipelineCycleSnapshot, kCaHistoryCapacity> ca_history_{};
    size_t ca_history_head_ = 0;
    size_t ca_history_size_ = 0;
};

}  // namespace simrv::pipeline
