/**
 * @file ExecuteStage.cpp
 * @brief Execution (EX) stage implementation.
 */
#include <bit>
#include <cstdint>
#include <utility>

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/debug/SpikeLockstep.hpp"
#include "simrv/execute/ExecuteUnit.hpp"
#include "simrv/pipeline/OperationTraits.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Helpers.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

using namespace simrv::isa;

// ==========================================
// EX (Execute) Stage
// ==========================================

void CPU::run_execute_stage(Machine& machine) { execute_core(machine); }

void CPU::execute_core(Machine& machine) {
    auto& ctx = active_context();
    if (simrv::compiler::unlikely(ctx.pending_exception.has_value())) {
        return;
    }

    if (ctx.op_id >= isa::OperationId::VSETVLI && ctx.op_id <= isa::OperationId::VWSLL_VI) {
        ctx.tkn = false;
        execute::ExecuteUnit::execute_vector(*this, machine, ctx.op_id, ctx.ir);
        // VS dirty tracking may be imprecise. Conservatively mark it Dirty
        // after dispatch because vector instructions can update registers,
        // vl/vtype/vstart/vxsat, or partial state before a restartable fault.
        state_.mstatus |= enum_mask(MstatusBit::Vs);
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
            ctx.wb_data = (ctx.cpc + ctx.imm).raw();
            break;
        case Opcode::Jal:
            ctx.tkn = true;
            ctx.wb_data = (ctx.cpc + ((ctx.cinsn != 0u) ? 2 : 4)).raw();
            ctx.jmp_pc = (ctx.cpc + ctx.imm).raw();
            break;
        case Opcode::Jalr:
            ctx.tkn = true;
            ctx.wb_data = (ctx.cpc + ((ctx.cinsn != 0u) ? 2 : 4)).raw();
            ctx.jmp_pc = (ctx.rrs1 + ctx.imm) & ~static_cast<Register>(1);
            if (state_.regs.xlen == 32) {
                ctx.jmp_pc =
                    static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(ctx.jmp_pc)));
            }
            break;
        case Opcode::Op:
            ctx.tkn = false;
            ctx.wb_data =
                execute::ExecuteUnit::aluInt(ctx.rrs1, ctx.rrs2, ctx.op_id, state_.regs.xlen);
            break;
        case Opcode::OpImm:
            ctx.tkn = false;
            ctx.wb_data =
                execute::ExecuteUnit::aluInt(ctx.rrs1, ctx.imm, ctx.op_id, state_.regs.xlen);
            break;
        case Opcode::OpImm32:
            ctx.tkn = false;
            ctx.wb_data = execute::ExecuteUnit::aluIntW(ctx.rrs1, ctx.imm, ctx.op_id);
            break;
        case Opcode::Op32:
            ctx.tkn = false;
            ctx.wb_data = execute::ExecuteUnit::aluIntW(ctx.rrs1, ctx.rrs2, ctx.op_id);
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
                decode_cache.flush();
            } else if (ctx.ir == 0x0100000f) {
                // RISC-V PAUSE (Zihintpause / cpu_relax): architectural NOP pipeline hint
            }
            break;
        case Opcode::Branch:
            ctx.tkn =
                execute::ExecuteUnit::branchTaken(ctx.rrs1, ctx.rrs2, ctx.funct3, state_.regs.xlen);
            ctx.jmp_pc = (ctx.cpc + ctx.imm).raw();
            if (state_.regs.xlen == 32) {
                ctx.jmp_pc =
                    static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(ctx.jmp_pc)));
            }
            break;
        case Opcode::Amo:
            ctx.tkn = false;
            ctx.mem_addr = ctx.rrs1;
            if (ctx.funct5 == Funct5Amo::Sc) {
                // Zalrsc requires natural alignment even when the reservation will make SC fail.
                // A failed reservation must not bypass the architecturally required exception.
                if (!amo_address_aligned(ctx.mem_addr, ctx.funct3)) {
                    ctx.pending_exception = ExceptionCode::MisalignedStore;
                    ctx.pending_tval = ctx.mem_addr;
                    break;
                }
                const bool native_success =
                    machine.memory_.reservation_table().check_and_clear_reservation(
                        static_cast<HartId>(state_.mhartid), ctx.rrs1);
                state_.reserved = native_success ? 1 : 0;
                state_.load_res = ctx.rrs1;
                ctx.wb_data = native_success ? 0 : 1;
                if (machine.lockstep() && machine.lockstep()->is_running()) {
                    if (native_success) {
                        auto sc_success_opt = machine.lockstep()->determine_sc_success();
                        if (sc_success_opt.has_value() && !sc_success_opt.value()) {
                            ctx.wb_data = 1;
                        }
                    }
                }
            }
            break;
        case Opcode::System:
            execute_system(machine);
            break;
        case Opcode::Custom0:
            ctx.tkn = false;
            break;
        case Opcode::MAdd:
        case Opcode::MSub:
        case Opcode::NMAdd:
        case Opcode::NMSub:
        case Opcode::OpFp:
            execute_fp(machine);
            break;
        default:
            ctx.tkn = false;
            ctx.pending_exception = ExceptionCode::IllegalInstruction;
            ctx.pending_tval = ctx.ir;
            break;
    }
}

void CPU::execute_system(Machine& machine) {
    auto& ctx = active_context();
    if (ctx.funct3 == Funct3::Priv) {
        switch (static_cast<Funct12Priv>(ctx.funct12)) {
            case Funct12Priv::Ecall:
                ctx.wb_data_csr =
                    enum_mask(ExceptionCode::UserEcall) + std::to_underlying(state_.priv);
                ctx.pending_exception = static_cast<ExceptionCode>(
                    enum_mask(ExceptionCode::UserEcall) + std::to_underlying(state_.priv));
                break;
            case Funct12Priv::Ebreak: {
                bool semihost_handled = false;
                const bool in_dram = machine.memory_geometry().contains(state_.pc - 4) &&
                                     machine.memory_geometry().contains(state_.pc + 4);
                if (in_dram) {
                    const Word inst_prev = simrv::memory::ram_read_fast(
                        state_.pc - 4, static_cast<Instruction>(Funct3::Lw), machine.ram_view());
                    const Word inst_next = simrv::memory::ram_read_fast(
                        state_.pc + 4, static_cast<Instruction>(Funct3::Lw), machine.ram_view());
                    if (inst_prev == 0x01f01013 && inst_next == 0x40705013) {
                        semihost_handled = true;
                        const Word semihost_op = state_.regs.read(RegId::A0);
                        const Address arg_ptr = state_.regs.read(RegId::A1);

                        switch (semihost_op) {
                            case 0x05: {
                                const Instruction load_op =
                                    kIsXLen64 ? static_cast<Instruction>(Funct3::Ld)
                                              : static_cast<Instruction>(Funct3::Lw);
                                const Address fd = simrv::memory::ram_read_fast(arg_ptr, load_op,
                                                                                machine.ram_view());
                                const Address buf_addr = simrv::memory::ram_read_fast(
                                    arg_ptr + (kIsXLen64 ? 8 : 4), load_op, machine.ram_view());
                                const Address len = simrv::memory::ram_read_fast(
                                    arg_ptr + (kIsXLen64 ? 16 : 8), load_op, machine.ram_view());
                                (void)fd;

                                if (machine.memory_geometry().contains(buf_addr)) {
                                    for (Address i = 0; i < len; ++i) {
                                        const auto ch = static_cast<uint8_t>(
                                            simrv::memory::ram_read_fast(
                                                buf_addr + i, static_cast<Instruction>(Funct3::Lb),
                                                machine.ram_view()) &
                                            0xFF);
                                        machine.console_write(static_cast<char>(ch));
                                    }
                                }
                                state_.regs.write(RegId::A0, 0);
                                break;
                            }
                            case 0x03: {
                                if (machine.memory_geometry().contains(arg_ptr)) {
                                    const auto ch = static_cast<uint8_t>(
                                        simrv::memory::ram_read_fast(
                                            arg_ptr, static_cast<Instruction>(Funct3::Lb),
                                            machine.ram_view()) &
                                        0xFF);
                                    machine.console_write(static_cast<char>(ch));
                                }
                                state_.regs.write(RegId::A0, 0);
                                break;
                            }
                            case 0x04: {
                                Address ptr = arg_ptr;
                                if (machine.memory_geometry().contains(ptr)) {
                                    while (true) {
                                        const auto ch = static_cast<uint8_t>(
                                            simrv::memory::ram_read_fast(
                                                ptr, static_cast<Instruction>(Funct3::Lb),
                                                machine.ram_view()) &
                                            0xFF);
                                        if (ch == 0) break;
                                        machine.console_write(static_cast<char>(ch));
                                        ptr++;
                                    }
                                }
                                state_.regs.write(RegId::A0, 0);
                                break;
                            }
                            default:
                                simrv::log::warn("__ Unhandled semihosting op: 0x{:02x}",
                                                 semihost_op);
                                state_.regs.write(RegId::A0, static_cast<Word>(-1));
                                break;
                        }

                        ctx.tkn = true;
                        ctx.jmp_pc = state_.pc + 8;
                    }
                }

                if (!semihost_handled) {
                    if (machine.debugger() && machine.debugger()->is_connected()) {
                        // Native guest EBREAK remains an architectural trap, but GDB receives the
                        // all-stop notification before execution resumes.
                        ctx.wb_data_csr = enum_mask(ExceptionCode::Breakpoint);
                        ctx.pending_exception = ExceptionCode::Breakpoint;
                        ctx.tkn = false;
                        machine.debug_halt(static_cast<HartId>(state_.mhartid), GdbSignal::SigTrap,
                                           "swbreak:;");
                    } else {
                        ctx.wb_data_csr = enum_mask(ExceptionCode::Breakpoint);
                        ctx.pending_exception = ExceptionCode::Breakpoint;
                        ctx.tkn = false;
                    }
                }
                break;
            }
            case Funct12Priv::Uret:
            case Funct12Priv::Sret:
            case Funct12Priv::Mret:
                ctx.tkn = true;
                ctx.jmp_pc = ctx.rcsr;
                break;
            case Funct12Priv::Wfi: {
                ctx.tkn = false;
                if (machine.appmode_enabled() && (state_.mip & state_.mie) == 0) {
                    const Counter cur_cmp = clint_mmio.mtimecmp.load(std::memory_order_relaxed);
                    if (cur_cmp > clint_mmio.mtime.load(std::memory_order_relaxed) &&
                        cur_cmp != std::numeric_limits<Counter>::max()) {
                        clint_mmio.mtime.store(cur_cmp, std::memory_order_relaxed);
                        evaluate_timer_interrupt();
                    }
                }
                break;
            }
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
            execute::ExecuteUnit::csrWriteValue(ctx.rcsr_write, ctx.rrs1, csr_val_imm, ctx.funct3);
        if (csr_result.has_value()) {
            ctx.tkn = false;
            ctx.wb_data_csr = csr_result.value();
        } else {
            ctx.pending_exception = static_cast<ExceptionCode>(csr_result.error());
            ctx.pending_tval = ctx.ir;
        }
    }
}

void CPU::execute_fp(Machine& /*machine*/) {
    auto& ctx = active_context();
    switch (ctx.opcode) {
        case Opcode::MAdd:
        case Opcode::MSub:
        case Opcode::NMAdd:
        case Opcode::NMSub: {
            ctx.tkn = false;
            const Word rm =
                (enum_mask(ctx.funct3) == 7) ? ((state_.fcsr >> 5) & 0x7) : enum_mask(ctx.funct3);
            if (simrv::compiler::unlikely(rm >= 5)) {
                ctx.pending_exception = ExceptionCode::IllegalInstruction;
                ctx.pending_tval = ctx.ir;
                break;
            }
            const Word fmt = ctx.funct7 & 0x3;
            const Word rs3 = (ctx.ir >> 27) & 0x1F;
            const CSRValue old_fcsr = state_.fcsr;
            const auto fp = execute::ExecuteUnit::fusedFp(
                ctx.opcode, fmt, std::to_underlying(ctx.rs1), std::to_underlying(ctx.rs2), rs3,
                enum_mask(ctx.funct3), state_.regs.fp_data_ptr(), state_.fcsr);
            if (state_.fcsr != old_fcsr) {
                state_.mstatus |= enum_mask(MstatusBit::Fs);
            }
            ctx.fp_wb_data = fp.fp_wb_data;
            ctx.fp_wb_enable = fp.fp_wb_enable;
            break;
        }
        case Opcode::OpFp: {
            ctx.tkn = false;
            const Word rm =
                (enum_mask(ctx.funct3) == 7) ? ((state_.fcsr >> 5) & 0x7) : enum_mask(ctx.funct3);
            if (simrv::compiler::unlikely(rm >= 5)) {
                ctx.pending_exception = ExceptionCode::IllegalInstruction;
                ctx.pending_tval = ctx.ir;
                break;
            }
            const CSRValue old_fcsr = state_.fcsr;
            const auto fp = execute::ExecuteUnit::opFp(
                ctx.funct7, ctx.funct3, std::to_underlying(ctx.rs1), std::to_underlying(ctx.rs2),
                ctx.rrs1, state_.regs.fp_data_ptr(), state_.fcsr);
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
            break;
    }
}

}  // namespace simrv::core
