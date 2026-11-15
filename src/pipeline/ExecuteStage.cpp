/**
 * @file ExecuteStage.cpp
 * @brief Execution (EX) stage implementation.
 */
#include <cstdint>
#include <utility>

#include "simrv/CompilerHints.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/debug/GdbStub.hpp"
#include "simrv/debug/SpikeLockstep.hpp"
#include "simrv/execute/ExecuteUnit.hpp"
#include "simrv/isa/Amo.hpp"
#include "simrv/isa/Priv.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/xlen/AddressTypes.hpp"
#include "simrv/xlen/RegisterTypes.hpp"

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

    if (isa::is_vector_op(ctx.op_id)) {
        ctx.tkn = false;
        execute::ExecuteUnit::execute_vector(*this, machine, ctx.op_id, ctx.ir);
        // VS dirty tracking may be imprecise. Conservatively mark it Dirty
        // after dispatch because vector instructions can update registers,
        // vl/vtype/vstart/vxsat, or partial state before a restartable fault.
        state_.mark_extension_dirty(MstatusBit::Vs);
        return;
    }

    if (isa::is_code_size_op(ctx.op_id)) {
        ctx.opcode = static_cast<Opcode>(0);
        ctx.tkn = false;
        ctx.fp_wb_enable = false;
        ctx.int_wb_from_fp = false;

        const uint16_t ir16 = static_cast<uint16_t>(ctx.ir_org & 0xFFFFU);
        const auto sreg_from_3bit = [](uint32_t idx) noexcept -> simrv::xlen::RegId {
            return static_cast<simrv::xlen::RegId>((idx < 2U) ? (8U + idx) : (16U + idx));
        };
        const auto build_rlist = [&](uint32_t rlist,
                                     std::array<simrv::xlen::RegId, 13>& out) -> uint32_t {
            static constexpr std::array<simrv::xlen::RegId, 13> kZcmpOrder = {
                simrv::xlen::RegId::Ra, simrv::xlen::RegId::S0, simrv::xlen::RegId::S1,
                simrv::xlen::RegId::S2, simrv::xlen::RegId::S3, simrv::xlen::RegId::S4,
                simrv::xlen::RegId::S5, simrv::xlen::RegId::S6, simrv::xlen::RegId::S7,
                simrv::xlen::RegId::S8, simrv::xlen::RegId::S9, simrv::xlen::RegId::S10,
                simrv::xlen::RegId::S11};
            if (rlist < 4U) return 0U;
            const uint32_t count = (rlist == 15U) ? 13U : (rlist - 3U);
            for (uint32_t i = 0; i < count; ++i) {
                out[i] = kZcmpOrder[i];
            }
            return count;
        };

        const uint32_t elem_bytes = state_.regs.xlen / 8U;
        const Funct3 mem_f3 = (elem_bytes == 8U) ? Funct3::Ld : Funct3::Lw;

        switch (ctx.op_id) {
            case OperationId::CM_MVA01S: {
                const uint32_t r1s = (ir16 >> 7U) & 7U;
                const uint32_t r2s = (ir16 >> 2U) & 7U;
                const auto v1 = state_.regs.read(sreg_from_3bit(r1s));
                const auto v2 = state_.regs.read(sreg_from_3bit(r2s));
                state_.regs.write(simrv::xlen::RegId::A0, v1);
                state_.regs.write(simrv::xlen::RegId::A1, v2);
                break;
            }
            case OperationId::CM_MVSA01: {
                const uint32_t r1s = (ir16 >> 7U) & 7U;
                const uint32_t r2s = (ir16 >> 2U) & 7U;
                if (r1s == r2s) {
                    ctx.pending_exception = ExceptionCode::IllegalInstruction;
                    ctx.pending_tval = ir16;
                    return;
                }
                const auto a0 = state_.regs.read(simrv::xlen::RegId::A0);
                const auto a1 = state_.regs.read(simrv::xlen::RegId::A1);
                state_.regs.write(sreg_from_3bit(r1s), a0);
                state_.regs.write(sreg_from_3bit(r2s), a1);
                break;
            }
            case OperationId::CM_PUSH: {
                const uint32_t rlist = (ir16 >> 4U) & 0xFU;
                const uint32_t spimm = (ir16 >> 2U) & 3U;
                if (rlist < 4U || (misa_has_extension(state_.misa, IsaExtension::E) &&
                                   state_.regs.xlen == 32 && rlist > 6U)) {
                    ctx.pending_exception = ExceptionCode::IllegalInstruction;
                    ctx.pending_tval = ir16;
                    return;
                }
                std::array<simrv::xlen::RegId, 13> regs_list{};
                const uint32_t count = build_rlist(rlist, regs_list);
                const uint32_t base_adj = ((count * elem_bytes) + 15U) & ~15U;
                const uint32_t stack_adj = base_adj + (spimm * 16U);
                const auto sp = state_.regs.read(simrv::xlen::RegId::Sp);
                simrv::xlen::Word addr = sp - elem_bytes;
                for (uint32_t idx = count; idx > 0U; --idx) {
                    const auto val = state_.regs.read(regs_list[idx - 1U]);
                    simrv::memory::MemoryAccess::storeInt(machine.memory_, *this,
                                                          simrv::xlen::VirtAddr{addr}, val, mem_f3);
                    if (ctx.pending_exception.has_value()) return;
                    addr -= elem_bytes;
                }
                state_.regs.write(simrv::xlen::RegId::Sp, sp - stack_adj);
                break;
            }
            case OperationId::CM_POP:
            case OperationId::CM_POPRET:
            case OperationId::CM_POPRETZ: {
                const uint32_t rlist = (ir16 >> 4U) & 0xFU;
                const uint32_t spimm = (ir16 >> 2U) & 3U;
                if (rlist < 4U || (misa_has_extension(state_.misa, IsaExtension::E) &&
                                   state_.regs.xlen == 32 && rlist > 6U)) {
                    ctx.pending_exception = ExceptionCode::IllegalInstruction;
                    ctx.pending_tval = ir16;
                    return;
                }
                std::array<simrv::xlen::RegId, 13> regs_list{};
                const uint32_t count = build_rlist(rlist, regs_list);
                const uint32_t base_adj = ((count * elem_bytes) + 15U) & ~15U;
                const uint32_t stack_adj = base_adj + (spimm * 16U);
                const auto sp = state_.regs.read(simrv::xlen::RegId::Sp);
                std::array<simrv::xlen::Word, 13> loaded{};
                simrv::xlen::Word addr = sp + stack_adj - elem_bytes;
                for (uint32_t idx = count; idx > 0U; --idx) {
                    loaded[idx - 1U] = simrv::memory::MemoryAccess::loadInt(
                        machine.memory_, *this, simrv::xlen::VirtAddr{addr}, mem_f3);
                    if (ctx.pending_exception.has_value()) return;
                    addr -= elem_bytes;
                }
                for (uint32_t i = 0; i < count; ++i) {
                    state_.regs.write(regs_list[i], loaded[i]);
                }
                state_.regs.write(simrv::xlen::RegId::Sp, sp + stack_adj);
                if (ctx.op_id == OperationId::CM_POPRETZ) {
                    state_.regs.write(simrv::xlen::RegId::A0, 0);
                }
                if (ctx.op_id == OperationId::CM_POPRET || ctx.op_id == OperationId::CM_POPRETZ) {
                    const auto ra = state_.regs.read(simrv::xlen::RegId::Ra);
                    ctx.tkn = 1;
                    ctx.jmp_pc = simrv::xlen::VirtAddr{ra & ~static_cast<simrv::xlen::Register>(1)};
                }
                break;
            }
            case OperationId::CM_JT:
            case OperationId::CM_JALT: {
                const uint32_t index = (ir16 >> 2U) & 0xFFU;
                const simrv::xlen::Word table_base =
                    static_cast<simrv::xlen::Word>(state_.jvt) & ~simrv::xlen::Word{0x3FULL};
                const simrv::xlen::Word entry_addr = table_base + (index * elem_bytes);
                const simrv::xlen::Word target = simrv::memory::MemoryAccess::loadInt(
                    machine.memory_, *this, simrv::xlen::VirtAddr{entry_addr}, mem_f3);
                if (ctx.pending_exception.has_value()) return;
                if (ctx.op_id == OperationId::CM_JALT) {
                    state_.regs.write(simrv::xlen::RegId::Ra, (ctx.cpc + 2).raw());
                }
                ctx.tkn = 1;
                ctx.jmp_pc = simrv::xlen::VirtAddr{target & ~static_cast<simrv::xlen::Register>(1)};
                break;
            }
            default:
                break;
        }
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
            ctx.jmp_pc = ctx.cpc + ctx.imm;
            break;
        case Opcode::Jalr:
            ctx.tkn = true;
            ctx.wb_data = (ctx.cpc + ((ctx.cinsn != 0u) ? 2 : 4)).raw();
            ctx.jmp_pc = simrv::xlen::VirtAddr{(ctx.rrs1 + ctx.imm) &
                                               ~static_cast<simrv::xlen::Register>(1)};
            if (state_.regs.xlen == 32) {
                ctx.jmp_pc = simrv::xlen::VirtAddr{static_cast<simrv::xlen::Register>(
                    static_cast<int64_t>(static_cast<int32_t>(ctx.jmp_pc.raw())))};
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
            ctx.mem_addr = simrv::xlen::VirtAddr{ctx.rrs1 + ctx.imm};
            break;
        case Opcode::StoreFp:
            ctx.tkn = false;
            ctx.mem_addr = simrv::xlen::VirtAddr{ctx.rrs1 + ctx.imm};
            ctx.fp_mem_wdata = state_.regs.read_fp(simrv::xlen::to_fp_reg_id(ctx.rs2));
            break;
        case Opcode::MiscMem:
            ctx.tkn = false;
            if (isa::is_cmo_op(ctx.op_id)) {
                ctx.mem_addr = simrv::xlen::VirtAddr{ctx.rrs1};
            } else if (ctx.funct3 == Funct3::FenceI) {
                icache.flush();
                decode_cache.flush();
                block_cache.flush();
            } else if (ctx.ir == 0x0100000f) {
                // RISC-V PAUSE (Zihintpause / cpu_relax): architectural NOP pipeline hint
            }
            break;
        case Opcode::Branch:
            ctx.tkn =
                execute::ExecuteUnit::branchTaken(ctx.rrs1, ctx.rrs2, ctx.funct3, state_.regs.xlen);
            ctx.jmp_pc = ctx.cpc + ctx.imm;
            if (state_.regs.xlen == 32) {
                ctx.jmp_pc = simrv::xlen::VirtAddr{static_cast<simrv::xlen::Register>(
                    static_cast<int64_t>(static_cast<int32_t>(ctx.jmp_pc.raw())))};
            }
            break;
        case Opcode::Amo:
            ctx.tkn = false;
            ctx.mem_addr = simrv::xlen::VirtAddr{ctx.rrs1};
            if (!amo_address_aligned(ctx.mem_addr, ctx.funct3)) {
                ctx.pending_exception = (ctx.funct5 == Funct5Amo::Lr) ? ExceptionCode::FaultLoad
                                                                      : ExceptionCode::FaultStore;
                ctx.pending_tval = ctx.mem_addr.raw();
                break;
            }
            break;
        case Opcode::System:
            if (isa::is_hypervisor_memory_op(ctx.op_id)) {
                ctx.tkn = false;
                ctx.mem_addr = simrv::xlen::VirtAddr{ctx.rrs1};
            } else if (ctx.op_id == MOP) {
                ctx.tkn = false;
                ctx.wb_data = 0;
            } else {
                execute_system(machine);
            }
            break;
        case Opcode::Custom0:
            ctx.tkn = false;
            ctx.wb_data =
                cfu_unit.execute(ctx.funct7, std::to_underlying(ctx.funct3), ctx.rrs1, ctx.rrs2);
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
    if (ctx.op_id == isa::OperationId::WRS_NTO || ctx.op_id == isa::OperationId::WRS_STO) {
        if (const auto exc =
                TrapController::check_privileged_instruction(state_, ctx.op_id, ctx.ir)) {
            ctx.pending_exception = *exc;
            ctx.pending_tval = ctx.ir;
            ctx.tkn = false;
            return;
        }
        ctx.tkn = false;
        return;
    }
    if (ctx.funct3 == Funct3::Priv) {
        switch (static_cast<Funct12Priv>(ctx.funct12)) {
            case Funct12Priv::Ecall:
                ctx.pending_exception = ecall_for_privilege(state_.priv, state_.virt);
                ctx.wb_data_csr = std::to_underlying(*ctx.pending_exception);
                break;
            case Funct12Priv::Ebreak: {
                bool semihost_handled = false;
                const simrv::xlen::PhysAddr pc_paddr{state_.pc.raw()};
                const bool in_dram = machine.memory_geometry().contains(pc_paddr - 4) &&
                                     machine.memory_geometry().contains(pc_paddr + 4);
                if (in_dram) {
                    const simrv::xlen::Word inst_prev = simrv::memory::ram_read_fast(
                        pc_paddr - 4, static_cast<simrv::xlen::Instruction>(Funct3::Lw),
                        machine.ram_view());
                    const simrv::xlen::Word inst_next = simrv::memory::ram_read_fast(
                        pc_paddr + 4, static_cast<simrv::xlen::Instruction>(Funct3::Lw),
                        machine.ram_view());
                    if (inst_prev == 0x01f01013 && inst_next == 0x40705013) {
                        semihost_handled = true;
                        const simrv::xlen::Word semihost_op =
                            state_.regs.read(simrv::xlen::RegId::A0);
                        const simrv::xlen::PhysAddr arg_ptr{
                            state_.regs.read(simrv::xlen::RegId::A1)};

                        switch (semihost_op) {
                            case 0x05: {
                                const simrv::xlen::Instruction load_op =
                                    simrv::xlen::kIsXLen64
                                        ? static_cast<simrv::xlen::Instruction>(Funct3::Ld)
                                        : static_cast<simrv::xlen::Instruction>(Funct3::Lw);
                                const simrv::xlen::Address fd = simrv::memory::ram_read_fast(
                                    arg_ptr, load_op, machine.ram_view());
                                const simrv::xlen::PhysAddr buf_addr{simrv::memory::ram_read_fast(
                                    arg_ptr + (simrv::xlen::kIsXLen64 ? 8 : 4), load_op,
                                    machine.ram_view())};
                                const simrv::xlen::Address len = simrv::memory::ram_read_fast(
                                    arg_ptr + (simrv::xlen::kIsXLen64 ? 16 : 8), load_op,
                                    machine.ram_view());
                                (void)fd;

                                if (machine.memory_geometry().contains(buf_addr)) {
                                    for (simrv::xlen::Address i = 0; i < len; ++i) {
                                        const auto ch = static_cast<uint8_t>(
                                            simrv::memory::ram_read_fast(
                                                buf_addr + i,
                                                static_cast<simrv::xlen::Instruction>(Funct3::Lb),
                                                machine.ram_view()) &
                                            0xFF);
                                        machine.console_write(static_cast<char>(ch));
                                    }
                                }
                                state_.regs.write(simrv::xlen::RegId::A0, 0);
                                break;
                            }
                            case 0x03: {
                                if (machine.memory_geometry().contains(arg_ptr)) {
                                    const auto ch = static_cast<uint8_t>(
                                        simrv::memory::ram_read_fast(
                                            arg_ptr,
                                            static_cast<simrv::xlen::Instruction>(Funct3::Lb),
                                            machine.ram_view()) &
                                        0xFF);
                                    machine.console_write(static_cast<char>(ch));
                                }
                                state_.regs.write(simrv::xlen::RegId::A0, 0);
                                break;
                            }
                            case 0x04: {
                                simrv::xlen::PhysAddr ptr = arg_ptr;
                                if (machine.memory_geometry().contains(ptr)) {
                                    while (true) {
                                        const auto ch = static_cast<uint8_t>(
                                            simrv::memory::ram_read_fast(
                                                ptr,
                                                static_cast<simrv::xlen::Instruction>(Funct3::Lb),
                                                machine.ram_view()) &
                                            0xFF);
                                        if (ch == 0) break;
                                        machine.console_write(static_cast<char>(ch));
                                        ptr += 1;
                                    }
                                }
                                state_.regs.write(simrv::xlen::RegId::A0, 0);
                                break;
                            }
                            default:
                                simrv::log::warn("__ Unhandled semihosting op: 0x{:02x}",
                                                 semihost_op);
                                state_.regs.write(simrv::xlen::RegId::A0,
                                                  static_cast<simrv::xlen::Word>(-1));
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
                        ctx.wb_data_csr = to_trap_cause(ExceptionCode::Breakpoint);
                        ctx.pending_exception = ExceptionCode::Breakpoint;
                        ctx.tkn = false;
                        machine.debug_halt(hart_id(), GdbSignal::SigTrap, "swbreak:;");
                    } else {
                        ctx.wb_data_csr = to_trap_cause(ExceptionCode::Breakpoint);
                        ctx.pending_exception = ExceptionCode::Breakpoint;
                        ctx.tkn = false;
                    }
                }
                break;
            }
            case Funct12Priv::Uret:
            case Funct12Priv::Sret:
            case Funct12Priv::Mret:
            case Funct12Priv::Mnret:
                ctx.tkn = true;
                ctx.jmp_pc = simrv::xlen::VirtAddr{ctx.rcsr};
                break;
            case Funct12Priv::Wfi: {
                ctx.tkn = false;
                fast_forward_wfi(machine);
                break;
            }
            default:
                if (ctx.funct7 == static_cast<simrv::xlen::Instruction>(Funct7Priv::SfenceVma) ||
                    ctx.funct7 == static_cast<simrv::xlen::Instruction>(Funct7Priv::SinvalVma) ||
                    ctx.funct7 == static_cast<simrv::xlen::Instruction>(Funct7Priv::HfenceVvma) ||
                    ctx.funct7 == static_cast<simrv::xlen::Instruction>(Funct7Priv::HinvalVvma) ||
                    ctx.funct7 == static_cast<simrv::xlen::Instruction>(Funct7Priv::HfenceGvma) ||
                    ctx.funct7 == static_cast<simrv::xlen::Instruction>(Funct7Priv::HinvalGvma) ||
                    static_cast<Funct12Priv>(ctx.funct12) == Funct12Priv::SfenceWInval ||
                    static_cast<Funct12Priv>(ctx.funct12) == Funct12Priv::SfenceInvalIr) {
                    ctx.tkn = false;
                }
                break;
        }
    } else {
        const auto csr_val_imm =
            ((std::to_underlying(ctx.funct3) & 4) != 0)
                ? static_cast<simrv::xlen::ImmValue>(std::to_underlying(ctx.rs1))
                : ctx.imm;
        auto csr_result =
            execute::ExecuteUnit::csrWriteValue(ctx.rcsr_write, ctx.rrs1, csr_val_imm, ctx.funct3);
        if (csr_result.has_value()) {
            ctx.tkn = false;
            ctx.wb_data_csr = csr_result.value();
        } else {
            ctx.pending_exception = to_exception_code(csr_result.error());
            ctx.pending_tval = ctx.ir;
        }
    }
}

void CPU::execute_fp(Machine& /*machine*/) {
    auto& ctx = active_context();
    const bool zfinx_active = state_.enabled_extensions.has(MultiLetterExtension::Zfinx) &&
                              !misa_has_extension(state_.misa, IsaExtension::F) &&
                              !misa_has_extension(state_.misa, IsaExtension::D);

    const auto load_zfinx_src = [&](std::array<simrv::xlen::FloatingRegister, 32>& fregs,
                                    uint32_t idx, FpFmt fmt) -> bool {
        if (state_.regs.xlen == 32 && fmt == FpFmt::Double && (idx & 1U) != 0U) {
            return false;
        }
        const auto reg = static_cast<simrv::xlen::RegId>(idx);
        if (fmt == FpFmt::Double) {
            if (state_.regs.xlen == 64) {
                fregs[idx] = (idx == 0U) ? 0ULL : static_cast<uint64_t>(state_.regs.read(reg));
            } else {
                const uint32_t lo = (idx == 0U) ? 0U : static_cast<uint32_t>(state_.regs.read(reg));
                const uint32_t hi = (idx == 0U) ? 0U
                                                : static_cast<uint32_t>(state_.regs.read(
                                                      static_cast<simrv::xlen::RegId>(idx + 1U)));
                fregs[idx] = (static_cast<uint64_t>(hi) << 32U) | lo;
            }
        } else if (fmt == FpFmt::Half) {
            const uint64_t raw =
                (idx == 0U) ? 0ULL : (static_cast<uint64_t>(state_.regs.read(reg)) & 0xFFFFULL);
            fregs[idx] = 0xFFFFFFFFFFFF0000ULL | raw;
        } else {
            const uint64_t raw =
                (idx == 0U) ? 0ULL : (static_cast<uint64_t>(state_.regs.read(reg)) & 0xFFFFFFFFULL);
            fregs[idx] = simrv::xlen::kF32BoxerBits | raw;
        }
        return true;
    };

    const auto store_zfinx_dst = [&](simrv::xlen::FloatingRegister val, FpFmt fmt) -> bool {
        const uint32_t rd_idx = std::to_underlying(ctx.rd);
        if (state_.regs.xlen == 32 && fmt == FpFmt::Double && (rd_idx & 1U) != 0U) {
            return false;
        }
        ctx.fp_wb_enable = false;
        ctx.int_wb_from_fp = true;
        if (fmt == FpFmt::Double) {
            if (state_.regs.xlen == 64) {
                ctx.wb_data = static_cast<simrv::xlen::Register>(val);
            } else {
                ctx.wb_data = static_cast<simrv::xlen::Register>(
                    static_cast<int64_t>(static_cast<int32_t>(val & 0xFFFFFFFFULL)));
                ctx.mem_rdata_hi = static_cast<simrv::xlen::Register>(
                    static_cast<int64_t>(static_cast<int32_t>((val >> 32U) & 0xFFFFFFFFULL)));
                ctx.int_wb_pair = (ctx.rd != simrv::xlen::RegId::Zero);
            }
        } else if (fmt == FpFmt::Half) {
            ctx.wb_data = static_cast<simrv::xlen::Register>(
                static_cast<int64_t>(static_cast<int16_t>(val & 0xFFFFULL)));
        } else {
            ctx.wb_data = static_cast<simrv::xlen::Register>(
                static_cast<int64_t>(static_cast<int32_t>(val & 0xFFFFFFFFULL)));
        }
        return true;
    };

    switch (ctx.opcode) {
        case Opcode::MAdd:
        case Opcode::MSub:
        case Opcode::NMAdd:
        case Opcode::NMSub: {
            ctx.tkn = false;
            const isa::RoundingMode rm = isa::resolve_rounding_mode(ctx.funct3, state_.fcsr);
            if (simrv::compiler::unlikely(!isa::is_valid_rounding_mode(rm))) {
                ctx.pending_exception = ExceptionCode::IllegalInstruction;
                ctx.pending_tval = ctx.ir;
                break;
            }
            const auto fmt = static_cast<FpFmt>(ctx.funct7 & 0x3);
            const auto rs3 = static_cast<simrv::xlen::FpRegId>((ctx.ir >> 27) & 0x1F);
            const simrv::xlen::CsrValue old_fcsr = state_.fcsr;
            if (zfinx_active) {
                std::array<simrv::xlen::FloatingRegister, 32> zfinx_fregs{};
                if (!load_zfinx_src(zfinx_fregs, std::to_underlying(ctx.rs1), fmt) ||
                    !load_zfinx_src(zfinx_fregs, std::to_underlying(ctx.rs2), fmt) ||
                    !load_zfinx_src(zfinx_fregs, std::to_underlying(rs3), fmt) ||
                    (state_.regs.xlen == 32 && fmt == FpFmt::Double &&
                     (std::to_underlying(ctx.rd) & 1U) != 0U)) {
                    ctx.pending_exception = ExceptionCode::IllegalInstruction;
                    ctx.pending_tval = ctx.ir;
                    break;
                }
                const auto fp = execute::ExecuteUnit::fusedFp(
                    ctx.opcode, fmt, simrv::xlen::to_fp_reg_id(ctx.rs1),
                    simrv::xlen::to_fp_reg_id(ctx.rs2), rs3,
                    static_cast<isa::RoundingMode>(ctx.funct3), zfinx_fregs.data(), state_.fcsr);
                if (fp.fp_wb_enable && !store_zfinx_dst(fp.fp_wb_data, fmt)) {
                    ctx.pending_exception = ExceptionCode::IllegalInstruction;
                    ctx.pending_tval = ctx.ir;
                }
                break;
            }
            const auto fp = execute::ExecuteUnit::fusedFp(
                ctx.opcode, fmt, simrv::xlen::to_fp_reg_id(ctx.rs1),
                simrv::xlen::to_fp_reg_id(ctx.rs2), rs3, static_cast<isa::RoundingMode>(ctx.funct3),
                state_.regs.fp_data_ptr(), state_.fcsr);
            if (state_.fcsr != old_fcsr) {
                state_.mark_extension_dirty(MstatusBit::Fs);
            }
            ctx.fp_wb_data = fp.fp_wb_data;
            ctx.fp_wb_enable = fp.fp_wb_enable;
            break;
        }
        case Opcode::OpFp: {
            ctx.tkn = false;
            const isa::RoundingMode rm = isa::resolve_rounding_mode(ctx.funct3, state_.fcsr);
            const bool uses_rm =
                (ctx.funct7 <= 0x0D && (ctx.funct7 & 0x03) <= 1) || ctx.funct7 == 0x2C ||
                ctx.funct7 == 0x2D || ctx.funct7 == 0x22 || ctx.funct7 == 0x25 ||
                ctx.funct7 == 0x26 || ctx.funct7 == 0x60 || ctx.funct7 == 0x68 ||
                ctx.funct7 == 0x69 ||
                ((ctx.funct7 == 0x20 || ctx.funct7 == 0x21) &&
                 (std::to_underlying(ctx.rs2) == 1 || std::to_underlying(ctx.rs2) == 4 ||
                  std::to_underlying(ctx.rs2) == 5));
            if (simrv::compiler::unlikely(uses_rm && !isa::is_valid_rounding_mode(rm))) {
                ctx.pending_exception = ExceptionCode::IllegalInstruction;
                ctx.pending_tval = ctx.ir;
                break;
            }
            const simrv::xlen::CsrValue old_fcsr = state_.fcsr;
            if (zfinx_active) {
                const auto fmt = static_cast<FpFmt>(ctx.funct7 & 0x3);
                const uint32_t rs1_idx = std::to_underlying(ctx.rs1);
                const uint32_t rs2_idx = std::to_underlying(ctx.rs2);
                // Determine source/dest widths for conversion ops
                FpFmt src1_fmt = fmt;
                FpFmt src2_fmt = fmt;
                FpFmt dst_fmt = fmt;
                const bool rs1_is_int =
                    (ctx.funct7 == 0x68 || ctx.funct7 == 0x69 || ctx.funct7 == 0x6A);
                if (ctx.funct7 == 0x20 || ctx.funct7 == 0x21 || ctx.funct7 == 0x22) {
                    // FCVT.<dst>.<src>
                    src1_fmt = (rs2_idx == 1U) ? FpFmt::Double
                                               : (rs2_idx == 2U ? FpFmt::Half : FpFmt::Single);
                }
                const bool has_rs2 = (ctx.funct7 <= 0x15 || ctx.funct7 == 0x50 ||
                                      ctx.funct7 == 0x51 || ctx.funct7 == 0x52);
                std::array<simrv::xlen::FloatingRegister, 32> zfinx_fregs{};
                if ((!rs1_is_int && !load_zfinx_src(zfinx_fregs, rs1_idx, src1_fmt)) ||
                    (has_rs2 && !load_zfinx_src(zfinx_fregs, rs2_idx, src2_fmt))) {
                    ctx.pending_exception = ExceptionCode::IllegalInstruction;
                    ctx.pending_tval = ctx.ir;
                    break;
                }
                const auto fp = execute::ExecuteUnit::opFp(
                    ctx.funct7, ctx.funct3, simrv::xlen::to_fp_reg_id(ctx.rs1),
                    simrv::xlen::to_fp_reg_id(ctx.rs2), ctx.rrs1, zfinx_fregs.data(), state_.fcsr);
                if (fp.int_wb_enable) {
                    ctx.wb_data = fp.int_wb_data;
                    ctx.int_wb_from_fp = true;
                    ctx.fp_wb_enable = false;
                } else if (fp.fp_wb_enable && !store_zfinx_dst(fp.fp_wb_data, dst_fmt)) {
                    ctx.pending_exception = ExceptionCode::IllegalInstruction;
                    ctx.pending_tval = ctx.ir;
                }
                break;
            }
            const auto fp = execute::ExecuteUnit::opFp(ctx.funct7, ctx.funct3,
                                                       simrv::xlen::to_fp_reg_id(ctx.rs1),
                                                       simrv::xlen::to_fp_reg_id(ctx.rs2), ctx.rrs1,
                                                       state_.regs.fp_data_ptr(), state_.fcsr);
            if (state_.fcsr != old_fcsr) {
                state_.mark_extension_dirty(MstatusBit::Fs);
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
