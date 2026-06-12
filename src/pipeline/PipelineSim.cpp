#include "simrv/pipeline/PipelineSim.hpp"
#include <algorithm>

namespace simrv::pipeline {

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

void PipelineSim::tick_pipeline() {
    // 1. Tick remaining redirect/control bubbles
    if (control_bubble_remaining_ > 0) {
        control_bubble_remaining_--;
        control_hazard_bubbles_++;
        bubble_cycles_++;
        stall_cycles_++;
        cycle_count_++;

        // Advance downstream, inserting bubble into IF
        w_reg_ = m_reg_;
        m_reg_ = e_reg_;
        e_reg_ = d_reg_;
        if (e_reg_.valid) {
            if (e_reg_.is_load) {
                e_reg_.remaining_latency = 1;
            } else if (e_reg_.op_id == OperationId::MUL || e_reg_.op_id == OperationId::MULH || 
                       e_reg_.op_id == OperationId::MULHSU || e_reg_.op_id == OperationId::MULHU ||
                       e_reg_.op_id == OperationId::MULW) {
                e_reg_.remaining_latency = static_cast<int>(config.mul_latency) - 1;
            } else if (e_reg_.op_id == OperationId::DIV || e_reg_.op_id == OperationId::DIVU || 
                       e_reg_.op_id == OperationId::REM || e_reg_.op_id == OperationId::REMU ||
                       e_reg_.op_id == OperationId::DIVW || e_reg_.op_id == OperationId::DIVUW ||
                       e_reg_.op_id == OperationId::REMW || e_reg_.op_id == OperationId::REMUW) {
                e_reg_.remaining_latency = static_cast<int>(config.div_latency) - 1;
                div_busy_cycles_remaining_ = config.div_latency - 1;
            } else {
                e_reg_.remaining_latency = 0;
            }
        }
        d_reg_ = f_reg_;
        f_reg_ = PipelineReg{}; // bubble
        return;
    }

    // Determine stall signals
    bool stall_mem = false;
    if (m_reg_.valid && m_reg_.dcache_miss) {
        if (dcache_stall_remaining_ > 0) {
            stall_mem = true;
        }
    }

    bool stall_ex = false;
    if (e_reg_.valid) {
        if (e_reg_.op_id == OperationId::DIV || e_reg_.op_id == OperationId::DIVU || 
            e_reg_.op_id == OperationId::REM || e_reg_.op_id == OperationId::REMU ||
            e_reg_.op_id == OperationId::DIVW || e_reg_.op_id == OperationId::DIVUW ||
            e_reg_.op_id == OperationId::REMW || e_reg_.op_id == OperationId::REMUW) {
            if (div_busy_cycles_remaining_ > 0) {
                stall_ex = true;
            }
        }
    }

    bool stall_id = false;
    if (d_reg_.valid) {
        // RAW data hazard check with forwarding
        bool reads_rs1 = (d_reg_.rs1 != static_cast<RegId>(0));
        bool reads_rs2 = (d_reg_.rs2 != static_cast<RegId>(0));
        if (reads_rs1 || reads_rs2) {
            // Check EX stage
            if (e_reg_.valid && e_reg_.writes_reg && e_reg_.rd != static_cast<RegId>(0)) {
                if ((reads_rs1 && d_reg_.rs1 == e_reg_.rd) || (reads_rs2 && d_reg_.rs2 == e_reg_.rd)) {
                    if (e_reg_.remaining_latency > 0) {
                        stall_id = true;
                    }
                }
            }
            // Check MEM stage
            if (m_reg_.valid && m_reg_.writes_reg && m_reg_.rd != static_cast<RegId>(0)) {
                if ((reads_rs1 && d_reg_.rs1 == m_reg_.rd) || (reads_rs2 && d_reg_.rs2 == m_reg_.rd)) {
                    if (m_reg_.remaining_latency > 0) {
                        stall_id = true;
                    }
                }
            }
        }
    }

    bool stall_if = false;
    if (f_reg_.valid) {
        if ((f_reg_.tlb_miss && tlb_stall_remaining_ > 0) || 
            (f_reg_.icache_miss && icache_stall_remaining_ > 0)) {
            stall_if = true;
        }
    }

    // Propagation of stalls
    bool MEM_stalled = stall_mem;
    bool EX_stalled = MEM_stalled || stall_ex;
    bool ID_stalled = EX_stalled || stall_id;
    bool IF_stalled = ID_stalled || stall_if;

    // Increment stats based on active stalls
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

    // Decrement remaining stall counters
    if (dcache_stall_remaining_ > 0) dcache_stall_remaining_--;
    if (div_busy_cycles_remaining_ > 0) div_busy_cycles_remaining_--;
    if (tlb_stall_remaining_ > 0) tlb_stall_remaining_--;
    if (icache_stall_remaining_ > 0) icache_stall_remaining_--;

    // Decrement execution latencies in stages
    if (e_reg_.valid && e_reg_.remaining_latency > 0) {
        e_reg_.remaining_latency--;
    }
    if (m_reg_.valid && m_reg_.remaining_latency > 0) {
        m_reg_.remaining_latency--;
    }
    if (w_reg_.valid && w_reg_.remaining_latency > 0) {
        w_reg_.remaining_latency--;
    }

    // 2. Resolve branches/jumps in EX stage
    if (e_reg_.valid && !e_reg_.control_resolved) {
        e_reg_.control_resolved = true;
        
        uint32_t control_bubbles = 0;
        const Register pc = e_reg_.pc;
        const Opcode opcode = e_reg_.opcode;
        const bool is_jump = e_reg_.is_jump;
        const bool is_branch = e_reg_.is_branch;
        const Register target_pc = e_reg_.target_pc;
        const bool branched = e_reg_.branched;

        // Lookup BTB
        const uint32_t btb_idx = (pc >> 1) & 0x7F; // 128 entries
        auto& btb_entry = btb_.at(btb_idx);
        const bool btb_hit = btb_entry.valid && (btb_entry.pc == pc);

        if (is_jump) {
            if (opcode == Opcode::Jalr) {
                if (btb_hit && btb_entry.target == target_pc) {
                    control_bubbles = 0;
                } else {
                    control_bubbles = config.branch_mispredict_penalty;
                    btb_entry.pc = pc;
                    btb_entry.target = target_pc;
                    btb_entry.valid = true;
                }
            } else { // JAL
                if (btb_hit && btb_entry.target == target_pc) {
                    control_bubbles = 0;
                } else {
                    control_bubbles = 1;
                    btb_entry.pc = pc;
                    btb_entry.target = target_pc;
                    btb_entry.valid = true;
                }
            }
        } else if (is_branch) {
            // 2-bit Bimodal Branch Predictor logic
            const uint32_t bht_idx = (pc >> 1) & 0xFF; // Index by PC
            const auto bht_idx_sz = static_cast<std::size_t>(bht_idx);
            uint8_t counter = branch_history_table_.at(bht_idx_sz);
            const bool predicted_taken = (counter >= 2);
            
            if (predicted_taken != branched) {
                control_bubbles = config.branch_mispredict_penalty;
            } else {
                if (predicted_taken) {
                    if (btb_hit && btb_entry.target == target_pc) {
                        control_bubbles = 0;
                    } else {
                        control_bubbles = config.branch_mispredict_penalty;
                    }
                } else {
                    control_bubbles = 0;
                }
            }

            // Update direction predictor counter (saturating 2-bit counter)
            if (branched) {
                if (counter < 3) counter++;
                btb_entry.pc = pc;
                btb_entry.target = target_pc;
                btb_entry.valid = true;
            } else {
                if (counter > 0) counter--;
            }
            branch_history_table_.at(bht_idx_sz) = counter;
        }

        if (control_bubbles > 0) {
            control_bubble_remaining_ = control_bubbles;
            // Flush IF and ID
            f_reg_ = PipelineReg{};
            d_reg_ = PipelineReg{};
            return; // Exit cycle early to trigger flush bubbles in next tick
        }
    }

    // 3. Stage register transfers
    // WB
    if (!MEM_stalled) {
        w_reg_ = m_reg_;
    } else {
        w_reg_ = PipelineReg{}; // bubble
    }

    // MEM
    if (!MEM_stalled) {
        if (!EX_stalled) {
            m_reg_ = e_reg_;
            if (m_reg_.valid && m_reg_.dcache_miss) {
                dcache_stall_remaining_ = config.dcache_miss_penalty;
            }
        } else {
            m_reg_ = PipelineReg{}; // bubble
        }
    }

    // EX
    if (!EX_stalled) {
        if (!ID_stalled) {
            e_reg_ = d_reg_;
            if (e_reg_.valid) {
                if (e_reg_.is_load) {
                    e_reg_.remaining_latency = 1;
                } else if (e_reg_.op_id == OperationId::MUL || e_reg_.op_id == OperationId::MULH || 
                           e_reg_.op_id == OperationId::MULHSU || e_reg_.op_id == OperationId::MULHU ||
                           e_reg_.op_id == OperationId::MULW) {
                    e_reg_.remaining_latency = static_cast<int>(config.mul_latency) - 1;
                } else if (e_reg_.op_id == OperationId::DIV || e_reg_.op_id == OperationId::DIVU || 
                           e_reg_.op_id == OperationId::REM || e_reg_.op_id == OperationId::REMU ||
                           e_reg_.op_id == OperationId::DIVW || e_reg_.op_id == OperationId::DIVUW ||
                           e_reg_.op_id == OperationId::REMW || e_reg_.op_id == OperationId::REMUW) {
                    e_reg_.remaining_latency = static_cast<int>(config.div_latency) - 1;
                    div_busy_cycles_remaining_ = config.div_latency - 1;
                } else {
                    e_reg_.remaining_latency = 0;
                }
            }
        } else {
            e_reg_ = PipelineReg{}; // bubble
        }
    }

    // ID
    if (!ID_stalled) {
        if (!IF_stalled) {
            d_reg_ = f_reg_;
        } else {
            d_reg_ = PipelineReg{}; // bubble
        }
    }

    // IF
    if (!IF_stalled) {
        f_reg_ = PipelineReg{}; // cleared to bubble, will be filled by step_instruction if it accepts one
    }
}

auto PipelineSim::step_instruction(Register pc, Opcode opcode, RegId rd, RegId rs1, RegId rs2,
                                  OperationId op_id, bool branched, bool is_branch, bool is_jump,
                                  bool icache_miss, bool dcache_miss, bool tlb_miss, Register target_pc) -> uint32_t {
    uint32_t cycles_spent = 0;

    // Loop until the IF stage can accept a new instruction.
    while (true) {
        bool stall_mem = (m_reg_.valid && m_reg_.dcache_miss && dcache_stall_remaining_ > 0);
        bool stall_ex = (e_reg_.valid && 
                         (e_reg_.op_id == OperationId::DIV || e_reg_.op_id == OperationId::DIVU || 
                          e_reg_.op_id == OperationId::REM || e_reg_.op_id == OperationId::REMU ||
                          e_reg_.op_id == OperationId::DIVW || e_reg_.op_id == OperationId::DIVUW ||
                          e_reg_.op_id == OperationId::REMW || e_reg_.op_id == OperationId::REMUW) && 
                         div_busy_cycles_remaining_ > 0);
        bool stall_id = false;
        if (d_reg_.valid) {
            bool reads_rs1 = (d_reg_.rs1 != static_cast<RegId>(0));
            bool reads_rs2 = (d_reg_.rs2 != static_cast<RegId>(0));
            if (reads_rs1 || reads_rs2) {
                if (e_reg_.valid && e_reg_.writes_reg && e_reg_.rd != static_cast<RegId>(0)) {
                    if ((reads_rs1 && d_reg_.rs1 == e_reg_.rd) || (reads_rs2 && d_reg_.rs2 == e_reg_.rd)) {
                        if (e_reg_.remaining_latency > 0) stall_id = true;
                    }
                }
                if (m_reg_.valid && m_reg_.writes_reg && m_reg_.rd != static_cast<RegId>(0)) {
                    if ((reads_rs1 && d_reg_.rs1 == m_reg_.rd) || (reads_rs2 && d_reg_.rs2 == m_reg_.rd)) {
                        if (m_reg_.remaining_latency > 0) stall_id = true;
                    }
                }
            }
        }
        bool stall_if = (f_reg_.valid && 
                         ((f_reg_.tlb_miss && tlb_stall_remaining_ > 0) || 
                          (f_reg_.icache_miss && icache_stall_remaining_ > 0)));

        bool IF_stalled = stall_mem || stall_ex || stall_id || stall_if;

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

    // Now feed the new instruction into IF
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

    // Initialize miss stalls if any
    if (tlb_miss) {
        tlb_stall_remaining_ = config.tlb_miss_penalty;
    }
    if (icache_miss) {
        icache_stall_remaining_ = config.icache_miss_penalty;
    }

    // Tick the pipeline once to represent this instruction's execution start.
    tick_pipeline();
    cycles_spent++;

    return cycles_spent;
}

} // namespace simrv::pipeline
