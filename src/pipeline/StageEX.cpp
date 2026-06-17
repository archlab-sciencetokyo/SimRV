/**
 * @file StageEX.cpp
 * @brief EX stage implementation for Machine.
 */
#include <iostream>
#include <thread>
#include <chrono>
#include "simrv/core/Logger.hpp"

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/execute/ExecuteUnit.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"
#include "simrv/debug/SpikeLockstep.hpp"

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
            // Preserve SUBW/SRAW bit (0x20) and M-extension bit (0x01)
            ctx.funct7 &= ((enum_mask(ctx.funct3) == 0x0u) ||
                           (enum_mask(ctx.funct3) == 0x5u))
                              ? 0x21
                              : 0x01;
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
                decode_cache.flush();
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
                const bool native_success = (ctx.rrs1 == state_.load_res) && (state_.reserved != 0u);
                ctx.wb_data = native_success ? 0 : 1;
                if (machine.spike_lockstep && machine.spike_lockstep->is_running()) {
                    if (native_success) {
                        auto sc_success_opt = machine.spike_lockstep->determine_sc_success();
                        if (sc_success_opt.has_value() && !sc_success_opt.value()) {
                            ctx.wb_data = 1;
                        }
                    }
                }
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
                     case Funct12Priv::Ebreak: {
                         bool semihost_handled = false;
                         const bool in_dram = simrv::memory::is_dram_addr(state_.pc - 4) && simrv::memory::is_dram_addr(state_.pc + 4);
                         if (in_dram) {
                             const Word inst_prev = simrv::memory::ram_read_fast(state_.pc - 4, static_cast<Instruction>(Funct3::Lw), machine.mmem);
                             const Word inst_next = simrv::memory::ram_read_fast(state_.pc + 4, static_cast<Instruction>(Funct3::Lw), machine.mmem);
                             if (inst_prev == 0x01f01013 && inst_next == 0x40705013) {
                                 semihost_handled = true;
                                 const Word semihost_op = state_.regs.read(static_cast<RegId>(10)); // a0
                                 const Address arg_ptr = state_.regs.read(static_cast<RegId>(11)); // a1
                                 
                                 if (semihost_op == 0x05) { // SYS_WRITE
                                     const Instruction load_op = kIsXLen64 ? static_cast<Instruction>(Funct3::Ld) : static_cast<Instruction>(Funct3::Lw);
                                     const Address fd = simrv::memory::ram_read_fast(arg_ptr, load_op, machine.mmem);
                                     const Address buf_addr = simrv::memory::ram_read_fast(arg_ptr + (kIsXLen64 ? 8 : 4), load_op, machine.mmem);
                                     const Address len = simrv::memory::ram_read_fast(arg_ptr + (kIsXLen64 ? 16 : 8), load_op, machine.mmem);
                                     (void)fd;
                                     
                                     if (simrv::memory::is_dram_addr(buf_addr)) {
                                         for (Address i = 0; i < len; ++i) {
                                             const auto ch = static_cast<uint8_t>(simrv::memory::ram_read_fast(buf_addr + i, static_cast<Instruction>(Funct3::Lb), machine.mmem) & 0xFF);
                                             if (machine.s_tuimode && machine.uart && machine.uart->tui()) {
                                                 machine.uart->tui()->handle_char_write(static_cast<char>(ch));
                                             } else {
                                                 (void)(::write(STDOUT_FILENO, &ch, 1) == 0);
                                             }
                                         }
                                     }
                                     state_.regs.write(static_cast<RegId>(10), 0); // Success: returns 0 bytes NOT written
                                 } else if (semihost_op == 0x03) { // SYS_WRITEC
                                     if (simrv::memory::is_dram_addr(arg_ptr)) {
                                         const auto ch = static_cast<uint8_t>(simrv::memory::ram_read_fast(arg_ptr, static_cast<Instruction>(Funct3::Lb), machine.mmem) & 0xFF);
                                         if (machine.s_tuimode && machine.uart && machine.uart->tui()) {
                                             machine.uart->tui()->handle_char_write(static_cast<char>(ch));
                                         } else {
                                             (void)(::write(STDOUT_FILENO, &ch, 1) == 0);
                                         }
                                     }
                                     state_.regs.write(static_cast<RegId>(10), 0);
                                 } else if (semihost_op == 0x04) { // SYS_WRITE0
                                     Address ptr = arg_ptr;
                                     if (simrv::memory::is_dram_addr(ptr)) {
                                         while (true) {
                                             const auto ch = static_cast<uint8_t>(simrv::memory::ram_read_fast(ptr, static_cast<Instruction>(Funct3::Lb), machine.mmem) & 0xFF);
                                             if (ch == 0) break;
                                             if (machine.s_tuimode && machine.uart && machine.uart->tui()) {
                                                 machine.uart->tui()->handle_char_write(static_cast<char>(ch));
                                             } else {
                                                 (void)(::write(STDOUT_FILENO, &ch, 1) == 0);
                                             }
                                             ptr++;
                                         }
                                     }
                                     state_.regs.write(static_cast<RegId>(10), 0);
                                 } else {
                                     simrv::log::warn("__ Unhandled semihosting op: 0x{:02x}", semihost_op);
                                     state_.regs.write(static_cast<RegId>(10), static_cast<Word>(-1));
                                 }
                                 
                                 // Advance PC past signature (skip srai instruction)
                                 ctx.tkn = true;
                                 ctx.jmp_pc = state_.pc + 8;
                             }
                         }
                         
                         if (!semihost_handled) {
                             ctx.wb_data_csr = enum_mask(ExceptionCode::Breakpoint);
                             ctx.pending_exception = ExceptionCode::Breakpoint;
                             ctx.tkn = false;
                             e_icount++;
                         }
                         break;
                     }
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
