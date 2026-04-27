/**
 * @file StageCommit.cpp
 * @brief Commit stage implementation for Machine.
 */
#include "Cpu.hpp"
#include "Define.hpp"
#include "Machine.hpp"
#include "XLen.hpp"

void CPU::run_commit_stage(Machine& machine) { commit_control_flow_and_traps(machine); }

/* commit_control_flow_and_traps(Complete) stage                                                                    */
void CPU::commit_control_flow_and_traps(Machine& machine) {
    if (pipeline_context.cinsn != 0u) { machine.e_ccount++; /** for evaluation **/
}

    const auto opcode = static_cast<Opcode>(pipeline_context.opcode);
    const auto funct3 = static_cast<Funct3>(pipeline_context.funct3);

    if (opcode == Opcode::System) {
        if (funct3 == Funct3::Priv) {
            switch (static_cast<Funct12Priv>(pipeline_context.funct12)) {
                case Funct12Priv::Uret: {
                    break;
                }
                case Funct12Priv::Sret: {
                    sret();
                    break;
                }
                case Funct12Priv::Mret: {
                    mret();
                    break;
                }
                default:
                    if (pipeline_context.funct7 == static_cast<Instruction>(Funct7Priv::SfenceVma)) {
                        TLB_flush();
                    }
                    break;
            }
        } else {
            write_csr(static_cast<CSRAddress>(pipeline_context.funct12),
                      pipeline_context.wb_data_csr);
        }
    }

    Word const pending_interrupts = mip & mie;
    Word enable_interrupts = 0;
    Word mask = 0;
    Word irq_num = 32;
    if (pending_interrupts != 0u) {
        switch (priv) {
            case kPrivMachine: {
                if ((mstatus & enum_mask(MstatusBit::Mie)) != 0u) {
                    enable_interrupts = ~mideleg;
                }
                break;
            }
            case kPrivSupervisor: {
                enable_interrupts = ~mideleg;
                if ((mstatus & enum_mask(MstatusBit::Sie)) != 0u) {
                    enable_interrupts |= mideleg;
                }
                break;
            }
            case kPrivUser: {
                enable_interrupts = ~0;
                break;
            }
            default:
                break;
        }
        mask = pending_interrupts & enable_interrupts;
        for (int i = 0; i < 32; i++) {
            if (((1 << i) & mask) != 0u) {
                irq_num = i;
                break;
            }
        }
    }
    if (pending_exception != ~0U) {
        raise_exception(pending_exception, pending_tval);
    } else {
        if (pipeline_context.tkn != 0u) {
            pc = pipeline_context.jmp_pc;
        } else {
            pc = pc + ((pipeline_context.cinsn != 0u) ? 2 : 4);
        }
        if (mask != 0) {
            raise_exception(kInterruptCauseBit | irq_num, pending_tval);
        }
    }
}
