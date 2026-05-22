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
    if (simrv::compiler::unlikely(ctx.pending_exception.has_value())) {
        return;
    }

    ctx.fp_wb_enable = false;
    ctx.int_wb_from_fp = false;

    switch (ctx.opcode) {
        case Opcode::Lui:
            ctx.tkn = false;
            ctx.wb_data = ctx.imm;
            break;
        case Opcode::Auipc:
            ctx.tkn = false;
            ctx.wb_data = state_.pc + ctx.imm;
            break;
        case Opcode::Jal:
            ctx.tkn = true;
            ctx.wb_data = state_.pc + ((ctx.cinsn != 0u) ? 2 : 4);
            ctx.jmp_pc = state_.pc + ctx.imm;
            break;
        case Opcode::Jalr:
            ctx.tkn = true;
            ctx.wb_data = state_.pc + ((ctx.cinsn != 0u) ? 2 : 4);
            ctx.jmp_pc = ctx.rrs1 + ctx.imm;
            break;
        case Opcode::Op:
            ctx.tkn = false;
            ctx.wb_data = execute::ExecuteUnit::aluInt(ctx.rrs1, ctx.rrs2, ctx.funct3, ctx.funct7);
            break;
        case Opcode::OpImm:
            ctx.tkn = false;
            ctx.funct7 &= (ctx.funct3 == Funct3::Add) ? 0 : 0x20;
            ctx.wb_data = execute::ExecuteUnit::aluInt(ctx.rrs1, ctx.imm, ctx.funct3, ctx.funct7);
            break;
        case Opcode::OpImm32:
            ctx.tkn = false;
            ctx.funct7 &= (enum_mask(ctx.funct3) == 0x5u) ? 0x20 : 0;
            ctx.wb_data = execute::ExecuteUnit::aluIntW(Opcode::OpImm32, ctx.rrs1, ctx.imm,
                                                        ctx.funct3, ctx.funct7);
            break;
        case Opcode::Op32:
            ctx.tkn = false;
            ctx.funct7 &= ((enum_mask(ctx.funct3) == 0x0u) ||
                           (enum_mask(ctx.funct3) == 0x5u))
                              ? 0x20
                              : 0;
            ctx.wb_data = execute::ExecuteUnit::aluIntW(Opcode::Op32, ctx.rrs1, ctx.rrs2,
                                                        ctx.funct3, ctx.funct7);
            break;
        case Opcode::Load:
        case Opcode::LoadFp:
        case Opcode::Store:
            ctx.tkn = false;
            ctx.mem_addr = ctx.rrs1 + ctx.imm;
            break;
        case Opcode::StoreFp:
            ctx.tkn = false;
            ctx.mem_addr = ctx.rrs1 + ctx.imm;
            ctx.fp_mem_wdata = state_.regs.read_fp(ctx.rs2);
            break;
        case Opcode::MiscMem:
            ctx.tkn = false;
            if (ctx.funct3 == Funct3::FenceI) {
                icache.flush();
                dcache.flush();
            }
            break;
        case Opcode::Branch:
            ctx.tkn = execute::ExecuteUnit::branchTaken(ctx.rrs1, ctx.rrs2, ctx.funct3);
            ctx.jmp_pc = state_.pc + ctx.imm;
            break;
        case Opcode::Amo:
            ctx.tkn = false;
            ctx.mem_addr = ctx.rrs1;
            if (ctx.funct5 == Funct5Amo::Sc) {
                ctx.wb_data = static_cast<Register>((ctx.rrs1 != state_.load_res) ||
                                                    !(state_.reserved != 0u));
            }
            break;
        case Opcode::System:
            if (ctx.funct3 == Funct3::Priv) {
                switch (static_cast<Funct12Priv>(ctx.funct12)) {
                    case Funct12Priv::Ecall:
                        ctx.wb_data_csr = enum_mask(ExceptionCode::UserEcall) + std::to_underlying(state_.priv);
                        ctx.pending_exception = static_cast<ExceptionCode>(enum_mask(ExceptionCode::UserEcall) + std::to_underlying(state_.priv));
                        e_icount++;
                        break;
                    case Funct12Priv::Ebreak:
                        ctx.tkn = false;
                        break;
                    case Funct12Priv::Uret:
                    case Funct12Priv::Sret:
                    case Funct12Priv::Mret:
                        ctx.tkn = true;
                        ctx.jmp_pc = ctx.rcsr;
                        break;
                    case Funct12Priv::Wfi:
                        ctx.tkn = false;
                        break;
                    default:
                        if (ctx.funct7 == static_cast<Instruction>(Funct7Priv::SfenceVma)) {
                            ctx.tkn = false;
                        }
                        break;
                }
            } else {
                const auto csr_val_imm = ((std::to_underlying(ctx.funct3) & 4) != 0)
                                             ? static_cast<ImmValue>(std::to_underlying(ctx.rs1))
                                             : ctx.imm;
                auto csr_result =
                    execute::ExecuteUnit::csrWriteValue(ctx.rcsr, ctx.rrs1, csr_val_imm, ctx.funct3);
                if (csr_result.has_value()) {
                    ctx.tkn = false;
                    ctx.wb_data_csr = csr_result.value();
                } else {
                    ctx.pending_exception = static_cast<ExceptionCode>(csr_result.error());
                    ctx.pending_tval = ctx.ir;
                }
            }
            break;
        case Opcode::MAdd:
        case Opcode::MSub:
        case Opcode::NMAdd:
        case Opcode::NMSub: {
            ctx.tkn = false;
            const Word rm = (enum_mask(ctx.funct3) == 7) ? ((state_.fcsr >> 5) & 0x7) : enum_mask(ctx.funct3);
            if (simrv::compiler::unlikely(rm >= 5)) {
                ctx.pending_exception = ExceptionCode::IllegalInstruction;
                ctx.pending_tval = ctx.ir;
                break;
            }
            const Word fmt = ctx.funct7 & 0x3;
            const Word rs3 = (ctx.ir >> 27) & 0x1F;
            const CSRValue old_fcsr = state_.fcsr;
            const auto fp = execute::ExecuteUnit::fusedFp(ctx.opcode, fmt,
                                                          std::to_underlying(ctx.rs1), std::to_underlying(ctx.rs2), rs3, enum_mask(ctx.funct3),
                                                          state_.regs.fp_data_ptr(), state_.fcsr);
            if (state_.fcsr != old_fcsr) {
                state_.mstatus |= enum_mask(MstatusBit::Fs);
            }
            ctx.fp_wb_data = fp.fp_wb_data;
            ctx.fp_wb_enable = fp.fp_wb_enable;
            break;
        }
        case Opcode::OpFp: {
            ctx.tkn = false;
            const Word rm = (enum_mask(ctx.funct3) == 7) ? ((state_.fcsr >> 5) & 0x7) : enum_mask(ctx.funct3);
            if (simrv::compiler::unlikely(rm >= 5)) {
                ctx.pending_exception = ExceptionCode::IllegalInstruction;
                ctx.pending_tval = ctx.ir;
                break;
            }
            const CSRValue old_fcsr = state_.fcsr;
            const auto fp =
                execute::ExecuteUnit::opFp(ctx.funct7, ctx.funct3, std::to_underlying(ctx.rs1), std::to_underlying(ctx.rs2), ctx.rrs1,
                                           state_.regs.fp_data_ptr(), state_.fcsr);
            if (state_.fcsr != old_fcsr) {
                state_.mstatus |= enum_mask(MstatusBit::Fs);
            }
            ctx.wb_data = fp.int_wb_data;
            ctx.int_wb_from_fp = fp.int_wb_enable;
            ctx.fp_wb_data = fp.fp_wb_data;
            ctx.fp_wb_enable = fp.fp_wb_enable;
            break;
        }
        default:
            ctx.tkn = false;
            ctx.pending_exception = ExceptionCode::IllegalInstruction;
            ctx.pending_tval = ctx.ir;
            break;
    }
}

}  // namespace simrv::core