/**
 * @file Cpu.cpp
 * @brief CPU core state/control implementation.
 */
#include "simrv/core/Cpu.hpp"

#include <atomic>

#include "simrv/core/Machine.hpp"
#include "simrv/core/Pmp.hpp"
#include "simrv/core/Tracer.hpp"
#include "simrv/debug/GdbStub.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/pipeline/OperationTraits.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

using namespace simrv::isa;

namespace {
SIMRV_ALWAYS_INLINE auto captures_tui_execution_detail(const Machine& machine) -> bool {
    const auto sink = machine.telemetry_sink_raw();
    return sink && sink->captures_execution_detail();
}

SIMRV_ALWAYS_INLINE auto is_tohost_addr(const Machine& machine, Address addr) -> bool {
    return (addr - machine.isa_test_tohost() < 8) || (addr - 0x80001000ULL < 8) ||
           (addr - 0x40008000ULL < 8);
}

template <typename T, typename Op>
auto atomic_update(std::atomic_ref<T>& atomic_mem, Op&& op) -> T {
    T old_val = atomic_mem.load(std::memory_order_relaxed);
    while (!atomic_mem.compare_exchange_weak(old_val, op(old_val), std::memory_order_acq_rel,
                                             std::memory_order_relaxed)) {
    }
    return old_val;
}

template <typename T>
auto execute_dram_amo(T* mem_ptr, Word rs2_val, Funct5Amo funct5) -> T {
    std::atomic_ref<T> atomic_mem(*mem_ptr);
    const auto op_val = static_cast<T>(rs2_val);
    switch (funct5) {
        case Funct5Amo::Swap:
            return atomic_mem.exchange(op_val, std::memory_order_acq_rel);
        case Funct5Amo::Add:
            return atomic_mem.fetch_add(op_val, std::memory_order_acq_rel);
        case Funct5Amo::Xor:
            return atomic_mem.fetch_xor(op_val, std::memory_order_acq_rel);
        case Funct5Amo::And:
            return atomic_mem.fetch_and(op_val, std::memory_order_acq_rel);
        case Funct5Amo::Or:
            return atomic_mem.fetch_or(op_val, std::memory_order_acq_rel);
        case Funct5Amo::Min: {
            using SignedT = std::make_signed_t<T>;
            return atomic_update(atomic_mem, [op_val](T old) {
                return static_cast<T>(
                    std::min<SignedT>(static_cast<SignedT>(old), static_cast<SignedT>(op_val)));
            });
        }
        case Funct5Amo::Max: {
            using SignedT = std::make_signed_t<T>;
            return atomic_update(atomic_mem, [op_val](T old) {
                return static_cast<T>(
                    std::max<SignedT>(static_cast<SignedT>(old), static_cast<SignedT>(op_val)));
            });
        }
        case Funct5Amo::Minu:
            return atomic_update(atomic_mem, [op_val](T old) { return std::min<T>(old, op_val); });
        case Funct5Amo::Maxu:
            return atomic_update(atomic_mem, [op_val](T old) { return std::max<T>(old, op_val); });
        default:
            return atomic_mem.load(std::memory_order_relaxed);
    }
}
}  // namespace

CPU::CPU() : plic_mmio(*this), clint_mmio(*this), csr_file(*this), sbi(*this) { reset(); }

void CPU::apply_cpu_model_config(const simrv::pipeline::CpuModelConfig& config) {
    // Validation is performed by the parser/editor.  Keeping this operation atomic at the CPU
    // boundary prevents partially-applied timing models from leaking into a live CA pipeline.
    cpu_model_config = config;
    pipeline_sim.config = config.pipeline;
    cycle_counter_start_delay_ = pipeline_sim.config.cycle_counter_start_delay;
    // BaseCache has bounded 16 KiB / 8-way backing, enough for the shipped FPGA profiles.
    // A validated model is required to fit before this point.
    (void)icache.configure(config.instruction_cache.capacity_bytes,
                           config.instruction_cache.associativity);
    (void)dcache.configure(config.data_cache.capacity_bytes, config.data_cache.associativity);
    ca_state.reset_instruction();
    ca_pipeline.reset();
    branch_predictor.configure(pipeline_sim.config.branch_predictor);
    branch_predictor.reset();
    const unsigned int target_xlen =
        (config.supported_xlen != 0) ? config.supported_xlen : simrv::xlen::kXLenBits;
    state_.misa = isa::misa_with_mxl(isa::misa_profile_bits(config.misa_profile), target_xlen);
    state_.initialize_lower_xlen_fields();
}

void CPU::reset() {
    state_ = ArchState{};
    prev_state_ = ArchState{};
    pipeline_context = simrv::pipeline::PipelineContext{};
    plic_mmio.reset();
    clint_mmio.reset();
    state_.regs.fill(0);
    state_.regs.fill_fp(0);
    state_.regs.fill_vector(VectorRegister{});
    // Set mstatus.VS to Clean (2 << 9)
    state_.mstatus = static_cast<CSRValue>(2) << 9;
    if constexpr (simrv::xlen::kIsXLen64) {
        state_.mstatus |= (static_cast<CSRValue>(2) << 34) | (static_cast<CSRValue>(2) << 32);
    }
    state_.initialize_lower_xlen_fields();
    TLB_flush();
    e_icount = 0;
    e_ccount = 0;
    cycle_counter_start_delay_ = pipeline_sim.config.cycle_counter_start_delay;
    e_instmix.fill(0);
    trace_history_head_ = 0;
    trace_history_size_ = 0;
    ca_state.reset_instruction();
    ca_pipeline.reset();
    branch_predictor.configure(pipeline_sim.config.branch_predictor);
    branch_predictor.reset();
}

void CPU::TLB_flush() {
    tlb.flush();
    decode_cache.flush();
    icache.flush();
    dcache.flush();
    soft_tlb_flush();
}
void CPU::TLB_flush(const core::TlbFlushFilter& filter) {
    tlb.flush_selective(filter);
    if (!filter.vaddr && !filter.asid) {
        decode_cache.flush();
    } else if (filter.vaddr) {
        decode_cache.flush_page(*filter.vaddr & ~Address{0xFFF});
    } else {
        decode_cache.flush();
    }
    soft_tlb_flush_selective(filter);
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
        for (auto& entry : soft_tlb_inst) {
            entry.invalidate();
        }
    }
}

void CPU::soft_tlb_flush_selective(const core::TlbFlushFilter& filter) {
    if (!filter.vaddr && !filter.asid) {
        soft_tlb_flush();
        return;
    }
    if (filter.vaddr) {
        const Address vpn = *filter.vaddr >> 12;
        const size_t tlb_idx = soft_tlb_index(vpn);
        auto invalidate_if_match = [&](SoftTlbEntry& entry) {
            if (entry.valid(soft_tlb_epoch)) {
                const uint64_t entry_vpn = (entry.tag >> 18) & 0xFFFFFFFFFFULL;
                const uint64_t entry_asid = (entry.tag >> 2) & 0xFFFFu;
                if (entry_vpn == (vpn & 0xFFFFFFFFFFULL) &&
                    (!filter.asid || entry_asid == *filter.asid)) {
                    entry.invalidate();
                }
            }
        };
        invalidate_if_match(soft_tlb_read[tlb_idx]);
        invalidate_if_match(soft_tlb_write[tlb_idx]);
        invalidate_if_match(soft_tlb_inst[tlb_idx]);
        return;
    }
    if (filter.asid) {
        auto invalidate_asid = [&](SoftTlbEntry& entry) {
            if (entry.valid(soft_tlb_epoch)) {
                const uint64_t entry_asid = (entry.tag >> 2) & 0xFFFFu;
                if (entry_asid == *filter.asid) {
                    entry.invalidate();
                }
            }
        };
        for (size_t i = 0; i < 2048; ++i) {
            invalidate_asid(soft_tlb_read[i]);
            invalidate_asid(soft_tlb_write[i]);
            invalidate_asid(soft_tlb_inst[i]);
        }
        return;
    }
}

auto CPU::get_mstatus(CSRValue mask) const -> CSRValue { return csr_file.getMstatus(mask); }

void CPU::set_mstatus(CSRValue wdata) { csr_file.setMstatus(wdata); }

auto CPU::read_csr(CSRAddress addr) const -> std::expected<CSRValue, ExceptionCode> {
    return csr_file.read(addr);
}

auto CPU::read_csr(CsrNumber addr) const -> std::expected<CSRValue, ExceptionCode> {
    return csr_file.read(addr);
}

auto CPU::write_csr(CSRAddress addr, CSRValue wdata) -> std::expected<void, ExceptionCode> {
    return csr_file.write(addr, wdata);
}

auto CPU::write_csr(CsrNumber addr, CSRValue wdata) -> std::expected<void, ExceptionCode> {
    return csr_file.write(addr, wdata);
}

void CPU::mret() { TrapController::mret(state_); }

void CPU::sret() { TrapController::sret(state_); }

void CPU::plic_update_mip() { InterruptController::updateMip(plic_mmio, state_); }

void CPU::plic_set_irq(IrqNumber irq_num, IrqLevel state) {
    InterruptController::setIrq(plic_mmio, irq_num, state == IrqLevel::Asserted ? 1 : 0);
}

void CPU::plic_set_irq(IrqNumber irq_num, bool asserted) {
    InterruptController::setIrq(plic_mmio, irq_num, asserted ? 1 : 0);
}

void CPU::plic_set_irq(IrqNumber irq_num, int state) {
    InterruptController::setIrq(plic_mmio, irq_num, state != 0 ? 1 : 0);
}

void CPU::raise_exception(TrapCause cause, CSRValue tval) {
    TrapController::raiseException(*this, cause, tval);
}

void CPU::evaluate_timer_interrupt() {
    Counter cur_mtime = 0;
    Counter cur_mtimecmp = 0;
    bool is_supervisor_timer = false;

    if (machine_ != nullptr) {
        auto& primary_clint = machine_->primary_hart().clint_mmio;
        cur_mtime = primary_clint.mtime.load(std::memory_order_relaxed);
        const auto hid = static_cast<size_t>(state_.mhartid);
        if (hid == 0) {
            cur_mtimecmp = primary_clint.mtimecmp.load(std::memory_order_relaxed);
            is_supervisor_timer = primary_clint.supervisor_timer.load(std::memory_order_relaxed);
        } else if (hid < ClintMmio::kMaxClintHarts) {
            cur_mtimecmp = primary_clint.hart_mtimecmp.at(hid).load(std::memory_order_relaxed);
            is_supervisor_timer =
                primary_clint.hart_supervisor_timer.at(hid).load(std::memory_order_relaxed);
        }
    } else {
        cur_mtime = clint_mmio.mtime.load(std::memory_order_relaxed);
        cur_mtimecmp = clint_mmio.mtimecmp.load(std::memory_order_relaxed);
        is_supervisor_timer = clint_mmio.supervisor_timer.load(std::memory_order_relaxed);
    }

    const bool pending = (cur_mtime >= cur_mtimecmp);
    if (is_supervisor_timer) {
        state_.mip &= ~enum_mask(MipBit::Mtip);
        state_.stip_timer = pending;
    } else {
        state_.stip_timer = false;
        if (pending)
            state_.mip |= enum_mask(MipBit::Mtip);
        else
            state_.mip &= ~enum_mask(MipBit::Mtip);
    }
    state_.refresh_supervisor_pending();
}

void CPU::run_fast_cycle_miss(Machine& machine) {
    const Counter retired_before = e_icount;
    bool success = fetch_stage(machine, state_.pc) && decode_stage(machine);

    if (success && !pipeline_context.pending_exception.has_value()) {
        CachedOp op;
        op.copy_from(pipeline_context);
        decode_cache.insert(state_.pc, op);
    }

    if (success) {
        if (simrv::compiler::likely(machine.appmode_enabled() && state_.priv == kPrivMachine &&
                                    (state_.mstatus & enum_mask(MstatusBit::Mprv)) == 0)) {
            success = execute_stage(machine);
            if (success) {
                run_memory_stage_baremetal(machine);
                success = !pipeline_context.pending_exception.has_value() &&
                          writeback_stage(machine) && commit_stage(machine);
            }
        } else {
            success = execute_stage(machine) && memory_stage(machine) && writeback_stage(machine) &&
                      commit_stage(machine);
        }
    }

    if (!success) {
        const auto cause =
            pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch);
        raise_exception(static_cast<TrapCause>(cause), pipeline_context.pending_tval);
    }

    tick_cycle_clock(machine);

    if (machine.telemetry_sink_raw()) {
        record_trace_for_tui(machine);
    }

    if (simrv::compiler::unlikely(machine.breakpoint_manager().has_any())) {
        if (auto hit = machine.breakpoint_manager().check_reg_changes(state_, prev_state_)) {
            if (auto* stub = machine.debugger(); stub && stub->is_connected()) {
                machine.debug_halt(static_cast<HartId>(state_.mhartid), GdbSignal::SigTrap);
            } else if (auto sink = machine.telemetry_sink_raw()) {
                sink->set_status_override(hit->description);
                sink->pause_loop();
            }
        }
    }
    machine.record_retired_instructions(e_icount - retired_before);
}

void CPU::run_cycle(Machine& machine) {
    const Counter retired_before = e_icount;
    if (simrv::compiler::unlikely(machine.breakpoint_manager().has_any())) {
        prev_state_ = state_;
        if (auto hit = machine.breakpoint_manager().check_pc(
                state_.pc, HartId{static_cast<uint32_t>(state_.mhartid)}, e_icount)) {
            if (auto* stub = machine.debugger(); stub && stub->is_connected()) {
                machine.breakpoint_manager().set_skip_once_pc(
                    state_.pc, HartId{static_cast<uint32_t>(state_.mhartid)}, e_icount);
                machine.debug_halt(static_cast<HartId>(state_.mhartid), GdbSignal::SigTrap,
                                   "swbreak:;");
                return;
            } else if (auto sink = machine.telemetry_sink_raw()) {
                machine.breakpoint_manager().set_skip_once_pc(
                    state_.pc, HartId{static_cast<uint32_t>(state_.mhartid)}, e_icount);
                sink->set_status_override(hit->description);
                sink->pause_loop();
                return;
            }
        }
    }
    if (machine.runtime_profile.is_cycle_mode()) {
        run_ca_pipeline_cycle(machine);
        const bool record_snapshots =
            pipeline_sim.config.record_snapshots &&
            (!machine.tui_enabled() || captures_tui_execution_detail(machine));
        const bool three_stage =
            pipeline_sim.config.pipeline_type == pipeline::PipelineType::ThreeStage;
        const bool icache_miss = ca_state.instruction_fill.active || ca_pipeline.fetch->icache_miss;
        const bool dcache_miss = ca_pipeline.memory->dcache_miss ||
                                 ca_pipeline.writeback->dcache_miss ||
                                 ca_pipeline.retired->dcache_miss;
        const bool instruction_walk = ca_state.instruction_walk.active;
        const bool data_walk = ca_state.data_walk.active;
        const bool tlb_miss = instruction_walk || data_walk || ca_pipeline.fetch->tlb_miss ||
                              ca_pipeline.memory->tlb_miss || ca_pipeline.writeback->tlb_miss;
        const bool fetch_stalled = ca_pipeline.fetch->remaining_latency != 0 ||
                                   ca_state.instruction_fill.active || instruction_walk;
        const bool decode_stalled = !three_stage && ca_pipeline.data_hazard_stall;
        const bool execute_stalled = (three_stage ? ca_pipeline.decode->remaining_latency
                                                  : ca_pipeline.execute->remaining_latency) != 0 ||
                                     ca_pipeline.data_hazard_stall;
        const bool memory_stalled = !three_stage && (ca_pipeline.memory->remaining_latency != 0 ||
                                                     ca_state.data_transfer.active || data_walk);
        const bool writeback_stalled =
            ca_pipeline.writeback->remaining_latency != 0 ||
            (three_stage && (ca_state.data_transfer.active || data_walk));
        const pipeline::PipelineCycleMetrics metrics{
            .fetch_stalled = fetch_stalled,
            .decode_stalled = decode_stalled,
            .execute_stalled = execute_stalled,
            .memory_stalled = memory_stalled,
            .writeback_stalled = writeback_stalled,
            .retired = ca_pipeline.retired_this_cycle,
            .icache_miss = icache_miss,
            .dcache_miss = dcache_miss,
            .tlb_miss = tlb_miss,
            .data_hazard_stall = ca_pipeline.data_hazard_stall,
            .control_flush = ca_pipeline.control_flush,
        };
        if (simrv::compiler::unlikely(record_snapshots)) {
            auto stage_event = [](const pipeline::CycleInstructionSlot& slot, bool stalled) {
                return pipeline::PipelineStageEvent{
                    .instruction = {.pc = slot.context.cpc.raw(),
                                    .opcode = slot.context.opcode,
                                    .rd = slot.context.rd,
                                    .rs1 = slot.context.rs1,
                                    .rs2 = slot.context.rs2,
                                    .op_id = slot.context.op_id,
                                    .branched = slot.context.tkn,
                                    .is_branch = slot.context.opcode == Opcode::Branch,
                                    .is_jump = slot.context.opcode == Opcode::Jal ||
                                               slot.context.opcode == Opcode::Jalr,
                                    .target_pc = slot.context.jmp_pc},
                    .remaining_latency = slot.remaining_latency,
                    .valid = slot.valid,
                    .stalled = stalled,
                };
            };
            pipeline_sim.advance_cycle({
                .fetch = stage_event(*ca_pipeline.fetch, fetch_stalled),
                .decode = three_stage ? pipeline::PipelineStageEvent{}
                                      : stage_event(*ca_pipeline.decode, decode_stalled),
                .execute = stage_event(three_stage ? *ca_pipeline.decode : *ca_pipeline.execute,
                                       execute_stalled),
                .memory = three_stage ? pipeline::PipelineStageEvent{}
                                      : stage_event(*ca_pipeline.memory, memory_stalled),
                .writeback = stage_event(
                    ca_pipeline.writeback->valid ? *ca_pipeline.writeback : *ca_pipeline.retired,
                    writeback_stalled),
                .retired = metrics.retired,
                .icache_miss = metrics.icache_miss,
                .dcache_miss = metrics.dcache_miss,
                .tlb_miss = metrics.tlb_miss,
                .data_hazard_stall = metrics.data_hazard_stall,
                .control_flush = metrics.control_flush,
            });
        } else {
            pipeline_sim.advance_cycle_fast(metrics);
        }
        const auto retired_pc = state_.pc;
        if (simrv::compiler::unlikely(ca_pipeline.retired_this_cycle &&
                                      captures_tui_execution_detail(machine))) {
            std::swap(pipeline_context, ca_pipeline.retired->context);
            record_trace_for_tui(machine);
            std::swap(pipeline_context, ca_pipeline.retired->context);
        }
        tick_cycle_clock(machine, ca_pipeline.retired_this_cycle);
        if (ca_pipeline.retired_this_cycle && state_.pc != retired_pc) {
            machine.memory().system_bus().cancel_source(simrv::memory::make_tl_source(
                static_cast<HartId>(state_.mhartid), simrv::memory::TlPort::Instruction));
            ca_pipeline.reset();
            ca_pipeline.initialized = true;
            ca_pipeline.fetch_pc = state_.pc;
            ca_state.instruction_fill.reset();
            ca_state.data_transfer.reset();
            ca_state.instruction_walk.reset();
            ca_state.data_walk.reset();
        }
        machine.record_retired_instructions(e_icount - retired_before);
        return;
    }
    if ((machine.runtime_profile.is_instruction_fast() ||
         machine.sampled_instruction_execution()) &&
        !machine.breakpoint_manager().has_any()) {
        // Mode 2: Cached fast-path functional execution (pre-decoded direct-lookup cache).
        // Bypasses pipeline orchestration and fetches from direct lookup cache if available.
        auto* cached = decode_cache.lookup(state_.pc);
        if (simrv::compiler::likely(cached != nullptr)) {
            const bool copy_ctx = captures_tui_execution_detail(machine) ||
                                  machine.lockstep_enabled() || machine.debugger_enabled() ||
                                  machine.branch_trace_enabled() ||
                                  (machine.configuration().execution.strace != 0);
            const bool inst_mix =
                machine.instruction_mix_enabled() || captures_tui_execution_detail(machine);
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
            run_fast_cycle_miss(machine);
            return;
        }
    } else {
        // Observable instruction mode uses the same semantic stages without coroutine state.
        bool success = fetch_stage(machine, state_.pc) && decode_stage(machine);
        if (success) {
            if (simrv::compiler::likely(machine.appmode_enabled() && state_.priv == kPrivMachine &&
                                        (state_.mstatus & enum_mask(MstatusBit::Mprv)) == 0)) {
                success = execute_stage(machine);
                if (success) {
                    run_memory_stage_baremetal(machine);
                    success = !pipeline_context.pending_exception.has_value() &&
                              writeback_stage(machine) && commit_stage(machine);
                }
            } else {
                success = execute_stage(machine) && memory_stage(machine) &&
                          writeback_stage(machine) && commit_stage(machine);
            }
        }
        if (!success) {
            const auto cause =
                pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch);
            raise_exception(static_cast<TrapCause>(cause), pipeline_context.pending_tval);
        }
    }

    tick_cycle_clock(machine);

    // Record trace logs for active debug TUI components.
    if (machine.telemetry_sink_raw()) {
        record_trace_for_tui(machine);
    }

    if (simrv::compiler::unlikely(machine.breakpoint_manager().has_any())) {
        if (auto hit = machine.breakpoint_manager().check_reg_changes(state_, prev_state_)) {
            if (auto* stub = machine.debugger(); stub && stub->is_connected()) {
                machine.debug_halt(static_cast<HartId>(state_.mhartid), GdbSignal::SigTrap);
            } else if (auto sink = machine.telemetry_sink_raw()) {
                sink->set_status_override(hit->description);
                sink->pause_loop();
            }
        }
    }
    machine.record_retired_instructions(e_icount - retired_before);
}

void CPU::advance_ca_cycle(Machine& machine) {
    // CA is a separate, one-transition-per-call engine.  The assertion-like guard makes it
    // impossible for an IA scheduling path to accidentally use CA as an instruction executor.
    if (!machine.runtime_profile.is_cycle_mode()) {
        return;
    }
    run_cycle(machine);
}

SIMRV_ALWAYS_INLINE void CPU::tick_cycle_clock(Machine& machine, bool interrupt_boundary) {
    if (cycle_counter_start_delay_ != 0) {
        --cycle_counter_start_delay_;
    } else {
        ++clint_mmio.mcycle;
    }
    if (machine.runtime_profile.is_cycle_mode()) {
        if (interrupt_boundary) handle_cached_interrupts();
        return;
    }
    if (state_.mhartid == 0) {
        ++clint_mmio.rtc_divider;
        if (clint_mmio.rtc_divider == 10) {
            ++clint_mmio.mtime;
            clint_mmio.rtc_divider = 0;
            evaluate_timer_interrupt();
            // Parallel harts evaluate their own interrupt state; hart 0 only publishes time.
            if (simrv::compiler::unlikely(!machine.config.execution.smp_multithreaded &&
                                          machine.num_harts() > 1)) {
                for (size_t i = 1; i < machine.num_harts(); ++i) {
                    auto& sec = machine.hart(i);
                    if (sec.hart_status.load(std::memory_order_relaxed) == HartStatus::Started) {
                        sec.evaluate_timer_interrupt();
                    }
                }
            }
        }
    } else {
        clint_mmio.mtime = machine.primary_hart().clint_mmio.mtime.load(std::memory_order_relaxed);
    }
    if (interrupt_boundary) handle_cached_interrupts();
}

void CPU::record_trace_for_tui(Machine& machine) {
    auto sink = machine.telemetry_sink_raw();
    if (!sink) return;

    const auto op_id = pipeline_context.op_id;
    const auto opcode = pipeline_context.opcode;
    const auto rd = pipeline_context.rd;
    const auto rs1 = pipeline_context.rs1;
    const auto rs2 = pipeline_context.rs2;

    if (!sink->is_trace_active()) {
        sink->record_flight_instruction(pipeline_context.cpc.raw(), opcode, op_id,
                                        static_cast<uint8_t>(state_.mhartid));
        return;
    }

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

    sink->record_instruction(pipeline_context.cpc.raw(), opcode, op_id, std::to_underlying(rd),
                             rd_val, std::to_underlying(rs1), rs1_val, std::to_underlying(rs2),
                             rs2_val, pipeline_context.imm, static_cast<uint8_t>(state_.mhartid));
}

void CPU::run_cycle_baremetal_miss(Machine& machine) {
    const Counter retired_before = e_icount;
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
    clint_mmio.rtc_divider++;
    if (clint_mmio.rtc_divider == 10) {
        clint_mmio.mtime++;
        clint_mmio.rtc_divider = 0;
        evaluate_timer_interrupt();
    }
    if (machine.telemetry_sink_raw()) {
        record_trace_for_tui(machine);
    }

    if (simrv::compiler::unlikely(machine.breakpoint_manager().has_any())) {
        if (auto hit = machine.breakpoint_manager().check_reg_changes(state_, prev_state_)) {
            if (auto* stub = machine.debugger(); stub && stub->is_connected()) {
                machine.debug_halt(static_cast<HartId>(state_.mhartid), GdbSignal::SigTrap);
            } else if (auto sink = machine.telemetry_sink_raw()) {
                sink->set_status_override(hit->description);
                sink->pause_loop();
            }
        }
    }
    machine.record_retired_instructions(e_icount - retired_before);
}

void CPU::run_cycle_baremetal(Machine& machine) {
    const Counter retired_before = e_icount;
    if (simrv::compiler::unlikely(machine.breakpoint_manager().has_any())) {
        prev_state_ = state_;
        if (auto hit = machine.breakpoint_manager().check_pc(
                state_.pc, HartId{static_cast<uint32_t>(state_.mhartid)}, e_icount)) {
            if (auto* stub = machine.debugger(); stub && stub->is_connected()) {
                machine.breakpoint_manager().set_skip_once_pc(
                    state_.pc, HartId{static_cast<uint32_t>(state_.mhartid)}, e_icount);
                machine.debug_halt(static_cast<HartId>(state_.mhartid), GdbSignal::SigTrap,
                                   "swbreak:;");
                return;
            }
            if (auto sink = machine.telemetry_sink_raw()) {
                machine.breakpoint_manager().set_skip_once_pc(
                    state_.pc, HartId{static_cast<uint32_t>(state_.mhartid)}, e_icount);
                sink->set_status_override(hit->description);
                sink->pause_loop();
                return;
            }
        }
    }
    pipeline_context.pending_exception = std::nullopt;
    pipeline_context.pending_tval = 0;

    if (machine.runtime_profile.is_instruction_fast() && !machine.breakpoint_manager().has_any()) {
        auto* cached = decode_cache.lookup(state_.pc);
        if (simrv::compiler::likely(cached != nullptr)) {
            const bool copy_ctx = captures_tui_execution_detail(machine) ||
                                  machine.lockstep_enabled() || machine.debugger_enabled() ||
                                  machine.branch_trace_enabled() ||
                                  (machine.configuration().execution.strace != 0);
            const bool inst_mix =
                machine.instruction_mix_enabled() || captures_tui_execution_detail(machine);
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
            clint_mmio.rtc_divider++;
            if (clint_mmio.rtc_divider == 10) {
                clint_mmio.mtime++;
                clint_mmio.rtc_divider = 0;
                evaluate_timer_interrupt();
            }
            if (machine.telemetry_sink_raw()) {
                record_trace_for_tui(machine);
            }
            if (simrv::compiler::unlikely(machine.breakpoint_manager().has_any())) {
                if (auto hit =
                        machine.breakpoint_manager().check_reg_changes(state_, prev_state_)) {
                    if (auto* stub = machine.debugger(); stub && stub->is_connected()) {
                        machine.debug_halt(static_cast<HartId>(state_.mhartid), GdbSignal::SigTrap);
                    } else if (auto sink = machine.telemetry_sink_raw()) {
                        sink->set_status_override(hit->description);
                        sink->pause_loop();
                    }
                }
            }
            machine.record_retired_instructions(e_icount - retired_before);
            return;
        } else {
            run_cycle_baremetal_miss(machine);
            return;
        }
    }

    run_cycle_baremetal_miss(machine);
}

void CPU::run_memory_stage_baremetal(Machine& machine) {
    if (simrv::compiler::unlikely(machine.breakpoint_manager().has_any())) {
        run_memory_stage(machine);
        return;
    }
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
    const size_t access_size = size_t{1} << (static_cast<unsigned>(ctx.funct3) & 0x3u);

    // Load Phase
    if (opcode == Opcode::Load || (opcode == Opcode::Amo && funct5 == Funct5Amo::Lr)) {
        if (simrv::compiler::likely(machine.memory_geometry().contains(addr, access_size))) {
            ctx.mem_rdata = simrv::memory::ram_read_fast(addr, static_cast<Instruction>(ctx.funct3),
                                                         machine.ram_view());
        } else {
            ctx.mem_rdata =
                simrv::memory::MemoryAccess::loadInt(machine.memory_, *this, addr, ctx.funct3);
        }
    } else if (opcode == Opcode::Amo && funct5 != Funct5Amo::Sc) {
        // Atomic DRAM AMO Execution (non-LR, non-SC)
        if (simrv::compiler::likely(machine.memory_geometry().contains(addr, access_size))) {
            if (ctx.funct3 == Funct3::Sw || ctx.funct3 == Funct3::Lw) {
                auto* ptr = reinterpret_cast<uint32_t*>(machine.ram_view().unchecked_ptr(addr));
                const auto prev = execute_dram_amo<uint32_t>(ptr, ctx.rrs2, funct5);
                ctx.mem_rdata = static_cast<Word>(static_cast<int32_t>(prev));
                ctx.wb_data = ctx.mem_rdata;
            } else if constexpr (simrv::xlen::kIsXLen64) {
                if (ctx.funct3 == Funct3::Sd || ctx.funct3 == Funct3::Ld) {
                    auto* ptr = reinterpret_cast<uint64_t*>(machine.ram_view().unchecked_ptr(addr));
                    const auto prev = execute_dram_amo<uint64_t>(ptr, ctx.rrs2, funct5);
                    ctx.mem_rdata = prev;
                    ctx.wb_data = ctx.mem_rdata;
                }
            }
        } else {
            ctx.mem_rdata =
                simrv::memory::MemoryAccess::loadInt(machine.memory_, *this, addr, ctx.funct3);
            ctx.wb_data = ctx.mem_rdata;
        }
    }

    if (opcode == Opcode::LoadFp) {
        if (simrv::compiler::likely(machine.memory_geometry().contains(addr, access_size))) {
            const auto f3 = ctx.funct3;
            switch (f3) {
                case Funct3::Flw: {
                    const Word lo = simrv::memory::ram_read_fast(
                        addr, static_cast<Instruction>(Funct3::Lw), machine.ram_view());
                    ctx.fp_mem_rdata = static_cast<uint64_t>(kF32BoxerBits) |
                                       static_cast<uint64_t>(lo & kLower32Mask);
                    break;
                }
                case Funct3::Fld: {
                    if constexpr (simrv::xlen::kIsXLen64) {
                        ctx.fp_mem_rdata =
                            static_cast<FloatingRegister>(simrv::memory::ram_read_fast(
                                addr, static_cast<Instruction>(Funct3::Ld), machine.ram_view()));
                    } else {
                        const Word lo = simrv::memory::ram_read_fast(
                            addr, static_cast<Instruction>(Funct3::Lw), machine.ram_view());
                        const Word hi = simrv::memory::ram_read_fast(
                            addr + 4, static_cast<Instruction>(Funct3::Lw), machine.ram_view());
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
        machine.memory_.reservation_table().set_reservation(static_cast<HartId>(state_.mhartid),
                                                            addr);
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
         (funct5 == Funct5Amo::Sc && (ctx.wb_data == 0u) && (state_.reserved != 0u)))) {
        if (simrv::compiler::likely(machine.memory_geometry().contains(addr, access_size))) {
            // Check tohost writes (fast filter for 0x80000000 or 0x40000000 regions)
            if (simrv::compiler::unlikely((addr & 0xC0000000ULL) != 0)) {
                if (simrv::compiler::unlikely(addr == machine.isa_test_tohost() ||
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
                                                     (addr == machine.isa_test_tohost() + 4 ||
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
            simrv::memory::ram_write_fast(addr, ctx.mem_wdata, static_cast<Instruction>(ctx.funct3),
                                          machine.ram_view());
        } else {
            simrv::memory::MemoryAccess::storeInt(machine.memory_, *this, addr, ctx.mem_wdata,
                                                  ctx.funct3);
        }
    } else if (opcode == Opcode::Amo && funct5 != Funct5Amo::Lr && funct5 != Funct5Amo::Sc &&
               !machine.memory_geometry().contains(addr, access_size)) {
        simrv::memory::MemoryAccess::storeInt(machine.memory_, *this, addr, ctx.mem_wdata,
                                              ctx.funct3);
    }

    if (opcode == Opcode::StoreFp) {
        if (simrv::compiler::likely(machine.memory_geometry().contains(addr, access_size))) {
            const auto f3 = ctx.funct3;
            switch (f3) {
                case Funct3::Fsw:
                    simrv::memory::ram_write_fast(
                        addr,
                        static_cast<Word>(ctx.fp_mem_wdata &
                                          static_cast<FloatingRegister>(kLower32Mask)),
                        static_cast<Instruction>(Funct3::Sw), machine.ram_view());
                    break;
                case Funct3::Fsd:
                    if constexpr (simrv::xlen::kIsXLen64) {
                        simrv::memory::ram_write_fast(addr, static_cast<Word>(ctx.fp_mem_wdata),
                                                      static_cast<Instruction>(Funct3::Sd),
                                                      machine.ram_view());
                    } else {
                        simrv::memory::ram_write_fast(
                            addr,
                            static_cast<Word>(ctx.fp_mem_wdata &
                                              static_cast<FloatingRegister>(kLower32Mask)),
                            static_cast<Instruction>(Funct3::Sw), machine.ram_view());
                        simrv::memory::ram_write_fast(
                            addr + 4,
                            static_cast<Word>((ctx.fp_mem_wdata >> 32) &
                                              static_cast<FloatingRegister>(kLower32Mask)),
                            static_cast<Instruction>(Funct3::Sw), machine.ram_view());
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
            machine.memory_.reservation_table().invalidate_matching(
                addr, static_cast<HartId>(state_.mhartid));
        }
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

namespace {
[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto is_aligned_for_funct3(Address addr,
                                                                       isa::Funct3 funct3) noexcept
    -> bool {
    const unsigned size_bytes = 1u << (static_cast<unsigned>(funct3) & 0x3u);
    const unsigned alignment_mask = size_bytes - 1u;
    return (addr & alignment_mask) == 0;
}

[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto access_size_for_funct3(isa::Funct3 funct3) noexcept
    -> unsigned {
    return 1u << (static_cast<unsigned>(funct3) & 0x3u);
}
}  // namespace

SIMRV_ALWAYS_INLINE void CPU::execute_cached_jal(CachedOp& op) {
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
    commit_cached_branch_target(op, pipeline_context.jmp_pc);
}

SIMRV_ALWAYS_INLINE void CPU::execute_cached_jalr(CachedOp& op, Register rrs1) {
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
    commit_cached_branch_target(op, pipeline_context.jmp_pc);
}

SIMRV_ALWAYS_INLINE void CPU::execute_cached_branch(CachedOp& op, Register rrs1, Register rrs2) {
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
    commit_cached_branch_target(op, target_pc);
}

auto CPU::try_fast_load(Machine& machine, Address mem_addr, Funct3 funct3, Register& out_val)
    -> bool {
    if (simrv::compiler::unlikely(!is_aligned_for_funct3(mem_addr, funct3))) {
        return false;
    }

    if (simrv::compiler::likely(state_.priv == kPrivMachine &&
                                (state_.mstatus & enum_mask(MstatusBit::Mprv)) == 0)) {
        const unsigned size_bytes = access_size_for_funct3(funct3);
        if (simrv::compiler::likely(machine.memory_geometry().contains(mem_addr, size_bytes))) {
            out_val = simrv::memory::ram_read_fast(mem_addr, static_cast<Instruction>(funct3),
                                                   machine.ram_view());
            return true;
        }
        return false;
    }

    const unsigned active_xlen = effective_data_xlen();
    if (active_xlen == 32) mem_addr &= 0xFFFFFFFFULL;
    const unsigned size_bytes = access_size_for_funct3(funct3);
    const PrivilegeLevel eff_priv = effective_data_privilege();

    if (simrv::compiler::likely(eff_priv != kPrivMachine &&
                                simrv::xlen::satp_translation_enabled(state_.satp, active_xlen))) {
        const Word current_asid = simrv::xlen::satp_asid(state_.satp, active_xlen);
        const Address vpn = mem_addr >> 12;
        const size_t tlb_idx = soft_tlb_index(vpn);
        const auto& entry = soft_tlb_read[tlb_idx];
        if (simrv::compiler::likely(entry.matches(vpn, current_asid, eff_priv, soft_tlb_epoch))) {
            if (simrv::compiler::likely(entry.host_ptr_base != nullptr)) {
                out_val = simrv::memory::host_read_fast(entry.host_ptr_base + (mem_addr & 0xFFF),
                                                        static_cast<Instruction>(funct3));
                return true;
            }
            Address const paddr = entry.paddr_base + (mem_addr & 0xFFF);
            if (simrv::compiler::likely(machine.memory_geometry().contains(paddr, size_bytes))) {
                out_val = simrv::memory::ram_read_fast(paddr, static_cast<Instruction>(funct3),
                                                       machine.ram_view());
                return true;
            }
            return false;
        }
        TLBEntry* tlb_e = tlb.lookup_data_r(mem_addr, current_asid, eff_priv);
        if (simrv::compiler::likely(tlb_e != nullptr)) {
            Address const paddr = tlb_e->p_addr + (mem_addr & 0xFFF);
            if (simrv::compiler::unlikely(!core::pmp::check_access(
                    state_, paddr, size_bytes, core::PmpAccessType::Read, eff_priv))) {
                return false;
            }
            Address const ppage = tlb_e->p_addr;
            Byte* host_base = machine.ram_view().contains(ppage, 4096)
                                  ? machine.ram_view().unchecked_ptr(ppage)
                                  : nullptr;
            if (host_base != nullptr) {
                soft_tlb_read[tlb_idx].set(vpn, current_asid, eff_priv, soft_tlb_epoch, ppage,
                                           host_base);
                out_val = simrv::memory::host_read_fast(host_base + (mem_addr & 0xFFF),
                                                        static_cast<Instruction>(funct3));
                return true;
            }
            if (simrv::compiler::likely(machine.memory_geometry().contains(paddr, size_bytes))) {
                out_val = simrv::memory::ram_read_fast(paddr, static_cast<Instruction>(funct3),
                                                       machine.ram_view());
                return true;
            }
        }
    } else {
        if (simrv::compiler::likely(machine.memory_geometry().contains(mem_addr, size_bytes))) {
            out_val = simrv::memory::ram_read_fast(mem_addr, static_cast<Instruction>(funct3),
                                                   machine.ram_view());
            return true;
        }
    }
    return false;
}

auto CPU::try_fast_store(Machine& machine, Address mem_addr, Funct3 funct3, Register rrs2) -> bool {
    if (simrv::compiler::unlikely(!is_aligned_for_funct3(mem_addr, funct3))) {
        return false;
    }

    if (simrv::compiler::likely(state_.priv == kPrivMachine &&
                                (state_.mstatus & enum_mask(MstatusBit::Mprv)) == 0)) {
        const unsigned size_bytes = access_size_for_funct3(funct3);
        if (simrv::compiler::likely(machine.memory_geometry().contains(mem_addr, size_bytes) &&
                                    !is_tohost_addr(machine, mem_addr))) {
            simrv::memory::ram_write_fast(mem_addr, rrs2, static_cast<Instruction>(funct3),
                                          machine.ram_view());
            return true;
        }
        return false;
    }

    const unsigned active_xlen = effective_data_xlen();
    if (active_xlen == 32) mem_addr &= 0xFFFFFFFFULL;
    const unsigned size_bytes = access_size_for_funct3(funct3);
    const PrivilegeLevel eff_priv = effective_data_privilege();

    if (simrv::compiler::likely(eff_priv != kPrivMachine &&
                                simrv::xlen::satp_translation_enabled(state_.satp, active_xlen))) {
        const Word current_asid = simrv::xlen::satp_asid(state_.satp, active_xlen);
        const Address vpn = mem_addr >> 12;
        const size_t tlb_idx = soft_tlb_index(vpn);
        const auto& entry = soft_tlb_write[tlb_idx];
        if (simrv::compiler::likely(entry.matches(vpn, current_asid, eff_priv, soft_tlb_epoch))) {
            Address const paddr = entry.paddr_base + (mem_addr & 0xFFF);
            if (simrv::compiler::unlikely(is_tohost_addr(machine, paddr))) {
                return false;
            }
            if (simrv::compiler::likely(entry.host_ptr_base != nullptr)) {
                simrv::memory::host_write_fast(entry.host_ptr_base + (mem_addr & 0xFFF), rrs2,
                                               static_cast<Instruction>(funct3));
                return true;
            }
            if (simrv::compiler::likely(machine.memory_geometry().contains(paddr, size_bytes))) {
                simrv::memory::ram_write_fast(paddr, rrs2, static_cast<Instruction>(funct3),
                                              machine.ram_view());
                return true;
            }
            return false;
        }
        TLBEntry* tlb_e = tlb.lookup_data_w(mem_addr, current_asid, eff_priv);
        if (simrv::compiler::likely(tlb_e != nullptr)) {
            Address const paddr = tlb_e->p_addr + (mem_addr & 0xFFF);
            if (simrv::compiler::unlikely(is_tohost_addr(machine, paddr))) {
                return false;
            }
            if (simrv::compiler::unlikely(!core::pmp::check_access(
                    state_, paddr, size_bytes, core::PmpAccessType::Write, eff_priv))) {
                return false;
            }
            Address const ppage = tlb_e->p_addr;
            Byte* host_base = machine.ram_view().contains(ppage, 4096)
                                  ? machine.ram_view().unchecked_ptr(ppage)
                                  : nullptr;
            if (host_base != nullptr) {
                soft_tlb_write[tlb_idx].set(vpn, current_asid, eff_priv, soft_tlb_epoch, ppage,
                                            host_base);
                simrv::memory::host_write_fast(host_base + (mem_addr & 0xFFF), rrs2,
                                               static_cast<Instruction>(funct3));
                return true;
            }
            if (simrv::compiler::likely(machine.memory_geometry().contains(paddr, size_bytes))) {
                simrv::memory::ram_write_fast(paddr, rrs2, static_cast<Instruction>(funct3),
                                              machine.ram_view());
                return true;
            }
        }
    } else {
        if (simrv::compiler::likely(machine.memory_geometry().contains(mem_addr, size_bytes) &&
                                    !is_tohost_addr(machine, mem_addr))) {
            simrv::memory::ram_write_fast(mem_addr, rrs2, static_cast<Instruction>(funct3),
                                          machine.ram_view());
            return true;
        }
    }
    return false;
}

SIMRV_ALWAYS_INLINE auto CPU::execute_cached_load(Machine& machine, CachedOp& op, Register rrs1)
    -> bool {
    Address const mem_addr = rrs1 + op.imm;
    if (simrv::compiler::unlikely(machine.tui_enabled() || machine.branch_trace_enabled())) {
        pipeline_context.mem_addr = mem_addr;
    }
    Register mem_rdata = 0;
    const unsigned size_bytes = access_size_for_funct3(op.funct3);
    if (simrv::compiler::likely(state_.priv == kPrivMachine &&
                                (state_.mstatus & enum_mask(MstatusBit::Mprv)) == 0 &&
                                is_aligned_for_funct3(mem_addr, op.funct3) &&
                                machine.memory_geometry().contains(mem_addr, size_bytes))) {
        mem_rdata = simrv::memory::ram_read_fast(mem_addr, static_cast<Instruction>(op.funct3),
                                                 machine.ram_view());
    } else if (!try_fast_load(machine, mem_addr, op.funct3, mem_rdata)) {
        op.copy_to(pipeline_context);
        pipeline_context.mem_addr = mem_addr;
        mem_rdata =
            simrv::memory::MemoryAccess::loadInt(machine.memory_, *this, mem_addr, op.funct3);
        if (pipeline_context.pending_exception.has_value()) {
            raise_exception(static_cast<TrapCause>(*pipeline_context.pending_exception),
                            pipeline_context.pending_tval);
            return false;
        }
    }

    state_.regs.write_branchless(op.rd, mem_rdata);
    advance_cached_pc(op);
    return true;
}

SIMRV_ALWAYS_INLINE auto CPU::execute_cached_store(Machine& machine, CachedOp& op, Register rrs1,
                                                   Register rrs2) -> bool {
    Address const mem_addr = rrs1 + op.imm;
    if (simrv::compiler::unlikely(machine.tui_enabled() || machine.branch_trace_enabled())) {
        pipeline_context.mem_addr = mem_addr;
    }
    const unsigned size_bytes = access_size_for_funct3(op.funct3);
    if (simrv::compiler::likely(state_.priv == kPrivMachine &&
                                (state_.mstatus & enum_mask(MstatusBit::Mprv)) == 0 &&
                                is_aligned_for_funct3(mem_addr, op.funct3) &&
                                machine.memory_geometry().contains(mem_addr, size_bytes) &&
                                !is_tohost_addr(machine, mem_addr))) {
        simrv::memory::ram_write_fast(mem_addr, rrs2, static_cast<Instruction>(op.funct3),
                                      machine.ram_view());
    } else if (!try_fast_store(machine, mem_addr, op.funct3, rrs2)) {
        op.copy_to(pipeline_context);
        pipeline_context.mem_addr = mem_addr;
        simrv::memory::MemoryAccess::storeInt(machine.memory_, *this, mem_addr, rrs2, op.funct3);
        if (pipeline_context.pending_exception.has_value()) {
            raise_exception(static_cast<TrapCause>(*pipeline_context.pending_exception),
                            pipeline_context.pending_tval);
            return false;
        }
    }

    state_.reserved = 0;
    if (simrv::compiler::unlikely(machine.num_harts() > 1 &&
                                  machine.memory_.reservation_table().may_have_reservations())) {
        machine.memory_.reservation_table().invalidate_matching(
            mem_addr, static_cast<HartId>(state_.mhartid));
    }
    advance_cached_pc(op);
    return true;
}

auto CPU::execute_cached_fallback(Machine& machine) -> void {
    fetch_operands(machine);
    bool success = false;
    if (simrv::compiler::likely(state_.priv == kPrivMachine &&
                                (state_.mstatus & enum_mask(MstatusBit::Mprv)) == 0)) {
        success = execute_stage(machine);
        if (success) {
            run_memory_stage_baremetal(machine);
            success = !pipeline_context.pending_exception.has_value() && writeback_stage(machine) &&
                      commit_stage(machine);
        }
    } else {
        success = execute_stage(machine) && memory_stage(machine) && writeback_stage(machine) &&
                  commit_stage(machine);
    }
    if (!success) {
        const auto cause =
            pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch);
        raise_exception(static_cast<TrapCause>(cause), pipeline_context.pending_tval);
    }
}

void CPU::dispatch_pending_interrupts() {
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
        op.copy_to(pipeline_context);
        pipeline_context.tlb_miss = false;
        pipeline_context.pending_tval = 0;
    } else {
        pipeline_context.pending_exception = std::nullopt;
        pipeline_context.opcode = op.opcode;
        pipeline_context.funct5 = op.funct5;
    }

    if constexpr (kInstMix) {
        e_instmix[static_cast<std::size_t>(op.op_id)]++;
    }

    // 3. Execute, Memory, Writeback, Commit — single flat op_id dispatch (no double switch).
    // ALU arms compute wb_data and break to the shared writeback tail below the switch.
    // Non-ALU arms (branches, jumps, loads, stores, fallback) handle their own commit and return.
    Register wb_data = 0;
    const Register rrs1 = state_.regs.read(op.rs1);
    switch (op.op_id) {
        // ---- Scalar ALU with two register operands ----
        case isa::ADD: {
            const Register rrs2 = state_.regs.read(op.rs2);
            wb_data = rrs1 + rrs2;
            break;
        }
        case isa::SUB: {
            const Register rrs2 = state_.regs.read(op.rs2);
            wb_data = rrs1 - rrs2;
            break;
        }
        case isa::AND: {
            const Register rrs2 = state_.regs.read(op.rs2);
            wb_data = rrs1 & rrs2;
            break;
        }
        case isa::OR: {
            const Register rrs2 = state_.regs.read(op.rs2);
            wb_data = rrs1 | rrs2;
            break;
        }
        case isa::XOR: {
            const Register rrs2 = state_.regs.read(op.rs2);
            wb_data = rrs1 ^ rrs2;
            break;
        }
        case isa::CFU: {
            const Register rrs2 = state_.regs.read(op.rs2);
            wb_data = static_cast<Register>(
                cfu_unit.execute(op.funct7, std::to_underlying(op.funct3),
                                 static_cast<uint32_t>(rrs1), static_cast<uint32_t>(rrs2)));
            break;
        }
        case isa::SLL: {
            const Register rrs2 = state_.regs.read(op.rs2);
            wb_data = rrs1 << (rrs2 & simrv::xlen::xlen_shift_mask());
            break;
        }
        case isa::SRL: {
            const Register rrs2 = state_.regs.read(op.rs2);
            wb_data = rrs1 >> (rrs2 & simrv::xlen::xlen_shift_mask());
            break;
        }
        case isa::SRA: {
            const Register rrs2 = state_.regs.read(op.rs2);
            wb_data = static_cast<Register>(static_cast<SignedWord>(rrs1) >>
                                            (rrs2 & simrv::xlen::xlen_shift_mask()));
            break;
        }
        case isa::SLT: {
            const Register rrs2 = state_.regs.read(op.rs2);
            wb_data = static_cast<Register>(static_cast<SignedWord>(rrs1) <
                                            static_cast<SignedWord>(rrs2));
            break;
        }
        case isa::SLTU: {
            const Register rrs2 = state_.regs.read(op.rs2);
            wb_data = static_cast<Register>(rrs1 < rrs2);
            break;
        }
        // ---- 32-bit (W) register ops (RV64 only) ----
        case isa::ADDW: {
            const Register rrs2 = state_.regs.read(op.rs2);
            wb_data =
                static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(rrs1 + rrs2)));
            break;
        }
        case isa::SUBW: {
            const Register rrs2 = state_.regs.read(op.rs2);
            wb_data =
                static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(rrs1 - rrs2)));
            break;
        }
        case isa::SLLW: {
            const Register rrs2 = state_.regs.read(op.rs2);
            wb_data = static_cast<Register>(static_cast<int64_t>(
                static_cast<int32_t>(static_cast<uint32_t>(rrs1) << (rrs2 & 0x1f))));
            break;
        }
        case isa::SRLW: {
            const Register rrs2 = state_.regs.read(op.rs2);
            wb_data = static_cast<Register>(static_cast<int64_t>(
                static_cast<int32_t>(static_cast<uint32_t>(rrs1) >> (rrs2 & 0x1f))));
            break;
        }
        case isa::SRAW: {
            const Register rrs2 = state_.regs.read(op.rs2);
            wb_data = static_cast<Register>(
                static_cast<int64_t>(static_cast<int32_t>(rrs1) >> (rrs2 & 0x1f)));
            break;
        }
        // ---- Scalar ALU with immediate ----
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
        // ---- 32-bit immediate ops (RV64 only) ----
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
            execute_cached_branch(op, rrs1, state_.regs.read(op.rs2));
            return;
        // ---- Jump instructions ----
        case isa::JAL:
            execute_cached_jal(op);
            return;
        case isa::JALR:
            execute_cached_jalr(op, rrs1);
            return;
        // ---- Loads ----
        case isa::LB:
        case isa::LH:
        case isa::LW:
        case isa::LD:
        case isa::LBU:
        case isa::LHU:
        case isa::LWU:
            execute_cached_load(machine, op, rrs1);
            return;
        // ---- Stores ----
        case isa::SB:
        case isa::SH:
        case isa::SW:
        case isa::SD:
            execute_cached_store(machine, op, rrs1, state_.regs.read(op.rs2));
            return;
        // ---- FP Loads ----
        case isa::FLW:
        case isa::FLD: {
            Address const mem_addr = rrs1 + op.imm;
            FloatingRegister mem_rdata = 0;
            const unsigned size_bytes = (op.funct3 == isa::Funct3::Fld) ? 8u : 4u;
            if (simrv::compiler::likely(state_.priv == kPrivMachine &&
                                        (state_.mstatus & enum_mask(MstatusBit::Mprv)) == 0 &&
                                        is_aligned_for_funct3(mem_addr, op.funct3) &&
                                        machine.memory_geometry().contains(mem_addr, size_bytes))) {
                mem_rdata = simrv::memory::ram_read_fast(
                    mem_addr, static_cast<Instruction>(op.funct3), machine.ram_view());
            } else {
                Word raw_val = 0;
                if (try_fast_load(machine, mem_addr, op.funct3, raw_val)) {
                    mem_rdata = raw_val;
                } else {
                    op.copy_to(pipeline_context);
                    pipeline_context.mem_addr = mem_addr;
                    mem_rdata = simrv::memory::MemoryAccess::loadFp(machine.memory_, *this,
                                                                    mem_addr, op.funct3);
                    if (pipeline_context.pending_exception.has_value()) {
                        raise_exception(static_cast<TrapCause>(*pipeline_context.pending_exception),
                                        pipeline_context.pending_tval);
                        return;
                    }
                }
            }
            state_.regs.write_fp(op.rd,
                                 (op.funct3 == isa::Funct3::Flw)
                                     ? (static_cast<FloatingRegister>(simrv::xlen::kF32BoxerBits) |
                                        (mem_rdata & 0xFFFFFFFFULL))
                                     : mem_rdata);
            advance_cached_pc(op);
            return;
        }
        // ---- FP Stores ----
        case isa::FSW:
        case isa::FSD: {
            Address const mem_addr = rrs1 + op.imm;
            FloatingRegister const fp_data = state_.regs.read_fp(op.rs2);
            const unsigned size_bytes = (op.funct3 == isa::Funct3::Fsd) ? 8u : 4u;
            if (simrv::compiler::likely(state_.priv == kPrivMachine &&
                                        (state_.mstatus & enum_mask(MstatusBit::Mprv)) == 0 &&
                                        is_aligned_for_funct3(mem_addr, op.funct3) &&
                                        machine.memory_geometry().contains(mem_addr, size_bytes))) {
                simrv::memory::ram_write_fast(
                    mem_addr, fp_data, static_cast<Instruction>(op.funct3), machine.ram_view());
            } else if (!try_fast_store(machine, mem_addr, op.funct3, fp_data)) {
                op.copy_to(pipeline_context);
                pipeline_context.mem_addr = mem_addr;
                simrv::memory::MemoryAccess::storeFp(machine.memory_, *this, mem_addr, fp_data,
                                                     op.funct3);
                if (pipeline_context.pending_exception.has_value()) {
                    raise_exception(static_cast<TrapCause>(*pipeline_context.pending_exception),
                                    pipeline_context.pending_tval);
                    return;
                }
            }
            advance_cached_pc(op);
            return;
        }
        // ---- FP Fused Operations ----
        case isa::FMADD_D:
        case isa::FMADD_S:
        case isa::FMSUB_D:
        case isa::FMSUB_S:
        case isa::FNMSUB_D:
        case isa::FNMSUB_S:
        case isa::FNMADD_D:
        case isa::FNMADD_S: {
            const Word rs3 = (op.ir >> 27) & 0x1F;
            const Word fmt = (op.ir >> 25) & 0x3;
            auto res = execute::ExecuteUnit::fusedFp(
                op.opcode, fmt, std::to_underlying(op.rs1), std::to_underlying(op.rs2), rs3,
                enum_mask(op.funct3), state_.regs.fp_data_ptr(), state_.fcsr);
            if (res.fp_wb_enable) {
                state_.regs.write_fp(op.rd, res.fp_wb_data);
            }
            advance_cached_pc(op);
            return;
        }
        // ---- FP Scalar Arithmetic ----
        case isa::FADD_D:
        case isa::FADD_S:
        case isa::FSUB_D:
        case isa::FSUB_S:
        case isa::FMUL_D:
        case isa::FMUL_S:
        case isa::FDIV_D:
        case isa::FDIV_S: {
            auto res = execute::ExecuteUnit::opFp(op.funct7, op.funct3, std::to_underlying(op.rs1),
                                                  std::to_underlying(op.rs2), rrs1,
                                                  state_.regs.fp_data_ptr(), state_.fcsr);
            if (res.fp_wb_enable) {
                state_.regs.write_fp(op.rd, res.fp_wb_data);
            }
            advance_cached_pc(op);
            return;
        }
        // WFI deliberately reaches the full path: fetch is the interrupt-delivery boundary, and
        // bypassing it in a cached idle loop can leave an OS waiting past a pending timer IRQ.
        // ---- Everything else: fallback to full pipeline stages ----
        default:
            if constexpr (!kCopyContext) {
                op.copy_to(pipeline_context);
                pipeline_context.tlb_miss = false;
                pipeline_context.pending_tval = 0;
            }
            execute_cached_fallback(machine);
            return;
    }

    // Shared writeback tail — only ALU arms (those that break) reach here.
    // Branchless GPR write: write rd unconditionally, then re-zero x0.
    state_.regs.write_branchless(op.rd, wb_data);
    advance_cached_pc(op);
}

template <bool kCopyContext, bool kInstMix, bool kPollPause>
SIMRV_ALWAYS_INLINE auto CPU::run_fast_baremetal_kernel(Machine& machine, uint32_t batch_size)
    -> uint32_t {
    uint32_t cached_ops = 0;
    Counter accumulated_retired = 0;
    for (uint32_t b = 0; b < batch_size; ++b) {
        auto* cached = decode_cache.lookup(state_.pc);
        if (simrv::compiler::likely(cached != nullptr)) {
            const Counter retired_before = e_icount;
            execute_cached_op_fast<kCopyContext, kInstMix>(machine, *cached);
            accumulated_retired += (e_icount - retired_before);
            ++cached_ops;
        } else {
            if (accumulated_retired > 0) {
                machine.record_retired_instructions(accumulated_retired);
                accumulated_retired = 0;
            }
            run_cycle_baremetal_miss(machine);
        }
        if (simrv::compiler::unlikely(
                machine.tohost != 0 || !machine.is_running() ||
                (kPollPause &&
                 ((b & 0xFFu) == 0 && (machine.is_paused() || machine.debug_pause_requested()))))) {
            break;
        }
    }
    if (accumulated_retired > 0) {
        machine.record_retired_instructions(accumulated_retired);
    }
    return cached_ops;
}

void CPU::run_fast_baremetal_batch(Machine& machine, uint32_t batch_size,
                                   const FastBatchPolicy& policy) {
    uint32_t cached_ops = 0;
    if (simrv::compiler::likely(!policy.copy_pipeline_context && !policy.collect_instruction_mix)) {
        cached_ops = policy.poll_pause
                         ? run_fast_baremetal_kernel<false, false, true>(machine, batch_size)
                         : run_fast_baremetal_kernel<false, false, false>(machine, batch_size);
    } else if (policy.copy_pipeline_context && policy.collect_instruction_mix) {
        cached_ops = policy.poll_pause
                         ? run_fast_baremetal_kernel<true, true, true>(machine, batch_size)
                         : run_fast_baremetal_kernel<true, true, false>(machine, batch_size);
    } else if (policy.copy_pipeline_context) {
        cached_ops = policy.poll_pause
                         ? run_fast_baremetal_kernel<true, false, true>(machine, batch_size)
                         : run_fast_baremetal_kernel<true, false, false>(machine, batch_size);
    } else {
        cached_ops = policy.poll_pause
                         ? run_fast_baremetal_kernel<false, true, true>(machine, batch_size)
                         : run_fast_baremetal_kernel<false, true, false>(machine, batch_size);
    }

    if (cached_ops > 0) {
        clint_mmio.mcycle += cached_ops;
        clint_mmio.rtc_divider += static_cast<int>(cached_ops);
        if (clint_mmio.rtc_divider >= 10) {
            clint_mmio.mtime += clint_mmio.rtc_divider / 10;
            clint_mmio.rtc_divider %= 10;
            evaluate_timer_interrupt();
        }
    }
}

template <bool kCopyContext, bool kInstMix, bool kPollPause>
SIMRV_ALWAYS_INLINE auto CPU::run_fast_os_kernel(Machine& machine, uint32_t batch_size)
    -> uint32_t {
    uint32_t cached_ops = 0;
    Counter accumulated_retired = 0;
    uint32_t chunk_cycles = 0;

    auto flush_clint_and_retired = [&]() {
        if (chunk_cycles > 0) {
            clint_mmio.mcycle += chunk_cycles;
            clint_mmio.rtc_divider += static_cast<int>(chunk_cycles);
            if (clint_mmio.rtc_divider >= 10) {
                clint_mmio.mtime += clint_mmio.rtc_divider / 10;
                clint_mmio.rtc_divider %= 10;
                evaluate_timer_interrupt();
                handle_cached_interrupts();
            }
            chunk_cycles = 0;
        }
        if (accumulated_retired > 0) {
            machine.record_retired_instructions(accumulated_retired);
            accumulated_retired = 0;
        }
    };

    for (uint32_t b = 0; b < batch_size; ++b) {
        auto* cached = decode_cache.lookup(state_.pc);
        if (simrv::compiler::likely(cached != nullptr)) {
            const Counter retired_before = e_icount;
            execute_cached_op_fast<kCopyContext, kInstMix>(machine, *cached);
            accumulated_retired += (e_icount - retired_before);
            ++cached_ops;
            ++chunk_cycles;

            if (simrv::compiler::unlikely(chunk_cycles >= 64)) {
                flush_clint_and_retired();
            }
        } else {
            flush_clint_and_retired();
            run_fast_cycle_miss(machine);
        }
        if (simrv::compiler::unlikely(
                machine.tohost != 0 || !machine.is_running() ||
                (kPollPause &&
                 ((b & 0xFFu) == 0 && (machine.is_paused() || machine.debug_pause_requested()))))) {
            break;
        }
    }
    flush_clint_and_retired();
    return cached_ops;
}

void CPU::run_fast_os_batch(Machine& machine, uint32_t batch_size, const FastBatchPolicy& policy) {
    if (simrv::compiler::likely(!policy.copy_pipeline_context && !policy.collect_instruction_mix)) {
        if (policy.poll_pause) {
            run_fast_os_kernel<false, false, true>(machine, batch_size);
        } else {
            run_fast_os_kernel<false, false, false>(machine, batch_size);
        }
    } else if (policy.copy_pipeline_context && policy.collect_instruction_mix) {
        if (policy.poll_pause) {
            run_fast_os_kernel<true, true, true>(machine, batch_size);
        } else {
            run_fast_os_kernel<true, true, false>(machine, batch_size);
        }
    } else if (policy.copy_pipeline_context) {
        if (policy.poll_pause) {
            run_fast_os_kernel<true, false, true>(machine, batch_size);
        } else {
            run_fast_os_kernel<true, false, false>(machine, batch_size);
        }
    } else {
        if (policy.poll_pause) {
            run_fast_os_kernel<false, true, true>(machine, batch_size);
        } else {
            run_fast_os_kernel<false, true, false>(machine, batch_size);
        }
    }
}

template void CPU::execute_cached_op_fast<false, false>(Machine& machine, CachedOp& op);
template void CPU::execute_cached_op_fast<true, false>(Machine& machine, CachedOp& op);
template void CPU::execute_cached_op_fast<false, true>(Machine& machine, CachedOp& op);
template void CPU::execute_cached_op_fast<true, true>(Machine& machine, CachedOp& op);
template auto CPU::run_fast_baremetal_kernel<false, false, false>(Machine&, uint32_t) -> uint32_t;
template auto CPU::run_fast_baremetal_kernel<false, false, true>(Machine&, uint32_t) -> uint32_t;
template auto CPU::run_fast_baremetal_kernel<true, false, false>(Machine&, uint32_t) -> uint32_t;
template auto CPU::run_fast_baremetal_kernel<true, false, true>(Machine&, uint32_t) -> uint32_t;
template auto CPU::run_fast_baremetal_kernel<false, true, false>(Machine&, uint32_t) -> uint32_t;
template auto CPU::run_fast_baremetal_kernel<false, true, true>(Machine&, uint32_t) -> uint32_t;
template auto CPU::run_fast_baremetal_kernel<true, true, false>(Machine&, uint32_t) -> uint32_t;
template auto CPU::run_fast_baremetal_kernel<true, true, true>(Machine&, uint32_t) -> uint32_t;
template auto CPU::run_fast_os_kernel<false, false, false>(Machine&, uint32_t) -> uint32_t;
template auto CPU::run_fast_os_kernel<false, false, true>(Machine&, uint32_t) -> uint32_t;
template auto CPU::run_fast_os_kernel<true, false, false>(Machine&, uint32_t) -> uint32_t;
template auto CPU::run_fast_os_kernel<true, false, true>(Machine&, uint32_t) -> uint32_t;
template auto CPU::run_fast_os_kernel<false, true, false>(Machine&, uint32_t) -> uint32_t;
template auto CPU::run_fast_os_kernel<false, true, true>(Machine&, uint32_t) -> uint32_t;
template auto CPU::run_fast_os_kernel<true, true, false>(Machine&, uint32_t) -> uint32_t;
template auto CPU::run_fast_os_kernel<true, true, true>(Machine&, uint32_t) -> uint32_t;

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
