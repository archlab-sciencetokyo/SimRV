/**
 * @file StageEX.cpp
 * @brief EX stage implementation for Machine.
 */
#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/execute/ExecuteUnit.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

void CPU::run_execute_stage(Machine& machine) { execute_core(machine); }

/* execute_core(Execution 1) stage */
void CPU::execute_core(Machine& machine) {
    auto& ctx = pipeline_context;
    if (simrv::compiler::unlikely(ctx.pending_exception != kWordAllOnes)) {
        return;
    }

    ctx.fp_wb_enable = 0;
    ctx.int_wb_from_fp = 0;

    switch (static_cast<Opcode>(ctx.opcode)) {
        case Opcode::Lui:
            ctx.tkn = 0;
            ctx.wb_data = ctx.imm << 12;
            break;
        case Opcode::Auipc:
            ctx.tkn = 0;
            ctx.wb_data = state_.pc + (ctx.imm << 12);
            break;
        case Opcode::Jal:
            ctx.tkn = 1;
            ctx.wb_data = state_.pc + ((ctx.cinsn != 0u) ? 2 : 4);
            ctx.jmp_pc = state_.pc + ctx.imm;
            break;
        case Opcode::Jalr:
            ctx.tkn = 1;
            ctx.wb_data = state_.pc + ((ctx.cinsn != 0u) ? 2 : 4);
            ctx.jmp_pc = ctx.rrs1 + ctx.imm;
            break;
        case Opcode::Op:
            ctx.tkn = 0;
            ctx.wb_data = execute::ExecuteUnit::aluInt(ctx.rrs1, ctx.rrs2, ctx.funct3, ctx.funct7);
            break;
        case Opcode::OpImm:
            ctx.tkn = 0;
            ctx.funct7 &= (static_cast<Funct3>(ctx.funct3) == Funct3::Add) ? 0 : 0x20;
            ctx.wb_data = execute::ExecuteUnit::aluInt(ctx.rrs1, ctx.imm, ctx.funct3, ctx.funct7);
            break;
        case Opcode::OpImm32:
            ctx.tkn = 0;
            ctx.funct7 &= (ctx.funct3 == static_cast<Instruction>(0x5u)) ? 0x20 : 0;
            ctx.wb_data = execute::ExecuteUnit::aluIntW(Opcode::OpImm32, ctx.rrs1, ctx.imm,
                                                        ctx.funct3, ctx.funct7);
            break;
        case Opcode::Op32:
            ctx.tkn = 0;
            ctx.funct7 &= (ctx.funct3 == static_cast<Instruction>(0x5u)) ? 0x20 : 0;
            ctx.wb_data = execute::ExecuteUnit::aluIntW(Opcode::Op32, ctx.rrs1, ctx.rrs2,
                                                        ctx.funct3, ctx.funct7);
            break;
        case Opcode::Load:
        case Opcode::LoadFp:
        case Opcode::Store:
            ctx.tkn = 0;
            ctx.mem_addr = ctx.rrs1 + ctx.imm;
            break;
        case Opcode::StoreFp:
            ctx.tkn = 0;
            ctx.mem_addr = ctx.rrs1 + ctx.imm;
            ctx.fp_mem_wdata = state_.regs.read_fp(ctx.rs2);
            break;
        case Opcode::MiscMem:
            ctx.tkn = 0;
            if (static_cast<Funct3>(ctx.funct3) == Funct3::FenceI) {
                icache.flush();
                dcache.flush();
            }
            break;
        case Opcode::Branch:
            ctx.tkn = execute::ExecuteUnit::branchTaken(ctx.rrs1, ctx.rrs2, ctx.funct3);
            ctx.jmp_pc = state_.pc + ctx.imm;
            break;
        case Opcode::Amo:
            ctx.tkn = 0;
            ctx.mem_addr = ctx.rrs1;
            if (static_cast<Funct5Amo>(ctx.funct5) == Funct5Amo::Sc) {
                ctx.wb_data = static_cast<Register>((ctx.rrs1 != state_.load_res) ||
                                                    !(state_.reserved != 0u));
            }
            break;
        case Opcode::System:
            if (static_cast<Funct3>(ctx.funct3) == Funct3::Priv) {
                switch (static_cast<Funct12Priv>(ctx.funct12)) {
                    case Funct12Priv::Ecall:
                        ctx.wb_data_csr = enum_mask(ExceptionCode::UserEcall) + state_.priv;
                        ctx.pending_exception = enum_mask(ExceptionCode::UserEcall) + state_.priv;
                        e_icount++;
                        break;
                    case Funct12Priv::Ebreak:
                        ctx.tkn = 0;
                        break;
                    case Funct12Priv::Uret:
                    case Funct12Priv::Sret:
                    case Funct12Priv::Mret:
                        ctx.tkn = 1;
                        ctx.jmp_pc = ctx.rcsr;
                        break;
                    case Funct12Priv::Wfi:
                        ctx.tkn = 0;
                        break;
                    default:
                        if (ctx.funct7 == static_cast<Instruction>(Funct7Priv::SfenceVma)) {
                            ctx.tkn = 0;
                        }
                        break;
                }
            } else {
                auto csr_result =
                    execute::ExecuteUnit::csrWriteValue(ctx.rcsr, ctx.rrs1, ctx.imm, ctx.funct3);
                if (csr_result.has_value()) {
                    ctx.tkn = 0;
                    ctx.wb_data_csr = csr_result.value();
                } else {
                    ctx.pending_exception = csr_result.error();
                    ctx.pending_tval = ctx.ir;
                }
            }
            break;
        case Opcode::MAdd:
        case Opcode::MSub:
        case Opcode::NMAdd:
        case Opcode::NMSub: {
            ctx.tkn = 0;
            const Word fmt = ctx.funct7 & 0x3;
            const Word rs3 = (ctx.ir >> 27) & 0x1F;
            const CSRValue old_fcsr = state_.fcsr;
            const auto fp = execute::ExecuteUnit::fusedFp(static_cast<Opcode>(ctx.opcode), fmt,
                                                          ctx.rs1, ctx.rs2, rs3, ctx.funct3,
                                                          state_.regs.fp_data_ptr(), state_.fcsr);
            if (state_.fcsr != old_fcsr) {
                state_.mstatus |= enum_mask(MstatusBit::Fs);
            }
            ctx.fp_wb_data = fp.fp_wb_data;
            ctx.fp_wb_enable = static_cast<Word>(fp.fp_wb_enable);
            break;
        }
        case Opcode::OpFp: {
            ctx.tkn = 0;
            const CSRValue old_fcsr = state_.fcsr;
            const auto fp =
                execute::ExecuteUnit::opFp(ctx.funct7, ctx.funct3, ctx.rs1, ctx.rs2, ctx.rrs1,
                                           state_.regs.fp_data_ptr(), state_.fcsr);
            if (state_.fcsr != old_fcsr) {
                state_.mstatus |= enum_mask(MstatusBit::Fs);
            }
            ctx.wb_data = fp.int_wb_data;
            ctx.int_wb_from_fp = static_cast<Word>(fp.int_wb_enable);
            ctx.fp_wb_data = fp.fp_wb_data;
            ctx.fp_wb_enable = static_cast<Word>(fp.fp_wb_enable);
            break;
        }
        default:
            ctx.tkn = 0;
            break;
    }
}

}  // namespace simrv::core