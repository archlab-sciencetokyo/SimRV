/**
 * @file Cpu.cpp
 * @brief CPU core state/control implementation.
 */
#include "simrv/core/Cpu.hpp"
#include <bit>

#include "simrv/core/Machine.hpp"
#include "simrv/core/Tracer.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/tui/Tui.hpp"

namespace simrv::core {

using namespace simrv::isa;

namespace {
auto is_tohost_addr(const Machine& machine, Address addr) -> bool {
    return (addr - machine.s_isatest_tohost < 8) ||
           (addr - 0x80001000ULL < 8) ||
           (addr - 0x40008000ULL < 8);
}
}  // namespace

CPU::CPU() : plic_mmio(*this), clint_mmio(*this), csr_file(*this), sbi(*this) {
    state_.regs.fill(0);
    state_.regs.fill_fp(0);
    state_.regs.fill_vector(VectorRegister{});
    // Set mstatus.VS to Clean (2 << 9)
    state_.mstatus = static_cast<CSRValue>(2) << 9;
    if constexpr (simrv::xlen::kIsXLen64) {
        state_.mstatus |= (static_cast<CSRValue>(2) << 34) | (static_cast<CSRValue>(2) << 32);
    }
    state_.update_xlen();
}

void CPU::TLB_flush() {
    tlb.flush();
    decode_cache.flush();
    soft_tlb_flush();
}

void CPU::TLB_flush(bool match_all_vaddr, Address vaddr, bool match_all_asid, Word asid) {
    tlb.flush_selective(match_all_vaddr, vaddr, match_all_asid, asid);
    decode_cache.flush();
    soft_tlb_flush();
}

void CPU::soft_tlb_flush() {
    for (auto& entry : soft_tlb_read) {
        entry.valid = false;
    }
    for (auto& entry : soft_tlb_write) {
        entry.valid = false;
    }
}

auto CPU::get_mstatus(CSRValue mask) const -> CSRValue { return csr_file.getMstatus(mask); }

void CPU::set_mstatus(CSRValue wdata) {
    const CSRValue old_mstatus = state_.mstatus;
    csr_file.setMstatus(wdata);
    if (state_.mstatus != old_mstatus) {
        TLB_flush();
    }
}

auto CPU::read_csr(CSRAddress addr) const -> std::expected<CSRValue, ExceptionCode> { return csr_file.read(addr); }

auto CPU::write_csr(CSRAddress addr, CSRValue wdata) -> std::expected<void, ExceptionCode> {
    const CSRValue old_mstatus = state_.mstatus;
    const CSRValue old_satp = state_.satp;

    auto result = csr_file.write(addr, wdata);
    if (!result) {
        return result;
    }

    if (state_.mstatus != old_mstatus || state_.satp != old_satp) {
        TLB_flush();
    }
    return {};
}

void CPU::mret() { TrapController::mret(state_); }

void CPU::sret() { TrapController::sret(state_); }

void CPU::plic_update_mip() { InterruptController::updateMip(plic_mmio, state_); }

void CPU::plic_set_irq(int irq_num, int state) {
    InterruptController::setIrq(plic_mmio, irq_num, state);
}

void CPU::raise_exception(TrapCause cause, CSRValue tval) {
    TrapController::raiseException(*this, cause, tval);
}

void CPU::evaluate_timer_interrupt() {
    if (clint_mmio.mtime == clint_mmio.last_mtime && clint_mmio.mtimecmp == clint_mmio.last_mtimecmp) {
        return;
    }
    clint_mmio.last_mtime = clint_mmio.mtime;
    clint_mmio.last_mtimecmp = clint_mmio.mtimecmp;

    const CSRValue mask = enum_mask(MipBit::Mtip) | enum_mask(MipBit::Stip);
    if (clint_mmio.mtime >= clint_mmio.mtimecmp) {
        if ((state_.mip & mask) != mask) {
            state_.mip |= mask;
        }
    } else {
        if ((state_.mip & mask) != 0) {
            state_.mip &= ~mask;
        }
    }
}

void CPU::run_cycle(Machine& machine) {
    uint32_t step_cycles = 1;

    if (machine.s_cycle_accurate) {
        uint64_t prev_imiss = icache.miss_count();
        uint64_t prev_dmiss = dcache.miss_count();

        if (!pipeline_task.handle) {
            pipeline_task = run_pipeline_coroutine(&machine);
        }

        auto err = pipeline_task.resume();
        if (err.has_value()) {
            raise_exception(static_cast<TrapCause>(err->code), err->tval);
        }

        bool icache_miss = (icache.miss_count() > prev_imiss);
        bool dcache_miss = (dcache.miss_count() > prev_dmiss);

        bool is_branch = (pipeline_context.opcode == Opcode::Branch);
        bool is_jump = (pipeline_context.opcode == Opcode::Jal || pipeline_context.opcode == Opcode::Jalr);
        bool branched = pipeline_context.tkn;

        step_cycles = pipeline_sim.step_instruction(
            pipeline_context.cpc, pipeline_context.opcode, pipeline_context.rd,
            pipeline_context.rs1, pipeline_context.rs2, pipeline_context.op_id, branched,
            is_branch, is_jump, icache_miss, dcache_miss, pipeline_context.tlb_miss, pipeline_context.jmp_pc
        );
    } else {
        if (machine.s_high_performance) {
            auto* cached = decode_cache.lookup(state_.pc);
            if (simrv::compiler::likely(cached != nullptr)) {
                execute_cached_op_fast(machine, *cached);
            } else {
                bool success = fetch_stage(machine, state_.pc) &&
                              decode_stage(machine);
                
                if (success && !pipeline_context.pending_exception.has_value()) {
                    CachedOp op;
                    op.copy_from(pipeline_context);
                    decode_cache.insert(state_.pc, op);
                }

                if (success) {
                    success = execute_stage(machine) &&
                              memory_stage(machine) &&
                              writeback_stage(machine) &&
                              commit_stage(machine);
                }

                if (!success) {
                    const auto cause = pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch);
                    raise_exception(static_cast<TrapCause>(cause), pipeline_context.pending_tval);
                }
            }
        } else {
            if (!pipeline_task.handle) {
                pipeline_task = run_pipeline_coroutine(&machine);
            }

            auto err = pipeline_task.resume();
            if (err.has_value()) {
                raise_exception(static_cast<TrapCause>(err->code), err->tval);
            }
        }
    }

    if (simrv::compiler::likely(!machine.s_cycle_accurate)) {
        clint_mmio.mcycle++;
        clint_mmio.rtc_divider++;
        if (clint_mmio.rtc_divider == 10) {
            clint_mmio.mtime++;
            clint_mmio.rtc_divider = 0;
            evaluate_timer_interrupt();
        }
    } else {
        clint_mmio.mcycle += step_cycles;
        clint_mmio.rtc_divider += static_cast<int>(step_cycles);
        if (clint_mmio.rtc_divider >= 10) {
            if (clint_mmio.rtc_divider < 20) {
                clint_mmio.mtime += 1;
                clint_mmio.rtc_divider -= 10;
            } else {
                clint_mmio.mtime += clint_mmio.rtc_divider / 10;
                clint_mmio.rtc_divider %= 10;
            }
            evaluate_timer_interrupt();
        }
    }

    if (machine.s_tuimode && machine.tui && machine.tui->is_trace_active()) {
        record_trace_for_tui(machine);
    }
}

void CPU::record_trace_for_tui(Machine& machine) {
    const auto op_id = pipeline_context.op_id;
    const auto opcode = pipeline_context.opcode;
    const auto rd = pipeline_context.rd;
    const auto rs1 = pipeline_context.rs1;
    const auto rs2 = pipeline_context.rs2;
    
    Register rd_val = 0;
    Register rs1_val = 0;
    Register rs2_val = 0;
    
    const bool rd_fp = isa::is_destination_fp(opcode, op_id);
    const bool rs1_fp = isa::is_rs1_fp(opcode, op_id);
    const bool rs2_fp = isa::is_rs2_fp(opcode, op_id);
    
    if (rd_fp) {
        rd_val = state_.regs.read_fp(rd);
    } else {
        rd_val = state_.regs.read(rd);
    }
    
    if (rs1_fp) {
        rs1_val = state_.regs.read_fp(rs1);
    } else {
        rs1_val = state_.regs.read(rs1);
    }
    
    if (rs2_fp) {
        rs2_val = state_.regs.read_fp(rs2);
    } else {
        rs2_val = state_.regs.read(rs2);
    }

    machine.tui->record_instruction(
        pipeline_context.cpc,
        opcode,
        op_id,
        std::to_underlying(rd),
        rd_val,
        std::to_underlying(rs1),
        rs1_val,
        std::to_underlying(rs2),
        rs2_val,
        pipeline_context.imm
    );
}

void CPU::run_cycle_baremetal(Machine& machine) {
    pipeline_context.pending_exception = std::nullopt;
    pipeline_context.pending_tval = 0;

    if (machine.s_high_performance) {
        auto* cached = decode_cache.lookup(state_.pc);
        if (simrv::compiler::likely(cached != nullptr)) {
            execute_cached_op_fast(machine, *cached);
            clint_mmio.mcycle++;
            if (machine.s_tuimode && machine.tui && machine.tui->is_trace_active()) {
                record_trace_for_tui(machine);
            }
            return;
        }
    }
    
    run_fetch_stage_baremetal(machine);
    const bool success = !pipeline_context.pending_exception.has_value() &&
                         decode_stage(machine);

    if (success && !pipeline_context.pending_exception.has_value()) {
        CachedOp op;
        op.copy_from(pipeline_context);
        decode_cache.insert(state_.pc, op);
    }

    if (success) {
        const bool rest_success = execute_stage(machine) &&
                                  (run_memory_stage_baremetal(machine), !pipeline_context.pending_exception.has_value()) &&
                                  writeback_stage(machine) &&
                                  (run_commit_stage_baremetal(machine), !pipeline_context.pending_exception.has_value());
        if (simrv::compiler::unlikely(!rest_success)) {
            const auto cause = pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch);
            raise_exception(static_cast<TrapCause>(cause), pipeline_context.pending_tval);
        }
    } else {
        const auto cause = pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch);
        raise_exception(static_cast<TrapCause>(cause), pipeline_context.pending_tval);
    }

    clint_mmio.mcycle++;
    if (machine.s_tuimode && machine.tui && machine.tui->is_trace_active()) {
        record_trace_for_tui(machine);
    }
}

void CPU::run_memory_stage_baremetal(Machine& machine) {
    auto& ctx = pipeline_context;
    if (ctx.pending_exception.has_value()) {
        return;
    }

    const auto opcode = static_cast<Opcode>(ctx.opcode);
    const auto funct5 = static_cast<Funct5Amo>(ctx.funct5);
    const auto addr = ctx.mem_addr;

    // Load Phase
    if (opcode == Opcode::Load || (opcode == Opcode::Amo && funct5 != Funct5Amo::Sc)) {
        if (simrv::compiler::likely(simrv::memory::is_dram_addr(addr))) {
            ctx.mem_rdata = simrv::memory::ram_read_fast(addr, static_cast<Instruction>(ctx.funct3), machine.mmem);
        } else {
            ctx.mem_rdata = simrv::memory::MemoryAccess::loadInt(machine.memory_, *this, addr, ctx.funct3);
        }
    }

    if (opcode == Opcode::LoadFp) {
        if (simrv::compiler::likely(simrv::memory::is_dram_addr(addr))) {
            const auto f3 = ctx.funct3;
            if (f3 == Funct3::Flw) {
                const Word lo = simrv::memory::ram_read_fast(addr, static_cast<Instruction>(Funct3::Lw), machine.mmem);
                ctx.fp_mem_rdata = static_cast<uint64_t>(kF32BoxerBits) | static_cast<uint64_t>(lo & kLower32Mask);
            } else if (f3 == Funct3::Fld) {
                if constexpr (simrv::xlen::kIsXLen64) {
                    ctx.fp_mem_rdata = static_cast<FloatingRegister>(
                        simrv::memory::ram_read_fast(addr, static_cast<Instruction>(Funct3::Ld), machine.mmem));
                } else {
                    const Word lo = simrv::memory::ram_read_fast(addr, static_cast<Instruction>(Funct3::Lw), machine.mmem);
                    const Word hi = simrv::memory::ram_read_fast(addr + 4, static_cast<Instruction>(Funct3::Lw), machine.mmem);
                    ctx.fp_mem_rdata = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
                }
            }
        } else {
            ctx.fp_mem_rdata = simrv::memory::MemoryAccess::loadFp(machine.memory_, *this, addr, ctx.funct3);
        }
    }

    if (opcode == Opcode::Amo && funct5 == Funct5Amo::Lr) {
        state_.load_res = addr;
        state_.reserved = 1;
    }

    // Prepare Store Data
    ctx.mem_wdata = (opcode != Opcode::Amo || funct5 == Funct5Amo::Sc)
                        ? ctx.rrs2
                        : execute::ExecuteUnit::aluAmo(ctx.rrs2, ctx.mem_rdata, funct5, ctx.funct3);

    if (opcode == Opcode::StoreFp) {
        ctx.mem_wdata =
            static_cast<Register>(ctx.fp_mem_wdata & static_cast<FloatingRegister>(kLower32Mask));
    }

    // Store Phase
    if ((opcode == Opcode::Store) ||
        (opcode == Opcode::Amo &&
          (funct5 == Funct5Amo::Sc && (ctx.wb_data == 0u) && (state_.reserved != 0u))) ||
        (opcode == Opcode::Amo && funct5 != Funct5Amo::Lr && funct5 != Funct5Amo::Sc)) {
        if (simrv::compiler::likely(simrv::memory::is_dram_addr(addr))) {
            // Check tohost writes
            if (simrv::compiler::unlikely(addr == machine.s_isatest_tohost || addr == 0x80001000 || addr == 0x40008000)) {
                const bool is_tohost_write =
                    simrv::xlen::kIsXLen64 ? (ctx.funct3 == Funct3::Sw ||
                                               ctx.funct3 == Funct3::Sd)
                                          : (ctx.funct3 == Funct3::Sw);
                if (is_tohost_write) {
                    machine.tohost = simrv::xlen::kIsXLen64
                                          ? ctx.mem_wdata
                                          : ((machine.tohost & 0xFFFFFFFF00000000ULL) | ctx.mem_wdata);
                }
            } else if (simrv::compiler::unlikely(!simrv::xlen::kIsXLen64 &&
                                                 (addr == machine.s_isatest_tohost + 4 || addr == 0x80001004 || addr == 0x40008004))) {
                const bool is_tohost_write = (ctx.funct3 == Funct3::Sw);
                if (is_tohost_write) {
                    machine.tohost = (machine.tohost & 0x00000000FFFFFFFFULL) |
                                      (static_cast<uint64_t>(ctx.mem_wdata) << 32);
                }
            }
            simrv::memory::ram_write_fast(addr, ctx.mem_wdata, static_cast<Instruction>(ctx.funct3), machine.mmem);
        } else {
            simrv::memory::MemoryAccess::storeInt(machine.memory_, *this, addr, ctx.mem_wdata, ctx.funct3);
        }
    }

    if (opcode == Opcode::StoreFp) {
        if (simrv::compiler::likely(simrv::memory::is_dram_addr(addr))) {
            const auto f3 = ctx.funct3;
            if (f3 == Funct3::Fsw) {
                simrv::memory::ram_write_fast(addr, static_cast<Word>(ctx.fp_mem_wdata & static_cast<FloatingRegister>(kLower32Mask)),
                                             static_cast<Instruction>(Funct3::Sw), machine.mmem);
            } else if (f3 == Funct3::Fsd) {
                if constexpr (simrv::xlen::kIsXLen64) {
                    simrv::memory::ram_write_fast(addr, static_cast<Word>(ctx.fp_mem_wdata),
                                                 static_cast<Instruction>(Funct3::Sd), machine.mmem);
                } else {
                    simrv::memory::ram_write_fast(addr, static_cast<Word>(ctx.fp_mem_wdata & static_cast<FloatingRegister>(kLower32Mask)),
                                                 static_cast<Instruction>(Funct3::Sw), machine.mmem);
                    simrv::memory::ram_write_fast(addr + 4, static_cast<Word>((ctx.fp_mem_wdata >> 32) & static_cast<FloatingRegister>(kLower32Mask)),
                                                 static_cast<Instruction>(Funct3::Sw), machine.mmem);
                }
            }
        } else {
            simrv::memory::MemoryAccess::storeFp(machine.memory_, *this, addr, ctx.fp_mem_wdata, ctx.funct3);
        }
    }

    if ((opcode == Opcode::Store) || (opcode == Opcode::StoreFp) ||
        (opcode == Opcode::Amo && funct5 != Funct5Amo::Lr)) {
        if (!ctx.pending_exception.has_value()) {
            state_.reserved = 0;
        }
    }
}

auto CPU::run_pipeline_coroutine(Machine* machine_ptr) -> simrv::pipeline::PipelineTask {
    Machine& machine = *machine_ptr;
    while (true) {
        if (!fetch_stage(machine, state_.pc)) {
            co_yield simrv::pipeline::StageError{.code = pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch),
                                .tval = pipeline_context.pending_tval};
            continue;
        }
        if (!decode_stage(machine)) {
            co_yield simrv::pipeline::StageError{.code = pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch),
                                .tval = pipeline_context.pending_tval};
            continue;
        }
        if (!execute_stage(machine)) {
            co_yield simrv::pipeline::StageError{.code = pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch),
                                .tval = pipeline_context.pending_tval};
            continue;
        }
        if (!memory_stage(machine)) {
            co_yield simrv::pipeline::StageError{.code = pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch),
                                .tval = pipeline_context.pending_tval};
            continue;
        }
        if (!writeback_stage(machine)) {
            co_yield simrv::pipeline::StageError{.code = pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch),
                                .tval = pipeline_context.pending_tval};
            continue;
        }
        if (!commit_stage(machine)) {
            co_yield simrv::pipeline::StageError{.code = pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch),
                                .tval = pipeline_context.pending_tval};
            continue;
        }
        co_yield std::nullopt;
    }
}

auto CPU::fetch_stage(Machine& machine, Address pc) -> bool {
    state_.pc = pc;
    pipeline_context.pending_exception = std::nullopt;
    pipeline_context.pending_tval = 0;

    run_fetch_stage(machine);

    return !pipeline_context.pending_exception.has_value();
}

auto CPU::decode_stage(Machine& machine) -> bool {
    run_decode_stage(machine);
    return !pipeline_context.pending_exception.has_value();
}

auto CPU::execute_stage(Machine& machine) -> bool {
    run_execute_stage(machine);
    return !pipeline_context.pending_exception.has_value();
}

auto CPU::memory_stage(Machine& machine) -> bool {
    run_memory_stage(machine);
    return !pipeline_context.pending_exception.has_value();
}

auto CPU::writeback_stage(Machine& machine) -> bool {
    run_writeback_stage(machine);
    return !pipeline_context.pending_exception.has_value();
}

auto CPU::commit_stage(Machine& machine) -> bool {
    run_commit_stage(machine);
    return !pipeline_context.pending_exception.has_value();
}

void CPU::execute_cached_lui(CachedOp& op) {
    if (op.rd != RegId::Zero) {
        state_.regs.write(op.rd, op.imm);
    }
    e_icount++;
    if (op.cinsn) e_ccount++;
    state_.pc += (op.cinsn ? 2 : 4);
    pc_sign_extend();
}

void CPU::execute_cached_auipc(CachedOp& op) {
    if (op.rd != RegId::Zero) {
        state_.regs.write(op.rd, state_.pc + op.imm);
    }
    e_icount++;
    if (op.cinsn) e_ccount++;
    state_.pc += (op.cinsn ? 2 : 4);
    pc_sign_extend();
}

void CPU::execute_cached_jal(CachedOp& op) {
    Register const next_pc = state_.pc + (op.cinsn ? 2 : 4);
    pipeline_context.tkn = true;
    pipeline_context.jmp_pc = state_.pc + op.imm;
    if (op.rd != RegId::Zero) {
        state_.regs.write(op.rd, next_pc);
    }
    const bool has_c = misa_has_extension(state_.misa, isa::IsaExtension::C);
    const Word alignment_mask = has_c ? 1u : 3u;
    if ((pipeline_context.jmp_pc & alignment_mask) != 0) {
        raise_exception(static_cast<TrapCause>(ExceptionCode::MisalignedFetch), pipeline_context.jmp_pc);
        return;
    }
    state_.pc = pipeline_context.jmp_pc;
    e_icount++;
    if (op.cinsn) e_ccount++;
    pc_sign_extend();
}

void CPU::execute_cached_jalr(CachedOp& op, Register rrs1) {
    Register const next_pc = state_.pc + (op.cinsn ? 2 : 4);
    pipeline_context.tkn = true;
    pipeline_context.jmp_pc = (rrs1 + op.imm) & ~static_cast<Register>(1);
    if (op.rd != RegId::Zero) {
        state_.regs.write(op.rd, next_pc);
    }
    const bool has_c = misa_has_extension(state_.misa, isa::IsaExtension::C);
    const Word alignment_mask = has_c ? 1u : 3u;
    if ((pipeline_context.jmp_pc & alignment_mask) != 0) {
        raise_exception(static_cast<TrapCause>(ExceptionCode::MisalignedFetch), pipeline_context.jmp_pc);
        return;
    }
    state_.pc = pipeline_context.jmp_pc;
    e_icount++;
    if (op.cinsn) e_ccount++;
    pc_sign_extend();
}

void CPU::execute_cached_branch(CachedOp& op, Register rrs1, Register rrs2) {
    bool const tkn = execute::ExecuteUnit::branchTaken(rrs1, rrs2, op.funct3);
    pipeline_context.tkn = tkn;
    pipeline_context.jmp_pc = state_.pc + op.imm;
    Register const target_pc = tkn ? pipeline_context.jmp_pc : (state_.pc + (op.cinsn ? 2 : 4));
    if (tkn) {
        const bool has_c = misa_has_extension(state_.misa, isa::IsaExtension::C);
        const Word alignment_mask = has_c ? 1u : 3u;
        if ((target_pc & alignment_mask) != 0) {
            raise_exception(static_cast<TrapCause>(ExceptionCode::MisalignedFetch), target_pc);
            return;
        }
    }
    state_.pc = target_pc;
    e_icount++;
    if (op.cinsn) e_ccount++;
    pc_sign_extend();
}

void CPU::execute_cached_op(CachedOp& op, Register rrs1, Register rrs2) {
    Register const wb_data = execute::ExecuteUnit::aluInt(rrs1, rrs2, op.op_id);
    if (op.rd != RegId::Zero) {
        state_.regs.write(op.rd, wb_data);
    }
    e_icount++;
    if (op.cinsn) e_ccount++;
    state_.pc += (op.cinsn ? 2 : 4);
    pc_sign_extend();
}

void CPU::execute_cached_op_imm(CachedOp& op, Register rrs1) {
    Register const wb_data = execute::ExecuteUnit::aluInt(rrs1, op.imm, op.op_id);
    if (op.rd != RegId::Zero) {
        state_.regs.write(op.rd, wb_data);
    }
    e_icount++;
    if (op.cinsn) e_ccount++;
    state_.pc += (op.cinsn ? 2 : 4);
    pc_sign_extend();
}

void CPU::execute_cached_op_imm32(CachedOp& op, Register rrs1) {
    Register const wb_data = execute::ExecuteUnit::aluIntW(rrs1, op.imm, op.op_id);
    if (op.rd != RegId::Zero) {
        state_.regs.write(op.rd, wb_data);
    }
    e_icount++;
    if (op.cinsn) e_ccount++;
    state_.pc += (op.cinsn ? 2 : 4);
    pc_sign_extend();
}

void CPU::execute_cached_op32(CachedOp& op, Register rrs1, Register rrs2) {
    Register const wb_data = execute::ExecuteUnit::aluIntW(rrs1, rrs2, op.op_id);
    if (op.rd != RegId::Zero) {
        state_.regs.write(op.rd, wb_data);
    }
    e_icount++;
    if (op.cinsn) e_ccount++;
    state_.pc += (op.cinsn ? 2 : 4);
    pc_sign_extend();
}

auto CPU::try_fast_load(Machine& machine, Address mem_addr, Funct3 funct3, Register& out_val) -> bool {
    const unsigned size_bytes = 1u << (static_cast<unsigned>(funct3) & 0x3u);
    const unsigned alignment_mask = size_bytes - 1u;
    if (simrv::compiler::unlikely((mem_addr & alignment_mask) != 0)) {
        return false;
    }
    if (machine.s_appmode) {
        if (simrv::compiler::likely(simrv::memory::is_dram_addr(mem_addr))) {
            out_val = simrv::memory::ram_read_fast(mem_addr, static_cast<Instruction>(funct3), machine.memory_.mmu()->mmem());
            return true;
        }
        return false;
    }
    const PrivilegeLevel eff_priv = effective_data_privilege();
    const bool translation_enabled = (eff_priv != kPrivMachine && simrv::xlen::satp_translation_enabled(state_.satp));
    if (!translation_enabled) {
        if (simrv::compiler::likely(simrv::memory::is_dram_addr(mem_addr))) {
            out_val = simrv::memory::ram_read_fast(mem_addr, static_cast<Instruction>(funct3), machine.memory_.mmu()->mmem());
            return true;
        }
    } else {
        const Word current_asid = simrv::xlen::satp_asid(state_.satp);
        size_t const tlb_idx = (mem_addr >> 12) & 2047;
        const auto& entry = soft_tlb_read[tlb_idx]; // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        if (simrv::compiler::likely(entry.valid && entry.vpn == (mem_addr >> 12) && entry.priv == eff_priv && 
            entry.asid == current_asid)) {
            if (simrv::compiler::likely(entry.host_ptr_base != nullptr)) {
                out_val = simrv::memory::host_read_fast(entry.host_ptr_base + (mem_addr & 0xFFF), static_cast<Instruction>(funct3));
                return true;
            }
            out_val = simrv::memory::ram_read_fast(entry.paddr_base + (mem_addr & 0xFFF), static_cast<Instruction>(funct3), machine.memory_.mmu()->mmem());
            return true;
        }
    }
    return false;
}

auto CPU::try_fast_store(Machine& machine, Address mem_addr, Funct3 funct3, Register rrs2) -> bool {
    const unsigned size_bytes = 1u << (static_cast<unsigned>(funct3) & 0x3u);
    const unsigned alignment_mask = size_bytes - 1u;
    if (simrv::compiler::unlikely((mem_addr & alignment_mask) != 0)) {
        return false;
    }
    if (machine.s_appmode) {
        if (simrv::compiler::likely(simrv::memory::is_dram_addr(mem_addr) && !is_tohost_addr(machine, mem_addr))) {
            simrv::memory::ram_write_fast(mem_addr, rrs2, static_cast<Instruction>(funct3), machine.memory_.mmu()->mmem());
            return true;
        }
        return false;
    }
    const PrivilegeLevel eff_priv = effective_data_privilege();
    const bool translation_enabled = (eff_priv != kPrivMachine && simrv::xlen::satp_translation_enabled(state_.satp));
    if (!translation_enabled) {
        if (simrv::compiler::likely(simrv::memory::is_dram_addr(mem_addr) && !is_tohost_addr(machine, mem_addr))) {
            simrv::memory::ram_write_fast(mem_addr, rrs2, static_cast<Instruction>(funct3), machine.memory_.mmu()->mmem());
            return true;
        }
    } else {
        const Word current_asid = simrv::xlen::satp_asid(state_.satp);
        size_t const tlb_idx = (mem_addr >> 12) & 2047;
        const auto& entry = soft_tlb_write[tlb_idx]; // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        if (simrv::compiler::likely(entry.valid && entry.vpn == (mem_addr >> 12) && entry.priv == eff_priv && 
            entry.asid == current_asid)) {
            if (simrv::compiler::likely(entry.host_ptr_base != nullptr)) {
                simrv::memory::host_write_fast(entry.host_ptr_base + (mem_addr & 0xFFF), rrs2, static_cast<Instruction>(funct3));
                return true;
            }
            Address const paddr = entry.paddr_base + (mem_addr & 0xFFF);
            if (simrv::compiler::likely(simrv::memory::is_dram_addr(paddr) && !is_tohost_addr(machine, paddr))) {
                simrv::memory::ram_write_fast(paddr, rrs2, static_cast<Instruction>(funct3), machine.memory_.mmu()->mmem());
                return true;
            }
        }
    }
    return false;
}

auto CPU::execute_cached_load(Machine& machine, CachedOp& op, Register rrs1) -> bool {
    Address const mem_addr = rrs1 + op.imm;
    pipeline_context.mem_addr = mem_addr;
    Register mem_rdata = 0;
    if (!try_fast_load(machine, mem_addr, op.funct3, mem_rdata)) {
        mem_rdata = simrv::memory::MemoryAccess::loadInt(machine.memory_, *this, mem_addr, op.funct3);
        if (pipeline_context.pending_exception.has_value()) {
            raise_exception(static_cast<TrapCause>(*pipeline_context.pending_exception), pipeline_context.pending_tval);
            return false;
        }
    }

    if (op.rd != RegId::Zero) {
        state_.regs.write(op.rd, mem_rdata);
    }
    e_icount++;
    if (op.cinsn) e_ccount++;
    state_.pc += (op.cinsn ? 2 : 4);
    pc_sign_extend();
    return true;
}

auto CPU::execute_cached_store(Machine& machine, CachedOp& op, Register rrs1, Register rrs2) -> bool {
    Address const mem_addr = rrs1 + op.imm;
    pipeline_context.mem_addr = mem_addr;
    if (!try_fast_store(machine, mem_addr, op.funct3, rrs2)) {
        simrv::memory::MemoryAccess::storeInt(machine.memory_, *this, mem_addr, rrs2, op.funct3);
        if (pipeline_context.pending_exception.has_value()) {
            raise_exception(static_cast<TrapCause>(*pipeline_context.pending_exception), pipeline_context.pending_tval);
            return false;
        }
    }

    state_.reserved = 0;
    e_icount++;
    if (op.cinsn) e_ccount++;
    state_.pc += (op.cinsn ? 2 : 4);
    pc_sign_extend();
    return true;
}

auto CPU::execute_cached_fallback(Machine& machine) -> void {
    fetch_operands(machine);
    bool success = execute_stage(machine) &&
                   memory_stage(machine) &&
                   writeback_stage(machine) &&
                   commit_stage(machine);
    if (!success) {
        const auto cause = pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch);
        raise_exception(static_cast<TrapCause>(cause), pipeline_context.pending_tval);
    }
}

void CPU::handle_cached_interrupts() {
    Word const pending_interrupts = state_.mip & state_.mie;
    if (simrv::compiler::unlikely(pending_interrupts != 0u)) {
        Word enable_interrupts = 0;
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
        Word const mask = pending_interrupts & enable_interrupts;
        if (mask != 0u) {
            Word const irq_num = static_cast<Word>(std::bit_width(mask) - 1);
            raise_exception(kInterruptCauseBit | irq_num, 0);
        }
    }
}

void CPU::execute_cached_op_fast(Machine& machine, CachedOp& op) {
    // 1. Copy metadata to pipeline_context to support tracers and lockstep only when needed
    if (simrv::compiler::unlikely(machine.s_tuimode || machine.s_lockstep_mode || 
                                  machine.s_gdb_mode || machine.s_bp_trace || 
                                  (machine.s_strace != 0))) {
        pipeline_context.copy_from(op);
        pipeline_context.tlb_miss = false;
        pipeline_context.pending_exception = std::nullopt;
        pipeline_context.pending_tval = 0;
    } else {
        pipeline_context.pending_exception = std::nullopt;
    }

    // 2. Fetch operands
    Register const rrs1 = state_.regs.read(op.rs1);
    Register const rrs2 = state_.regs.read(op.rs2);

    // 3. Execute, Memory, Writeback, Commit in a monolithic fast path
    switch (op.opcode) {
        case Opcode::Lui:
            execute_cached_lui(op);
            break;
        case Opcode::Auipc:
            execute_cached_auipc(op);
            break;
        case Opcode::Jal:
            execute_cached_jal(op);
            break;
        case Opcode::Jalr:
            execute_cached_jalr(op, rrs1);
            break;
        case Opcode::Branch:
            execute_cached_branch(op, rrs1, rrs2);
            break;
        case Opcode::Op:
            execute_cached_op(op, rrs1, rrs2);
            break;
        case Opcode::OpImm:
            execute_cached_op_imm(op, rrs1);
            break;
        case Opcode::OpImm32:
            execute_cached_op_imm32(op, rrs1);
            break;
        case Opcode::Op32:
            execute_cached_op32(op, rrs1, rrs2);
            break;
        case Opcode::Load:
            if (!execute_cached_load(machine, op, rrs1)) {
                return;
            }
            break;
        case Opcode::Store:
            if (!execute_cached_store(machine, op, rrs1, rrs2)) {
                return;
            }
            break;
        default:
            pipeline_context.copy_from(op);
            pipeline_context.tlb_miss = false;
            pipeline_context.pending_exception = std::nullopt;
            pipeline_context.pending_tval = 0;
            execute_cached_fallback(machine);
            return;
    }

    // 4. Handle pending interrupts (same check as commit_stage)
    handle_cached_interrupts();
}

}  // namespace simrv::core