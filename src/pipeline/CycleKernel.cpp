#include <cstdlib>
#include <print>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/Tracer.hpp"
#include "simrv/execute/ExecuteUnit.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/pipeline/OperationTraits.hpp"
#include "simrv/tui/Tui.hpp"

namespace simrv::core {

using pipeline::CycleInstructionSlot;

namespace {
[[nodiscard]] auto is_store_conditional(const pipeline::PipelineContext& context) noexcept -> bool {
    return context.opcode == isa::Opcode::Amo && context.funct5 == isa::Funct5Amo::Sc;
}

[[nodiscard]] auto compute_div_latency(const pipeline::PipelineContext& ctx,
                                       const pipeline::CpuConfig& config, unsigned xlen) noexcept
    -> simrv::xlen::LatencyCycles {
    if (!config.divider_early_out) {
        return config.div_latency;
    }
    const auto u1 = ctx.rrs1;
    const auto u2 = ctx.rrs2;
    if (u2 == 0) return 1;

    const bool is_w = (ctx.opcode == isa::Opcode::Op32 || ctx.opcode == isa::Opcode::OpImm32);
    const bool is_signed = (ctx.op_id == isa::DIV || ctx.op_id == isa::REM ||
                            ctx.op_id == isa::DIVW || ctx.op_id == isa::REMW);

    uint64_t mag1 = 0;
    uint64_t mag2 = 0;

    if (is_w || xlen == 32) {
        const auto raw1 = static_cast<uint32_t>(u1);
        const auto raw2 = static_cast<uint32_t>(u2);
        if (is_signed) {
            const auto s1 = static_cast<int32_t>(raw1);
            const auto s2 = static_cast<int32_t>(raw2);
            if (s1 == std::numeric_limits<int32_t>::min() && s2 == -1) return 1;
            mag1 = s1 < 0 ? static_cast<uint64_t>(-static_cast<int64_t>(s1))
                          : static_cast<uint64_t>(s1);
            mag2 = s2 < 0 ? static_cast<uint64_t>(-static_cast<int64_t>(s2))
                          : static_cast<uint64_t>(s2);
        } else {
            mag1 = raw1;
            mag2 = raw2;
        }
        if (mag1 < mag2) return 2;
        const auto clz1 = std::countl_zero(static_cast<uint32_t>(mag1));
        const auto clz2 = std::countl_zero(static_cast<uint32_t>(mag2));
        const auto diff = clz2 > clz1 ? (clz2 - clz1) : 0;
        return std::clamp<simrv::xlen::LatencyCycles>(
            static_cast<simrv::xlen::LatencyCycles>(2 + diff), 2, config.div_latency);
    } else {
        if (is_signed) {
            const auto s1 = static_cast<int64_t>(u1);
            const auto s2 = static_cast<int64_t>(u2);
            if (s1 == std::numeric_limits<int64_t>::min() && s2 == -1) return 1;
            mag1 = s1 < 0 ? static_cast<uint64_t>(-s1) : static_cast<uint64_t>(s1);
            mag2 = s2 < 0 ? static_cast<uint64_t>(-s2) : static_cast<uint64_t>(s2);
        } else {
            mag1 = u1;
            mag2 = u2;
        }
        if (mag1 < mag2) return 2;
        const auto clz1 = std::countl_zero(mag1);
        const auto clz2 = std::countl_zero(mag2);
        const auto diff = clz2 > clz1 ? (clz2 - clz1) : 0;
        return std::clamp<simrv::xlen::LatencyCycles>(
            static_cast<simrv::xlen::LatencyCycles>(2 + (diff / 2)), 2, config.div_latency);
    }
}

const bool g_retire_trace = std::getenv("SIMRV_RETIRE_TRACE") != nullptr;
const bool g_cache_trace = std::getenv("SIMRV_CACHE_TRACE") != nullptr;
const bool g_branch_trace = std::getenv("SIMRV_BRANCH_TRACE") != nullptr;

void trace_branch(const pipeline::PipelineContext& context,
                  const simrv::pipeline::BranchPrediction& prediction, simrv::xlen::VirtAddr target,
                  bool mispredict) {
    if (!g_branch_trace) return;
    std::println(stderr,
                 "BRTRACE pc={:08x} ir={:08x} taken={} target={:08x} pred_taken={} "
                 "pred_target={:08x} miss={}",
                 context.cpc.raw(), context.ir, context.tkn, target.raw(),
                 prediction.predicted_taken, prediction.predicted_target.raw(), mispredict);
}

void populate_exec_latency(pipeline::CycleInstructionSlot& slot, const pipeline::CpuConfig& config,
                           execute::CfuUnit& cfu_unit, unsigned xlen) {
    const auto exec_class = pipeline::operation::info(slot.context.op_id).execution_class;
    if (simrv::compiler::unlikely(slot.context.traits.is_serializing ||
                                  exec_class != pipeline::operation::ExecutionClass::Default)) {
        using pipeline::operation::ExecutionClass;
        switch (exec_class) {
            case ExecutionClass::Multiply:
                slot.remaining_latency = config.mul_latency;
                break;
            case ExecutionClass::DivideOrRemainder:
                slot.remaining_latency = compute_div_latency(slot.context, config, xlen);
                break;
            case ExecutionClass::FpDivideOrSqrt:
                slot.remaining_latency = config.fp_div_latency;
                break;
            case ExecutionClass::FpAlu:
                slot.remaining_latency = config.fp_alu_latency;
                break;
            case ExecutionClass::Cfu:
                slot.remaining_latency = cfu_unit.query_stall_cycles(
                    slot.context.funct7, std::to_underlying(slot.context.funct3));
                break;
            default:
                if (slot.context.opcode == isa::Opcode::System) {
                    slot.remaining_latency = config.csr_flush_penalty;
                } else if (slot.context.opcode == isa::Opcode::MiscMem) {
                    slot.remaining_latency = config.fence_flush_penalty;
                }
                break;
        }
    }
}
}  // namespace

template <pipeline::PipelineTopologySpec Spec>
void CPU::run_ca_pipeline_cycle_impl(Machine& machine) {
    auto& pipe = ca_pipeline;
    // The retired slot is presentation state: fast CA only consumes the retirement bit, while
    // Observable snapshots, TUI tracing, and architectural tracing need the full retiring
    // instruction context. Avoid copying a complete PipelineContext on every retirement when
    // none of those consumers is active.
    const bool retain_retired_slot =
        machine.trace().needs_retiring_context() ||
        (pipeline_sim.config.record_snapshots &&
         (!machine.tui_enabled() ||
          (machine.telemetry_sink() && machine.telemetry_sink()->captures_execution_detail())));
    pipe.retired_this_cycle = false;
    if (retain_retired_slot) pipe.retired->clear();
    pipe.data_hazard_stall = false;
    pipe.control_flush = false;
    ca_state.retired_this_cycle = false;
    ca_state.waiting_for_interconnect = false;
    if (ca_state.host_write_pending) {
        if (ca_state.host_write_remaining > 0) {
            --ca_state.host_write_remaining;
            return;
        }
    }
    if (pipe.frontend_refill_stall > 0) {
        --pipe.frontend_refill_stall;
        return;
    }
    if (!pipe.initialized) {
        pipe.fetch_pc = state_.pc;
        pipe.initialized = true;
    }
    if (simrv::compiler::unlikely(branch_predictor.has_registered_btb())) {
        branch_predictor.latch_btb_read(pipe.fetch_pc);
    }

    auto run_with_context = [&](CycleInstructionSlot& slot, auto&& operation) -> bool {
        active_context_ = &slot.context;
        const bool result = operation();
        active_context_ = &pipeline_context;
        return result;
    };
    auto cancel_port = [&](simrv::memory::TlPort port) {
        machine.memory_.system_bus().cancel_source(simrv::memory::make_tl_source(hart_id(), port));
    };
    auto flush_to = [&](simrv::xlen::VirtAddr pc) {
        pipe.flush_younger();
        pipe.control_flush = true;
        pipe.fetch_pc = pc;
        cancel_port(simrv::memory::TlPort::Instruction);
        ca_state.instruction_fill.reset();
        ca_state.instruction_prefetch.reset();
        ca_state.instruction_walk.reset();
        pipe.frontend_blocked = false;
        // Execute-stage resolution already consumes the redirect cycle and the next cycle
        // refills IF. Add only the remaining bubbles needed to reach the configured total
        // misprediction penalty.
        if (pipeline_sim.config.branch_mispredict_penalty > 2) {
            pipe.control_recovery_bubbles = pipeline_sim.config.branch_mispredict_penalty - 2;
        }
    };
    auto trap_at_retirement = [&](CycleInstructionSlot& slot) {
        const auto cause = slot.context.pending_exception.value_or(ExceptionCode::MisalignedFetch);
        const auto tval = slot.context.pending_tval;
        state_.pc = slot.context.cpc;
        cancel_port(simrv::memory::TlPort::Instruction);
        cancel_port(simrv::memory::TlPort::Data);
        pipe.reset();
        ca_state.reset_instruction();
        raise_exception(static_cast<simrv::xlen::TrapCause>(cause), tval);
        pipe.fetch_pc = state_.pc;
        pipe.initialized = true;
    };
    auto apply_forwarding = [&](pipeline::PipelineContext& consumer) {
        if (!pipeline_sim.config.enable_forwarding || !pipe.scoreboard.has_forwardable_int())
            return;
        if (consumer.traits.reads_rs1_int && consumer.rs1 != simrv::xlen::RegId::Zero &&
            pipe.scoreboard.can_forward_int(consumer.rs1)) {
            consumer.rrs1 = pipe.scoreboard.get_forwarded_int_unchecked(consumer.rs1);
        }
        if (consumer.traits.reads_rs2_int && consumer.rs2 != simrv::xlen::RegId::Zero &&
            pipe.scoreboard.can_forward_int(consumer.rs2)) {
            consumer.rrs2 = pipe.scoreboard.get_forwarded_int_unchecked(consumer.rs2);
        }
    };

    // Retire first so all younger stages observe the architecturally committed state.
    auto* writeback = pipe.writeback;
    if (writeback->valid) {
        if (writeback->remaining_latency > 0) {
            --writeback->remaining_latency;
        } else {
            bool ready = true;
            if (writeback->serializing && !writeback->executed) {
                ready = run_with_context(*writeback, [&] {
                    fetch_operands(machine);
                    if (active_context().pending_exception.has_value()) return false;
                    if (!execute_stage(machine)) return false;
                    writeback->executed = true;
                    if (writeback->context.traits.is_mem_load || writeback->context.traits.is_cfu ||
                        is_store_conditional(writeback->context)) {
                        writeback->wb_valid = false;
                    } else if (writeback->writes_int &&
                               writeback->wb_dest != simrv::xlen::RegId::Zero) {
                        writeback->wb_val = (writeback->context.opcode == isa::Opcode::System &&
                                             writeback->context.funct3 != isa::Funct3::Priv)
                                                ? writeback->context.rcsr
                                                : writeback->context.wb_data;
                        writeback->wb_valid = true;
                    }
                    return true;
                });
            }
            if constexpr (!Spec.has_decode && !Spec.has_execute) {
                if (!writeback->executed && !writeback->serializing) {
                    ready = run_with_context(*writeback, [&] {
                        fetch_operands(machine);
                        apply_forwarding(active_context());
                        if (active_context().pending_exception.has_value()) return false;
                        return execute_stage(machine);
                    });
                    writeback->executed = true;
                    if (writeback->context.traits.is_mem_load ||
                        is_store_conditional(writeback->context)) {
                        writeback->wb_valid = false;
                    } else if (writeback->writes_int &&
                               writeback->wb_dest != simrv::xlen::RegId::Zero) {
                        writeback->wb_val = (writeback->context.opcode == isa::Opcode::System &&
                                             writeback->context.funct3 != isa::Funct3::Priv)
                                                ? writeback->context.rcsr
                                                : writeback->context.wb_data;
                        writeback->wb_valid = true;
                    }
                    if (writeback->context.traits.is_control ||
                        writeback->prediction.false_control_alias) {
                        const simrv::xlen::VirtAddr sequential =
                            writeback->context.cpc + (writeback->context.cinsn != 0u ? 2 : 4);
                        const simrv::xlen::VirtAddr target =
                            writeback->context.tkn ? writeback->context.jmp_pc : sequential;
                        const simrv::pipeline::BranchFeedback feedback{
                            .pc = writeback->context.cpc,
                            .actual_taken = writeback->context.tkn,
                            .actual_target = target,
                            .opcode = writeback->context.opcode,
                            .op_id = writeback->context.op_id,
                            .rd = writeback->context.rd,
                            .rs1 = writeback->context.rs1,
                            .prediction = writeback->prediction,
                        };
                        branch_predictor.update(feedback);
                        const bool dir_mispredict =
                            (writeback->context.tkn != writeback->prediction.predicted_taken);
                        const bool target_mispredict =
                            writeback->context.tkn &&
                            (target != writeback->prediction.predicted_target);
                        trace_branch(writeback->context, writeback->prediction, target,
                                     dir_mispredict || target_mispredict);
                        if (dir_mispredict || target_mispredict) {
                            record_hpm_event(HpmEvent::BranchMispredict);
                            branch_predictor.restore_speculation(writeback->prediction);
                            pipe.fetch->invalidate();
                            pipe.control_flush = true;
                            pipe.fetch_pc = target;
                            cancel_port(simrv::memory::TlPort::Instruction);
                            ca_state.instruction_fill.reset();
                            ca_state.instruction_walk.reset();
                            pipe.frontend_blocked = false;
                        }
                    }
                }
            }
            if (ready && (Spec.memory_stages == 0 || writeback->serializing) &&
                !writeback->memory_complete) {
                const auto misses_before = dcache.miss_count();
                ready = run_with_context(*writeback, [&] {
                    if (!memory_stage(machine)) return false;
                    if (ca_state.waiting_for_interconnect) return false;
                    writeback->memory_complete = true;
                    if (writeback->context.traits.is_mem_load) {
                        writeback->wb_val = writeback->context.mem_rdata;
                        writeback->wb_valid = (writeback->writes_int &&
                                               writeback->wb_dest != simrv::xlen::RegId::Zero);
                    } else if (writeback->context.traits.is_cfu) {
                        writeback->wb_val = writeback->context.wb_data;
                        writeback->wb_valid = (writeback->writes_int &&
                                               writeback->wb_dest != simrv::xlen::RegId::Zero);
                    } else if (is_store_conditional(writeback->context)) {
                        writeback->wb_val = writeback->context.wb_data;
                        writeback->wb_valid = (writeback->writes_int &&
                                               writeback->wb_dest != simrv::xlen::RegId::Zero);
                    }
                    return true;
                });
                if (dcache.miss_count() != misses_before) {
                    writeback->dcache_miss = true;
                }
                if (!ready && ca_state.waiting_for_interconnect) return;
            }
            if (!ready || writeback->context.pending_exception.has_value()) {
                trap_at_retirement(*writeback);
                return;
            }
            const bool was_serializing = writeback->serializing;
            if (simrv::compiler::likely(!was_serializing && !writeback->context.traits.writes_fp &&
                                        writeback->context.opcode != isa::Opcode::System &&
                                        !writeback->context.pending_exception.has_value() &&
                                        (state_.mip & state_.mie) == 0 &&
                                        !machine.instruction_mix_enabled())) {
                account_retirement();
                if (writeback->wb_valid) {
                    state_.regs.write_branchless(writeback->wb_dest, writeback->wb_val);
                }
                if (writeback->context.cinsn != 0u) {
                    ++e_ccount;
                }
            } else {
                const bool retired = run_with_context(
                    *writeback, [&] { return writeback_stage(machine) && commit_stage(machine); });
                if (!retired) {
                    trap_at_retirement(*writeback);
                    return;
                }
            }
            if (ca_state.host_write_pending && ca_state.host_write_remaining == 0) {
                const auto value = ca_state.pending_host_value;
                const auto kind =
                    static_cast<Machine::TohostWriteKind>(ca_state.pending_host_write_kind);
                const auto fallback_cycles =
                    static_cast<uint32_t>(ca_state.host_write_fallback_cycles);
                ca_state.host_write_pending = false;
                machine.publish_tohost_write(value, kind, fallback_cycles);
            }
            if (simrv::compiler::unlikely(g_retire_trace)) {
                std::println(stderr, "CYCLETRACE cycle={} inst={} pc={:08x} ir={:08x}",
                             clint_mmio.mcycle, e_icount, writeback->context.cpc.raw(),
                             writeback->context.ir);
            }
            const bool is_wfi = writeback->context.op_id == isa::OperationId::WFI;
            if (retain_retired_slot) *pipe.retired = *writeback;
            writeback->invalidate();
            pipe.retired_this_cycle = true;
            ca_state.retired_this_cycle = true;
            if (is_wfi && (state_.mip & state_.mie) == 0) {
                wfi_quiescent_ = true;
            }
            if (was_serializing) {
                pipe.fetch_pc = state_.pc;
                pipe.frontend_blocked = false;
            }
        }
    }

    // Second memory stage (for split-memory topologies such as SixStageMem).
    if constexpr (Spec.memory_stages == 2) {
        auto* memory2 = pipe.memory2;
        if (memory2->valid && !pipe.writeback->valid) {
            if (memory2->remaining_latency > 0) {
                --memory2->remaining_latency;
            } else {
                std::swap(pipe.writeback, pipe.memory2);
                pipe.memory2->invalidate();
            }
        }
    }

    // Memory stage owns the single per-hart data transaction until completion.
    if constexpr (Spec.memory_stages >= 1) {
        auto* memory = pipe.memory;
        auto*& next_mem_stage = [&]() -> auto*& {
            if constexpr (Spec.memory_stages == 2) {
                return pipe.memory2;
            } else {
                return pipe.writeback;
            }
        }();
        if (memory->valid && !next_mem_stage->valid) {
            if (memory->serializing) {
                std::swap(next_mem_stage, pipe.memory);
                pipe.memory->invalidate();
            } else if (memory->remaining_latency > 0) {
                --memory->remaining_latency;
            } else {
                if (!memory->memory_complete) {
                    const auto mem_op = memory->context.opcode;
                    const bool is_mem_op =
                        memory->context.traits.is_mem_load || memory->context.traits.is_mem_store ||
                        mem_op == isa::Opcode::LoadFp || mem_op == isa::Opcode::StoreFp ||
                        mem_op == isa::Opcode::Amo;
                    if (simrv::compiler::likely(!is_mem_op)) {
                        memory->memory_complete = true;
                        if (simrv::compiler::unlikely(memory->context.traits.is_cfu)) {
                            memory->wb_val = memory->context.wb_data;
                            memory->wb_valid =
                                (memory->writes_int && memory->wb_dest != simrv::xlen::RegId::Zero);
                        }
                    } else {
                        const auto misses_before = dcache.miss_count();
                        const bool success =
                            run_with_context(*memory, [&] { return memory_stage(machine); });
                        if (dcache.miss_count() != misses_before) {
                            memory->dcache_miss = true;
                        }
                        if (ca_state.waiting_for_interconnect) return;
                        memory->memory_complete = true;
                        if (memory->context.traits.is_mem_load) {
                            memory->wb_val = memory->context.mem_rdata;
                            memory->wb_valid =
                                (memory->writes_int && memory->wb_dest != simrv::xlen::RegId::Zero);
                        } else if (memory->context.traits.is_cfu) {
                            memory->wb_val = memory->context.wb_data;
                            memory->wb_valid =
                                (memory->writes_int && memory->wb_dest != simrv::xlen::RegId::Zero);
                        } else if (is_store_conditional(memory->context)) {
                            memory->wb_val = memory->context.wb_data;
                            memory->wb_valid =
                                (memory->writes_int && memory->wb_dest != simrv::xlen::RegId::Zero);
                        }
                        if (!success && !memory->context.pending_exception.has_value()) {
                            memory->context.pending_exception = ExceptionCode::FaultLoad;
                        }
                    }
                }
                if (memory->remaining_latency == 0) {
                    std::swap(next_mem_stage, pipe.memory);
                    pipe.memory->invalidate();
                }
            }
        }
    }

    // Execute is side-effect-free for non-serializing instructions.
    if constexpr (Spec.has_execute) {
        auto* execute = pipe.execute;
        if (execute->valid && !pipe.memory->valid) {
            if (simrv::compiler::unlikely(execute->remaining_latency > 0)) {
                --execute->remaining_latency;
            } else {
                if (!execute->serializing && !execute->executed) {
                    auto& ctx = execute->context;
                    bool executed_fast = false;
                    switch (ctx.opcode) {
                        case isa::Opcode::OpImm:
                            ctx.tkn = false;
                            ctx.wb_data = execute::ExecuteUnit::aluInt(
                                ctx.rrs1, ctx.imm, ctx.op_id, state_.regs.xlen, ctx.funct7);
                            executed_fast = true;
                            break;
                        case isa::Opcode::Op:
                            ctx.tkn = false;
                            ctx.wb_data = execute::ExecuteUnit::aluInt(
                                ctx.rrs1, ctx.rrs2, ctx.op_id, state_.regs.xlen, ctx.funct7);
                            executed_fast = true;
                            break;
                        case isa::Opcode::OpImm32:
                            ctx.tkn = false;
                            ctx.wb_data =
                                execute::ExecuteUnit::aluIntW(ctx.rrs1, ctx.imm, ctx.op_id);
                            executed_fast = true;
                            break;
                        case isa::Opcode::Op32:
                            ctx.tkn = false;
                            ctx.wb_data =
                                execute::ExecuteUnit::aluIntW(ctx.rrs1, ctx.rrs2, ctx.op_id);
                            executed_fast = true;
                            break;
                        case isa::Opcode::Lui:
                            ctx.tkn = false;
                            ctx.wb_data = ctx.imm;
                            executed_fast = true;
                            break;
                        case isa::Opcode::Auipc:
                            ctx.tkn = false;
                            ctx.wb_data = (ctx.cpc + ctx.imm).raw();
                            executed_fast = true;
                            break;
                        case isa::Opcode::Load:
                        case isa::Opcode::Store:
                            ctx.tkn = false;
                            ctx.mem_addr = simrv::xlen::VirtAddr{ctx.rrs1 + ctx.imm};
                            executed_fast = true;
                            break;
                        case isa::Opcode::Branch:
                            ctx.tkn = execute::ExecuteUnit::branchTaken(
                                ctx.rrs1, ctx.rrs2, ctx.funct3, state_.regs.xlen);
                            ctx.jmp_pc = ctx.cpc + ctx.imm;
                            if (state_.regs.xlen == 32) {
                                ctx.jmp_pc = simrv::xlen::VirtAddr{
                                    static_cast<simrv::xlen::Register>(static_cast<int64_t>(
                                        static_cast<int32_t>(ctx.jmp_pc.raw())))};
                            }
                            executed_fast = true;
                            break;
                        case isa::Opcode::Jal:
                            ctx.tkn = true;
                            ctx.wb_data = (ctx.cpc + ((ctx.cinsn != 0u) ? 2 : 4)).raw();
                            ctx.jmp_pc = ctx.cpc + ctx.imm;
                            executed_fast = true;
                            break;
                        case isa::Opcode::Jalr:
                            ctx.tkn = true;
                            ctx.wb_data = (ctx.cpc + ((ctx.cinsn != 0u) ? 2 : 4)).raw();
                            ctx.jmp_pc = simrv::xlen::VirtAddr{
                                (ctx.rrs1 + ctx.imm) & ~static_cast<simrv::xlen::Register>(1)};
                            if (state_.regs.xlen == 32) {
                                ctx.jmp_pc = simrv::xlen::VirtAddr{
                                    static_cast<simrv::xlen::Register>(static_cast<int64_t>(
                                        static_cast<int32_t>(ctx.jmp_pc.raw())))};
                            }
                            executed_fast = true;
                            break;
                        default:
                            break;
                    }
                    if (!executed_fast) {
                        const auto saved_pc = state_.pc;
                        state_.pc = execute->context.cpc;
                        (void)run_with_context(*execute, [&] { return execute_stage(machine); });
                        state_.pc = saved_pc;
                    }
                    execute->executed = true;
                    if (simrv::compiler::unlikely(execute->context.traits.is_mem_load ||
                                                  execute->context.traits.is_cfu ||
                                                  is_store_conditional(execute->context))) {
                        execute->wb_valid = false;
                    } else if (execute->writes_int &&
                               execute->wb_dest != simrv::xlen::RegId::Zero) {
                        execute->wb_val = simrv::compiler::unlikely(
                                              execute->context.opcode == isa::Opcode::System &&
                                              execute->context.funct3 != isa::Funct3::Priv)
                                              ? execute->context.rcsr
                                              : execute->context.wb_data;
                        execute->wb_valid = true;
                    }

                    if (simrv::compiler::unlikely(execute->context.traits.is_control ||
                                                  execute->prediction.false_control_alias)) {
                        const simrv::xlen::VirtAddr sequential =
                            execute->context.cpc + (execute->context.cinsn != 0u ? 2 : 4);
                        const simrv::xlen::VirtAddr target =
                            execute->context.tkn ? execute->context.jmp_pc
                                                 : simrv::xlen::VirtAddr{sequential};

                        const simrv::pipeline::BranchFeedback feedback{
                            .pc = execute->context.cpc,
                            .actual_taken = execute->context.tkn,
                            .actual_target = target,
                            .opcode = execute->context.opcode,
                            .op_id = execute->context.op_id,
                            .rd = execute->context.rd,
                            .rs1 = execute->context.rs1,
                            .prediction = execute->prediction,
                        };
                        branch_predictor.update(feedback);

                        const bool dir_mispredict =
                            (execute->context.tkn != execute->prediction.predicted_taken);
                        const bool target_mispredict =
                            execute->context.tkn &&
                            (target != execute->prediction.predicted_target);
                        const bool mispredict = dir_mispredict || target_mispredict;
                        trace_branch(execute->context, execute->prediction, target, mispredict);

                        if (mispredict) {
                            record_hpm_event(HpmEvent::BranchMispredict);
                            branch_predictor.restore_speculation(execute->prediction);
                            flush_to(target);
                        }
                    }
                }
                std::swap(pipe.memory, pipe.execute);
                pipe.execute->invalidate();
                const bool is_load = pipe.memory->context.traits.is_mem_load ||
                                     pipe.memory->context.opcode == isa::Opcode::LoadFp;
                const bool is_store = pipe.memory->context.traits.is_mem_store ||
                                      pipe.memory->context.opcode == isa::Opcode::StoreFp;
                if (simrv::compiler::unlikely(cpu_model_config.data_cache.hit_latency > 1) &&
                    pipe.memory->valid &&
                    (is_load || (is_store && !cpu_model_config.data_cache.write_through))) {
                    pipe.memory->remaining_latency = cpu_model_config.data_cache.hit_latency - 1;
                }
            }
        }
    }

    auto issue_to_execute = [&](pipeline::CycleInstructionSlot*& src_slot) {
        if (!src_slot->valid || pipe.execute->valid) return;
        pipe.sync_scoreboard(/*include_decode=*/false);
        const bool hazard = pipe.scoreboard.has_raw_hazard(src_slot->context,
                                                           pipeline_sim.config.enable_forwarding);
        pipe.data_hazard_stall = hazard;
        const bool serial_wait =
            src_slot->serializing &&
            (pipe.memory->valid || pipe.memory2->valid || pipe.writeback->valid);
        if (hazard || serial_wait) return;

        if (simrv::compiler::likely(src_slot->context.opcode != isa::Opcode::System &&
                                    !src_slot->context.pending_exception.has_value())) {
            src_slot->context.rrs1 = state_.regs.read(src_slot->context.rs1);
            src_slot->context.rrs2 = state_.regs.read(src_slot->context.rs2);
            src_slot->context.rcsr = 0;
            apply_forwarding(src_slot->context);
        } else {
            (void)run_with_context(*src_slot, [&] {
                fetch_operands(machine);
                apply_forwarding(active_context());
                return !active_context().pending_exception.has_value();
            });
        }
        auto* ex_slot = src_slot;
        std::swap(pipe.execute, src_slot);
        src_slot->invalidate();
        populate_exec_latency(*ex_slot, pipeline_sim.config, cfu_unit, state_.regs.xlen);
    };

    // Optional Issue stage (for SixStageIssue: IF -> ID -> IS -> EX -> MEM -> WB).
    if constexpr (Spec.has_issue) {
        issue_to_execute(pipe.issue);
    }

    // Decode waits for unresolved producers (unless Issue stage owns scoreboard checks).
    if constexpr (Spec.has_decode) {
        if constexpr (Spec.has_issue) {
            if (pipe.decode->valid && !pipe.issue->valid) {
                std::swap(pipe.issue, pipe.decode);
                pipe.decode->invalidate();
            }
        } else if constexpr (Spec.has_execute) {
            issue_to_execute(pipe.decode);
        } else {
            auto* decode = pipe.decode;
            if (decode->valid && !pipe.execute->valid) {
                pipe.sync_scoreboard(/*include_decode=*/false);
                const bool hazard = pipe.scoreboard.has_raw_hazard(
                    decode->context, pipeline_sim.config.enable_forwarding);
                pipe.data_hazard_stall = hazard;
                const bool serial_wait =
                    decode->serializing && (pipe.memory->valid || pipe.writeback->valid);
                if (!hazard && !serial_wait) {
                    if (!decode->executed && !decode->serializing) {
                        (void)run_with_context(*decode, [&] {
                            fetch_operands(machine);
                            apply_forwarding(active_context());
                            if (active_context().pending_exception.has_value()) return false;
                            return execute_stage(machine);
                        });
                        decode->executed = true;
                        if (decode->context.traits.is_mem_load ||
                            is_store_conditional(decode->context)) {
                            decode->wb_valid = false;
                        } else if (decode->writes_int &&
                                   decode->wb_dest != simrv::xlen::RegId::Zero) {
                            decode->wb_val = (decode->context.opcode == isa::Opcode::System &&
                                              decode->context.funct3 != isa::Funct3::Priv)
                                                 ? decode->context.rcsr
                                                 : decode->context.wb_data;
                            decode->wb_valid = true;
                        }
                        const auto exec_class =
                            pipeline::operation::info(decode->context.op_id).execution_class;
                        if (exec_class == pipeline::operation::ExecutionClass::Multiply) {
                            decode->remaining_latency = pipeline_sim.config.mul_latency;
                        } else if (exec_class ==
                                   pipeline::operation::ExecutionClass::DivideOrRemainder) {
                            decode->remaining_latency = compute_div_latency(
                                decode->context, pipeline_sim.config, state_.regs.xlen);
                        } else if (decode->context.traits.is_cfu) {
                            decode->remaining_latency = cfu_unit.query_stall_cycles(
                                decode->context.funct7, std::to_underlying(decode->context.funct3));
                        }
                        if (decode->context.traits.is_control ||
                            decode->prediction.false_control_alias) {
                            const simrv::xlen::VirtAddr sequential =
                                decode->context.cpc + (decode->context.cinsn != 0u ? 2 : 4);
                            const simrv::xlen::VirtAddr target =
                                decode->context.tkn ? decode->context.jmp_pc : sequential;

                            const simrv::pipeline::BranchFeedback feedback{
                                .pc = decode->context.cpc,
                                .actual_taken = decode->context.tkn,
                                .actual_target = target,
                                .opcode = decode->context.opcode,
                                .op_id = decode->context.op_id,
                                .rd = decode->context.rd,
                                .rs1 = decode->context.rs1,
                                .prediction = decode->prediction,
                            };
                            branch_predictor.update(feedback);

                            const bool dir_mispredict =
                                (decode->context.tkn != decode->prediction.predicted_taken);
                            const bool target_mispredict =
                                decode->context.tkn &&
                                (target != decode->prediction.predicted_target);
                            const bool mispredict = dir_mispredict || target_mispredict;
                            trace_branch(decode->context, decode->prediction, target, mispredict);

                            if (mispredict) {
                                record_hpm_event(HpmEvent::BranchMispredict);
                                branch_predictor.restore_speculation(decode->prediction);
                                // The resolving instruction itself occupies the combined ID/EX
                                // stage. Only the younger fetch-stage instruction is squashed.
                                pipe.fetch->invalidate();
                                pipe.control_flush = true;
                                pipe.fetch_pc = target;
                                cancel_port(simrv::memory::TlPort::Instruction);
                                ca_state.instruction_fill.reset();
                                ca_state.instruction_walk.reset();
                                pipe.frontend_blocked = false;
                            }
                        }
                    }
                    if (decode->remaining_latency > 0) {
                        --decode->remaining_latency;
                    } else if (!pipe.writeback->valid) {
                        if (decode->serializing && decode->context.opcode == isa::Opcode::System) {
                            decode->remaining_latency = pipeline_sim.config.csr_flush_penalty;
                        } else if (decode->serializing &&
                                   decode->context.opcode == isa::Opcode::MiscMem) {
                            decode->remaining_latency = pipeline_sim.config.fence_flush_penalty;
                        }
                        std::swap(pipe.writeback, pipe.decode);
                        pipe.decode->invalidate();
                    }
                }
            }
        }
    }

    // Optional second fetch stage (for split-fetch topologies such as SixStageFetch).
    if constexpr (Spec.fetch_stages == 2) {
        if (pipe.fetch2->valid && !pipe.decode->valid) {
            std::swap(pipe.decode, pipe.fetch2);
            pipe.fetch2->invalidate();
        }
    }

    auto* fetch = pipe.fetch;
    if (fetch->valid && fetch->remaining_latency > 0) {
        --fetch->remaining_latency;
    }
    auto*& next_fetch_stage = [&]() -> auto*& {
        if constexpr (Spec.fetch_stages == 2) {
            return pipe.fetch2;
        } else if constexpr (Spec.has_decode) {
            return pipe.decode;
        } else {
            return pipe.writeback;
        }
    }();
    if (fetch->valid && fetch->remaining_latency == 0 && !next_fetch_stage->valid) {
        std::swap(next_fetch_stage, pipe.fetch);
        pipe.fetch->invalidate();
        fetch = pipe.fetch;
    }

    if (pipe.control_recovery_bubbles > 0) {
        --pipe.control_recovery_bubbles;
    } else if (!fetch->valid && !pipe.frontend_blocked) {
        const auto committed_pc = state_.pc;
        state_.pc = pipe.fetch_pc;
        active_context_ = &fetch->context;
        const auto misses_before = icache.miss_count();
        const bool fetched = fetch_stage(machine, pipe.fetch_pc);
        const bool waiting = ca_state.waiting_for_interconnect;
        state_.pc = committed_pc;
        if (icache.miss_count() != misses_before) {
            ca_state.icache_miss = true;
        }
        if (waiting) {
            active_context_ = &pipeline_context;
            return;
        }

        fetch->valid = true;
        if (!fetched) {
            fetch->serializing = true;
            pipe.frontend_blocked = true;
        } else {
            if (simrv::compiler::unlikely(fetch->context.opcode == static_cast<isa::Opcode>(0) &&
                                          fetch->context.op_id == isa::ILLEGAL)) {
                decode_fields(machine);
            }
            fetch->serializing = fetch->context.traits.is_serializing;
            fetch->writes_int = fetch->context.traits.writes_int;
            fetch->writes_fp = fetch->context.traits.writes_fp;
            fetch->wb_dest = fetch->context.rd;
            fetch->wb_valid = false;
            fetch->wb_val = 0;
            if (instruction_cache_timing.enabled()) {
                const auto access =
                    instruction_cache_timing.access(fetch->context.cpc, ca_state.icache_miss);
                const auto refill_wait = access.latency > 0 ? access.latency - 1 : 0;
                if (!access.hit &&
                    cpu_model_config.instruction_front_cache.freeze_pipeline_on_refill) {
                    pipe.frontend_refill_stall = refill_wait;
                    fetch->remaining_latency = 0;
                } else {
                    fetch->remaining_latency = refill_wait;
                }
                if (simrv::compiler::unlikely(g_cache_trace) &&
                    (!access.hit || ca_state.icache_miss)) {
                    std::println(stderr,
                                 "CACHETRACE cycle={} pc={:08x} hit={} level={} backing_miss={} "
                                 "latency={}",
                                 pipeline_sim.cycle_count(), fetch->context.cpc.raw(), access.hit,
                                 access.hit_level, ca_state.icache_miss, access.latency);
                }
            } else if (cpu_model_config.instruction_cache.hit_latency > 1) {
                fetch->remaining_latency = cpu_model_config.instruction_cache.hit_latency - 1;
            }
            const simrv::xlen::Address width = fetch->context.cinsn != 0u ? 2 : 4;
            const simrv::xlen::VirtAddr sequential = fetch->context.cpc + width;

            if (simrv::compiler::unlikely(fetch->context.traits.is_control ||
                                          branch_predictor.config().predict_non_control)) {
                branch_predictor.predict(fetch->context.cpc, fetch->context, fetch->prediction);
                if (fetch->prediction.predicted_taken && fetch->prediction.predicted_target != 0) {
                    pipe.fetch_pc = fetch->prediction.predicted_target;
                    pipe.frontend_blocked = fetch->serializing;
                } else if (fetch->context.opcode == isa::Opcode::Jalr &&
                           !fetch->prediction.btb_hit && !fetch->prediction.ras_hit) {
                    pipe.fetch_pc = sequential;
                    pipe.frontend_blocked = true;
                } else {
                    pipe.fetch_pc = sequential;
                    pipe.frontend_blocked = fetch->serializing;
                }
            } else {
                pipe.fetch_pc = sequential;
                pipe.frontend_blocked = fetch->serializing;
            }

            fetch->icache_miss = ca_state.icache_miss;
            ca_state.icache_miss = false;
            fetch->tlb_miss = fetch->context.tlb_miss;
        }
        active_context_ = &pipeline_context;
    }
    if (simrv::compiler::unlikely(machine.tui_enabled() || pipeline_sim.config.record_snapshots)) {
        pipe.sync_scoreboard();
    }
}

void CPU::run_ca_pipeline_cycle(Machine& machine) {
    using DispatchFn = void (CPU::*)(Machine&);
    static constexpr auto kDispatchTable = []<size_t... Is>(std::index_sequence<Is...>) constexpr {
        return std::array<DispatchFn, sizeof...(Is)>{
            &CPU::run_ca_pipeline_cycle_impl<pipeline::kPipelineCatalog[Is].spec>...};
    }(std::make_index_sequence<pipeline::kPipelineCatalog.size()>{});

    const auto index = static_cast<size_t>(pipeline_sim.config.pipeline_type);
    const auto fn = index < kDispatchTable.size() ? kDispatchTable[index] : kDispatchTable[2];
    (this->*fn)(machine);
}

}  // namespace simrv::core
