/**
 * @file ThreeStagePipeline.cpp
 * @brief Implementation of 3-stage embedded microcontroller pipeline.
 */
#include "simrv/pipeline/ThreeStagePipeline.hpp"

#include <algorithm>
#include <utility>

namespace simrv::pipeline {

using simrv::isa::OperationId;
using enum simrv::isa::OperationId;

namespace {

auto is_mul_op(OperationId op_id) -> bool {
    switch (op_id) {
        case MUL:
        case MULH:
        case MULHSU:
        case MULHU:
        case MULW:
            return true;
        default:
            return false;
    }
}

auto is_div_rem_op(OperationId op_id) -> bool {
    switch (op_id) {
        case DIV:
        case DIVU:
        case REM:
        case REMU:
        case DIVW:
        case DIVUW:
        case REMW:
        case REMUW:
            return true;
        default:
            return false;
    }
}

auto is_fp_alu_op(OperationId op_id) -> bool {
    switch (op_id) {
        case FADD_S:
        case FSUB_S:
        case FMUL_S:
        case FMADD_S:
        case FMSUB_S:
        case FNMADD_S:
        case FNMSUB_S:
        case FADD_D:
        case FSUB_D:
        case FMUL_D:
        case FMADD_D:
        case FMSUB_D:
        case FNMADD_D:
        case FNMSUB_D:
        case FMIN_S:
        case FMAX_S:
        case FMIN_D:
        case FMAX_D:
        case FSGNJ_S:
        case FSGNJN_S:
        case FSGNJX_S:
        case FSGNJ_D:
        case FSGNJN_D:
        case FSGNJX_D:
            return true;
        default:
            return false;
    }
}

auto is_fp_div_op(OperationId op_id) -> bool {
    switch (op_id) {
        case FDIV_S:
        case FSQRT_S:
        case FDIV_D:
        case FSQRT_D:
            return true;
        default:
            return false;
    }
}

auto is_load_instruction(isa::Opcode opcode) -> bool {
    return opcode == isa::Opcode::Load || opcode == isa::Opcode::LoadFp;
}

auto is_store_instruction(isa::Opcode opcode) -> bool {
    return opcode == isa::Opcode::Store || opcode == isa::Opcode::StoreFp;
}

auto reads_rs1_reg(isa::Opcode opcode) -> bool {
    switch (opcode) {
        case isa::Opcode::Op:
        case isa::Opcode::Op32:
        case isa::Opcode::OpImm:
        case isa::Opcode::OpImm32:
        case isa::Opcode::Load:
        case isa::Opcode::LoadFp:
        case isa::Opcode::Store:
        case isa::Opcode::StoreFp:
        case isa::Opcode::Branch:
        case isa::Opcode::Jalr:
        case isa::Opcode::System:
        case isa::Opcode::OpFp:
            return true;
        default:
            return false;
    }
}

auto reads_rs2_reg(isa::Opcode opcode) -> bool {
    switch (opcode) {
        case isa::Opcode::Op:
        case isa::Opcode::Op32:
        case isa::Opcode::Store:
        case isa::Opcode::StoreFp:
        case isa::Opcode::Branch:
        case isa::Opcode::OpFp:
            return true;
        default:
            return false;
    }
}

}  // namespace

ThreeStagePipeline::ThreeStagePipeline(const CpuConfig& cfg) : config_(cfg), btb_(cfg.btb_entries) {
    reset();
}

void ThreeStagePipeline::reset() {
    f_reg_ = PipelineReg{};
    e_reg_ = PipelineReg{};
    w_reg_ = PipelineReg{};

    control_bubble_remaining_ = 0;
    tlb_stall_remaining_ = 0;
    icache_stall_remaining_ = 0;
    dcache_stall_remaining_ = 0;
    div_busy_cycles_remaining_ = 0;

    cycle_count_ = 0;
    stall_cycles_ = 0;
    bubble_cycles_ = 0;
    icache_stalls_ = 0;
    dcache_stalls_ = 0;
    tlb_stalls_ = 0;
    structural_stalls_ = 0;
    data_hazard_stalls_ = 0;
    control_hazard_bubbles_ = 0;

    branch_history_table_.fill(1);  // Weakly not taken
    for (auto& entry : btb_) {
        entry.valid = false;
        entry.pc = 0;
        entry.target = 0;
    }
    gshare_history_ = 0;
    ring_head_ = 0;
    ring_size_ = 0;
}

void ThreeStagePipeline::init_execution_latency(PipelineReg& reg) {
    if (!reg.valid) return;
    if (is_div_rem_op(reg.op_id)) {
        reg.remaining_latency = static_cast<int>(config_.div_latency);
        div_busy_cycles_remaining_ = config_.div_latency;
    } else if (is_fp_div_op(reg.op_id)) {
        reg.remaining_latency = static_cast<int>(config_.fp_div_latency);
    } else if (is_fp_alu_op(reg.op_id)) {
        reg.remaining_latency = static_cast<int>(config_.fp_alu_latency);
    } else if (is_mul_op(reg.op_id)) {
        reg.remaining_latency = static_cast<int>(config_.mul_latency);
    } else if (is_load_instruction(reg.opcode)) {
        reg.remaining_latency = 1;
    } else {
        reg.remaining_latency = 0;
    }
}

auto ThreeStagePipeline::check_hazard_with_stage(const PipelineReg& stage_reg, bool reads_rs1,
                                                 bool reads_rs2) const -> bool {
    if (!stage_reg.valid) return false;

    if (reads_rs1 && f_reg_.rs1 != RegId::Zero) {
        if (stage_reg.writes_reg && stage_reg.rd == f_reg_.rs1) {
            if (stage_reg.is_load && stage_reg.remaining_latency > 0) return true;
            if (!config_.enable_forwarding && stage_reg.remaining_latency > 0) return true;
        }
    }
    if (reads_rs2 && f_reg_.rs2 != RegId::Zero) {
        if (stage_reg.writes_reg && stage_reg.rd == f_reg_.rs2) {
            if (stage_reg.is_load && stage_reg.remaining_latency > 0) return true;
            if (!config_.enable_forwarding && stage_reg.remaining_latency > 0) return true;
        }
    }
    return false;
}

auto ThreeStagePipeline::evaluate_branch_prediction(Register pc, Register target_pc, bool branched)
    -> bool {
    const uint32_t bht_idx = (pc >> 1) & 0xFF;
    const uint8_t state = branch_history_table_[bht_idx];
    const bool predicted_taken = (state >= 2);

    // Update BHT counter (2-bit saturating)
    if (branched) {
        if (branch_history_table_[bht_idx] < 3) branch_history_table_[bht_idx]++;
    } else {
        if (branch_history_table_[bht_idx] > 0) branch_history_table_[bht_idx]--;
    }

    if (config_.btb_entries > 0 && !btb_.empty()) {
        const uint32_t btb_idx = (pc >> 1) % config_.btb_entries;
        if (branched) {
            btb_[btb_idx] = {pc, target_pc, true};
        }
    }

    return predicted_taken == branched;
}

auto ThreeStagePipeline::step_instruction(Register pc, isa::Opcode opcode, RegId rd, RegId rs1,
                                          RegId rs2, isa::OperationId op_id, bool branched,
                                          bool is_branch, bool is_jump, bool icache_miss,
                                          bool dcache_miss, bool tlb_miss, Register target_pc)
    -> uint32_t {
    uint32_t cycle_latency = 1;

    // Handle TLB / I-Cache misses on Fetch
    if (tlb_miss) {
        tlb_stall_remaining_ = config_.tlb_miss_penalty;
        tlb_stalls_ += config_.tlb_miss_penalty;
        stall_cycles_ += config_.tlb_miss_penalty;
        cycle_count_ += config_.tlb_miss_penalty;
        cycle_latency += config_.tlb_miss_penalty;
    } else if (icache_miss) {
        icache_stall_remaining_ = config_.icache_miss_penalty;
        icache_stalls_ += config_.icache_miss_penalty;
        stall_cycles_ += config_.icache_miss_penalty;
        cycle_count_ += config_.icache_miss_penalty;
        cycle_latency += config_.icache_miss_penalty;
    }

    // Populate newly fetched instruction into F stage
    f_reg_.pc = pc;
    f_reg_.opcode = opcode;
    f_reg_.rd = rd;
    f_reg_.rs1 = rs1;
    f_reg_.rs2 = rs2;
    f_reg_.op_id = op_id;
    f_reg_.writes_reg =
        (rd != RegId::Zero && opcode != isa::Opcode::Branch && opcode != isa::Opcode::Store);
    f_reg_.is_load = is_load_instruction(opcode);
    f_reg_.valid = true;
    f_reg_.is_branch = is_branch;
    f_reg_.is_jump = is_jump;
    f_reg_.branched = branched;
    f_reg_.target_pc = target_pc;

    // Check data hazard (Load-use or multi-cycle ALU in execution)
    const bool reads_rs1 = reads_rs1_reg(opcode);
    const bool reads_rs2 = reads_rs2_reg(opcode);

    bool hazard_stall = check_hazard_with_stage(e_reg_, reads_rs1, reads_rs2);
    if (hazard_stall) {
        data_hazard_stalls_++;
        stall_cycles_++;
        cycle_count_++;
        cycle_latency++;
    }

    // Check branch/jump misprediction penalty in 3-stage pipeline (1 cycle penalty)
    if (is_branch) {
        bool correct = evaluate_branch_prediction(pc, target_pc, branched);
        if (!correct) {
            control_bubble_remaining_ = 1;
            control_hazard_bubbles_++;
            bubble_cycles_++;
            cycle_count_++;
            cycle_latency++;
        }
    } else if (is_jump && opcode == isa::Opcode::Jalr) {
        // JALR target resolved in ID/EX -> 1 bubble
        control_bubble_remaining_ = 1;
        control_hazard_bubbles_++;
        bubble_cycles_++;
        cycle_count_++;
        cycle_latency++;
    }

    // Handle D-Cache miss on Memory stage
    if (dcache_miss && (is_load_instruction(opcode) || is_store_instruction(opcode))) {
        dcache_stall_remaining_ = config_.dcache_miss_penalty;
        dcache_stalls_ += config_.dcache_miss_penalty;
        stall_cycles_ += config_.dcache_miss_penalty;
        cycle_count_ += config_.dcache_miss_penalty;
        cycle_latency += config_.dcache_miss_penalty;
    }

    // Multi-cycle execution stalls (e.g. DIV / FP-DIV)
    if (is_div_rem_op(op_id)) {
        const uint32_t div_stall = (config_.div_latency > 1) ? (config_.div_latency - 1) : 0;
        structural_stalls_ += div_stall;
        stall_cycles_ += div_stall;
        cycle_count_ += div_stall;
        cycle_latency += div_stall;
    }

    // Advance 3-stage pipeline registers: W <- E, E <- F
    w_reg_ = e_reg_;
    e_reg_ = f_reg_;
    init_execution_latency(e_reg_);

    cycle_count_++;
    push_cycle_snapshot(false, hazard_stall, false);

    return cycle_latency;
}

void ThreeStagePipeline::push_cycle_snapshot(bool f_stalled, bool e_stalled, bool w_stalled) {
    if (!config_.record_snapshots) return;

    PipelineCycleSnapshot snap{};
    snap.cycle = cycle_count_;

    snap.f = {f_reg_.inst_id, f_reg_.pc, f_reg_.op_id, f_reg_.valid, f_stalled};
    snap.d = {e_reg_.inst_id, e_reg_.pc, e_reg_.op_id, e_reg_.valid, e_stalled};
    snap.e = {e_reg_.inst_id, e_reg_.pc, e_reg_.op_id, e_reg_.valid, e_stalled};
    snap.m = {w_reg_.inst_id, w_reg_.pc, w_reg_.op_id, w_reg_.valid, w_stalled};
    snap.w = {w_reg_.inst_id, w_reg_.pc, w_reg_.op_id, w_reg_.valid, w_stalled};

    cycle_ring_buffer_[ring_head_] = snap;
    ring_head_ = (ring_head_ + 1) % kHistoryCapacity;
    if (ring_size_ < kHistoryCapacity) ring_size_++;
}

auto ThreeStagePipeline::get_stats() const -> PipelineStats {
    PipelineStats s{};
    s.cycle_count = cycle_count_;
    s.stall_cycles = stall_cycles_;
    s.bubble_cycles = bubble_cycles_;
    s.icache_stalls = icache_stalls_;
    s.dcache_stalls = dcache_stalls_;
    s.tlb_stalls = tlb_stalls_;
    s.structural_stalls = structural_stalls_;
    s.data_hazard_stalls = data_hazard_stalls_;
    s.control_hazard_bubbles = control_hazard_bubbles_;
    return s;
}

auto ThreeStagePipeline::get_cycle_history() const -> std::vector<PipelineCycleSnapshot> {
    std::vector<PipelineCycleSnapshot> out;
    out.reserve(ring_size_);
    for (size_t i = 0; i < ring_size_; ++i) {
        size_t idx = (ring_head_ + kHistoryCapacity - ring_size_ + i) % kHistoryCapacity;
        out.push_back(cycle_ring_buffer_[idx]);
    }
    return out;
}

auto ThreeStagePipeline::get_bht_entry(Register pc) const -> uint8_t {
    return branch_history_table_[(pc >> 1) & 0xFF];
}

auto ThreeStagePipeline::get_btb_target(Register pc) const -> std::pair<bool, Register> {
    if (config_.btb_entries == 0 || btb_.empty()) return {false, 0};
    const uint32_t idx = (pc >> 1) % config_.btb_entries;
    if (btb_[idx].valid && btb_[idx].pc == pc) {
        return {true, btb_[idx].target};
    }
    return {false, 0};
}

auto ThreeStagePipeline::save_state() const -> PipelineSimState {
    PipelineSimState s{};
    s.f_reg = f_reg_;
    s.d_reg = e_reg_;
    s.e_reg = e_reg_;
    s.m_reg = w_reg_;
    s.w_reg = w_reg_;
    s.branch_history_table = branch_history_table_;
    s.btb = btb_;
    s.cycle_history = get_cycle_history();
    s.control_bubble_remaining = control_bubble_remaining_;
    s.tlb_stall_remaining = tlb_stall_remaining_;
    s.icache_stall_remaining = icache_stall_remaining_;
    s.dcache_stall_remaining = dcache_stall_remaining_;
    s.div_busy_cycles_remaining = div_busy_cycles_remaining_;
    s.stats = get_stats();
    s.gshare_history = gshare_history_;
    return s;
}

void ThreeStagePipeline::restore_state(const PipelineSimState& state) {
    f_reg_ = state.f_reg;
    e_reg_ = state.e_reg;
    w_reg_ = state.w_reg;
    branch_history_table_ = state.branch_history_table;
    btb_ = state.btb;
    control_bubble_remaining_ = state.control_bubble_remaining;
    tlb_stall_remaining_ = state.tlb_stall_remaining;
    icache_stall_remaining_ = state.icache_stall_remaining;
    dcache_stall_remaining_ = state.dcache_stall_remaining;
    div_busy_cycles_remaining_ = state.div_busy_cycles_remaining;
    cycle_count_ = state.stats.cycle_count;
    stall_cycles_ = state.stats.stall_cycles;
    bubble_cycles_ = state.stats.bubble_cycles;
    icache_stalls_ = state.stats.icache_stalls;
    dcache_stalls_ = state.stats.dcache_stalls;
    tlb_stalls_ = state.stats.tlb_stalls;
    structural_stalls_ = state.stats.structural_stalls;
    data_hazard_stalls_ = state.stats.data_hazard_stalls;
    control_hazard_bubbles_ = state.stats.control_hazard_bubbles;
    gshare_history_ = state.gshare_history;
}

}  // namespace simrv::pipeline
