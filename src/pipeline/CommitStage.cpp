/**
 * @file CommitStage.cpp
 * @brief Writeback (WB) and Commit stages implementation.
 */
#include <cstdint>
#include <utility>

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/pipeline/RetirementEffects.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Helpers.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

using namespace simrv::isa;

// ==========================================
// WB (Writeback) Stage
// ==========================================

void CPU::run_writeback_stage(Machine& machine) { writeback_registers(machine); }

void CPU::writeback_registers(Machine& machine) {
    const auto effects = pipeline::build_writeback_effects(active_context());
    e_icount += effects.increments_instruction_count;
    if (simrv::compiler::unlikely(machine.instruction_mix_enabled()) &&
        effects.increments_instruction_count != 0) {
        e_instmix[static_cast<std::size_t>(active_context().op_id)]++;
    }
    if (effects.floating_write.enabled) {
        state_.regs.write_fp(effects.floating_write.destination, effects.floating_write.value);
    }
    if (effects.marks_floating_point_dirty) state_.mstatus |= enum_mask(MstatusBit::Fs);
    if (effects.integer_write.enabled) {
        state_.regs.write_branchless(effects.integer_write.destination,
                                     effects.integer_write.value);
    }
}

// ==========================================
// COMMIT (Commit) Stage
// ==========================================

void CPU::run_commit_stage(Machine& machine) { commit_control_flow_and_traps(machine); }

void CPU::commit_control_flow_and_traps([[maybe_unused]] Machine& machine) {
    auto& ctx = active_context();
    if (ctx.cinsn != 0u && !ctx.pending_exception.has_value()) {
        e_ccount++;
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
                    }
                    break;
            }
        } else {
            const bool is_write = (funct3 == Funct3::Csrrw || funct3 == Funct3::Csrrwi) ||
                                  (std::to_underlying(ctx.rs1) != 0);
            if (is_write) {
                auto res = write_csr(static_cast<CSRAddress>(ctx.funct12), ctx.wb_data_csr);
                if (!res) {
                    ctx.pending_exception = res.error();
                }
            }
        }
    }

    Word const pending_interrupts = state_.mip & state_.mie;
    Word enable_interrupts = 0;
    Word mask = 0;
    Word irq_num = 32;
    if (simrv::compiler::unlikely(pending_interrupts != 0u)) {
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
        if (mask != 0) {
            irq_num = select_highest_priority_interrupt(mask);
        }
    }
    if (ctx.pending_exception.has_value()) {
        state_.pc = ctx.cpc.raw();
        raise_exception(std::to_underlying(*ctx.pending_exception), ctx.pending_tval);
    } else {
        if (ctx.tkn != 0u) {
            const bool has_c = misa_has_extension(state_.misa, isa::IsaExtension::C);
            const Word alignment_mask = has_c ? 1u : 3u;
            if ((ctx.jmp_pc & alignment_mask) != 0) {
                ctx.pending_exception = ExceptionCode::MisalignedFetch;
                ctx.pending_tval = ctx.jmp_pc;
                raise_exception(std::to_underlying(*ctx.pending_exception), ctx.pending_tval);
                return;
            }
            state_.pc = ctx.jmp_pc;
        } else {
            state_.pc = (ctx.cpc + ((ctx.cinsn != 0u) ? 2 : 4)).raw();
        }
        if (state_.regs.xlen == 32) {
            state_.pc =
                static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(state_.pc)));
        }
        if (mask != 0) {
            raise_exception(kInterruptCauseBit | irq_num, 0);
        }
    }
}

void CPU::run_commit_stage_baremetal([[maybe_unused]] Machine& machine) {
    auto& ctx = active_context();
    if (ctx.cinsn != 0u && !ctx.pending_exception.has_value()) {
        e_ccount++;
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
                    }
                    break;
            }
        } else {
            const bool is_write = (funct3 == Funct3::Csrrw || funct3 == Funct3::Csrrwi) ||
                                  (std::to_underlying(ctx.rs1) != 0);
            if (is_write) {
                auto res = write_csr(static_cast<CSRAddress>(ctx.funct12), ctx.wb_data_csr);
                if (!res) {
                    ctx.pending_exception = res.error();
                }
            }
        }
    }

    Word const pending_interrupts = state_.mip & state_.mie;
    Word enable_interrupts = 0;
    Word mask = 0;
    Word irq_num = 32;
    if (simrv::compiler::unlikely(pending_interrupts != 0u)) {
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
        if (mask != 0) {
            irq_num = select_highest_priority_interrupt(mask);
        }
    }

    if (ctx.pending_exception.has_value()) {
        state_.pc = ctx.cpc.raw();
        raise_exception(std::to_underlying(*ctx.pending_exception), ctx.pending_tval);
    } else {
        if (ctx.tkn != 0u) {
            const bool has_c = misa_has_extension(state_.misa, isa::IsaExtension::C);
            const Word alignment_mask = has_c ? 1u : 3u;
            if ((ctx.jmp_pc & alignment_mask) != 0) {
                ctx.pending_exception = ExceptionCode::MisalignedFetch;
                ctx.pending_tval = ctx.jmp_pc;
                raise_exception(std::to_underlying(*ctx.pending_exception), ctx.pending_tval);
                return;
            }
            state_.pc = ctx.jmp_pc;
        } else {
            state_.pc = (ctx.cpc + ((ctx.cinsn != 0u) ? 2 : 4)).raw();
        }
        if (state_.regs.xlen == 32) {
            state_.pc =
                static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(state_.pc)));
        }
        if (simrv::compiler::unlikely(mask != 0)) {
            raise_exception(kInterruptCauseBit | irq_num, 0);
        }
    }
}

}  // namespace simrv::core
