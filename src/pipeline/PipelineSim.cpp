#include "simrv/pipeline/PipelineSim.hpp"
#include <algorithm>

namespace simrv::pipeline {

namespace {

auto is_mul_op(OperationId op_id) -> bool {
    switch (op_id) {
        case OperationId::MUL:
        case OperationId::MULH:
        case OperationId::MULHSU:
        case OperationId::MULHU:
        case OperationId::MULW:
            return true;
        default:
            return false;
    }
}

auto is_div_rem_op(OperationId op_id) -> bool {
    switch (op_id) {
        case OperationId::DIV:
        case OperationId::DIVU:
        case OperationId::REM:
        case OperationId::REMU:
        case OperationId::DIVW:
        case OperationId::DIVUW:
        case OperationId::REMW:
        case OperationId::REMUW:
            return true;
        default:
            return false;
    }
}

} // namespace

PipelineSim::PipelineSim() {
    branch_history_table_.fill(1); // Default: Weakly Not Taken
    btb_.fill(BtbEntry{});
}

void PipelineSim::reset() {
    f_reg_ = PipelineReg{};
    d_reg_ = PipelineReg{};
    e_reg_ = PipelineReg{};
    m_reg_ = PipelineReg{};
    w_reg_ = PipelineReg{};

    branch_history_table_.fill(1);
    btb_.fill(BtbEntry{});

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
}

void PipelineSim::init_execution_latency(PipelineReg& reg) {
    if (!reg.valid) return;

    if (reg.is_load) {
        reg.remaining_latency = 1;
    } else if (is_mul_op(reg.op_id)) {
        reg.remaining_latency = static_cast<int>(config.mul_latency) - 1;
    } else if (is_div_rem_op(reg.op_id)) {
        reg.remaining_latency = static_cast<int>(config.div_latency) - 1;
        div_busy_cycles_remaining_ = config.div_latency - 1;
    } else {
        reg.remaining_latency = 0;
    }
}

auto PipelineSim::check_stall_mem() const -> bool {
    return m_reg_.valid && m_reg_.dcache_miss && dcache_stall_remaining_ > 0;
}

auto PipelineSim::check_stall_ex() const -> bool {
    if (!e_reg_.valid) return false;
    if (e_reg_.op_id == OperationId::DIV || e_reg_.op_id == OperationId::DIVU || 
        e_reg_.op_id == OperationId::REM || e_reg_.op_id == OperationId::REMU ||
        e_reg_.op_id == OperationId::DIVW || e_reg_.op_id == OperationId::DIVUW ||
        e_reg_.op_id == OperationId::REMW || e_reg_.op_id == OperationId::REMUW) {
        return div_busy_cycles_remaining_ > 0;
    }
    return false;
}

auto PipelineSim::check_hazard_with_stage(const PipelineReg& stage_reg, bool reads_rs1, bool reads_rs2) const -> bool {
    if (!stage_reg.valid || !stage_reg.writes_reg || stage_reg.rd == static_cast<RegId>(0)) {
        return false;
    }
    if ((reads_rs1 && d_reg_.rs1 == stage_reg.rd) || (reads_rs2 && d_reg_.rs2 == stage_reg.rd)) {
        return stage_reg.remaining_latency > 0;
    }
    return false;
}

auto PipelineSim::check_stall_id() const -> bool {
    if (!d_reg_.valid) return false;
    const bool reads_rs1 = (d_reg_.rs1 != static_cast<RegId>(0));
    const bool reads_rs2 = (d_reg_.rs2 != static_cast<RegId>(0));
    if (!reads_rs1 && !reads_rs2) return false;

    return check_hazard_with_stage(e_reg_, reads_rs1, reads_rs2) ||
           check_hazard_with_stage(m_reg_, reads_rs1, reads_rs2);
}

auto PipelineSim::check_stall_if() const -> bool {
    return f_reg_.valid && 
           ((f_reg_.tlb_miss && tlb_stall_remaining_ > 0) || 
            (f_reg_.icache_miss && icache_stall_remaining_ > 0));
}

auto PipelineSim::resolve_jump_ex(BtbEntry& btb_entry, Register pc, Opcode opcode, Register target_pc) -> uint32_t {
    const bool btb_hit = btb_entry.valid && (btb_entry.pc == pc);
    if (opcode == Opcode::Jalr) {
        if (btb_hit && btb_entry.target == target_pc) {
            return 0;
        }
        btb_entry.pc = pc;
        btb_entry.target = target_pc;
        btb_entry.valid = true;
        return config.branch_mispredict_penalty;
    } else {
        if (btb_hit && btb_entry.target == target_pc) {
            return 0;
        }
        btb_entry.pc = pc;
        btb_entry.target = target_pc;
        btb_entry.valid = true;
        return 1;
    }
}

auto PipelineSim::resolve_branch_ex(BtbEntry& btb_entry, Register pc, Register target_pc, bool branched) -> uint32_t {
    const uint32_t bht_idx = (pc >> 1) & 0xFF;
    const auto bht_idx_sz = static_cast<std::size_t>(bht_idx);
    uint8_t counter = branch_history_table_.at(bht_idx_sz);
    const bool predicted_taken = (counter >= 2);
    const bool btb_hit = btb_entry.valid && (btb_entry.pc == pc);

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

    if (branched) {
        if (counter < 3) counter++;
        btb_entry.pc = pc;
        btb_entry.target = target_pc;
        btb_entry.valid = true;
    } else {
        if (counter > 0) counter--;
    }
    branch_history_table_.at(bht_idx_sz) = counter;

    return control_bubbles;
}

auto PipelineSim::resolve_branches_ex() -> bool {
    if (!e_reg_.valid || e_reg_.control_resolved) {
        return false;
    }
    e_reg_.control_resolved = true;
    
    const uint32_t btb_idx = (e_reg_.pc >> 1) & 0x7F;
    auto& btb_entry = btb_.at(btb_idx);

    uint32_t control_bubbles = 0;
    if (e_reg_.is_jump) {
        control_bubbles = resolve_jump_ex(btb_entry, e_reg_.pc, e_reg_.opcode, e_reg_.target_pc);
    } else if (e_reg_.is_branch) {
        control_bubbles = resolve_branch_ex(btb_entry, e_reg_.pc, e_reg_.target_pc, e_reg_.branched);
    }

    if (control_bubbles > 0) {
        control_bubble_remaining_ = control_bubbles;
        f_reg_ = PipelineReg{};
        d_reg_ = PipelineReg{};
        return true;
    }
    return false;
}

void PipelineSim::update_stall_stats(bool stall_mem, bool stall_ex, bool stall_id, bool stall_if) {
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

void PipelineSim::decrement_latencies() {
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

void PipelineSim::stage_register_transfers(bool MEM_stalled, bool EX_stalled, bool ID_stalled, bool IF_stalled) {
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

void PipelineSim::tick_pipeline() {
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
    if (tlb_stall_remaining_ > 0) tlb_stall_remaining_--;
    if (icache_stall_remaining_ > 0) icache_stall_remaining_--;

    decrement_latencies();

    if (resolve_branches_ex()) {
        return;
    }

    stage_register_transfers(MEM_stalled, EX_stalled, ID_stalled, IF_stalled);
}

auto PipelineSim::step_instruction(Register pc, Opcode opcode, RegId rd, RegId rs1, RegId rs2,
                                  OperationId op_id, bool branched, bool is_branch, bool is_jump,
                                  bool icache_miss, bool dcache_miss, bool tlb_miss, Register target_pc) -> uint32_t {
    uint32_t cycles_spent = 0;

    while (true) {
        const bool IF_stalled = check_stall_mem() || check_stall_ex() || check_stall_id() || check_stall_if();

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

    f_reg_.pc = pc;
    f_reg_.opcode = opcode;
    f_reg_.rd = rd;
    f_reg_.rs1 = rs1;
    f_reg_.rs2 = rs2;
    f_reg_.op_id = op_id;
    f_reg_.writes_reg = (rd != static_cast<RegId>(0)) && 
                        (opcode != Opcode::Branch) && 
                        (opcode != Opcode::Store) && 
                        (opcode != Opcode::StoreFp);
    f_reg_.is_load = (opcode == Opcode::Load) || (opcode == Opcode::LoadFp);
    f_reg_.tlb_miss = tlb_miss;
    f_reg_.icache_miss = icache_miss;
    f_reg_.dcache_miss = dcache_miss;
    f_reg_.is_branch = is_branch;
    f_reg_.is_jump = is_jump;
    f_reg_.branched = branched;
    f_reg_.target_pc = target_pc;
    f_reg_.valid = true;
    f_reg_.control_resolved = false;

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

} // namespace simrv::pipeline
