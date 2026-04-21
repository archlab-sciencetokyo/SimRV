/**
 * @file StageEX.cpp
 * @brief EX stage implementation for Machine.
 */
#include "Machine.hpp"

void CPU::run_execute_stage(Machine& machine) { execute_core(machine); }

/* execute_core(Execution 1) stage                                                                 */
void CPU::execute_core(Machine& machine) {
    pipeline_context.fp_wb_enable = 0;
    pipeline_context.int_wb_from_fp = 0;

    switch (static_cast<Opcode>(pipeline_context.opcode)) {
        case Opcode::Lui: {
            pipeline_context.tkn = 0;
            pipeline_context.wb_data = pipeline_context.imm << 12;
            break;
        }
        case Opcode::Auipc: {
            pipeline_context.tkn = 0;
            pipeline_context.wb_data = pc + (pipeline_context.imm << 12);
            break;
        }
        case Opcode::Jal: {
            pipeline_context.tkn = 1;
            pipeline_context.wb_data = pc + (pipeline_context.cinsn ? 2 : 4);
            pipeline_context.jmp_pc = pc + pipeline_context.imm;
            break;
        }
        case Opcode::Jalr: {
            pipeline_context.tkn = 1;
            pipeline_context.wb_data = pc + (pipeline_context.cinsn ? 2 : 4);
            pipeline_context.jmp_pc = pipeline_context.rrs1 + pipeline_context.imm;
            break;
        }
        case Opcode::Op: {
            pipeline_context.tkn = 0;
            pipeline_context.wb_data =
                execute_unit.aluInt(pipeline_context.rrs1, pipeline_context.rrs2,
                                    pipeline_context.funct3, pipeline_context.funct7);
            break;
        }
        case Opcode::Load:
        case Opcode::LoadFp:
        case Opcode::Store: {
            pipeline_context.tkn = 0;
            pipeline_context.mem_addr = pipeline_context.rrs1 + pipeline_context.imm;
            break;
        }
        case Opcode::StoreFp: {
            pipeline_context.tkn = 0;
            pipeline_context.mem_addr = pipeline_context.rrs1 + pipeline_context.imm;
            pipeline_context.fp_mem_wdata = freg[pipeline_context.rs2];
            break;
        }
        case Opcode::MiscMem: {
            pipeline_context.tkn = 0;
            break;
        }
        case Opcode::Branch: {
            pipeline_context.tkn = execute_unit.branchTaken(
                pipeline_context.rrs1, pipeline_context.rrs2, pipeline_context.funct3);
            pipeline_context.jmp_pc = pc + pipeline_context.imm;
            break;
        }
        case Opcode::OpImm: {
            pipeline_context.tkn = 0;
            pipeline_context.funct7 &=
                (static_cast<Funct3>(pipeline_context.funct3) == Funct3::Add) ? 0 : 0x20;
            pipeline_context.wb_data =
                execute_unit.aluInt(pipeline_context.rrs1, pipeline_context.imm,
                                    pipeline_context.funct3, pipeline_context.funct7);
            break;
        }
        case Opcode::Amo: {
            pipeline_context.tkn = 0;
            pipeline_context.mem_addr = pipeline_context.rrs1;
            if (static_cast<Funct5Amo>(pipeline_context.funct5) == Funct5Amo::Sc) {
                pipeline_context.wb_data = !((pipeline_context.rrs1 == load_res) && reserved);
            }
            break;
        }
        case Opcode::System: {
            if (static_cast<Funct3>(pipeline_context.funct3) == Funct3::Priv) {
                switch (static_cast<Funct12Priv>(pipeline_context.funct12)) {
                    case Funct12Priv::Ecall: {
                        pipeline_context.wb_data_csr = enum_mask(ExceptionCode::UserEcall) + priv;
                        pending_exception = enum_mask(ExceptionCode::UserEcall) + priv;
                        machine.e_icount++;
                        break;
                    }
                    case Funct12Priv::Ebreak: {
                        pipeline_context.tkn = 0;
                        break;
                    }
                    case Funct12Priv::Uret:
                    case Funct12Priv::Sret:
                    case Funct12Priv::Mret: {
                        pipeline_context.tkn = 1;
                        pipeline_context.jmp_pc = pipeline_context.rcsr;
                        break;
                    }
                    case Funct12Priv::Wfi: {
                        pipeline_context.tkn = 0;
                        break;
                    }
                    default: {
                        if (pipeline_context.funct7 ==
                            static_cast<Instruction>(Funct7Priv::SfenceVma)) {
                            pipeline_context.tkn = 0;
                        }
                        break;
                    }
                }
            } else {
                pipeline_context.tkn = 0;
                pipeline_context.wb_data_csr =
                    execute_unit.csrWriteValue(pipeline_context.rcsr, pipeline_context.rrs1,
                                               pipeline_context.imm, pipeline_context.funct3);
            }
            break;
        }
        case Opcode::MAdd:
        case Opcode::MSub:
        case Opcode::NMAdd:
        case Opcode::NMSub: {
            pipeline_context.tkn = 0;
            const Word fmt = (pipeline_context.ir >> 25) & 0x3;
            const Word rs3 = (pipeline_context.ir >> 27) & 0x1f;
            const FpExecResult fp = execute_unit.fusedFp(
                static_cast<Opcode>(pipeline_context.opcode), fmt, pipeline_context.rs1,
                pipeline_context.rs2, rs3, pipeline_context.funct3, freg.data(), fcsr);
            pipeline_context.fp_wb_data = fp.fp_wb_data;
            pipeline_context.fp_wb_enable = fp.fp_wb_enable;
            break;
        }
        case Opcode::OpFp: {
            pipeline_context.tkn = 0;
            const FpExecResult fp =
                execute_unit.opFp(pipeline_context.funct7, pipeline_context.funct3,
                                  pipeline_context.rs2, pipeline_context.rs1, pipeline_context.rs2,
                                  pipeline_context.rrs1, freg.data(), fcsr);
            pipeline_context.wb_data = fp.int_wb_data;
            pipeline_context.int_wb_from_fp = fp.int_wb_enable;
            pipeline_context.fp_wb_data = fp.fp_wb_data;
            pipeline_context.fp_wb_enable = fp.fp_wb_enable;
            break;
        }
        default: {
            pipeline_context.tkn = 0;
            break;
        }
    }
}
