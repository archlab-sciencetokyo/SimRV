/**
 * @file StageCommit.cpp
 * @brief Commit stage implementation for Machine.
 */
#include "Machine.hpp"

void CPU::run_commit_stage(Machine& machine) { commit_control_flow_and_traps(machine); }

/* commit_control_flow_and_traps(Complete) stage                                                                    */
void CPU::commit_control_flow_and_traps(Machine& machine) {
    if (pipeline_context.cinsn) machine.e_ccount++; /** for evaluation **/

    const Opcode opcode = static_cast<Opcode>(pipeline_context.opcode);
    const Funct3 funct3 = static_cast<Funct3>(pipeline_context.funct3);

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

    Word pending_interrupts = mip & mie;
    Word enable_interrupts = 0, mask = 0, irq_num = 32;
    if (pending_interrupts) {
        switch (priv) {
            case kPrivMachine: {
                if (mstatus & enum_mask(MstatusBit::Mie)) {
                    enable_interrupts = ~mideleg;
                }
                break;
            }
            case kPrivSupervisor: {
                enable_interrupts = ~mideleg;
                if (mstatus & enum_mask(MstatusBit::Sie)) {
                    enable_interrupts |= mideleg;
                }
                break;
            }
            case kPrivUser: {
                enable_interrupts = ~0;
                break;
            }
        }
        mask = pending_interrupts & enable_interrupts;
        for (int i = 0; i < 32; i++) {
            if ((1 << i) & mask) {
                irq_num = i;
                break;
            }
        }
    }
    if (pending_exception != ~0u) {
        raise_exception(pending_exception, pending_tval);
    } else {
        if (pipeline_context.tkn) {
            pc = pipeline_context.jmp_pc;
        } else {
            pc = pc + (pipeline_context.cinsn ? 2 : 4);
        }
        if (mask != 0) {
            raise_exception(kInterruptCauseBit | irq_num, pending_tval);
        }
    }
}
