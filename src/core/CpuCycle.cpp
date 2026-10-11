/**
 * @file CpuCycle.cpp
 * @brief CPU cycle control and uncached pipeline execution.
 */
#include <array>
#include <atomic>
#include <string_view>

#include "CpuExecutionHelpers.hpp"
#include "simrv/CompilerHints.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/PlatformTiming.hpp"
#include "simrv/core/Pmp.hpp"
#include "simrv/core/TelemetrySink.hpp"
#include "simrv/core/Tracer.hpp"
#include "simrv/debug/BreakpointManager.hpp"
#include "simrv/debug/GdbStub.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/isa/Amo.hpp"
#include "simrv/isa/Common.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/pipeline/OperationTraits.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/AddressTypes.hpp"
#include "simrv/xlen/IdentifierTypes.hpp"

namespace simrv::core {
using isa::OperationId;
namespace {
SIMRV_ALWAYS_INLINE auto captures_tui_execution_detail(const Machine& machine) -> bool {
    const auto sink = machine.telemetry_sink_raw();
    return sink && sink->captures_execution_detail();
}

template <typename T, typename Op>
auto atomic_update(std::atomic_ref<T>& atomic_mem, const Op& op) -> T {
    T old_val = atomic_mem.load(std::memory_order_relaxed);
    while (!atomic_mem.compare_exchange_weak(old_val, op(old_val), std::memory_order_acq_rel,
                                             std::memory_order_relaxed)) {
    }
    return old_val;
}

template <typename T>
auto execute_dram_amo(T* mem_ptr, simrv::xlen::Word rs2_val, Funct5Amo funct5) -> T {
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

void CPU::run_fast_cycle_miss(Machine& machine) {
    const simrv::xlen::Counter retired_before = e_icount;
    bool success = fetch_stage(machine, state_.pc) && decode_stage(machine);

    if (success && !pipeline_context.pending_exception.has_value()) {
        CachedOp op;
        op.copy_from(pipeline_context);
        decode_cache.insert(state_.pc, op);
    }

    if (success) {
        if (simrv::compiler::likely(machine.appmode_enabled() && state_.priv == kPrivMachine &&
                                    (state_.mstatus & MstatusBit::Mprv) == 0)) {
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
        raise_exception(static_cast<simrv::xlen::TrapCause>(cause), pipeline_context.pending_tval);
    }

    tick_cycle_clock(machine);

    if (machine.telemetry_sink_raw()) {
        record_trace_for_tui(machine);
    }

    if (simrv::compiler::unlikely(machine.breakpoint_manager().has_any())) {
        if (auto hit = machine.breakpoint_manager().check_reg_changes(state_, prev_state_)) {
            if (auto* stub = machine.debugger(); stub && stub->is_connected()) {
                machine.debug_halt(static_cast<simrv::xlen::HartId>(state_.mhartid),
                                   GdbSignal::SigTrap);
            } else if (auto sink = machine.telemetry_sink_raw()) {
                sink->set_status_override(hit->description);
                sink->pause_loop();
            }
        }
    }
    if (e_icount != retired_before && machine.trace().needs_retiring_context()) {
        machine.trace().log_architecture_retirement(*this, pipeline_context);
    }
    machine.record_retired_instructions(e_icount - retired_before);
}

void CPU::run_cycle(Machine& machine) {
    apply_hart_control_boundary();
    apply_pending_interrupt_lines(machine);
    CPU* previous_active_cpu = active_execution_cpu_;
    active_execution_cpu_ = this;
    struct BoundaryCommit {
        CPU& cpu;
        CPU* previous;
        ~BoundaryCommit() {
            cpu.apply_hart_control_boundary();
            active_execution_cpu_ = previous;
        }
    } boundary_commit{*this, previous_active_cpu};
    const simrv::xlen::Counter retired_before = e_icount;
    if (simrv::compiler::unlikely(machine.breakpoint_manager().has_any())) {
        prev_state_ = state_;
        if (auto hit = machine.breakpoint_manager().check_pc(
                state_.pc, simrv::xlen::HartId{static_cast<uint32_t>(state_.mhartid)}, e_icount)) {
            if (auto* stub = machine.debugger(); stub && stub->is_connected()) {
                machine.breakpoint_manager().set_skip_once_pc(
                    state_.pc, simrv::xlen::HartId{static_cast<uint32_t>(state_.mhartid)},
                    e_icount);
                machine.debug_halt(static_cast<simrv::xlen::HartId>(state_.mhartid),
                                   GdbSignal::SigTrap, "swbreak:;");
                return;
            } else if (auto sink = machine.telemetry_sink_raw()) {
                machine.breakpoint_manager().set_skip_once_pc(
                    state_.pc, simrv::xlen::HartId{static_cast<uint32_t>(state_.mhartid)},
                    e_icount);
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
        const bool instruction_walk = ca_state.instruction_walk.active;
        const bool data_walk = ca_state.data_walk.active;
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
        if (machine.trace().is_architecture_trace_enabled()) {
            std::array<simrv::core::PipelineStallRecord, 5> stall_storage{};
            size_t stall_count = 0;
            const auto add_stall = [&stall_storage, &stall_count](
                                       std::string_view stage,
                                       const pipeline::CycleInstructionSlot& slot, bool stalled,
                                       std::string_view reason) {
                if (!stalled) return;
                stall_storage[stall_count++] = {.stage = stage,
                                                .pc = slot.context.cpc.raw(),
                                                .remaining_cycles = slot.remaining_latency,
                                                .reason = reason};
            };
            add_stall("fetch", *ca_pipeline.fetch, fetch_stalled,
                      ca_pipeline.fetch->remaining_latency != 0
                          ? "latency"
                          : (ca_state.instruction_fill.active ? "instruction_fill" : "page_walk"));
            if (!three_stage) {
                add_stall("decode", *ca_pipeline.decode, decode_stalled, "data_hazard");
            }
            add_stall("execute", three_stage ? *ca_pipeline.decode : *ca_pipeline.execute,
                      execute_stalled,
                      ca_pipeline.data_hazard_stall
                          ? "data_hazard"
                          : ((three_stage ? ca_pipeline.decode->remaining_latency
                                          : ca_pipeline.execute->remaining_latency) != 0
                                 ? "latency"
                                 : "downstream_backpressure"));
            if (!three_stage) {
                add_stall("memory", *ca_pipeline.memory, memory_stalled,
                          data_walk
                              ? "page_walk"
                              : (ca_state.data_transfer.active ? "data_transfer" : "latency"));
            }
            add_stall("writeback",
                      ca_pipeline.writeback->valid ? *ca_pipeline.writeback : *ca_pipeline.retired,
                      writeback_stalled,
                      data_walk ? "page_walk"
                                : (ca_state.data_transfer.active ? "data_transfer" : "latency"));
            machine.trace().log_pipeline_stalls(
                *this, std::span<const simrv::core::PipelineStallRecord>(stall_storage.data(),
                                                                         stall_count));
        }
        if (simrv::compiler::unlikely(record_snapshots)) {
            const bool icache_miss =
                ca_state.instruction_fill.active || ca_pipeline.fetch->icache_miss;
            const bool dcache_miss = ca_pipeline.memory->dcache_miss ||
                                     ca_pipeline.writeback->dcache_miss ||
                                     ca_pipeline.retired->dcache_miss;
            const bool tlb_miss = instruction_walk || data_walk || ca_pipeline.fetch->tlb_miss ||
                                  ca_pipeline.memory->tlb_miss || ca_pipeline.writeback->tlb_miss;
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
                                    .target_pc = slot.context.jmp_pc.raw()},
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
                .retired = ca_pipeline.retired_this_cycle,
                .icache_miss = icache_miss,
                .dcache_miss = dcache_miss,
                .tlb_miss = tlb_miss,
                .data_hazard_stall = ca_pipeline.data_hazard_stall,
                .control_flush = ca_pipeline.control_flush,
            });
        } else {
            pipeline_sim.record_cycle_stats(
                fetch_stalled, decode_stalled, execute_stalled, memory_stalled, writeback_stalled,
                ca_pipeline.data_hazard_stall, ca_pipeline.control_flush,
                ca_state.instruction_fill.active || ca_pipeline.fetch->icache_miss,
                ca_pipeline.memory->dcache_miss || ca_pipeline.writeback->dcache_miss ||
                    ca_pipeline.retired->dcache_miss,
                instruction_walk || data_walk || ca_pipeline.fetch->tlb_miss ||
                    ca_pipeline.memory->tlb_miss || ca_pipeline.writeback->tlb_miss);
        }
        const auto retired_pc = state_.pc;
        const bool capture_tui = captures_tui_execution_detail(machine);
        const bool retain_retiring_context =
            capture_tui || machine.trace().needs_retiring_context();
        if (simrv::compiler::unlikely(ca_pipeline.retired_this_cycle && retain_retiring_context)) {
            std::swap(pipeline_context, ca_pipeline.retired->context);
            if (capture_tui) record_trace_for_tui(machine);
        }
        tick_cycle_clock(machine, ca_pipeline.retired_this_cycle);
        if (ca_pipeline.retired_this_cycle) {
            if (e_icount != retired_before && machine.trace().needs_retiring_context()) {
                machine.trace().log_architecture_retirement(*this, pipeline_context);
            }
            if (retain_retiring_context) {
                std::swap(pipeline_context, ca_pipeline.retired->context);
            }
        }
        if (ca_pipeline.retired_this_cycle && state_.pc != retired_pc) {
            machine.memory().system_bus().cancel_source(
                simrv::memory::make_tl_source(static_cast<simrv::xlen::HartId>(state_.mhartid),
                                              simrv::memory::TlPort::Instruction));
            ca_pipeline.reset();
            ca_pipeline.initialized = true;
            ca_pipeline.fetch_pc = state_.pc;
            ca_state.instruction_fill.reset();
            ca_state.data_transfer.reset();
            ca_state.instruction_walk.reset();
            ca_state.data_walk.reset();
        }
        if (ca_pipeline.retired_this_cycle) {
            machine.record_retired_instructions(e_icount - retired_before);
        }
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
                                  machine.trace().is_architecture_trace_enabled() ||
                                  machine.lockstep_enabled() || machine.debugger_enabled() ||
                                  machine.branch_trace_enabled() ||
                                  (machine.configuration().execution.strace != 0);
            const bool inst_mix =
                machine.instruction_mix_enabled() || captures_tui_execution_detail(machine);
            if (simrv::compiler::unlikely(copy_ctx && inst_mix)) {
                execute_cached_op_fast<true, true>(machine, *cached, machine.ram_view());
            } else if (simrv::compiler::unlikely(copy_ctx)) {
                execute_cached_op_fast<true, false>(machine, *cached, machine.ram_view());
            } else if (simrv::compiler::unlikely(inst_mix)) {
                execute_cached_op_fast<false, true>(machine, *cached, machine.ram_view());
            } else {
                execute_cached_op_fast<false, false>(machine, *cached, machine.ram_view());
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
                                        (state_.mstatus & MstatusBit::Mprv) == 0)) {
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
            raise_exception(static_cast<simrv::xlen::TrapCause>(cause),
                            pipeline_context.pending_tval);
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
                machine.debug_halt(static_cast<simrv::xlen::HartId>(state_.mhartid),
                                   GdbSignal::SigTrap);
            } else if (auto sink = machine.telemetry_sink_raw()) {
                sink->set_status_override(hit->description);
                sink->pause_loop();
            }
        }
    }
    if (e_icount != retired_before && machine.trace().needs_retiring_context()) {
        machine.trace().log_architecture_retirement(*this, pipeline_context);
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
        if (clint_mmio.rtc_divider == timing::kCyclesPerTimebaseTick) {
            ++clint_mmio.mtime;
            clint_mmio.rtc_divider = 0;
            evaluate_timer_interrupt();
            // Parallel harts evaluate their own interrupt state; hart 0 only publishes time.
            if (simrv::compiler::unlikely(!machine.is_smp_enabled() && machine.num_harts() > 1)) {
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

    simrv::xlen::Register rd_val = 0;
    simrv::xlen::Register rs1_val = 0;
    simrv::xlen::Register rs2_val = 0;

    const bool rd_fp = isa::is_destination_fp(opcode, op_id);
    const bool rs1_fp = isa::is_rs1_fp(opcode, op_id);
    const bool rs2_fp = isa::is_rs2_fp(opcode, op_id);

    if (rd_fp) {
        rd_val = state_.regs.read_fp(simrv::xlen::to_fp_reg_id(rd));
    } else {
        rd_val = state_.regs.read(rd);
    }

    if (rs1_fp) {
        rs1_val = state_.regs.read_fp(simrv::xlen::to_fp_reg_id(rs1));
    } else {
        rs1_val = state_.regs.read(rs1);
    }

    if (rs2_fp) {
        rs2_val = state_.regs.read_fp(simrv::xlen::to_fp_reg_id(rs2));
    } else {
        rs2_val = state_.regs.read(rs2);
    }

    sink->record_instruction(pipeline_context.cpc.raw(), opcode, op_id, std::to_underlying(rd),
                             rd_val, std::to_underlying(rs1), rs1_val, std::to_underlying(rs2),
                             rs2_val, pipeline_context.imm, static_cast<uint8_t>(state_.mhartid));
}

void CPU::run_cycle_baremetal_miss(Machine& machine) {
    const simrv::xlen::Counter retired_before = e_icount;
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
            raise_exception(static_cast<simrv::xlen::TrapCause>(cause),
                            pipeline_context.pending_tval);
        }
    } else {
        const auto cause =
            pipeline_context.pending_exception.value_or(ExceptionCode::MisalignedFetch);
        raise_exception(static_cast<simrv::xlen::TrapCause>(cause), pipeline_context.pending_tval);
    }

    clint_mmio.mcycle++;
    clint_mmio.rtc_divider++;
    if (clint_mmio.rtc_divider == timing::kCyclesPerTimebaseTick) {
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
                machine.debug_halt(static_cast<simrv::xlen::HartId>(state_.mhartid),
                                   GdbSignal::SigTrap);
            } else if (auto sink = machine.telemetry_sink_raw()) {
                sink->set_status_override(hit->description);
                sink->pause_loop();
            }
        }
    }
    if (e_icount != retired_before && machine.trace().needs_retiring_context()) {
        machine.trace().log_architecture_retirement(*this, pipeline_context);
    }
    machine.record_retired_instructions(e_icount - retired_before);
}

void CPU::run_cycle_baremetal(Machine& machine) {
    apply_hart_control_boundary();
    apply_pending_interrupt_lines(machine);
    CPU* previous_active_cpu = active_execution_cpu_;
    active_execution_cpu_ = this;
    struct BoundaryCommit {
        CPU& cpu;
        CPU* previous;
        ~BoundaryCommit() {
            cpu.apply_hart_control_boundary();
            active_execution_cpu_ = previous;
        }
    } boundary_commit{*this, previous_active_cpu};
    const simrv::xlen::Counter retired_before = e_icount;
    if (simrv::compiler::unlikely(machine.breakpoint_manager().has_any())) {
        prev_state_ = state_;
        if (auto hit = machine.breakpoint_manager().check_pc(
                state_.pc, simrv::xlen::HartId{static_cast<uint32_t>(state_.mhartid)}, e_icount)) {
            if (auto* stub = machine.debugger(); stub && stub->is_connected()) {
                machine.breakpoint_manager().set_skip_once_pc(
                    state_.pc, simrv::xlen::HartId{static_cast<uint32_t>(state_.mhartid)},
                    e_icount);
                machine.debug_halt(static_cast<simrv::xlen::HartId>(state_.mhartid),
                                   GdbSignal::SigTrap, "swbreak:;");
                return;
            }
            if (auto sink = machine.telemetry_sink_raw()) {
                machine.breakpoint_manager().set_skip_once_pc(
                    state_.pc, simrv::xlen::HartId{static_cast<uint32_t>(state_.mhartid)},
                    e_icount);
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
                                  machine.trace().is_architecture_trace_enabled() ||
                                  machine.lockstep_enabled() || machine.debugger_enabled() ||
                                  machine.branch_trace_enabled() ||
                                  (machine.configuration().execution.strace != 0);
            const bool inst_mix =
                machine.instruction_mix_enabled() || captures_tui_execution_detail(machine);
            if (simrv::compiler::unlikely(copy_ctx && inst_mix)) {
                execute_cached_op_fast<true, true>(machine, *cached, machine.ram_view());
            } else if (simrv::compiler::unlikely(copy_ctx)) {
                execute_cached_op_fast<true, false>(machine, *cached, machine.ram_view());
            } else if (simrv::compiler::unlikely(inst_mix)) {
                execute_cached_op_fast<false, true>(machine, *cached, machine.ram_view());
            } else {
                execute_cached_op_fast<false, false>(machine, *cached, machine.ram_view());
            }
            clint_mmio.mcycle++;
            clint_mmio.rtc_divider++;
            if (clint_mmio.rtc_divider == timing::kCyclesPerTimebaseTick) {
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
                        machine.debug_halt(static_cast<simrv::xlen::HartId>(state_.mhartid),
                                           GdbSignal::SigTrap);
                    } else if (auto sink = machine.telemetry_sink_raw()) {
                        sink->set_status_override(hit->description);
                        sink->pause_loop();
                    }
                }
            }
            if (e_icount != retired_before && machine.trace().needs_retiring_context()) {
                machine.trace().log_architecture_retirement(*this, pipeline_context);
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
    // The specialized direct-RAM stage does not implement PMP checks. Preserve the full
    // memory-stage path whenever locked PMP entries make machine-mode accesses restrictive.
    if (simrv::compiler::unlikely(state_.has_locked_pmp)) {
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
    ctx.mem_paddr = addr;

    if (opcode == Opcode::Amo && (funct5 == Funct5Amo::Lr || funct5 == Funct5Amo::Sc)) {
        run_memory_stage(machine);
        return;
    }

    // Load Phase
    if (opcode == Opcode::Load || (opcode == Opcode::Amo && funct5 == Funct5Amo::Lr)) {
        if (simrv::compiler::likely(machine.memory_geometry().contains(addr, access_size))) {
            ctx.mem_rdata = simrv::memory::ram_read_fast(
                addr, static_cast<simrv::xlen::Instruction>(ctx.funct3), machine.ram_view());
        } else {
            ctx.mem_rdata =
                simrv::memory::MemoryAccess::loadInt(machine.memory_, *this, addr, ctx.funct3);
        }
    } else if (opcode == Opcode::Amo && funct5 != Funct5Amo::Sc) {
        if (simrv::compiler::unlikely(!isa::amo_address_aligned(addr, ctx.funct3))) {
            ctx.pending_exception = ExceptionCode::MisalignedStore;
            ctx.pending_tval = addr;
            return;
        }
        // Atomic DRAM AMO Execution (non-LR, non-SC)
        if (simrv::compiler::likely(machine.memory_geometry().contains(addr, access_size))) {
            if (ctx.funct3 == Funct3::Sw || ctx.funct3 == Funct3::Lw) {
                auto* ptr = reinterpret_cast<uint32_t*>(machine.ram_view().unchecked_ptr(addr));
                const auto prev = execute_dram_amo<uint32_t>(ptr, ctx.rrs2, funct5);
                ctx.mem_rdata = static_cast<simrv::xlen::Word>(static_cast<int32_t>(prev));
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
                    const simrv::xlen::Word lo = simrv::memory::ram_read_fast(
                        addr, static_cast<simrv::xlen::Instruction>(Funct3::Lw),
                        machine.ram_view());
                    ctx.fp_mem_rdata = static_cast<uint64_t>(simrv::xlen::kF32BoxerBits) |
                                       static_cast<uint64_t>(lo & simrv::xlen::kLower32Mask);
                    break;
                }
                case Funct3::Fld: {
                    if constexpr (simrv::xlen::kIsXLen64) {
                        ctx.fp_mem_rdata =
                            static_cast<simrv::xlen::FloatingRegister>(simrv::memory::ram_read_fast(
                                addr, static_cast<simrv::xlen::Instruction>(Funct3::Ld),
                                machine.ram_view()));
                    } else {
                        const simrv::xlen::Word lo = simrv::memory::ram_read_fast(
                            addr, static_cast<simrv::xlen::Instruction>(Funct3::Lw),
                            machine.ram_view());
                        const simrv::xlen::Word hi = simrv::memory::ram_read_fast(
                            addr + 4, static_cast<simrv::xlen::Instruction>(Funct3::Lw),
                            machine.ram_view());
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
        machine.memory_.reservation_table().set_reservation(
            static_cast<simrv::xlen::HartId>(state_.mhartid), addr);
    }

    // Prepare Store Data
    ctx.mem_wdata = (opcode != Opcode::Amo || funct5 == Funct5Amo::Sc)
                        ? ctx.rrs2
                        : execute::ExecuteUnit::aluAmo(ctx.rrs2, ctx.mem_rdata, funct5, ctx.funct3);

    if (opcode == Opcode::StoreFp) {
        ctx.mem_wdata = static_cast<simrv::xlen::Register>(
            ctx.fp_mem_wdata &
            static_cast<simrv::xlen::FloatingRegister>(simrv::xlen::kLower32Mask));
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
            simrv::memory::ram_write_fast(addr, ctx.mem_wdata,
                                          static_cast<simrv::xlen::Instruction>(ctx.funct3),
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
        if (simrv::compiler::likely(machine.memory_geometry().contains(addr, access_size) &&
                                    !is_tohost_addr(machine, addr))) {
            const auto f3 = ctx.funct3;
            switch (f3) {
                case Funct3::Fsw:
                    simrv::memory::ram_write_fast(
                        addr,
                        static_cast<simrv::xlen::Word>(
                            ctx.fp_mem_wdata &
                            static_cast<simrv::xlen::FloatingRegister>(simrv::xlen::kLower32Mask)),
                        static_cast<simrv::xlen::Instruction>(Funct3::Sw), machine.ram_view());
                    break;
                case Funct3::Fsd:
                    if constexpr (simrv::xlen::kIsXLen64) {
                        simrv::memory::ram_write_fast(
                            addr, static_cast<simrv::xlen::Word>(ctx.fp_mem_wdata),
                            static_cast<simrv::xlen::Instruction>(Funct3::Sd), machine.ram_view());
                    } else {
                        simrv::memory::ram_write_fast(
                            addr,
                            static_cast<simrv::xlen::Word>(
                                ctx.fp_mem_wdata & static_cast<simrv::xlen::FloatingRegister>(
                                                       simrv::xlen::kLower32Mask)),
                            static_cast<simrv::xlen::Instruction>(Funct3::Sw), machine.ram_view());
                        simrv::memory::ram_write_fast(
                            addr + 4,
                            static_cast<simrv::xlen::Word>(
                                (ctx.fp_mem_wdata >> 32) &
                                static_cast<simrv::xlen::FloatingRegister>(
                                    simrv::xlen::kLower32Mask)),
                            static_cast<simrv::xlen::Instruction>(Funct3::Sw), machine.ram_view());
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
            machine.memory_.reservation_table().invalidate_range(
                ctx.mem_paddr, access_size, static_cast<simrv::xlen::HartId>(state_.mhartid));
        }
    }
}

auto CPU::fetch_stage(Machine& machine, simrv::xlen::Address pc) -> bool {
    state_.pc = pc;
    auto& context = active_context();
    context.pending_exception = std::nullopt;
    context.pending_tval = 0;
    context.mem_paddr = simrv::xlen::PhysAddr{0};
    context.sc_reservation_checked = false;
    context.sc_reservation_succeeded = false;
    // Instruction mode reuses one context across whole instructions. A previous
    // memory operation may have marked its load phase complete; clear that latch
    // before decoding the next instruction so cache misses still perform loads.
    if (!machine.runtime_profile.is_cycle_mode()) {
        context.memory_load_complete = false;
    }

    run_fetch_stage(machine);

    return !pipeline_context.pending_exception.has_value();
}

auto CPU::decode_stage(Machine& machine) -> bool {
    run_decode_stage(machine);
    return !pipeline_context.pending_exception.has_value();
}

auto CPU::execute_stage(Machine& machine) -> bool {
    if (machine.trace().is_register_write_trace_enabled() && pipeline_context.traits.writes_vec) {
        if (pipeline_context.trace_sequence == 0)
            pipeline_context.trace_sequence = ++next_register_trace_sequence_;
        machine.trace().capture_register_write_before(*this, pipeline_context);
    }
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

}  // namespace simrv::core
