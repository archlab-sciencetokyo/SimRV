/**
 * @file CommitStage.cpp
 * @brief Writeback (WB) and Commit stages implementation.
 */
#include <cstdint>
#include <utility>

#include "simrv/CompilerHints.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/Tracer.hpp"
#include "simrv/isa/Priv.hpp"
#include "simrv/pipeline/RetirementEffects.hpp"
#include "simrv/xlen/AddressTypes.hpp"

namespace simrv::core {

using namespace simrv::isa;

// ==========================================
// WB (Writeback) Stage
// ==========================================

void CPU::run_writeback_stage(Machine& machine) { writeback_registers(machine); }

void CPU::writeback_registers(Machine& machine) {
    const auto effects = pipeline::build_writeback_effects(active_context());
    account_retirement(effects.increments_instruction_count);
    if (effects.increments_instruction_count != 0) {
        record_hpm_event(HpmEvent::Instructions, effects.increments_instruction_count);
        record_hpm_instruction_classes(active_context().op_id,
                                       static_cast<Opcode>(active_context().opcode),
                                       active_context().cinsn != 0);
    }
    if (simrv::compiler::unlikely(machine.instruction_mix_enabled()) &&
        effects.increments_instruction_count != 0) {
        const auto op = active_context().op_id;
        e_instmix[static_cast<std::size_t>(op)]++;
        const auto cur_cycle = clint_mmio.mcycle;
        const auto delta = cur_cycle > last_retire_cycle_ ? (cur_cycle - last_retire_cycle_) : 1;
        last_retire_cycle_ = cur_cycle;
        perf_counters.record_retire(op, delta);
    }
    if (effects.floating_write.enabled) {
        if (machine.trace().is_register_write_trace_enabled()) {
            active_context().fp_old_value = state_.regs.read_fp(effects.floating_write.destination);
        }
        state_.regs.write_fp(effects.floating_write.destination, effects.floating_write.value);
    }
    if (effects.marks_floating_point_dirty) state_.mark_extension_dirty(MstatusBit::Fs);
    if (effects.integer_write.enabled) {
        state_.regs.write_branchless(effects.integer_write.destination,
                                     effects.integer_write.value);
    }
}

// ==========================================
// COMMIT (Commit) Stage
// ==========================================

namespace {
[[nodiscard]] inline auto xret_target_aligned(const ArchState& state,
                                              simrv::xlen::VirtAddr jmp_pc) noexcept -> bool {
    const bool has_c = misa_has_extension(state.misa, isa::IsaExtension::C);
    const simrv::xlen::Word alignment_mask = has_c ? 1u : 3u;
    return (jmp_pc & alignment_mask) == 0;
}
}  // namespace

void CPU::run_commit_stage(Machine& machine) { commit_control_flow_and_traps(machine); }

void CPU::commit_control_flow_and_traps([[maybe_unused]] Machine& machine) {
    auto& ctx = active_context();
    if (ctx.cinsn != 0u && !ctx.pending_exception.has_value()) {
        e_ccount++;
    }

    const auto opcode = static_cast<Opcode>(ctx.opcode);
    const auto funct3 = static_cast<Funct3>(ctx.funct3);

    if (!ctx.pending_exception.has_value() && opcode == Opcode::System && ctx.op_id != MOP &&
        !isa::is_hypervisor_memory_op(ctx.op_id)) {
        if (funct3 == Funct3::Priv) {
            switch (static_cast<Funct12Priv>(ctx.funct12)) {
                case Funct12Priv::Uret: {
                    break;
                }
                case Funct12Priv::Sret: {
                    if (!xret_target_aligned(state_, ctx.jmp_pc)) {
                        ctx.pending_exception = ExceptionCode::MisalignedFetch;
                        ctx.pending_tval = ctx.jmp_pc.raw();
                        break;
                    }
                    sret();
                    break;
                }
                case Funct12Priv::Mret: {
                    if (!xret_target_aligned(state_, ctx.jmp_pc)) {
                        ctx.pending_exception = ExceptionCode::MisalignedFetch;
                        ctx.pending_tval = ctx.jmp_pc.raw();
                        break;
                    }
                    mret();
                    break;
                }
                default:
                    if (ctx.funct7 ==
                            static_cast<simrv::xlen::Instruction>(Funct7Priv::SfenceVma) ||
                        ctx.funct7 ==
                            static_cast<simrv::xlen::Instruction>(Funct7Priv::SinvalVma)) {
                        const bool match_all_vaddr = (std::to_underlying(ctx.rs1) == 0);
                        const bool match_all_asid = (std::to_underlying(ctx.rs2) == 0);
                        TLB_flush(match_all_vaddr, ctx.rrs1, match_all_asid,
                                  static_cast<simrv::xlen::Word>(ctx.rrs2));
                    } else if (ctx.funct7 ==
                                   static_cast<simrv::xlen::Instruction>(Funct7Priv::HfenceVvma) ||
                               ctx.funct7 == static_cast<simrv::xlen::Instruction>(Funct7Priv::HinvalVvma) ||
                               ctx.funct7 == static_cast<simrv::xlen::Instruction>(
                                                 Funct7Priv::HfenceGvma) ||
                               ctx.funct7 == static_cast<simrv::xlen::Instruction>(
                                                 Funct7Priv::HinvalGvma)) {
                        TLB_flush();
                    }
                    break;
            }
        } else {
            const bool is_write = (funct3 == Funct3::Csrrw || funct3 == Funct3::Csrrwi) ||
                                  (std::to_underlying(ctx.rs1) != 0);
            if (is_write) {
                auto res = write_csr(simrv::xlen::CsrNumber{ctx.funct12}, ctx.wb_data_csr);
                if (!res) {
                    ctx.pending_exception = res.error();
                }
            }
        }
    }

    if (ctx.pending_exception.has_value()) {
        state_.pc = ctx.cpc.raw();
        raise_exception(std::to_underlying(*ctx.pending_exception), ctx.pending_tval);
    } else {
        if (ctx.tkn != 0u) {
            const bool has_c = misa_has_extension(state_.misa, isa::IsaExtension::C);
            const simrv::xlen::Word alignment_mask = has_c ? 1u : 3u;
            if ((ctx.jmp_pc & alignment_mask) != 0) {
                ctx.pending_exception = ExceptionCode::MisalignedFetch;
                ctx.pending_tval = ctx.jmp_pc.raw();
                raise_exception(std::to_underlying(*ctx.pending_exception), ctx.pending_tval);
                return;
            }
            state_.pc = ctx.jmp_pc.raw();
        } else {
            state_.pc = (ctx.cpc + ((ctx.cinsn != 0u) ? 2 : 4)).raw();
        }
        if (state_.regs.xlen == 32) {
            state_.pc = static_cast<simrv::xlen::Register>(
                static_cast<int64_t>(static_cast<int32_t>(state_.pc)));
        }
        dispatch_pending_interrupts();
    }
}

void CPU::run_commit_stage_baremetal([[maybe_unused]] Machine& machine) {
    commit_control_flow_and_traps(machine);
}

}  // namespace simrv::core
