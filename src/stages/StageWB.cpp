/**
 * @file StageWB.cpp
 * @brief WB stage implementation for Machine.
 */
#include "Machine.hpp"

void CPU::run_writeback_stage(Machine& machine) { writeback_registers(machine); }

/* writeback_registers(Write Back) stage                                                                  */
void CPU::writeback_registers(Machine& machine) {
    if (pending_exception != ~0u) return;

    machine.e_icount++;

    Word wire_wb_r_data = 0;
    Word wire_wb_r_enable = 0;

    const Opcode opcode = static_cast<Opcode>(pipeline_context.opcode);
    const Funct5Amo funct5 = static_cast<Funct5Amo>(pipeline_context.funct5);
    const Funct3 funct3 = static_cast<Funct3>(pipeline_context.funct3);

    if ((opcode == Opcode::Load) || (opcode == Opcode::Amo && funct5 != Funct5Amo::Sc)) {
        wire_wb_r_data = pipeline_context.mem_rdata;
        wire_wb_r_enable = 1;
    } else if (opcode == Opcode::System && funct3 != Funct3::Priv) {
        wire_wb_r_data = pipeline_context.rcsr;
        wire_wb_r_enable = 1;
    } else {
        if ((opcode == Opcode::Amo && funct5 == Funct5Amo::Sc) || (opcode == Opcode::Lui) ||
            (opcode == Opcode::Auipc) || (opcode == Opcode::Jal) || (opcode == Opcode::Jalr) ||
            (opcode == Opcode::Op) || (opcode == Opcode::OpImm) ||
            (opcode == Opcode::OpFp && pipeline_context.int_wb_from_fp)) {
            wire_wb_r_data = pipeline_context.wb_data;
            wire_wb_r_enable = 1;
        }
    }

    if (opcode == Opcode::LoadFp) {
        freg[pipeline_context.rd] = pipeline_context.fp_mem_rdata;
    }

    if ((opcode == Opcode::OpFp || opcode == Opcode::MAdd || opcode == Opcode::MSub ||
         opcode == Opcode::NMAdd || opcode == Opcode::NMSub) &&
        pipeline_context.fp_wb_enable) {
        freg[pipeline_context.rd] = pipeline_context.fp_wb_data;
    }

    if (wire_wb_r_enable && pipeline_context.rd != 0) {
        reg[pipeline_context.rd] = wire_wb_r_data; /* regifile write port 1 */
    }
}
