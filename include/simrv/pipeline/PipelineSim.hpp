#pragma once

#include <array>
#include <cstdint>
#include <simrv/Define.hpp>
#include <simrv/isa/Base.hpp>
#include <simrv/pipeline/CycleTransition.hpp>
#include <simrv/xlen/Types.hpp>
#include <vector>

namespace simrv::pipeline {

enum class ForwardSource : uint8_t { None = 0, Execute = 1, Memory = 2, Writeback = 3 };

struct PipelineReg {
    InstructionId inst_id = 0;
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
    LatencyCycles remaining_latency = 0;  // Cycles until value is ready for forwarding
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
    LatencyCycles remaining_latency = 0;
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
    LatencyCycles mul_latency = 3;
    LatencyCycles div_latency = 18;
    LatencyCycles fp_alu_latency = 4;
    LatencyCycles fp_div_latency = 16;
    LatencyCycles csr_flush_penalty = 3;
    LatencyCycles fence_flush_penalty = 4;
    bool enable_forwarding = true;
    bool record_snapshots = false;
    PipelineType pipeline_type = PipelineType::FiveStage;
    BranchPredictorConfig branch_predictor{};
};

struct PipelineCycleSnapshot {
    CycleCount cycle = 0;
    struct StageInfo {
        InstructionId inst_id = 0;
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
    Counter cycle_count = 0;
    Counter stall_cycles = 0;
    Counter bubble_cycles = 0;
    Counter icache_stalls = 0;
    Counter dcache_stalls = 0;
    Counter tlb_stalls = 0;
    Counter structural_stalls = 0;
    Counter data_hazard_stalls = 0;
    Counter control_hazard_bubbles = 0;
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
    SIMRV_ALWAYS_INLINE void advance_cycle_fast(const PipelineCycleMetrics& metrics) noexcept {
        ca_kernel_active_ = true;
        update_stats(metrics);
    }

    // Getters for statistics
    [[nodiscard]] auto cycle_count() const -> Counter;
    [[nodiscard]] auto stall_cycles() const -> Counter;
    [[nodiscard]] auto bubble_cycles() const -> Counter;
    [[nodiscard]] auto icache_stalls() const -> Counter;
    [[nodiscard]] auto dcache_stalls() const -> Counter;
    [[nodiscard]] auto tlb_stalls() const -> Counter;
    [[nodiscard]] auto structural_stalls() const -> Counter;
    [[nodiscard]] auto data_hazard_stalls() const -> Counter;
    [[nodiscard]] auto control_hazard_bubbles() const -> Counter;
    [[nodiscard]] auto cycle_history() const noexcept -> PipelineHistoryView;
    [[nodiscard]] auto get_stats() const -> PipelineStats;

    // Model state getters for TUI compatibility
    [[nodiscard]] auto f_reg() const -> const PipelineReg&;
    [[nodiscard]] auto d_reg() const -> const PipelineReg&;
    [[nodiscard]] auto e_reg() const -> const PipelineReg&;
    [[nodiscard]] auto m_reg() const -> const PipelineReg&;
    [[nodiscard]] auto w_reg() const -> const PipelineReg&;
    [[nodiscard]] auto stage_reg(PipelineStage stage) const -> const PipelineReg&;

    [[nodiscard]] auto div_busy_cycles_remaining() const -> LatencyCycles;
    [[nodiscard]] auto icache_stall_remaining() const -> LatencyCycles;
    [[nodiscard]] auto dcache_stall_remaining() const -> LatencyCycles;
    [[nodiscard]] auto tlb_stall_remaining() const -> LatencyCycles;
    [[nodiscard]] auto control_bubble_remaining() const -> LatencyCycles;

    [[nodiscard]] auto save_state() const -> PipelineSimState;
    void restore_state(const PipelineSimState& state);

   private:
    SIMRV_ALWAYS_INLINE void update_stats(const PipelineCycleMetrics& metrics) noexcept {
        ++ca_stats_.cycle_count;
        const bool stalled = metrics.fetch_stalled || metrics.decode_stalled ||
                             metrics.execute_stalled || metrics.memory_stalled ||
                             metrics.writeback_stalled;
        ca_stats_.stall_cycles += stalled;
        ca_stats_.icache_stalls += metrics.icache_miss && metrics.fetch_stalled;
        ca_stats_.dcache_stalls +=
            metrics.dcache_miss && (metrics.memory_stalled || metrics.writeback_stalled);
        ca_stats_.tlb_stalls += metrics.tlb_miss && stalled;
        ca_stats_.structural_stalls += metrics.execute_stalled;
        ca_stats_.data_hazard_stalls += metrics.data_hazard_stall;
        ca_stats_.control_hazard_bubbles += metrics.control_flush;
        ca_stats_.bubble_cycles += metrics.control_flush;
    }

    bool ca_kernel_active_ = false;
    std::array<PipelineReg, 5> ca_regs_{};
    PipelineStats ca_stats_{};
    static constexpr size_t kCaHistoryCapacity = PipelineHistoryView::kCapacity;
    std::array<PipelineCycleSnapshot, kCaHistoryCapacity> ca_history_{};
    size_t ca_history_head_ = 0;
    size_t ca_history_size_ = 0;
};

}  // namespace simrv::pipeline
