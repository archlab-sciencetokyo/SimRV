#include "simrv/pipeline/PipelineSim.hpp"
#include <algorithm>

namespace simrv::pipeline {

PipelineSim::PipelineSim() {
    branch_history_table_.fill(false); // Default: predict Not Taken
}

void PipelineSim::reset() {
    in_flight_.clear();
    cycle_count_ = 0;
    stall_cycles_ = 0;
    bubble_cycles_ = 0;
    icache_stalls_ = 0;
    dcache_stalls_ = 0;
    data_hazard_stalls_ = 0;
    control_hazard_bubbles_ = 0;
}

auto PipelineSim::step_instruction(Register pc, Opcode opcode, RegId rd, RegId rs1, RegId rs2,
                                  OperationId op_id, bool branched, bool is_branch, bool is_jump,
                                  bool icache_miss, bool dcache_miss) -> uint32_t {
    uint32_t stalls = 0;

    // 1. Model I-Cache Miss Stall (10 cycles)
    if (icache_miss) {
        stalls += 10;
        icache_stalls_ += 10;
    }

    // 2. Model Data Hazards (RAW)
    bool has_dependency = false;
    int raw_stall = 0;
    
    bool reads_rs1 = (rs1 != static_cast<RegId>(0));
    bool reads_rs2 = (rs2 != static_cast<RegId>(0));

    for (const auto& instr : in_flight_) {
        if (instr.writes_reg && instr.rd != static_cast<RegId>(0)) {
            if ((reads_rs1 && rs1 == instr.rd) || (reads_rs2 && rs2 == instr.rd)) {
                has_dependency = true;
                // If result is not ready (remaining_latency > 0), consumer must stall
                if (instr.remaining_latency > raw_stall) {
                    raw_stall = instr.remaining_latency;
                }
            }
        }
    }

    if (has_dependency && raw_stall > 0) {
        stalls += raw_stall;
        data_hazard_stalls_ += raw_stall;
        
        // Advance in-flight instruction latencies by the stall amount
        for (auto& instr : in_flight_) {
            instr.remaining_latency = std::max(0, instr.remaining_latency - raw_stall);
        }
    }

    // 3. Model Control Hazards (Jumps & Branch Mispredictions)
    uint32_t branch_bubbles = 0;
    if (is_jump) {
        if (opcode == Opcode::Jalr) {
            // JALR target resolution takes 2 cycles (2 bubbles)
            branch_bubbles = 2;
        } else {
            // JAL target resolution takes 1 cycle (1 bubble)
            branch_bubbles = 1;
        }
    } else if (is_branch) {
        // Simple 1-bit Branch Predictor logic
        uint32_t bht_idx = (pc >> 1) & 0xFF; // Index by PC
        const auto bht_idx_sz = static_cast<std::size_t>(bht_idx);
        bool predicted_taken = branch_history_table_.at(bht_idx_sz);
        
        if (predicted_taken != branched) {
            // Misprediction: flush pipeline (2 bubbles penalty)
            branch_bubbles = 2;
            // Update BHT
            branch_history_table_.at(bht_idx_sz) = branched;
        }
    }

    stalls += branch_bubbles;
    control_hazard_bubbles_ += branch_bubbles;
    bubble_cycles_ += branch_bubbles;

    // 4. Model D-Cache Miss Stall (15 cycles)
    if (dcache_miss) {
        stalls += 15;
        dcache_stalls_ += 15;
    }

    // 5. Update In-Flight queue: tick aging
    for (auto& instr : in_flight_) {
        instr.remaining_latency = std::max(0, instr.remaining_latency - 1);
    }
    
    // Remove completed in-flight instructions (latency reached 0)
    std::erase_if(in_flight_, [](const InFlightInstr& instr) -> bool {
        return instr.remaining_latency <= 0;
    });

    // 6. Push current instruction to in-flight queue if it writes a register
    bool writes_reg = (rd != static_cast<RegId>(0)) && 
                      (opcode != Opcode::Branch) && 
                      (opcode != Opcode::Store) && 
                      (opcode != Opcode::StoreFp);

    if (writes_reg) {
        bool is_load = (opcode == Opcode::Load) || (opcode == Opcode::LoadFp);
        int latency = 1; // Default ALU latency is 1 cycle (ready in next cycle, 0 stall with forwarding)
        if (is_load) {
            latency = 2; // Load data is ready in MEM (1 load-use stall cycle needed if consumed immediately)
        } else if (op_id == OperationId::MUL || op_id == OperationId::MULH || 
                   op_id == OperationId::MULHSU || op_id == OperationId::MULHU ||
                   op_id == OperationId::MULW) {
            latency = 3; // Multiply takes 3 cycles
        } else if (op_id == OperationId::DIV || op_id == OperationId::DIVU || 
                   op_id == OperationId::REM || op_id == OperationId::REMU ||
                   op_id == OperationId::DIVW || op_id == OperationId::DIVUW ||
                   op_id == OperationId::REMW || op_id == OperationId::REMUW) {
            latency = 20; // Division takes 20 cycles
        }
        
        in_flight_.push_back(InFlightInstr{
            .rd = rd,
            .writes_reg = true,
            .is_load = is_load,
            .remaining_latency = latency - 1
        });
    }

    uint32_t total_cycles = 1 + stalls;
    cycle_count_ += total_cycles;
    stall_cycles_ += stalls;

    return total_cycles;
}

} // namespace simrv::pipeline
