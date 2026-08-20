#include "simrv/pipeline/InOrderPipeline.hpp"

#include <utility>

namespace simrv::pipeline {

using simrv::isa::OperationId;
using enum simrv::isa::OperationId;

namespace {

auto is_mul_op(isa::OperationId op_id) -> bool {
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

auto is_div_rem_op(isa::OperationId op_id) -> bool {
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

auto is_fp_alu_op(isa::OperationId op_id) -> bool {
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
        case FCVT_S_D:
        case FCVT_D_S:
        case FCVT_W_S:
        case FCVT_WU_S:
        case FCVT_S_W:
        case FCVT_S_WU:
        case FCVT_L_S:
        case FCVT_LU_S:
        case FCVT_S_L:
        case FCVT_S_LU:
        case FCVT_W_D:
        case FCVT_WU_D:
        case FCVT_D_W:
        case FCVT_D_WU:
        case FCVT_L_D:
        case FCVT_LU_D:
        case FCVT_D_L:
        case FCVT_D_LU:
            return true;
        default:
            return false;
    }
}

auto is_fp_div_sqrt_op(isa::OperationId op_id) -> bool {
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

}  // namespace

InOrderPipeline::InOrderPipeline(const CpuConfig& cfg) : config(cfg) {
    branch_history_table_.fill(1);  // Default: Weakly Not Taken
    btb_.resize(config.btb_entries);
}

void InOrderPipeline::reset() {
    f_reg_ = PipelineReg{};
    d_reg_ = PipelineReg{};
    e_reg_ = PipelineReg{};
    m_reg_ = PipelineReg{};
    w_reg_ = PipelineReg{};

    branch_history_table_.fill(1);
    btb_.clear();
    btb_.resize(config.btb_entries);
    ring_head_.store(0, std::memory_order_relaxed);
    gshare_history_ = 0;

    control_bubble_remaining_ = 0;
    tlb_stall_remaining_ = 0;
    icache_stall_remaining_ = 0;
    dcache_stall_remaining_ = 0;
    div_busy_cycles_remaining_ = 0;
    fdiv_busy_cycles_remaining_ = 0;

    cycle_count_ = 0;
    next_inst_id_ = 0;
    stall_cycles_ = 0;
    bubble_cycles_ = 0;
    icache_stalls_ = 0;
    dcache_stalls_ = 0;
    tlb_stalls_ = 0;
    structural_stalls_ = 0;
    data_hazard_stalls_ = 0;
    control_hazard_bubbles_ = 0;
}

void InOrderPipeline::init_execution_latency(PipelineReg& reg) {
    if (!reg.valid) return;

    if (reg.is_load) {
        reg.remaining_latency = 1;
    } else if (is_mul_op(reg.op_id)) {
        reg.remaining_latency = static_cast<int>(config.mul_latency) - 1;
    } else if (is_div_rem_op(reg.op_id)) {
        reg.remaining_latency = static_cast<int>(config.div_latency) - 1;
        div_busy_cycles_remaining_ = config.div_latency - 1;
    } else if (is_fp_div_sqrt_op(reg.op_id)) {
        reg.remaining_latency = static_cast<int>(config.fp_div_latency) - 1;
        fdiv_busy_cycles_remaining_ = config.fp_div_latency - 1;
    } else if (is_fp_alu_op(reg.op_id)) {
        reg.remaining_latency = static_cast<int>(config.fp_alu_latency) - 1;
    } else {
        reg.remaining_latency = 0;
    }
}

auto InOrderPipeline::check_stall_mem() const -> bool {
    return m_reg_.valid && m_reg_.dcache_miss && dcache_stall_remaining_ > 0;
}

auto InOrderPipeline::check_stall_ex() const -> bool {
    if (!e_reg_.valid) return false;
    if (is_div_rem_op(e_reg_.op_id)) {
        return div_busy_cycles_remaining_ > 0;
    }
    if (is_fp_div_sqrt_op(e_reg_.op_id)) {
        return fdiv_busy_cycles_remaining_ > 0;
    }
    return false;
}

auto InOrderPipeline::check_hazard_with_stage(const PipelineReg& stage_reg, bool reads_rs1,
                                              bool reads_rs2) const -> bool {
    if (!stage_reg.valid || (stage_reg.rd_mask == 0 && stage_reg.rd_fp_mask == 0)) {
        return false;
    }
    const uint32_t rs_mask = (reads_rs1 ? (1u << static_cast<uint32_t>(d_reg_.rs1)) : 0u) |
                             (reads_rs2 ? (1u << static_cast<uint32_t>(d_reg_.rs2)) : 0u);
    const uint32_t active_mask = d_reg_.is_fp_op ? stage_reg.rd_fp_mask : stage_reg.rd_mask;
    if ((active_mask & rs_mask) != 0) {
        bool forward_enabled = config.enable_forwarding;
        if (&stage_reg == &e_reg_) {
            forward_enabled = forward_enabled && config.enable_ex_forwarding;
        } else if (&stage_reg == &m_reg_) {
            forward_enabled = forward_enabled && config.enable_mem_forwarding;
        }
        if (forward_enabled) {
            return stage_reg.remaining_latency > 0;
        }
        return true;
    }
    return false;
}

auto InOrderPipeline::check_stall_id() const -> bool {
    if (!d_reg_.valid) return false;
    const uint32_t rs1_val = static_cast<uint32_t>(d_reg_.rs1);
    const uint32_t rs2_val = static_cast<uint32_t>(d_reg_.rs2);
    const uint32_t rs1_mask = (rs1_val != 0) ? (1u << rs1_val) : 0u;
    const uint32_t rs2_mask = (rs2_val != 0) ? (1u << rs2_val) : 0u;
    const uint32_t rs_mask = rs1_mask | rs2_mask;
    if (rs_mask == 0) return false;

    // Check GPR integer hazard
    if (e_reg_.valid && (e_reg_.rd_mask & rs_mask) != 0) {
        const bool fwd = config.enable_forwarding && config.enable_ex_forwarding;
        if (!fwd || e_reg_.remaining_latency > 0) {
            return true;
        }
    }
    if (m_reg_.valid && (m_reg_.rd_mask & rs_mask) != 0) {
        const bool fwd = config.enable_forwarding && config.enable_mem_forwarding;
        if (!fwd || m_reg_.remaining_latency > 0) {
            return true;
        }
    }

    // Check FPR floating-point hazard
    if (d_reg_.is_fp_op) {
        if (e_reg_.valid && (e_reg_.rd_fp_mask & rs_mask) != 0) {
            const bool fwd = config.enable_forwarding && config.enable_ex_forwarding;
            if (!fwd || e_reg_.remaining_latency > 0) {
                return true;
            }
        }
        if (m_reg_.valid && (m_reg_.rd_fp_mask & rs_mask) != 0) {
            const bool fwd = config.enable_forwarding && config.enable_mem_forwarding;
            if (!fwd || m_reg_.remaining_latency > 0) {
                return true;
            }
        }
    }

    return false;
}

auto InOrderPipeline::check_stall_if() const -> bool {
    return f_reg_.valid && ((f_reg_.tlb_miss && tlb_stall_remaining_ > 0) ||
                            (f_reg_.icache_miss && icache_stall_remaining_ > 0));
}

auto InOrderPipeline::resolve_jump_ex(BtbEntry& btb_entry, Register pc, isa::Opcode opcode,
                                      Register target_pc) -> uint32_t {
    const bool btb_hit = (config.btb_entries > 0) && btb_entry.valid && (btb_entry.pc == pc);
    if (opcode == isa::Opcode::Jalr) {
        if (btb_hit && btb_entry.target == target_pc) {
            return 0;
        }
        if (config.btb_entries > 0) {
            btb_entry.pc = pc;
            btb_entry.target = target_pc;
            btb_entry.valid = true;
        }
        return config.branch_mispredict_penalty;
    } else {
        if (btb_hit && btb_entry.target == target_pc) {
            return 0;
        }
        if (config.btb_entries > 0) {
            btb_entry.pc = pc;
            btb_entry.target = target_pc;
            btb_entry.valid = true;
        }
        return 1;
    }
}

auto InOrderPipeline::resolve_branch_ex(BtbEntry& btb_entry, Register pc, Register target_pc,
                                        bool branched) -> uint32_t {
    bool predicted_taken = false;
    uint32_t bht_idx = 0;

    switch (config.bp_type) {
        case BranchPredictorType::StaticNotTaken:
            predicted_taken = false;
            break;
        case BranchPredictorType::StaticTaken:
            predicted_taken = true;
            break;
        case BranchPredictorType::OneBitBimodal:
            bht_idx = (pc >> 1) & 0xFF;
            predicted_taken = (branch_history_table_.at(bht_idx) != 0);
            break;
        case BranchPredictorType::TwoBitBimodal:
            bht_idx = (pc >> 1) & 0xFF;
            predicted_taken = (branch_history_table_.at(bht_idx) >= 2);
            break;
        case BranchPredictorType::Gshare:
            bht_idx = ((pc >> 1) ^ gshare_history_) & 0xFF;
            predicted_taken = (branch_history_table_.at(bht_idx) >= 2);
            break;
        default:
            std::unreachable();
    }

    const bool btb_hit = (config.btb_entries > 0) && btb_entry.valid && (btb_entry.pc == pc);

    uint32_t control_bubbles = 0;
    if (predicted_taken != branched) {
        control_bubbles = config.branch_mispredict_penalty;
    } else if (predicted_taken) {
        if (btb_hit && btb_entry.target == target_pc) {
            control_bubbles = 0;
        } else {
            control_bubbles = config.branch_mispredict_penalty;
        }
    } else {
        control_bubbles = 0;
    }

    // Update predictor tables
    switch (config.bp_type) {
        case BranchPredictorType::OneBitBimodal:
            branch_history_table_.at(bht_idx) = branched ? 1 : 0;
            break;
        case BranchPredictorType::TwoBitBimodal:
        case BranchPredictorType::Gshare: {
            uint8_t counter = branch_history_table_.at(bht_idx);
            if (branched) {
                if (counter < 3) counter++;
            } else {
                if (counter > 0) counter--;
            }
            branch_history_table_.at(bht_idx) = counter;
            break;
        }
        default:
            break;
    }

    if (config.bp_type == BranchPredictorType::Gshare) {
        gshare_history_ = ((gshare_history_ << 1) | (branched ? 1 : 0)) &
                          ((1u << config.global_history_bits) - 1);
    }

    // Update BTB
    if (config.btb_entries > 0) {
        if (branched) {
            btb_entry.pc = pc;
            btb_entry.target = target_pc;
            btb_entry.valid = true;
        }
    }

    return control_bubbles;
}

auto InOrderPipeline::resolve_branches_ex() -> bool {
    if (!e_reg_.valid || e_reg_.control_resolved) {
        return false;
    }
    e_reg_.control_resolved = true;

    BtbEntry dummy{};
    BtbEntry* entry_ptr = &dummy;
    if (config.btb_entries > 0 && !btb_.empty()) {
        const uint32_t btb_idx = (e_reg_.pc >> 1) % config.btb_entries;
        entry_ptr = &btb_.at(btb_idx);
    }

    uint32_t control_bubbles = 0;
    if (e_reg_.is_jump) {
        control_bubbles = resolve_jump_ex(*entry_ptr, e_reg_.pc, e_reg_.opcode, e_reg_.target_pc);
    } else if (e_reg_.is_branch) {
        control_bubbles =
            resolve_branch_ex(*entry_ptr, e_reg_.pc, e_reg_.target_pc, e_reg_.branched);
    }

    if (control_bubbles > 0) {
        control_bubble_remaining_ = control_bubbles;
        f_reg_ = PipelineReg{};
        d_reg_ = PipelineReg{};
        return true;
    }
    return false;
}

void InOrderPipeline::update_stall_stats(bool stall_mem, bool stall_ex, bool stall_id,
                                         bool stall_if) {
    if (stall_mem) {
        dcache_stalls_++;
        stall_cycles_++;
    } else if (stall_ex) {
        structural_stalls_++;
        stall_cycles_++;
    } else if (stall_id) {
        data_hazard_stalls_++;
        stall_cycles_++;
    } else if (stall_if) {
        if (f_reg_.tlb_miss) {
            tlb_stalls_++;
        } else {
            icache_stalls_++;
        }
        stall_cycles_++;
    }
    cycle_count_++;
}

void InOrderPipeline::decrement_latencies() {
    if (e_reg_.valid && e_reg_.remaining_latency > 0) {
        e_reg_.remaining_latency--;
    }
    if (m_reg_.valid && m_reg_.remaining_latency > 0) {
        m_reg_.remaining_latency--;
    }
    if (w_reg_.valid && w_reg_.remaining_latency > 0) {
        w_reg_.remaining_latency--;
    }
}

void InOrderPipeline::stage_register_transfers(bool MEM_stalled, bool EX_stalled, bool ID_stalled,
                                               bool IF_stalled) {
    if (!MEM_stalled) {
        w_reg_ = m_reg_;
    } else {
        w_reg_ = PipelineReg{};
    }

    if (!MEM_stalled) {
        if (!EX_stalled) {
            m_reg_ = e_reg_;
            if (m_reg_.valid && m_reg_.dcache_miss) {
                dcache_stall_remaining_ = config.dcache_miss_penalty;
            }
        } else {
            m_reg_ = PipelineReg{};
        }
    }

    if (!EX_stalled) {
        if (!ID_stalled) {
            e_reg_ = d_reg_;
            init_execution_latency(e_reg_);
        } else {
            e_reg_ = PipelineReg{};
        }
    }

    if (!ID_stalled) {
        if (!IF_stalled) {
            d_reg_ = f_reg_;
        } else {
            d_reg_ = PipelineReg{};
        }
    }

    if (!IF_stalled) {
        f_reg_ = PipelineReg{};
    }
}

void InOrderPipeline::tick_pipeline() {
    if (control_bubble_remaining_ > 0) {
        control_bubble_remaining_--;
        control_hazard_bubbles_++;
        bubble_cycles_++;
        stall_cycles_++;
        cycle_count_++;

        w_reg_ = m_reg_;
        m_reg_ = e_reg_;
        e_reg_ = d_reg_;
        init_execution_latency(e_reg_);
        d_reg_ = f_reg_;
        f_reg_ = PipelineReg{};
        record_cycle_snapshot();
        return;
    }

    const bool stall_mem = check_stall_mem();
    const bool stall_ex = check_stall_ex();
    const bool stall_id = check_stall_id();
    const bool stall_if = check_stall_if();

    const bool MEM_stalled = stall_mem;
    const bool EX_stalled = MEM_stalled || stall_ex;
    const bool ID_stalled = EX_stalled || stall_id;
    const bool IF_stalled = ID_stalled || stall_if;

    update_stall_stats(stall_mem, stall_ex, stall_id, stall_if);

    if (dcache_stall_remaining_ > 0) dcache_stall_remaining_--;
    if (div_busy_cycles_remaining_ > 0) div_busy_cycles_remaining_--;
    if (fdiv_busy_cycles_remaining_ > 0) fdiv_busy_cycles_remaining_--;
    if (tlb_stall_remaining_ > 0) tlb_stall_remaining_--;
    if (icache_stall_remaining_ > 0) icache_stall_remaining_--;

    decrement_latencies();

    if (resolve_branches_ex()) {
        record_cycle_snapshot();
        return;
    }

    stage_register_transfers(MEM_stalled, EX_stalled, ID_stalled, IF_stalled);
    record_cycle_snapshot();
}

auto InOrderPipeline::step_instruction(Register pc, isa::Opcode opcode, RegId rd, RegId rs1,
                                       RegId rs2, isa::OperationId op_id, bool branched,
                                       bool is_branch, bool is_jump, bool icache_miss,
                                       bool dcache_miss, bool tlb_miss, Register target_pc)
    -> uint32_t {
    uint32_t cycles_spent = 0;

    while (true) {
        const bool IF_stalled =
            check_stall_mem() || check_stall_ex() || check_stall_id() || check_stall_if();

        if (!IF_stalled && control_bubble_remaining_ == 0) {
            if (f_reg_.valid) {
                tick_pipeline();
                cycles_spent++;
            } else {
                break;
            }
        } else {
            tick_pipeline();
            cycles_spent++;
        }
    }

    const bool is_fp = (opcode == isa::Opcode::OpFp || opcode == isa::Opcode::MAdd ||
                        opcode == isa::Opcode::MSub || opcode == isa::Opcode::NMAdd ||
                        opcode == isa::Opcode::NMSub || opcode == isa::Opcode::LoadFp);

    f_reg_.inst_id = ++next_inst_id_;
    f_reg_.pc = pc;
    f_reg_.opcode = opcode;
    f_reg_.rd = rd;
    f_reg_.rs1 = rs1;
    f_reg_.rs2 = rs2;
    f_reg_.op_id = op_id;
    f_reg_.is_fp_op = is_fp;

    f_reg_.writes_reg = (rd != static_cast<RegId>(0)) && !is_fp && (opcode != isa::Opcode::Branch) &&
                        (opcode != isa::Opcode::Store) && (opcode != isa::Opcode::StoreFp);
    f_reg_.rd_mask = f_reg_.writes_reg ? (1u << static_cast<uint32_t>(rd)) : 0u;

    f_reg_.writes_fp_reg = is_fp && (opcode != isa::Opcode::StoreFp);
    f_reg_.rd_fp_mask = f_reg_.writes_fp_reg ? (1u << static_cast<uint32_t>(rd)) : 0u;

    f_reg_.is_load = (opcode == isa::Opcode::Load) || (opcode == isa::Opcode::LoadFp);
    f_reg_.tlb_miss = tlb_miss;
    f_reg_.icache_miss = icache_miss;
    f_reg_.dcache_miss = dcache_miss;
    f_reg_.is_branch = is_branch;
    f_reg_.is_jump = is_jump;
    f_reg_.branched = branched;
    f_reg_.target_pc = target_pc;
    f_reg_.valid = true;
    f_reg_.control_resolved = false;

    // Serialization / pipeline flush bubbles for CSR updates and FENCE instructions
    if (opcode == isa::Opcode::System) {
        if (op_id == OperationId::FENCE_I || op_id == OperationId::SFENCE_VMA) {
            control_bubble_remaining_ = config.fence_flush_penalty;
        } else if (op_id == OperationId::CSRRW || op_id == OperationId::CSRRS ||
                   op_id == OperationId::CSRRC) {
            control_bubble_remaining_ = config.csr_flush_penalty;
        }
    }

    if (tlb_miss) {
        tlb_stall_remaining_ = config.tlb_miss_penalty;
    }
    if (icache_miss) {
        icache_stall_remaining_ = config.icache_miss_penalty;
    }

    tick_pipeline();
    cycles_spent++;

    return cycles_spent;
}

void InOrderPipeline::record_cycle_snapshot() {
    if (!config.record_snapshots) {
        return;
    }

    PipelineCycleSnapshot snap;
    snap.cycle = cycle_count_;

    snap.f.inst_id = f_reg_.inst_id;
    snap.f.pc = f_reg_.pc;
    snap.f.op_id = f_reg_.op_id;
    snap.f.valid = f_reg_.valid;
    snap.f.stalled = (icache_stall_remaining_ > 0 || tlb_stall_remaining_ > 0);

    snap.d.inst_id = d_reg_.inst_id;
    snap.d.pc = d_reg_.pc;
    snap.d.op_id = d_reg_.op_id;
    snap.d.valid = d_reg_.valid;
    snap.d.stalled = check_stall_id();

    snap.e.inst_id = e_reg_.inst_id;
    snap.e.pc = e_reg_.pc;
    snap.e.op_id = e_reg_.op_id;
    snap.e.valid = e_reg_.valid;
    snap.e.stalled = (div_busy_cycles_remaining_ > 0);

    snap.m.inst_id = m_reg_.inst_id;
    snap.m.pc = m_reg_.pc;
    snap.m.op_id = m_reg_.op_id;
    snap.m.valid = m_reg_.valid;
    snap.m.stalled = (dcache_stall_remaining_ > 0);

    snap.w.inst_id = w_reg_.inst_id;
    snap.w.pc = w_reg_.pc;
    snap.w.op_id = w_reg_.op_id;
    snap.w.valid = w_reg_.valid;
    snap.w.stalled = false;

    const uint64_t head = ring_head_.fetch_add(1, std::memory_order_relaxed);
    cycle_ring_buffer_[head % kHistoryCapacity] = snap;
}

auto InOrderPipeline::get_stats() const -> PipelineStats {
    return PipelineStats{.cycle_count = cycle_count_,
                         .stall_cycles = stall_cycles_,
                         .bubble_cycles = bubble_cycles_,
                         .icache_stalls = icache_stalls_,
                         .dcache_stalls = dcache_stalls_,
                         .tlb_stalls = tlb_stalls_,
                         .structural_stalls = structural_stalls_,
                         .data_hazard_stalls = data_hazard_stalls_,
                         .control_hazard_bubbles = control_hazard_bubbles_};
}

auto InOrderPipeline::get_cycle_history() const -> std::vector<PipelineCycleSnapshot> {
    std::vector<PipelineCycleSnapshot> result;
    const uint64_t head = ring_head_.load(std::memory_order_relaxed);
    const size_t count = (head < kHistoryCapacity) ? static_cast<size_t>(head) : kHistoryCapacity;
    result.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        const size_t idx = (head - count + i) % kHistoryCapacity;
        result.push_back(cycle_ring_buffer_[idx]);
    }
    return result;
}

auto InOrderPipeline::get_bht_entry(Register pc) const -> uint8_t {
    uint32_t const idx = (pc >> 1) & 0xFF;
    return branch_history_table_.at(idx);
}

auto InOrderPipeline::get_btb_target(Register pc) const -> std::pair<bool, Register> {
    if (config.btb_entries == 0 || btb_.empty()) {
        return {false, 0};
    }
    uint32_t const idx = (pc >> 1) % config.btb_entries;
    const auto& entry = btb_.at(idx);
    if (entry.valid && entry.pc == pc) {
        return {true, entry.target};
    }
    return {false, 0};
}

auto InOrderPipeline::save_state() const -> PipelineSimState {
    PipelineSimState state;
    state.f_reg = f_reg_;
    state.d_reg = d_reg_;
    state.e_reg = e_reg_;
    state.m_reg = m_reg_;
    state.w_reg = w_reg_;
    state.branch_history_table = branch_history_table_;
    state.btb = btb_;
    state.cycle_history = get_cycle_history();
    state.control_bubble_remaining = control_bubble_remaining_;
    state.tlb_stall_remaining = tlb_stall_remaining_;
    state.icache_stall_remaining = icache_stall_remaining_;
    state.dcache_stall_remaining = dcache_stall_remaining_;
    state.div_busy_cycles_remaining = div_busy_cycles_remaining_;
    state.stats = get_stats();
    state.gshare_history = gshare_history_;
    return state;
}

void InOrderPipeline::restore_state(const PipelineSimState& state) {
    f_reg_ = state.f_reg;
    d_reg_ = state.d_reg;
    e_reg_ = state.e_reg;
    m_reg_ = state.m_reg;
    w_reg_ = state.w_reg;
    branch_history_table_ = state.branch_history_table;
    btb_ = state.btb;

    ring_head_.store(0, std::memory_order_relaxed);
    for (const auto& snap : state.cycle_history) {
        const uint64_t head = ring_head_.fetch_add(1, std::memory_order_relaxed);
        cycle_ring_buffer_[head % kHistoryCapacity] = snap;
    }

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
