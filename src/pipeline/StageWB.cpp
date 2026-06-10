/**
 * @file StageWB.cpp
 * @brief WB stage implementation for Machine.
 */
#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

void CPU::run_writeback_stage(Machine& machine) { writeback_registers(machine); }

/* writeback_registers(Write Back) stage */
void CPU::writeback_registers([[maybe_unused]] Machine& machine) {
    auto& ctx = pipeline_context;
    if (ctx.pending_exception.has_value()) {
        return;
    }

    e_icount++;

    Word wire_wb_r_data = 0;
    Word wire_wb_r_enable = 0;

    const auto opcode = static_cast<Opcode>(ctx.opcode);
    const auto funct5 = static_cast<Funct5Amo>(ctx.funct5);
    const auto funct3 = static_cast<Funct3>(ctx.funct3);

    if ((opcode == Opcode::Load) || (opcode == Opcode::Amo && funct5 != Funct5Amo::Sc)) {
        wire_wb_r_data = ctx.mem_rdata;
        wire_wb_r_enable = 1;
    } else if (opcode == Opcode::System && funct3 != Funct3::Priv) {
        wire_wb_r_data = ctx.rcsr;
        wire_wb_r_enable = 1;
    } else {
        if ((opcode == Opcode::Amo && funct5 == Funct5Amo::Sc) || (opcode == Opcode::Lui) ||
            (opcode == Opcode::Auipc) || (opcode == Opcode::Jal) || (opcode == Opcode::Jalr) ||
            (opcode == Opcode::Op) || (opcode == Opcode::OpImm) || (opcode == Opcode::Op32) ||
            (opcode == Opcode::OpImm32) || (opcode == Opcode::OpFp && (ctx.int_wb_from_fp != 0u))) {
            wire_wb_r_data = ctx.wb_data;
            wire_wb_r_enable = 1;
        }
    }

    if (opcode == Opcode::LoadFp) {
        state_.regs.write_fp(ctx.rd, ctx.fp_mem_rdata);
        state_.mstatus |= enum_mask(MstatusBit::Fs);
    }

    if ((opcode == Opcode::OpFp || opcode == Opcode::MAdd || opcode == Opcode::MSub ||
         opcode == Opcode::NMAdd || opcode == Opcode::NMSub) &&
        (ctx.fp_wb_enable != 0u)) {
        state_.regs.write_fp(ctx.rd, ctx.fp_wb_data);
        state_.mstatus |= enum_mask(MstatusBit::Fs);
    }

    if (wire_wb_r_enable != 0u) {
        state_.regs.write(ctx.rd, wire_wb_r_data); /* regfile write port 1 */
    }
}

} // namespace simrv::core
