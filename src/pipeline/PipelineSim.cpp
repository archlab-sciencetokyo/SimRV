#include "simrv/pipeline/PipelineSim.hpp"

#include <algorithm>
#include <stdexcept>

#include "simrv/pipeline/OperationTraits.hpp"

namespace simrv::pipeline {

auto PipelineHistoryView::at(size_t index) const -> const PipelineCycleSnapshot& {
    if (history_ == nullptr || index >= size_) throw std::out_of_range("pipeline history index");
    const auto first = (head_ + kCapacity - size_) % kCapacity;
    return history_->at((first + index) % kCapacity);
}

void PipelineSim::reset() {
    ca_kernel_active_ = false;
    ca_regs_ = {};
    ca_stats_ = {};
    ca_history_head_ = 0;
    ca_history_size_ = 0;
}

void PipelineSim::advance_cycle(const PipelineCycleEvent& event) {
    ca_kernel_active_ = true;
    auto copy_stage = [](const PipelineStageEvent& stage) {
        return PipelineReg{.pc = stage.instruction.pc,
                           .opcode = stage.instruction.opcode,
                           .rd = stage.instruction.rd,
                           .rs1 = stage.instruction.rs1,
                           .rs2 = stage.instruction.rs2,
                           .op_id = stage.instruction.op_id,
                           .remaining_latency = static_cast<int>(stage.remaining_latency),
                           .valid = stage.valid,
                           .is_branch = stage.instruction.is_branch,
                           .is_jump = stage.instruction.is_jump,
                           .branched = stage.instruction.branched,
                           .target_pc = stage.instruction.target_pc};
    };
    ca_regs_[0] = copy_stage(event.fetch);
    ca_regs_[1] = copy_stage(event.decode);
    ca_regs_[2] = copy_stage(event.execute);
    ca_regs_[3] = copy_stage(event.memory);
    ca_regs_[4] = copy_stage(event.writeback);
    ca_regs_[0].icache_miss = event.icache_miss;
    ca_regs_[0].tlb_miss = event.tlb_miss;
    ca_regs_[3].dcache_miss = event.dcache_miss;

    const PipelineCycleMetrics metrics{
        .fetch_stalled = event.fetch.stalled,
        .decode_stalled = event.decode.stalled,
        .execute_stalled = event.execute.stalled,
        .memory_stalled = event.memory.stalled,
        .writeback_stalled = event.writeback.stalled,
        .retired = event.retired,
        .icache_miss = event.icache_miss,
        .dcache_miss = event.dcache_miss,
        .tlb_miss = event.tlb_miss,
        .data_hazard_stall = event.data_hazard_stall,
        .control_flush = event.control_flush,
    };
    update_stats(metrics);

    if (!config.record_snapshots) return;
    PipelineCycleSnapshot snapshot{};
    snapshot.cycle = ca_stats_.cycle_count;
    auto stage_info = [](const PipelineReg& reg, bool stage_stalled) {
        return PipelineCycleSnapshot::StageInfo{
            .pc = reg.pc, .op_id = reg.op_id, .valid = reg.valid, .stalled = stage_stalled};
    };
    snapshot.f = stage_info(ca_regs_[0], event.fetch.stalled);
    snapshot.d = stage_info(ca_regs_[1], event.decode.stalled);
    snapshot.e = stage_info(ca_regs_[2], event.execute.stalled);
    snapshot.m = stage_info(ca_regs_[3], event.memory.stalled);
    snapshot.w = stage_info(ca_regs_[4], event.writeback.stalled);
    ca_history_[ca_history_head_] = snapshot;
    ca_history_head_ = (ca_history_head_ + 1) % kCaHistoryCapacity;
    if (ca_history_size_ < kCaHistoryCapacity) ++ca_history_size_;
}

void PipelineSim::advance_cycle_fast(const PipelineCycleMetrics& metrics) noexcept {
    ca_kernel_active_ = true;
    update_stats(metrics);
}

void PipelineSim::update_stats(const PipelineCycleMetrics& metrics) noexcept {
    ++ca_stats_.cycle_count;
    const bool stalled = metrics.fetch_stalled || metrics.decode_stalled ||
                         metrics.execute_stalled || metrics.memory_stalled ||
                         metrics.writeback_stalled;
    // These counters are updated for every CA transition.  Boolean-to-integer accumulation keeps
    // the fast engine's accounting branch-free while preserving the one-count-per-cycle model.
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

// Getters for statistics
auto PipelineSim::cycle_count() const -> uint64_t { return ca_stats_.cycle_count; }
auto PipelineSim::stall_cycles() const -> uint64_t { return ca_stats_.stall_cycles; }
auto PipelineSim::bubble_cycles() const -> uint64_t { return ca_stats_.bubble_cycles; }
auto PipelineSim::icache_stalls() const -> uint64_t { return ca_stats_.icache_stalls; }
auto PipelineSim::dcache_stalls() const -> uint64_t { return ca_stats_.dcache_stalls; }
auto PipelineSim::tlb_stalls() const -> uint64_t { return ca_stats_.tlb_stalls; }
auto PipelineSim::structural_stalls() const -> uint64_t { return ca_stats_.structural_stalls; }
auto PipelineSim::data_hazard_stalls() const -> uint64_t { return ca_stats_.data_hazard_stalls; }
auto PipelineSim::control_hazard_bubbles() const -> uint64_t {
    return ca_stats_.control_hazard_bubbles;
}
auto PipelineSim::cycle_history() const noexcept -> PipelineHistoryView {
    if (!ca_kernel_active_) return {};
    return PipelineHistoryView(&ca_history_, ca_history_head_, ca_history_size_);
}

namespace {
const PipelineReg kEmptyPipelineRegister{};
}

auto PipelineSim::f_reg() const -> const PipelineReg& {
    if (ca_kernel_active_) return ca_regs_[0];
    return kEmptyPipelineRegister;
}
auto PipelineSim::d_reg() const -> const PipelineReg& {
    if (ca_kernel_active_) return ca_regs_[1];
    return kEmptyPipelineRegister;
}
auto PipelineSim::e_reg() const -> const PipelineReg& {
    if (ca_kernel_active_) return ca_regs_[2];
    return kEmptyPipelineRegister;
}
auto PipelineSim::m_reg() const -> const PipelineReg& {
    if (ca_kernel_active_) return ca_regs_[3];
    return kEmptyPipelineRegister;
}
auto PipelineSim::w_reg() const -> const PipelineReg& {
    if (ca_kernel_active_) return ca_regs_[4];
    return kEmptyPipelineRegister;
}

auto PipelineSim::div_busy_cycles_remaining() const -> uint32_t {
    return operation::is_divide_or_remainder(ca_regs_[2].op_id)
               ? static_cast<uint32_t>(std::max(0, ca_regs_[2].remaining_latency))
               : 0;
}
auto PipelineSim::icache_stall_remaining() const -> uint32_t {
    return ca_regs_[0].icache_miss ? 1 : 0;
}
auto PipelineSim::dcache_stall_remaining() const -> uint32_t {
    return (ca_regs_[3].dcache_miss || ca_regs_[4].dcache_miss) ? 1 : 0;
}
auto PipelineSim::tlb_stall_remaining() const -> uint32_t { return ca_regs_[0].tlb_miss ? 1 : 0; }
auto PipelineSim::control_bubble_remaining() const -> uint32_t { return 0; }

auto PipelineSim::get_stats() const -> PipelineStats { return ca_stats_; }

auto PipelineSim::save_state() const -> PipelineSimState {
    PipelineSimState state{};
    if (ca_kernel_active_) {
        state.ca_kernel_active = true;
        state.stats = ca_stats_;
        const auto history = cycle_history();
        state.cycle_history.reserve(history.size());
        for (size_t i = 0; i < history.size(); ++i) state.cycle_history.push_back(history.at(i));
        state.f_reg = ca_regs_[0];
        state.d_reg = ca_regs_[1];
        state.e_reg = ca_regs_[2];
        state.m_reg = ca_regs_[3];
        state.w_reg = ca_regs_[4];
    }
    return state;
}

void PipelineSim::restore_state(const PipelineSimState& state) {
    ca_kernel_active_ = state.ca_kernel_active;
    if (ca_kernel_active_) {
        ca_stats_ = state.stats;
        ca_regs_ = {state.f_reg, state.d_reg, state.e_reg, state.m_reg, state.w_reg};
        ca_history_head_ = 0;
        ca_history_size_ = 0;
        for (const auto& snapshot : state.cycle_history) {
            ca_history_[ca_history_head_] = snapshot;
            ca_history_head_ = (ca_history_head_ + 1) % kCaHistoryCapacity;
            if (ca_history_size_ < kCaHistoryCapacity) ++ca_history_size_;
        }
    }
}

}  // namespace simrv::pipeline
