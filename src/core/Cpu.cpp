/**
 * @file Cpu.cpp
 * @brief CPU core state/control implementation.
 */
#include "simrv/core/Cpu.hpp"

#include <bit>

#include "simrv/core/Machine.hpp"
#include "simrv/core/Tracer.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

using namespace simrv::isa;

namespace {
SIMRV_ALWAYS_INLINE auto is_tohost_addr(const Machine& machine, Address addr) -> bool {
    return (addr - machine.s_isatest_tohost < 8) || (addr - 0x80001000ULL < 8) ||
           (addr - 0x40008000ULL < 8);
}
}  // namespace

CPU::CPU() : plic_mmio(*this), clint_mmio(*this), csr_file(*this), sbi(*this) { reset(); }

void CPU::reset() {
    state_ = ArchState{};
    prev_state_ = ArchState{};
    state_.regs.fill(0);
    state_.regs.fill_fp(0);
    state_.regs.fill_vector(VectorRegister{});
    // Set mstatus.VS to Clean (2 << 9)
    state_.mstatus = static_cast<CSRValue>(2) << 9;
    if constexpr (simrv::xlen::kIsXLen64) {
        state_.mstatus |= (static_cast<CSRValue>(2) << 34) | (static_cast<CSRValue>(2) << 32);
    }
    state_.update_xlen();
    TLB_flush();
    e_icount = 0;
    e_ccount = 0;
    e_instmix.fill(0);
    undo_stack.clear();
    trace_history_head_ = 0;
    trace_history_size_ = 0;
    if (pipeline_task.handle) {
        pipeline_task.handle.destroy();
        pipeline_task.handle = nullptr;
    }
}

void CPU::TLB_flush() {
    tlb.flush();
    decode_cache.flush();
    icache.flush();
    dcache.flush();
    soft_tlb_flush();
}

void CPU::TLB_flush(bool match_all_vaddr, Address vaddr, bool match_all_asid, Word asid) {
    tlb.flush_selective(match_all_vaddr, vaddr, match_all_asid, asid);
    decode_cache.flush();
    soft_tlb_flush_selective(match_all_vaddr, vaddr, match_all_asid, asid);
}

void CPU::soft_tlb_flush() {
    if (simrv::compiler::unlikely(++soft_tlb_epoch == 0)) {
        soft_tlb_epoch = 1;
        for (auto& entry : soft_tlb_read) {
            entry.invalidate();
        }
        for (auto& entry : soft_tlb_write) {
            entry.invalidate();
        }
    }
}

void CPU::soft_tlb_flush_selective(bool /*match_all_vaddr*/, Address /*vaddr*/,
                                   bool /*match_all_asid*/, Word /*asid*/) {
    soft_tlb_flush();
}

auto CPU::get_mstatus(CSRValue mask) const -> CSRValue { return csr_file.getMstatus(mask); }

void CPU::set_mstatus(CSRValue wdata) { csr_file.setMstatus(wdata); }

auto CPU::read_csr(CSRAddress addr) const -> std::expected<CSRValue, ExceptionCode> {
    return csr_file.read(addr);
}

auto CPU::write_csr(CSRAddress addr, CSRValue wdata) -> std::expected<void, ExceptionCode> {
    return csr_file.write(addr, wdata);
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
    if (clint_mmio.mtime == clint_mmio.last_mtime &&
        clint_mmio.mtimecmp == clint_mmio.last_mtimecmp) {
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
    if (simrv::compiler::unlikely(machine.breakpoints.has_any())) {
        prev_state_ = state_;
        if (auto hit = machine.breakpoints.check_pc(state_.pc)) {
            if (machine.tui) {
                machine.breakpoints.set_skip_once_pc(state_.pc);
                machine.tui->set_status_override(hit->description);
                machine.tui->pause_loop();
            }
        }
    }
    // If rollback support is active, record architectural and memory checkpoints before updating
    // state.
    if (simrv::compiler::unlikely(machine.s_rollback_enabled)) {
        if (!machine.breakpoints.has_any()) {
            prev_state_ = state_;
        }
        push_undo_state();
    }
    uint32_t step_cycles = 1;

    if (machine.s_cycle_accurate) {
        // Mode 1: Cycle-accurate pipeline execution.
        // Runs stages via persistent coroutines and tracks pipeline stalls, cache misses, and
        // hazard resolutions.
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
        bool is_jump =
            (pipeline_context.opcode == Opcode::Jal || pipeline_context.opcode == Opcode::Jalr);
        bool branched = pipeline_context.tkn;

        step_cycles = pipeline_sim.step_instruction(
            pipeline_context.cpc, pipeline_context.opcode, pipeline_context.rd,
            pipeline_context.rs1, pipeline_context.rs2, pipeline_context.op_id, branched, is_branch,
            is_jump, icache_miss, dcache_miss, pipeline_context.tlb_miss, pipeline_context.jmp_pc);
    } else {
        if (machine.s_high_performance) {
            // Mode 2: Cached fast-path functional execution (pre-decoded direct-lookup cache).
            // Bypasses pipeline orchestration and fetches from direct lookup cache if available.
            auto* cached = decode_cache.lookup(state_.pc);
            if (simrv::compiler::likely(cached != nullptr)) {
                const bool copy_ctx = machine.s_tuimode || machine.s_lockstep_mode ||
                                      machine.s_gdb_mode || machine.s_bp_trace ||
                                      (machine.s_strace != 0);
                const bool inst_mix = machine.s_use_mix || machine.s_tuimode;
                if (simrv::compiler::unlikely(copy_ctx && inst_mix)) {
                    execute_cached_op_fast<true, true>(machine, *cached);
                } else if (simrv::compiler::unlikely(copy_ctx)) {
                    execute_cached_op_fast<true, false>(machine, *cached);
                } else if (simrv::compiler::unlikely(inst_mix)) {
                    execute_cached_op_fast<false, true>(machine, *cached);
                } else {
                    execute_cached_op_fast<false, false>(machine, *cached);
                }
            } else {
                // Decode cache miss: run basic functional stages sequentially and cache the decoded
                // op.
                bool success = fetch_stage(machine, state_.pc) && decode_stage(machine);

                if (success && !pipeline_context.pending_exception.has_value()) {
                    CachedOp op;
                    op.copy_from(pipeline_context);
                    decode_cache.insert(state_.pc, op);
                }

                if (success) {
                    success = execute_stage(machine) && memory_stage(machine) &&
                              writeback_stage(machine) && commit_stage(machine);
                }

                if (!success) {
                    const auto cause =
                        pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch);
                    raise_exception(static_cast<TrapCause>(cause), pipeline_context.pending_tval);
                }
            }
        } else {
            // Mode 3: Normal pipeline coroutine execution.
            // Bypasses cycle-accurate latency step tracking but executes instruction stages via
            // coroutine states.
            if (!pipeline_task.handle) {
                pipeline_task = run_pipeline_coroutine(&machine);
            }

            auto err = pipeline_task.resume();
            if (err.has_value()) {
                raise_exception(static_cast<TrapCause>(err->code), err->tval);
            }
        }
    }

    // CLINT real-time-clock interrupt and cycle counter increments.
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

    // Record trace logs for active debug TUI components.
    if (machine.s_tuimode && machine.tui && machine.tui->is_trace_active()) {
        record_trace_for_tui(machine);
    }

    if (simrv::compiler::unlikely(machine.breakpoints.has_any())) {
        if (auto hit = machine.breakpoints.check_reg_changes(state_, prev_state_)) {
            if (machine.tui) {
                machine.tui->set_status_override(hit->description);
                machine.tui->pause_loop();
            }
        }
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

    machine.tui->record_instruction(pipeline_context.cpc, opcode, op_id, std::to_underlying(rd),
                                    rd_val, std::to_underlying(rs1), rs1_val,
                                    std::to_underlying(rs2), rs2_val, pipeline_context.imm);
}

void CPU::run_cycle_baremetal(Machine& machine) {
    if (simrv::compiler::unlikely(machine.breakpoints.has_any())) {
        prev_state_ = state_;
        if (auto hit = machine.breakpoints.check_pc(state_.pc)) {
            if (machine.tui) {
                machine.tui->set_status_override(hit->description);
                machine.tui->pause_loop();
            }
        }
    }
    if (simrv::compiler::unlikely(machine.s_rollback_enabled)) {
        if (!machine.breakpoints.has_any()) {
            prev_state_ = state_;
        }
        push_undo_state();
    }
    pipeline_context.pending_exception = std::nullopt;
    pipeline_context.pending_tval = 0;

    if (machine.s_high_performance) {
        auto* cached = decode_cache.lookup(state_.pc);
        if (simrv::compiler::likely(cached != nullptr)) {
            const bool copy_ctx = machine.s_tuimode || machine.s_lockstep_mode ||
                                  machine.s_gdb_mode || machine.s_bp_trace ||
                                  (machine.s_strace != 0);
            const bool inst_mix = machine.s_use_mix || machine.s_tuimode;
            if (simrv::compiler::unlikely(copy_ctx && inst_mix)) {
                execute_cached_op_fast<true, true>(machine, *cached);
            } else if (simrv::compiler::unlikely(copy_ctx)) {
                execute_cached_op_fast<true, false>(machine, *cached);
            } else if (simrv::compiler::unlikely(inst_mix)) {
                execute_cached_op_fast<false, true>(machine, *cached);
            } else {
                execute_cached_op_fast<false, false>(machine, *cached);
            }
            clint_mmio.mcycle++;
            if (machine.s_tuimode && machine.tui && machine.tui->is_trace_active()) {
                record_trace_for_tui(machine);
            }
            if (simrv::compiler::unlikely(machine.breakpoints.has_any())) {
                if (auto hit = machine.breakpoints.check_reg_changes(state_, prev_state_)) {
                    if (machine.tui) {
                        machine.tui->set_status_override(hit->description);
                        machine.tui->pause_loop();
                    }
                }
            }
            return;
        }
    }

    run_fetch_stage_baremetal(machine);
    const bool success = !pipeline_context.pending_exception.has_value() && decode_stage(machine);

    if (success && !pipeline_context.pending_exception.has_value()) {
        CachedOp op;
        op.copy_from(pipeline_context);
        decode_cache.insert(state_.pc, op);
    }

    if (success) {
        const bool rest_success =
            execute_stage(machine) &&
            (run_memory_stage_baremetal(machine),
             !pipeline_context.pending_exception.has_value()) &&
            writeback_stage(machine) &&
            (run_commit_stage_baremetal(machine), !pipeline_context.pending_exception.has_value());
        if (simrv::compiler::unlikely(!rest_success)) {
            const auto cause =
                pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch);
            raise_exception(static_cast<TrapCause>(cause), pipeline_context.pending_tval);
        }
    } else {
        const auto cause =
            pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch);
        raise_exception(static_cast<TrapCause>(cause), pipeline_context.pending_tval);
    }

    clint_mmio.mcycle++;
    if (machine.s_tuimode && machine.tui && machine.tui->is_trace_active()) {
        record_trace_for_tui(machine);
    }

    if (simrv::compiler::unlikely(machine.breakpoints.has_any())) {
        if (auto hit = machine.breakpoints.check_reg_changes(state_, prev_state_)) {
            if (machine.tui) {
                machine.tui->set_status_override(hit->description);
                machine.tui->pause_loop();
            }
        }
    }
}

void CPU::run_memory_stage_baremetal(Machine& machine) {
    auto& ctx = pipeline_context;
    if (ctx.pending_exception.has_value()) {
        return;
    }

    if (ctx.op_id >= OperationId::VSETVLI && ctx.op_id <= OperationId::VWSLL_VI) {
        return;
    }

    const auto opcode = static_cast<Opcode>(ctx.opcode);
    const auto funct5 = static_cast<Funct5Amo>(ctx.funct5);
    const auto addr = ctx.mem_addr;

    // Load Phase
    if (opcode == Opcode::Load || (opcode == Opcode::Amo && funct5 != Funct5Amo::Sc)) {
        if (simrv::compiler::likely(simrv::memory::is_dram_addr(addr))) {
            ctx.mem_rdata = simrv::memory::ram_read_fast(addr, static_cast<Instruction>(ctx.funct3),
                                                         machine.mmem);
        } else {
            ctx.mem_rdata =
                simrv::memory::MemoryAccess::loadInt(machine.memory_, *this, addr, ctx.funct3);
        }
    }

    if (opcode == Opcode::LoadFp) {
        if (simrv::compiler::likely(simrv::memory::is_dram_addr(addr))) {
            const auto f3 = ctx.funct3;
            switch (f3) {
                case Funct3::Flw: {
                    const Word lo = simrv::memory::ram_read_fast(
                        addr, static_cast<Instruction>(Funct3::Lw), machine.mmem);
                    ctx.fp_mem_rdata = static_cast<uint64_t>(kF32BoxerBits) |
                                       static_cast<uint64_t>(lo & kLower32Mask);
                    break;
                }
                case Funct3::Fld: {
                    if constexpr (simrv::xlen::kIsXLen64) {
                        ctx.fp_mem_rdata =
                            static_cast<FloatingRegister>(simrv::memory::ram_read_fast(
                                addr, static_cast<Instruction>(Funct3::Ld), machine.mmem));
                    } else {
                        const Word lo = simrv::memory::ram_read_fast(
                            addr, static_cast<Instruction>(Funct3::Lw), machine.mmem);
                        const Word hi = simrv::memory::ram_read_fast(
                            addr + 4, static_cast<Instruction>(Funct3::Lw), machine.mmem);
                        ctx.fp_mem_rdata =
                            static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
                    }
                    break;
                }
                default:
                    break;
            }
        } else {
            ctx.fp_mem_rdata =
                simrv::memory::MemoryAccess::loadFp(machine.memory_, *this, addr, ctx.funct3);
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
            // Check tohost writes (fast filter for 0x80000000 or 0x40000000 regions)
            if (simrv::compiler::unlikely((addr & 0xC0000000ULL) != 0)) {
                if (simrv::compiler::unlikely(addr == machine.s_isatest_tohost ||
                                              addr == 0x80001000ULL || addr == 0x40008000ULL)) {
                    const bool is_tohost_write =
                        simrv::xlen::kIsXLen64
                            ? (ctx.funct3 == Funct3::Sw || ctx.funct3 == Funct3::Sd)
                            : (ctx.funct3 == Funct3::Sw);
                    if (is_tohost_write) {
                        machine.tohost.store(
                            simrv::xlen::kIsXLen64
                                ? ctx.mem_wdata
                                : ((machine.tohost.load(std::memory_order_relaxed) &
                                    0xFFFFFFFF00000000ULL) |
                                   ctx.mem_wdata),
                            std::memory_order_relaxed);
                    }
                } else if (simrv::compiler::unlikely(!simrv::xlen::kIsXLen64 &&
                                                     (addr == machine.s_isatest_tohost + 4 ||
                                                      addr == 0x80001004ULL ||
                                                      addr == 0x40008004ULL))) {
                    const bool is_tohost_write = (ctx.funct3 == Funct3::Sw);
                    if (is_tohost_write) {
                        machine.tohost.store((machine.tohost.load(std::memory_order_relaxed) &
                                              0x00000000FFFFFFFFULL) |
                                                 (static_cast<uint64_t>(ctx.mem_wdata) << 32),
                                             std::memory_order_relaxed);
                    }
                }
            }
            if (machine.s_rollback_enabled) {
                Word old_val = simrv::memory::ram_read_fast(
                    addr, static_cast<Instruction>(ctx.funct3), machine.mmem);
                record_mem_write(addr, old_val, static_cast<Instruction>(ctx.funct3));
            }
            simrv::memory::ram_write_fast(addr, ctx.mem_wdata, static_cast<Instruction>(ctx.funct3),
                                          machine.mmem);
        } else {
            simrv::memory::MemoryAccess::storeInt(machine.memory_, *this, addr, ctx.mem_wdata,
                                                  ctx.funct3);
        }
    }

    if (opcode == Opcode::StoreFp) {
        if (simrv::compiler::likely(simrv::memory::is_dram_addr(addr))) {
            const auto f3 = ctx.funct3;
            switch (f3) {
                case Funct3::Fsw:
                    if (machine.s_rollback_enabled) {
                        Word old_val = simrv::memory::ram_read_fast(
                            addr, static_cast<Instruction>(Funct3::Sw), machine.mmem);
                        record_mem_write(addr, old_val, static_cast<Instruction>(Funct3::Sw));
                    }
                    simrv::memory::ram_write_fast(
                        addr,
                        static_cast<Word>(ctx.fp_mem_wdata &
                                          static_cast<FloatingRegister>(kLower32Mask)),
                        static_cast<Instruction>(Funct3::Sw), machine.mmem);
                    break;
                case Funct3::Fsd:
                    if constexpr (simrv::xlen::kIsXLen64) {
                        if (machine.s_rollback_enabled) {
                            Word old_val = simrv::memory::ram_read_fast(
                                addr, static_cast<Instruction>(Funct3::Sd), machine.mmem);
                            record_mem_write(addr, old_val, static_cast<Instruction>(Funct3::Sd));
                        }
                        simrv::memory::ram_write_fast(addr, static_cast<Word>(ctx.fp_mem_wdata),
                                                      static_cast<Instruction>(Funct3::Sd),
                                                      machine.mmem);
                    } else {
                        if (machine.s_rollback_enabled) {
                            Word old1 = simrv::memory::ram_read_fast(
                                addr, static_cast<Instruction>(Funct3::Sw), machine.mmem);
                            record_mem_write(addr, old1, static_cast<Instruction>(Funct3::Sw));
                            Word old2 = simrv::memory::ram_read_fast(
                                addr + 4, static_cast<Instruction>(Funct3::Sw), machine.mmem);
                            record_mem_write(addr + 4, old2, static_cast<Instruction>(Funct3::Sw));
                        }
                        simrv::memory::ram_write_fast(
                            addr,
                            static_cast<Word>(ctx.fp_mem_wdata &
                                              static_cast<FloatingRegister>(kLower32Mask)),
                            static_cast<Instruction>(Funct3::Sw), machine.mmem);
                        simrv::memory::ram_write_fast(
                            addr + 4,
                            static_cast<Word>((ctx.fp_mem_wdata >> 32) &
                                              static_cast<FloatingRegister>(kLower32Mask)),
                            static_cast<Instruction>(Funct3::Sw), machine.mmem);
                    }
                    break;
                default:
                    break;
            }
        } else {
            simrv::memory::MemoryAccess::storeFp(machine.memory_, *this, addr, ctx.fp_mem_wdata,
                                                 ctx.funct3);
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
            co_yield simrv::pipeline::StageError{
                .code = pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch),
                .tval = pipeline_context.pending_tval};
            continue;
        }
        if (!decode_stage(machine)) {
            co_yield simrv::pipeline::StageError{
                .code = pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch),
                .tval = pipeline_context.pending_tval};
            continue;
        }
        if (!execute_stage(machine)) {
            co_yield simrv::pipeline::StageError{
                .code = pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch),
                .tval = pipeline_context.pending_tval};
            continue;
        }
        if (!memory_stage(machine)) {
            co_yield simrv::pipeline::StageError{
                .code = pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch),
                .tval = pipeline_context.pending_tval};
            continue;
        }
        if (!writeback_stage(machine)) {
            co_yield simrv::pipeline::StageError{
                .code = pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch),
                .tval = pipeline_context.pending_tval};
            continue;
        }
        if (!commit_stage(machine)) {
            co_yield simrv::pipeline::StageError{
                .code = pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch),
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
    state_.pc += op.len;
    pc_sign_extend();
}

void CPU::execute_cached_auipc(CachedOp& op) {
    if (op.rd != RegId::Zero) {
        state_.regs.write(op.rd, state_.pc + op.imm);
    }
    e_icount++;
    if (op.cinsn) e_ccount++;
    state_.pc += op.len;
    pc_sign_extend();
}

void CPU::execute_cached_jal(CachedOp& op) {
    Register const next_pc = state_.pc + op.len;
    pipeline_context.tkn = true;
    pipeline_context.jmp_pc = state_.pc + op.imm;
    if (op.rd != RegId::Zero) {
        state_.regs.write(op.rd, next_pc);
    }
    const bool has_c = misa_has_extension(state_.misa, isa::IsaExtension::C);
    const Word alignment_mask = has_c ? 1u : 3u;
    if ((pipeline_context.jmp_pc & alignment_mask) != 0) {
        raise_exception(static_cast<TrapCause>(ExceptionCode::MisalignedFetch),
                        pipeline_context.jmp_pc);
        return;
    }
    state_.pc = pipeline_context.jmp_pc;
    e_icount++;
    if (op.cinsn) e_ccount++;
    pc_sign_extend();
}

void CPU::execute_cached_jalr(CachedOp& op, Register rrs1) {
    Register const next_pc = state_.pc + op.len;
    pipeline_context.tkn = true;
    pipeline_context.jmp_pc = (rrs1 + op.imm) & ~static_cast<Register>(1);
    if (state_.regs.xlen == 32) {
        pipeline_context.jmp_pc = static_cast<Register>(
            static_cast<int64_t>(static_cast<int32_t>(pipeline_context.jmp_pc)));
    }
    if (op.rd != RegId::Zero) {
        state_.regs.write(op.rd, next_pc);
    }
    const bool has_c = misa_has_extension(state_.misa, isa::IsaExtension::C);
    const Word alignment_mask = has_c ? 1u : 3u;
    if ((pipeline_context.jmp_pc & alignment_mask) != 0) {
        raise_exception(static_cast<TrapCause>(ExceptionCode::MisalignedFetch),
                        pipeline_context.jmp_pc);
        return;
    }
    state_.pc = pipeline_context.jmp_pc;
    e_icount++;
    if (op.cinsn) e_ccount++;
    pc_sign_extend();
}

void CPU::execute_cached_branch(CachedOp& op, Register rrs1, Register rrs2) {
    bool tkn = false;
    switch (op.funct3) {
        case isa::Funct3::Beq:
            tkn = (rrs1 == rrs2);
            break;
        case isa::Funct3::Bne:
            tkn = (rrs1 != rrs2);
            break;
        case isa::Funct3::Blt:
            tkn = (static_cast<SignedWord>(rrs1) < static_cast<SignedWord>(rrs2));
            break;
        case isa::Funct3::Bge:
            tkn = (static_cast<SignedWord>(rrs1) >= static_cast<SignedWord>(rrs2));
            break;
        case isa::Funct3::Bltu:
            tkn = (rrs1 < rrs2);
            break;
        case isa::Funct3::Bgeu:
            tkn = (rrs1 >= rrs2);
            break;
        default:
            tkn = execute::ExecuteUnit::branchTaken(rrs1, rrs2, op.funct3, state_.regs.xlen);
            break;
    }
    pipeline_context.tkn = tkn;
    Register const target_pc = tkn ? (state_.pc + op.imm) : (state_.pc + op.len);
    state_.pc = target_pc;
    e_icount++;
    if (op.cinsn) e_ccount++;
    pc_sign_extend();
}

void CPU::execute_cached_op(CachedOp& op, Register rrs1, Register rrs2) {
    Register wb_data = 0;
    switch (op.op_id) {
        case isa::ADD:
            wb_data = rrs1 + rrs2;
            break;
        case isa::SUB:
            wb_data = rrs1 - rrs2;
            break;
        case isa::AND:
            wb_data = rrs1 & rrs2;
            break;
        case isa::OR:
            wb_data = rrs1 | rrs2;
            break;
        case isa::XOR:
            wb_data = rrs1 ^ rrs2;
            break;
        case isa::SLL:
            wb_data = rrs1 << (rrs2 & simrv::xlen::xlen_shift_mask());
            break;
        case isa::SRL:
            wb_data = rrs1 >> (rrs2 & simrv::xlen::xlen_shift_mask());
            break;
        case isa::SRA:
            wb_data = static_cast<Register>(static_cast<SignedWord>(rrs1) >>
                                            (rrs2 & simrv::xlen::xlen_shift_mask()));
            break;
        case isa::SLT:
            wb_data = static_cast<Register>(static_cast<SignedWord>(rrs1) <
                                            static_cast<SignedWord>(rrs2));
            break;
        case isa::SLTU:
            wb_data = static_cast<Register>(rrs1 < rrs2);
            break;
        default:
            wb_data = execute::ExecuteUnit::aluInt(rrs1, rrs2, op.op_id, state_.regs.xlen);
            break;
    }
    if (op.rd != RegId::Zero) {
        state_.regs.write(op.rd, wb_data);
    }
    e_icount++;
    if (op.cinsn) e_ccount++;
    state_.pc += op.len;
    pc_sign_extend();
}

void CPU::execute_cached_op_imm(CachedOp& op, Register rrs1) {
    Register wb_data = 0;
    switch (op.op_id) {
        case isa::ADDI:
            wb_data = rrs1 + op.imm;
            break;
        case isa::ANDI:
            wb_data = rrs1 & op.imm;
            break;
        case isa::ORI:
            wb_data = rrs1 | op.imm;
            break;
        case isa::XORI:
            wb_data = rrs1 ^ op.imm;
            break;
        case isa::SLLI:
            wb_data = rrs1 << (op.imm & simrv::xlen::xlen_shift_mask());
            break;
        case isa::SRLI:
            wb_data = rrs1 >> (op.imm & simrv::xlen::xlen_shift_mask());
            break;
        case isa::SRAI:
            wb_data = static_cast<Register>(static_cast<SignedWord>(rrs1) >>
                                            (op.imm & simrv::xlen::xlen_shift_mask()));
            break;
        case isa::SLTI:
            wb_data = static_cast<Register>(static_cast<SignedWord>(rrs1) <
                                            static_cast<SignedWord>(op.imm));
            break;
        case isa::SLTIU:
            wb_data = static_cast<Register>(rrs1 < static_cast<Register>(op.imm));
            break;
        default:
            wb_data = execute::ExecuteUnit::aluInt(rrs1, op.imm, op.op_id, state_.regs.xlen);
            break;
    }
    if (op.rd != RegId::Zero) {
        state_.regs.write(op.rd, wb_data);
    }
    e_icount++;
    if (op.cinsn) e_ccount++;
    state_.pc += op.len;
    pc_sign_extend();
}

void CPU::execute_cached_op_imm32(CachedOp& op, Register rrs1) {
    Register wb_data = 0;
    switch (op.op_id) {
        case isa::ADDIW:
            wb_data =
                static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(rrs1 + op.imm)));
            break;
        case isa::SLLIW:
            wb_data = static_cast<Register>(static_cast<int64_t>(
                static_cast<int32_t>(static_cast<uint32_t>(rrs1) << (op.imm & 0x1f))));
            break;
        case isa::SRLIW:
            wb_data = static_cast<Register>(static_cast<int64_t>(
                static_cast<int32_t>(static_cast<uint32_t>(rrs1) >> (op.imm & 0x1f))));
            break;
        case isa::SRAIW:
            wb_data = static_cast<Register>(
                static_cast<int64_t>(static_cast<int32_t>(rrs1) >> (op.imm & 0x1f)));
            break;
        default:
            wb_data = execute::ExecuteUnit::aluIntW(rrs1, op.imm, op.op_id);
            break;
    }
    if (op.rd != RegId::Zero) {
        state_.regs.write(op.rd, wb_data);
    }
    e_icount++;
    if (op.cinsn) e_ccount++;
    state_.pc += op.len;
    pc_sign_extend();
}

void CPU::execute_cached_op32(CachedOp& op, Register rrs1, Register rrs2) {
    Register wb_data = 0;
    switch (op.op_id) {
        case isa::ADDW:
            wb_data =
                static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(rrs1 + rrs2)));
            break;
        case isa::SUBW:
            wb_data =
                static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(rrs1 - rrs2)));
            break;
        case isa::SLLW:
            wb_data = static_cast<Register>(static_cast<int64_t>(
                static_cast<int32_t>(static_cast<uint32_t>(rrs1) << (rrs2 & 0x1f))));
            break;
        default:
            wb_data = execute::ExecuteUnit::aluIntW(rrs1, rrs2, op.op_id);
            break;
    }
    if (op.rd != RegId::Zero) {
        state_.regs.write(op.rd, wb_data);
    }
    e_icount++;
    if (op.cinsn) e_ccount++;
    state_.pc += op.len;
    pc_sign_extend();
}

auto CPU::try_fast_load(Machine& machine, Address mem_addr, Funct3 funct3, Register& out_val)
    -> bool {
    const unsigned size_bytes = 1u << (static_cast<unsigned>(funct3) & 0x3u);
    const unsigned alignment_mask = size_bytes - 1u;
    if (simrv::compiler::unlikely((mem_addr & alignment_mask) != 0)) {
        return false;
    }
    const PrivilegeLevel eff_priv = effective_data_privilege();
    const bool translation_enabled =
        (eff_priv != kPrivMachine && simrv::xlen::satp_translation_enabled(state_.satp));
    if (!translation_enabled) {
        if (simrv::compiler::likely(simrv::memory::is_dram_addr(mem_addr))) {
            out_val = simrv::memory::ram_read_fast(mem_addr, static_cast<Instruction>(funct3),
                                                   machine.mmem);
            return true;
        }
    } else {
        const Word current_asid = simrv::xlen::satp_asid(state_.satp);
        const Address vpn = mem_addr >> 12;
        const size_t tlb_idx = vpn & 2047u;
        const auto& entry = soft_tlb_read
            [tlb_idx];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        if (simrv::compiler::likely(entry.matches(vpn, current_asid, eff_priv, soft_tlb_epoch))) {
            if (simrv::compiler::likely(entry.host_ptr_base != nullptr)) {
                out_val = simrv::memory::host_read_fast(entry.host_ptr_base + (mem_addr & 0xFFF),
                                                        static_cast<Instruction>(funct3));
                return true;
            }
            out_val = simrv::memory::ram_read_fast(entry.paddr_base + (mem_addr & 0xFFF),
                                                   static_cast<Instruction>(funct3), machine.mmem);
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
    const PrivilegeLevel eff_priv = effective_data_privilege();
    const bool translation_enabled =
        (eff_priv != kPrivMachine && simrv::xlen::satp_translation_enabled(state_.satp));
    if (!translation_enabled) {
        if (simrv::compiler::likely(simrv::memory::is_dram_addr(mem_addr) &&
                                    !is_tohost_addr(machine, mem_addr))) {
            if (simrv::compiler::unlikely(machine.s_rollback_enabled)) {
                Word old_val = simrv::memory::ram_read_fast(
                    mem_addr, static_cast<Instruction>(funct3), machine.mmem);
                record_mem_write(mem_addr, old_val, static_cast<Instruction>(funct3));
            }
            simrv::memory::ram_write_fast(mem_addr, rrs2, static_cast<Instruction>(funct3),
                                          machine.mmem);
            return true;
        }
    } else {
        const Word current_asid = simrv::xlen::satp_asid(state_.satp);
        const Address vpn = mem_addr >> 12;
        const size_t tlb_idx = vpn & 2047u;
        const auto& entry = soft_tlb_write
            [tlb_idx];  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
        if (simrv::compiler::likely(entry.matches(vpn, current_asid, eff_priv, soft_tlb_epoch))) {
            Address const paddr = entry.paddr_base + (mem_addr & 0xFFF);
            if (simrv::compiler::unlikely(machine.s_rollback_enabled) &&
                simrv::memory::is_dram_addr(paddr)) {
                Word old_val = simrv::memory::ram_read_fast(paddr, static_cast<Instruction>(funct3),
                                                            machine.mmem);
                record_mem_write(paddr, old_val, static_cast<Instruction>(funct3));
            }
            if (simrv::compiler::likely(entry.host_ptr_base != nullptr)) {
                simrv::memory::host_write_fast(entry.host_ptr_base + (mem_addr & 0xFFF), rrs2,
                                               static_cast<Instruction>(funct3));
                return true;
            }
            if (simrv::compiler::likely(simrv::memory::is_dram_addr(paddr) &&
                                        !is_tohost_addr(machine, paddr))) {
                simrv::memory::ram_write_fast(paddr, rrs2, static_cast<Instruction>(funct3),
                                              machine.mmem);
                return true;
            }
        }
    }
    return false;
}

auto CPU::execute_cached_load(Machine& machine, CachedOp& op, Register rrs1) -> bool {
    Address const mem_addr = rrs1 + op.imm;
    if (simrv::compiler::unlikely(machine.s_tuimode || machine.s_bp_trace)) {
        pipeline_context.mem_addr = mem_addr;
    }
    Register mem_rdata = 0;
    if (!try_fast_load(machine, mem_addr, op.funct3, mem_rdata)) {
        mem_rdata =
            simrv::memory::MemoryAccess::loadInt(machine.memory_, *this, mem_addr, op.funct3);
        if (pipeline_context.pending_exception.has_value()) {
            raise_exception(static_cast<TrapCause>(*pipeline_context.pending_exception),
                            pipeline_context.pending_tval);
            return false;
        }
    }

    if (op.rd != RegId::Zero) {
        state_.regs.write(op.rd, mem_rdata);
    }
    e_icount++;
    if (op.cinsn) e_ccount++;
    state_.pc += op.len;
    pc_sign_extend();
    return true;
}

auto CPU::execute_cached_store(Machine& machine, CachedOp& op, Register rrs1, Register rrs2)
    -> bool {
    Address const mem_addr = rrs1 + op.imm;
    if (simrv::compiler::unlikely(machine.s_tuimode || machine.s_bp_trace)) {
        pipeline_context.mem_addr = mem_addr;
    }
    if (!try_fast_store(machine, mem_addr, op.funct3, rrs2)) {
        simrv::memory::MemoryAccess::storeInt(machine.memory_, *this, mem_addr, rrs2, op.funct3);
        if (pipeline_context.pending_exception.has_value()) {
            raise_exception(static_cast<TrapCause>(*pipeline_context.pending_exception),
                            pipeline_context.pending_tval);
            return false;
        }
    }

    state_.reserved = 0;
    e_icount++;
    if (op.cinsn) e_ccount++;
    state_.pc += op.len;
    pc_sign_extend();
    return true;
}

auto CPU::execute_cached_fallback(Machine& machine) -> void {
    fetch_operands(machine);
    bool success = execute_stage(machine) && memory_stage(machine) && writeback_stage(machine) &&
                   commit_stage(machine);
    if (!success) {
        const auto cause =
            pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch);
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
            Word const irq_num = select_highest_priority_interrupt(mask);
            raise_exception(kInterruptCauseBit | irq_num, 0);
        }
    }
}

template <bool kCopyContext, bool kInstMix>
void CPU::execute_cached_op_fast(Machine& machine, CachedOp& op) {
    if constexpr (kCopyContext) {
        pipeline_context.copy_from(op);
        pipeline_context.tlb_miss = false;
        pipeline_context.pending_tval = 0;
    } else {
        pipeline_context.pending_exception = std::nullopt;
    }

    if constexpr (kInstMix) {
        e_instmix[static_cast<std::size_t>(op.op_id)]++;
    }

    const auto cached_opc = static_cast<Opcode>(op.opcode);
    const bool is_fp_cached = (cached_opc == Opcode::LoadFp) || (cached_opc == Opcode::StoreFp) ||
                              (cached_opc == Opcode::OpFp) || (cached_opc == Opcode::MAdd) ||
                              (cached_opc == Opcode::MSub) || (cached_opc == Opcode::NMAdd) ||
                              (cached_opc == Opcode::NMSub);
    const bool is_vec_cached = (cached_opc == Opcode::OpV);

    if (simrv::compiler::unlikely(
            (is_fp_cached && (state_.mstatus & enum_mask(MstatusBit::Fs)) == 0) ||
            (is_vec_cached && (state_.mstatus & enum_mask(MstatusBit::Vs)) == 0))) {
        if constexpr (!kCopyContext) {
            pipeline_context.copy_from(op);
            pipeline_context.tlb_miss = false;
            pipeline_context.pending_tval = 0;
        }
        execute_cached_fallback(machine);
        return;
    }

    // 3. Execute, Memory, Writeback, Commit — single flat op_id dispatch (no double switch).
    // ALU arms compute wb_data and break to the shared writeback tail below the switch.
    // Non-ALU arms (branches, jumps, loads, stores, fallback) handle their own commit and return.
    Register rrs1 = 0, rrs2 = 0;
    Register wb_data = 0;
    switch (op.op_id) {
        // ---- Scalar ALU with two register operands ----
        case isa::ADD:
            rrs1 = state_.regs.read(op.rs1);
            rrs2 = state_.regs.read(op.rs2);
            wb_data = rrs1 + rrs2;
            break;
        case isa::SUB:
            rrs1 = state_.regs.read(op.rs1);
            rrs2 = state_.regs.read(op.rs2);
            wb_data = rrs1 - rrs2;
            break;
        case isa::AND:
            rrs1 = state_.regs.read(op.rs1);
            rrs2 = state_.regs.read(op.rs2);
            wb_data = rrs1 & rrs2;
            break;
        case isa::OR:
            rrs1 = state_.regs.read(op.rs1);
            rrs2 = state_.regs.read(op.rs2);
            wb_data = rrs1 | rrs2;
            break;
        case isa::XOR:
            rrs1 = state_.regs.read(op.rs1);
            rrs2 = state_.regs.read(op.rs2);
            wb_data = rrs1 ^ rrs2;
            break;
        case isa::SLL:
            rrs1 = state_.regs.read(op.rs1);
            rrs2 = state_.regs.read(op.rs2);
            wb_data = rrs1 << (rrs2 & simrv::xlen::xlen_shift_mask());
            break;
        case isa::SRL:
            rrs1 = state_.regs.read(op.rs1);
            rrs2 = state_.regs.read(op.rs2);
            wb_data = rrs1 >> (rrs2 & simrv::xlen::xlen_shift_mask());
            break;
        case isa::SRA:
            rrs1 = state_.regs.read(op.rs1);
            rrs2 = state_.regs.read(op.rs2);
            wb_data = static_cast<Register>(static_cast<SignedWord>(rrs1) >>
                                            (rrs2 & simrv::xlen::xlen_shift_mask()));
            break;
        case isa::SLT:
            rrs1 = state_.regs.read(op.rs1);
            rrs2 = state_.regs.read(op.rs2);
            wb_data = static_cast<Register>(static_cast<SignedWord>(rrs1) <
                                            static_cast<SignedWord>(rrs2));
            break;
        case isa::SLTU:
            rrs1 = state_.regs.read(op.rs1);
            rrs2 = state_.regs.read(op.rs2);
            wb_data = static_cast<Register>(rrs1 < rrs2);
            break;
        // ---- 32-bit (W) register ops (RV64 only) ----
        case isa::ADDW:
            rrs1 = state_.regs.read(op.rs1);
            rrs2 = state_.regs.read(op.rs2);
            wb_data =
                static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(rrs1 + rrs2)));
            break;
        case isa::SUBW:
            rrs1 = state_.regs.read(op.rs1);
            rrs2 = state_.regs.read(op.rs2);
            wb_data =
                static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(rrs1 - rrs2)));
            break;
        case isa::SLLW:
            rrs1 = state_.regs.read(op.rs1);
            rrs2 = state_.regs.read(op.rs2);
            wb_data = static_cast<Register>(static_cast<int64_t>(
                static_cast<int32_t>(static_cast<uint32_t>(rrs1) << (rrs2 & 0x1f))));
            break;
        case isa::SRLW:
            rrs1 = state_.regs.read(op.rs1);
            rrs2 = state_.regs.read(op.rs2);
            wb_data = static_cast<Register>(static_cast<int64_t>(
                static_cast<int32_t>(static_cast<uint32_t>(rrs1) >> (rrs2 & 0x1f))));
            break;
        case isa::SRAW:
            rrs1 = state_.regs.read(op.rs1);
            rrs2 = state_.regs.read(op.rs2);
            wb_data = static_cast<Register>(
                static_cast<int64_t>(static_cast<int32_t>(rrs1) >> (rrs2 & 0x1f)));
            break;
        // ---- Scalar ALU with immediate ----
        case isa::ADDI:
            rrs1 = state_.regs.read(op.rs1);
            wb_data = rrs1 + op.imm;
            break;
        case isa::ANDI:
            rrs1 = state_.regs.read(op.rs1);
            wb_data = rrs1 & op.imm;
            break;
        case isa::ORI:
            rrs1 = state_.regs.read(op.rs1);
            wb_data = rrs1 | op.imm;
            break;
        case isa::XORI:
            rrs1 = state_.regs.read(op.rs1);
            wb_data = rrs1 ^ op.imm;
            break;
        case isa::SLLI:
            rrs1 = state_.regs.read(op.rs1);
            wb_data = rrs1 << (op.imm & simrv::xlen::xlen_shift_mask());
            break;
        case isa::SRLI:
            rrs1 = state_.regs.read(op.rs1);
            wb_data = rrs1 >> (op.imm & simrv::xlen::xlen_shift_mask());
            break;
        case isa::SRAI:
            rrs1 = state_.regs.read(op.rs1);
            wb_data = static_cast<Register>(static_cast<SignedWord>(rrs1) >>
                                            (op.imm & simrv::xlen::xlen_shift_mask()));
            break;
        case isa::SLTI:
            rrs1 = state_.regs.read(op.rs1);
            wb_data = static_cast<Register>(static_cast<SignedWord>(rrs1) <
                                            static_cast<SignedWord>(op.imm));
            break;
        case isa::SLTIU:
            rrs1 = state_.regs.read(op.rs1);
            wb_data = static_cast<Register>(rrs1 < static_cast<Register>(op.imm));
            break;
        // ---- 32-bit immediate ops (RV64 only) ----
        case isa::ADDIW:
            rrs1 = state_.regs.read(op.rs1);
            wb_data =
                static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(rrs1 + op.imm)));
            break;
        case isa::SLLIW:
            rrs1 = state_.regs.read(op.rs1);
            wb_data = static_cast<Register>(static_cast<int64_t>(
                static_cast<int32_t>(static_cast<uint32_t>(rrs1) << (op.imm & 0x1f))));
            break;
        case isa::SRLIW:
            rrs1 = state_.regs.read(op.rs1);
            wb_data = static_cast<Register>(static_cast<int64_t>(
                static_cast<int32_t>(static_cast<uint32_t>(rrs1) >> (op.imm & 0x1f))));
            break;
        case isa::SRAIW:
            rrs1 = state_.regs.read(op.rs1);
            wb_data = static_cast<Register>(
                static_cast<int64_t>(static_cast<int32_t>(rrs1) >> (op.imm & 0x1f)));
            break;
        // ---- Upper-immediate (no rs1) ----
        case isa::LUI:
            wb_data = op.imm;
            break;
        case isa::AUIPC:
            wb_data = state_.pc + op.imm;
            break;
        // ---- Branches: handle internally, then return ----
        case isa::BEQ:
        case isa::BNE:
        case isa::BLT:
        case isa::BGE:
        case isa::BLTU:
        case isa::BGEU:
            execute_cached_branch(op, state_.regs.read(op.rs1), state_.regs.read(op.rs2));
            handle_cached_interrupts();
            return;
        // ---- Jump instructions ----
        case isa::JAL:
            execute_cached_jal(op);
            handle_cached_interrupts();
            return;
        case isa::JALR:
            execute_cached_jalr(op, state_.regs.read(op.rs1));
            handle_cached_interrupts();
            return;
        // ---- Loads ----
        case isa::LB:
        case isa::LH:
        case isa::LW:
        case isa::LD:
        case isa::LBU:
        case isa::LHU:
        case isa::LWU:
        case isa::FLW:
        case isa::FLD:
            if (!execute_cached_load(machine, op, state_.regs.read(op.rs1))) return;
            handle_cached_interrupts();
            return;
        // ---- Stores ----
        case isa::SB:
        case isa::SH:
        case isa::SW:
        case isa::SD:
        case isa::FSW:
        case isa::FSD:
            if (!execute_cached_store(machine, op, state_.regs.read(op.rs1),
                                      state_.regs.read(op.rs2)))
                return;
            handle_cached_interrupts();
            return;
        // ---- Everything else: fallback to full pipeline stages ----
        default:
            if constexpr (!kCopyContext) {
                pipeline_context.copy_from(op);
                pipeline_context.tlb_miss = false;
                pipeline_context.pending_tval = 0;
            }
            execute_cached_fallback(machine);
            return;
    }

    // Shared writeback tail — only ALU arms (those that break) reach here.
    // Branchless GPR write: write rd unconditionally, then re-zero x0.
    state_.regs.write_branchless(op.rd, wb_data);
    e_icount++;
    if (op.cinsn) e_ccount++;
    state_.pc += op.len;
    pc_sign_extend();

    // 4. Handle pending interrupts (same check as commit_stage)
    handle_cached_interrupts();
}

void CPU::run_fast_baremetal_batch(Machine& machine, uint32_t batch_size) {
    const bool copy_ctx = machine.s_tuimode || machine.s_lockstep_mode || machine.s_gdb_mode ||
                          machine.s_bp_trace || (machine.s_strace != 0);
    const bool inst_mix = machine.s_use_mix || machine.s_tuimode;

    for (uint32_t b = 0; b < batch_size; ++b) {
        auto* cached = decode_cache.lookup(state_.pc);
        if (simrv::compiler::likely(cached != nullptr)) {
            if (simrv::compiler::unlikely(copy_ctx && inst_mix)) {
                execute_cached_op_fast<true, true>(machine, *cached);
            } else if (simrv::compiler::unlikely(copy_ctx)) {
                execute_cached_op_fast<true, false>(machine, *cached);
            } else if (simrv::compiler::unlikely(inst_mix)) {
                execute_cached_op_fast<false, true>(machine, *cached);
            } else {
                execute_cached_op_fast<false, false>(machine, *cached);
            }
        } else {
            run_cycle_baremetal(machine);
        }
        if (simrv::compiler::unlikely(machine.tohost != 0)) {
            break;
        }
    }
}

template void CPU::execute_cached_op_fast<false, false>(Machine& machine, CachedOp& op);
template void CPU::execute_cached_op_fast<true, false>(Machine& machine, CachedOp& op);
template void CPU::execute_cached_op_fast<false, true>(Machine& machine, CachedOp& op);
template void CPU::execute_cached_op_fast<true, true>(Machine& machine, CachedOp& op);

void CPU::push_undo_state() {
    UndoStep step;
    step.state = state_;
    step.pipeline_context = pipeline_context;
    step.pipeline_sim_state = pipeline_sim.save_state();
    step.clint_state = ClintState{.mtime = clint_mmio.mtime,
                                  .mtimecmp = clint_mmio.mtimecmp,
                                  .mcycle = clint_mmio.mcycle,
                                  .rtc_divider = clint_mmio.rtc_divider,
                                  .last_mtime = clint_mmio.last_mtime,
                                  .last_mtimecmp = clint_mmio.last_mtimecmp};
    step.e_icount = e_icount;
    step.e_ccount = e_ccount;
    step.e_instmix = e_instmix;
    undo_stack.push_front(std::move(step));
    if (undo_stack.size() > 1024) {
        undo_stack.pop_back();
    }
}

void CPU::record_mem_write(Address paddr, Word old_data, Instruction funct3) {
    if (undo_stack.empty()) return;
    auto& step = undo_stack.front();
    step.mem_writes.push_back(
        MemWriteRecord{.addr = paddr, .old_data = old_data, .funct3 = funct3});
}

auto CPU::perform_backstep() -> bool {
    if (undo_stack.empty()) return false;
    auto step = std::move(undo_stack.front());
    undo_stack.pop_front();
    if (machine_) {
        machine_->is_shutdown_ = false;
        machine_->is_running_ = true;
        for (auto it = step.mem_writes.rbegin(); it != step.mem_writes.rend(); ++it) {
            simrv::memory::ram_write_fast(it->addr, it->old_data, it->funct3,
                                          machine_->memory_.mmu()->mmem());
        }
    }
    state_ = step.state;
    pipeline_context = step.pipeline_context;
    if (step.pipeline_sim_state.has_value()) {
        pipeline_sim.restore_state(*step.pipeline_sim_state);
    }
    pipeline_task = {};
    clint_mmio.mtime = step.clint_state.mtime;
    clint_mmio.mtimecmp = step.clint_state.mtimecmp;
    clint_mmio.mcycle = step.clint_state.mcycle;
    clint_mmio.rtc_divider = step.clint_state.rtc_divider;
    clint_mmio.last_mtime = step.clint_state.last_mtime;
    clint_mmio.last_mtimecmp = step.clint_state.last_mtimecmp;
    e_icount = step.e_icount;
    e_ccount = step.e_ccount;
    e_instmix = step.e_instmix;
    soft_tlb_flush();
    return true;
}

void CPU::push_trace_history(Address pc, Instruction inst, const std::string& symbol) {
    // O(1) ring buffer write — no heap allocation, no shifting
    trace_history_buf_[trace_history_head_] = TraceHistoryEntry{pc, inst, symbol};
    trace_history_head_ = (trace_history_head_ + 1) % kTraceHistoryCapacity;
    if (trace_history_size_ < kTraceHistoryCapacity) {
        trace_history_size_++;
    }
}

auto CPU::trace_history_view() const -> std::vector<TraceHistoryEntry> {
    std::vector<TraceHistoryEntry> result;
    result.reserve(trace_history_size_);
    // oldest entry is at (head - size) wrapping around
    const std::size_t start =
        (trace_history_head_ + kTraceHistoryCapacity - trace_history_size_) % kTraceHistoryCapacity;
    for (std::size_t i = 0; i < trace_history_size_; ++i) {
        result.push_back(trace_history_buf_[(start + i) % kTraceHistoryCapacity]);
    }
    return result;
}

}  // namespace simrv::core