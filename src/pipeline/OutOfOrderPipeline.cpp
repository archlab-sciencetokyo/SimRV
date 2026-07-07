#include "simrv/pipeline/OutOfOrderPipeline.hpp"
#include "simrv/isa/Common.hpp"

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

} // namespace

OutOfOrderPipeline::OutOfOrderPipeline(const CpuConfig& cfg) : config(cfg) {
    rob_.resize(config.rob_size);
    rs_.resize(config.rs_size);
    branch_history_table_.fill(1);
    btb_.resize(config.btb_entries);
    reset();
}

void OutOfOrderPipeline::reset() {
    rob_.assign(config.rob_size, RobEntry{});
    rs_.assign(config.rs_size, RsEntry{});
    rat_gpr_.fill(-1);
    rat_fpr_.fill(-1);
    rob_head_ = 0;
    rob_tail_ = 0;
    
    branch_history_table_.fill(1);
    btb_.clear();
    btb_.resize(config.btb_entries);
    {
        std::scoped_lock lock(history_mutex_);
        cycle_history_.clear();
    }
    gshare_history_ = 0;

    ooo_fetch_stall_remaining_ = 0;
    tlb_stall_remaining_ = 0;
    icache_stall_remaining_ = 0;
    fetch_queue_.clear();

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

auto OutOfOrderPipeline::count_rob_in_flight() const -> size_t {
    size_t count = 0;
    for (const auto& entry : rob_) {
        if (entry.valid) count++;
    }
    return count;
}

auto OutOfOrderPipeline::count_rs_in_flight() const -> size_t {
    size_t count = 0;
    for (const auto& entry : rs_) {
        if (entry.busy) count++;
    }
    return count;
}

auto OutOfOrderPipeline::resolve_jump_ex(BtbEntry& btb_entry, Register pc, isa::Opcode opcode, Register target_pc) -> uint32_t {
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

auto OutOfOrderPipeline::resolve_branch_ex(BtbEntry& btb_entry, Register pc, Register target_pc, bool branched) -> uint32_t {
    bool predicted_taken = false;
    uint32_t bht_idx = 0;
    
    if (config.bp_type == BranchPredictorType::StaticNotTaken) {
        predicted_taken = false;
    } else if (config.bp_type == BranchPredictorType::StaticTaken) {
        predicted_taken = true;
    } else if (config.bp_type == BranchPredictorType::OneBitBimodal) {
        bht_idx = (pc >> 1) & 0xFF;
        predicted_taken = (branch_history_table_.at(bht_idx) != 0);
    } else if (config.bp_type == BranchPredictorType::TwoBitBimodal) {
        bht_idx = (pc >> 1) & 0xFF;
        predicted_taken = (branch_history_table_.at(bht_idx) >= 2);
    } else if (config.bp_type == BranchPredictorType::Gshare) {
        bht_idx = ((pc >> 1) ^ gshare_history_) & 0xFF;
        predicted_taken = (branch_history_table_.at(bht_idx) >= 2);
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
    if (config.bp_type == BranchPredictorType::OneBitBimodal) {
        branch_history_table_.at(bht_idx) = branched ? 1 : 0;
    } else if (config.bp_type == BranchPredictorType::TwoBitBimodal || config.bp_type == BranchPredictorType::Gshare) {
        uint8_t counter = branch_history_table_.at(bht_idx);
        if (branched) {
            if (counter < 3) counter++;
        } else {
            if (counter > 0) counter--;
        }
        branch_history_table_.at(bht_idx) = counter;
    }

    if (config.bp_type == BranchPredictorType::Gshare) {
        gshare_history_ = ((gshare_history_ << 1) | (branched ? 1 : 0)) & ((1u << config.global_history_bits) - 1);
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

void OutOfOrderPipeline::tick_pipeline_ooo() {
    cycle_count_++;

    // 1. Advance Execution in Functional Units
    for (auto& entry : rs_) {
        if (entry.busy && entry.executing) {
            if (entry.remaining_latency > 0) {
                entry.remaining_latency--;
            }
            if (entry.is_load_store && entry.memory_stall_remaining > 0) {
                entry.memory_stall_remaining--;
            }
            
            // On completion, broadcast tag to waiting instructions on Common Data Bus (CDB)
            if (entry.remaining_latency == 0 && (!entry.is_load_store || entry.memory_stall_remaining == 0)) {
                const int32_t tag = entry.rob_tag;
                
                // CDB Broadcast
                for (auto& other : rs_) {
                    if (other.busy && !other.executing) {
                        if (other.src1_rob_tag == tag) {
                            other.src1_ready = true;
                            other.src1_rob_tag = -1;
                        }
                        if (other.src2_rob_tag == tag) {
                            other.src2_ready = true;
                            other.src2_rob_tag = -1;
                        }
                    }
                }
                
                rob_[tag].is_ready = true;
                entry.busy = false;
                entry.executing = false;
            }
        }
    }

    // 2. Issue Stage
    uint32_t issued_count = 0;
    // Check if div is busy
    bool div_busy = false;
    for (const auto& entry : rs_) {
        if (entry.busy && entry.executing && is_div_rem_op(entry.op_id)) {
            div_busy = true;
            break;
        }
    }

    for (auto& entry : rs_) {
        if (issued_count >= config.ooo_issue_width) break;
        
        if (entry.busy && !entry.executing && entry.src1_ready && entry.src2_ready) {
            // Check structural hazard (non-pipelined divider)
            if (is_div_rem_op(entry.op_id) && div_busy) {
                continue; // Divider busy, cannot issue
            }
            
            // Issue instruction
            entry.executing = true;
            issued_count++;
            
            // Initialize latency
            if (is_mul_op(entry.op_id)) {
                entry.remaining_latency = config.mul_latency;
            } else if (is_div_rem_op(entry.op_id)) {
                entry.remaining_latency = config.div_latency;
                div_busy = true; // Mark divider busy
            } else {
                entry.remaining_latency = 1;
            }
        }
    }

    // 3. Commit / Retire Stage (In-order from head of ROB)
    uint32_t committed_count = 0;
    while (committed_count < config.ooo_commit_width) {
        if (count_rob_in_flight() == 0) break;
        
        const uint32_t head = rob_head_;
        if (!rob_[head].valid) break;
        
        if (!rob_[head].is_ready) {
            // Head-of-line blocking
            data_hazard_stalls_++;
            stall_cycles_++;
            break; 
        }
        
        // Handle branch misprediction recovery
        if (rob_[head].is_mispredicted) {
            control_hazard_bubbles_++;
            bubble_cycles_ += config.branch_mispredict_penalty;
            stall_cycles_ += config.branch_mispredict_penalty;
            cycle_count_ += config.branch_mispredict_penalty;
            
            // Pipeline Flush
            rs_.assign(config.rs_size, RsEntry{});
            rob_.assign(config.rob_size, RobEntry{});
            rat_gpr_.fill(-1);
            rat_fpr_.fill(-1);
            rob_head_ = 0;
            rob_tail_ = 0;
            fetch_queue_.clear();
            break; // Stop committing this cycle after flush
        }
        
        // Normal Commit
        const auto opcode = rob_[head].opcode;
        const auto op_id = rob_[head].op_id;
        const auto rd = rob_[head].rd;
        
        if (rd != static_cast<RegId>(0)) {
            if (simrv::isa::is_destination_fp(opcode, op_id)) {
                if (rat_fpr_[std::to_underlying(rd)] == static_cast<int32_t>(head)) {
                    rat_fpr_[std::to_underlying(rd)] = -1;
                }
            } else {
                if (rat_gpr_[std::to_underlying(rd)] == static_cast<int32_t>(head)) {
                    rat_gpr_[std::to_underlying(rd)] = -1;
                }
            }
        }
        
        rob_[head].valid = false;
        rob_head_ = (rob_head_ + 1) % config.rob_size;
        committed_count++;
    }

    // Decrement fetch/tlb/icache stalls if active
    if (ooo_fetch_stall_remaining_ > 0) ooo_fetch_stall_remaining_--;
    if (tlb_stall_remaining_ > 0) tlb_stall_remaining_--;
    if (icache_stall_remaining_ > 0) icache_stall_remaining_--;

    record_cycle_snapshot_ooo();
}

auto OutOfOrderPipeline::step_instruction(Register pc, isa::Opcode opcode, RegId rd, RegId rs1, RegId rs2,
                                         isa::OperationId op_id, bool branched, bool is_branch, bool is_jump,
                                         bool icache_miss, bool dcache_miss, bool tlb_miss, Register target_pc) -> uint32_t {
    uint32_t cycles_spent = 0;

    // Simulate fetch buffer/queue backpressure
    while (true) {
        const bool rob_full = (count_rob_in_flight() >= config.rob_size);
        const bool rs_full = (count_rs_in_flight() >= config.rs_size);
        const bool fetch_stalled = (tlb_stall_remaining_ > 0 || icache_stall_remaining_ > 0 || ooo_fetch_stall_remaining_ > 0);
        
        if (rob_full || rs_full || fetch_stalled) {
            if (rob_full) structural_stalls_++;
            tick_pipeline_ooo();
            cycles_spent++;
        } else {
            break;
        }
    }

    // Allocate ROB & RS
    const uint32_t tag = rob_tail_;
    rob_tail_ = (rob_tail_ + 1) % config.rob_size;

    rob_[tag] = RobEntry{
        .pc = pc,
        .opcode = opcode,
        .rd = rd,
        .op_id = op_id,
        .is_ready = false,
        .is_mispredicted = false,
        .valid = true
    };

    // Allocate RS
    int rs_index = -1;
    for (size_t i = 0; i < rs_.size(); i++) {
        if (!rs_[i].busy) {
            rs_index = static_cast<int>(i);
            break;
        }
    }

    if (rs_index != -1) {
        auto& entry = rs_[rs_index];
        entry.busy = true;
        entry.opcode = opcode;
        entry.op_id = op_id;
        entry.rd = rd;
        entry.rob_tag = static_cast<int32_t>(tag);
        entry.executing = false;
        entry.is_load_store = (opcode == isa::Opcode::Load) || (opcode == isa::Opcode::LoadFp) ||
                              (opcode == isa::Opcode::Store) || (opcode == isa::Opcode::StoreFp);
        
        // Handle memory stall penalties
        if (entry.is_load_store) {
            if (dcache_miss) {
                entry.memory_stall_remaining = config.dcache_miss_penalty;
                dcache_stalls_++;
            } else if (tlb_miss) {
                entry.memory_stall_remaining = config.tlb_miss_penalty;
                tlb_stalls_++;
            } else {
                entry.memory_stall_remaining = 0;
            }
        }

        // Rename & Dependency Check for Source 1
        if (rs1 == RegId::Zero) {
            entry.src1_ready = true;
            entry.src1_rob_tag = -1;
        } else {
            const bool fp1 = simrv::isa::is_rs1_fp(opcode, op_id);
            const int32_t producer = fp1 ? rat_fpr_[std::to_underlying(rs1)] : rat_gpr_[std::to_underlying(rs1)];
            if (producer != -1 && !rob_[producer].is_ready) {
                entry.src1_ready = false;
                entry.src1_rob_tag = producer;
            } else {
                entry.src1_ready = true;
                entry.src1_rob_tag = -1;
            }
        }

        // Rename & Dependency Check for Source 2
        if (rs2 == RegId::Zero) {
            entry.src2_ready = true;
            entry.src2_rob_tag = -1;
        } else {
            const bool fp2 = simrv::isa::is_rs2_fp(opcode, op_id);
            const int32_t producer = fp2 ? rat_fpr_[std::to_underlying(rs2)] : rat_gpr_[std::to_underlying(rs2)];
            if (producer != -1 && !rob_[producer].is_ready) {
                entry.src2_ready = false;
                entry.src2_rob_tag = producer;
            } else {
                entry.src2_ready = true;
                entry.src2_rob_tag = -1;
            }
        }

        // Destination Rename Update
        if (rd != RegId::Zero) {
            const bool writes = (rd != static_cast<RegId>(0)) && 
                                (opcode != isa::Opcode::Branch) && 
                                (opcode != isa::Opcode::Store) && 
                                (opcode != isa::Opcode::StoreFp);
            if (writes) {
                if (simrv::isa::is_destination_fp(opcode, op_id)) {
                    rat_fpr_[std::to_underlying(rd)] = static_cast<int32_t>(tag);
                } else {
                    rat_gpr_[std::to_underlying(rd)] = static_cast<int32_t>(tag);
                }
            }
        }
    }

    // Evaluate branch misprediction at dispatch
    if (is_branch || is_jump) {
        BtbEntry dummy{};
        BtbEntry* entry_ptr = &dummy;
        if (config.btb_entries > 0 && !btb_.empty()) {
            const uint32_t btb_idx = (pc >> 1) % config.btb_entries;
            entry_ptr = &btb_.at(btb_idx);
        }

        uint32_t control_bubbles = 0;
        if (is_jump) {
            control_bubbles = resolve_jump_ex(*entry_ptr, pc, opcode, target_pc);
        } else {
            control_bubbles = resolve_branch_ex(*entry_ptr, pc, target_pc, branched);
        }

        if (control_bubbles > 0) {
            rob_[tag].is_mispredicted = true;
        }
    }

    // Handle Fetch stalling penalties
    if (tlb_miss) {
        tlb_stall_remaining_ = config.tlb_miss_penalty;
        tlb_stalls_++;
    }
    if (icache_miss) {
        icache_stall_remaining_ = config.icache_miss_penalty;
        icache_stalls_++;
    }

    tick_pipeline_ooo();
    cycles_spent++;

    return cycles_spent;
}

// Stage mapping getters for TUI representation
auto OutOfOrderPipeline::f_reg() const -> PipelineReg {
    PipelineReg reg{};
    if (count_rob_in_flight() > 0) {
        // Fetch represents the tail of the ROB (newest dispatch)
        const uint32_t idx = (rob_tail_ == 0 ? config.rob_size : rob_tail_) - 1;
        if (rob_[idx].valid) {
            reg.pc = rob_[idx].pc;
            reg.opcode = rob_[idx].opcode;
            reg.op_id = rob_[idx].op_id;
            reg.valid = true;
        }
    }
    return reg;
}

auto OutOfOrderPipeline::d_reg() const -> PipelineReg {
    PipelineReg reg{};
    // Decode represents the oldest instruction waiting in the RS
    for (const auto& entry : rs_) {
        if (entry.busy && !entry.executing) {
            reg.pc = rob_[entry.rob_tag].pc;
            reg.opcode = entry.opcode;
            reg.op_id = entry.op_id;
            reg.valid = true;
            break;
        }
    }
    return reg;
}

auto OutOfOrderPipeline::e_reg() const -> PipelineReg {
    PipelineReg reg{};
    // Execute represents the oldest arithmetic instruction currently executing
    for (const auto& entry : rs_) {
        if (entry.busy && entry.executing && !entry.is_load_store) {
            reg.pc = rob_[entry.rob_tag].pc;
            reg.opcode = entry.opcode;
            reg.op_id = entry.op_id;
            reg.valid = true;
            break;
        }
    }
    return reg;
}

auto OutOfOrderPipeline::m_reg() const -> PipelineReg {
    PipelineReg reg{};
    // Memory represents the oldest load/store instruction executing in the LSU
    for (const auto& entry : rs_) {
        if (entry.busy && entry.executing && entry.is_load_store) {
            reg.pc = rob_[entry.rob_tag].pc;
            reg.opcode = entry.opcode;
            reg.op_id = entry.op_id;
            reg.valid = true;
            break;
        }
    }
    return reg;
}

auto OutOfOrderPipeline::w_reg() const -> PipelineReg {
    PipelineReg reg{};
    // Writeback represents the head of the ROB (oldest committing)
    const uint32_t head = rob_head_;
    if (rob_[head].valid) {
        reg.pc = rob_[head].pc;
        reg.opcode = rob_[head].opcode;
        reg.op_id = rob_[head].op_id;
        reg.valid = true;
    }
    return reg;
}

auto OutOfOrderPipeline::div_busy_cycles_remaining() const -> uint32_t {
    uint32_t max_cycles = 0;
    for (const auto& entry : rs_) {
        if (entry.busy && entry.executing && is_div_rem_op(entry.op_id)) {
            max_cycles = std::max(max_cycles, entry.remaining_latency);
        }
    }
    return max_cycles;
}

auto OutOfOrderPipeline::dcache_stall_remaining() const -> uint32_t {
    uint32_t max_cycles = 0;
    for (const auto& entry : rs_) {
        if (entry.busy && entry.executing && entry.is_load_store) {
            max_cycles = std::max(max_cycles, entry.memory_stall_remaining);
        }
    }
    return max_cycles;
}

auto OutOfOrderPipeline::get_stats() const -> PipelineStats {
    return PipelineStats{
        .cycle_count = cycle_count_,
        .stall_cycles = stall_cycles_,
        .bubble_cycles = bubble_cycles_,
        .icache_stalls = icache_stalls_,
        .dcache_stalls = dcache_stalls_,
        .tlb_stalls = tlb_stalls_,
        .structural_stalls = structural_stalls_,
        .data_hazard_stalls = data_hazard_stalls_,
        .control_hazard_bubbles = control_hazard_bubbles_
    };
}

auto OutOfOrderPipeline::get_cycle_history() const -> std::vector<PipelineCycleSnapshot> {
    std::scoped_lock lock(history_mutex_);
    return {cycle_history_.begin(), cycle_history_.end()};
}

auto OutOfOrderPipeline::get_bht_entry(Register pc) const -> uint8_t {
    const uint32_t idx = (pc >> 1) & 0xFF;
    return branch_history_table_.at(idx);
}

auto OutOfOrderPipeline::get_btb_target(Register pc) const -> std::pair<bool, Register> {
    if (config.btb_entries == 0 || btb_.empty()) {
        return {false, 0};
    }
    const uint32_t idx = (pc >> 1) % config.btb_entries;
    const auto& entry = btb_.at(idx);
    if (entry.valid && entry.pc == pc) {
        return {true, entry.target};
    }
    return {false, 0};
}

void OutOfOrderPipeline::record_cycle_snapshot_ooo() {
    PipelineCycleSnapshot snap;
    snap.cycle = cycle_count_;

    snap.f = { .pc = f_reg().pc, .op_id = f_reg().op_id, .valid = f_reg().valid, .stalled = (ooo_fetch_stall_remaining_ > 0) };
    snap.d = { .pc = d_reg().pc, .op_id = d_reg().op_id, .valid = d_reg().valid, .stalled = (count_rs_in_flight() >= config.rs_size) };
    snap.e = { .pc = e_reg().pc, .op_id = e_reg().op_id, .valid = e_reg().valid, .stalled = (div_busy_cycles_remaining() > 0) };
    snap.m = { .pc = m_reg().pc, .op_id = m_reg().op_id, .valid = m_reg().valid, .stalled = (dcache_stall_remaining() > 0) };
    snap.w = { .pc = w_reg().pc, .op_id = w_reg().op_id, .valid = w_reg().valid, .stalled = false };

    {
        std::scoped_lock lock(history_mutex_);
        cycle_history_.push_back(snap);
        if (cycle_history_.size() > 80) {
            cycle_history_.pop_front();
        }
    }
}

auto OutOfOrderPipeline::get_rob_occupancy() const -> size_t {
    return count_rob_in_flight();
}

auto OutOfOrderPipeline::get_rob_entries() const -> std::vector<RobEntryInfo> {
    std::vector<RobEntryInfo> result;
    uint32_t i = rob_head_;
    const size_t size = count_rob_in_flight();
    for (size_t k = 0; k < size; k++) {
        if (rob_[i].valid) {
            result.push_back({
                .pc = rob_[i].pc,
                .op_id = rob_[i].op_id,
                .ready = rob_[i].is_ready,
                .head = (i == rob_head_),
                .tail = (i == (rob_tail_ == 0 ? config.rob_size - 1 : rob_tail_ - 1))
            });
        }
        i = (i + 1) % config.rob_size;
    }
    return result;
}

auto OutOfOrderPipeline::get_rs_occupancy() const -> size_t {
    return count_rs_in_flight();
}

} // namespace simrv::pipeline
