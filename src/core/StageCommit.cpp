/**
 * @file StageCommit.cpp
 * @brief Commit stage implementation for Machine.
 */
#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

void CPU::run_commit_stage(Machine& machine) { commit_control_flow_and_traps(machine); }

/* commit_control_flow_and_traps(Complete) stage */
void CPU::commit_control_flow_and_traps(Machine& machine) {
    auto& ctx = pipeline_context;
    if (ctx.cinsn != 0u && !ctx.pending_exception.has_value()) {
        e_ccount++; /** for evaluation **/
    }

    const auto opcode = static_cast<Opcode>(ctx.opcode);
    const auto funct3 = static_cast<Funct3>(ctx.funct3);

    if (!ctx.pending_exception.has_value() && opcode == Opcode::System) {
        if (funct3 == Funct3::Priv) {
            switch (static_cast<Funct12Priv>(ctx.funct12)) {
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
                    if (ctx.funct7 == static_cast<Instruction>(Funct7Priv::SfenceVma)) {
                        const bool match_all_vaddr = (std::to_underlying(ctx.rs1) == 0);
                        const bool match_all_asid = (std::to_underlying(ctx.rs2) == 0);
                        TLB_flush(match_all_vaddr, ctx.rrs1, match_all_asid,
                                  static_cast<Word>(ctx.rrs2));
                        dcache.flush();
                    }
                    break;
            }
        } else {
            auto res = write_csr(static_cast<CSRAddress>(ctx.funct12), ctx.wb_data_csr); if (!res) { ctx.pending_exception = res.error(); }
        }
    }

    Word const pending_interrupts = state_.mip & state_.mie;
    Word enable_interrupts = 0;
    Word mask = 0;
    Word irq_num = 32;
    if (pending_interrupts != 0u) {
        switch (state_.priv) {
            case kPrivMachine: {
                if ((state_.mstatus & enum_mask(MstatusBit::Mie)) != 0u) {
                    enable_interrupts = ~state_.mideleg;
                }
                break;
            }
            case kPrivSupervisor: {
                enable_interrupts = ~state_.mideleg;
                if ((state_.mstatus & enum_mask(MstatusBit::Sie)) != 0u) {
                    enable_interrupts |= state_.mideleg;
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
        for (int i = 31; i >= 0; i--) {
            if (((1u << i) & mask) != 0u) {
                irq_num = i;
                break;
            }
        }
    }
    if (ctx.pending_exception.has_value()) {
        raise_exception(std::to_underlying(*ctx.pending_exception), ctx.pending_tval);
    } else {
        if (ctx.tkn != 0u) {
            state_.pc = ctx.jmp_pc;
        } else {
            state_.pc = state_.pc + ((ctx.cinsn != 0u) ? 2 : 4);
        }
        if (mask != 0) {
            raise_exception(kInterruptCauseBit | irq_num, ctx.pending_tval);
        }
    }
}

}  // namespace simrv::core