#include "simrv/core/Cpu.hpp"

#include <algorithm>
#include <utility>

#include "simrv/core/Machine.hpp"
#include "simrv/pipeline/OperationTraits.hpp"
#include "simrv/pipeline/RetirementEffects.hpp"

namespace simrv::core {

namespace {

using pipeline::CycleInstructionSlot;

[[nodiscard]] auto is_control(const pipeline::PipelineContext& context) -> bool {
    return context.opcode == isa::Opcode::Branch || context.opcode == isa::Opcode::Jal ||
           context.opcode == isa::Opcode::Jalr;
}

[[nodiscard]] auto is_serializing(const pipeline::PipelineContext& context) -> bool {
    const auto opcode = context.opcode;
    const bool vector = context.op_id >= isa::OperationId::VSETVLI &&
                        context.op_id <= isa::OperationId::VWSLL_VI;
    return vector || opcode == isa::Opcode::System || opcode == isa::Opcode::MiscMem ||
           opcode == isa::Opcode::Amo || opcode == isa::Opcode::OpFp ||
           opcode == isa::Opcode::LoadFp || opcode == isa::Opcode::StoreFp ||
           opcode == isa::Opcode::MAdd || opcode == isa::Opcode::MSub ||
           opcode == isa::Opcode::NMAdd || opcode == isa::Opcode::NMSub;
}

[[nodiscard]] auto writes_integer(const pipeline::PipelineContext& context) -> bool {
    if (context.rd == RegId::Zero) return false;
    switch (context.opcode) {
        case isa::Opcode::Branch:
        case isa::Opcode::Store:
        case isa::Opcode::StoreFp:
        case isa::Opcode::MiscMem: return false;
        default: return !isa::is_destination_fp(context.opcode, context.op_id);
    }
}

[[nodiscard]] auto has_raw_dependency(const CycleInstructionSlot& consumer,
                                      const CycleInstructionSlot& producer) -> bool {
    if (!consumer.valid || !producer.valid || !writes_integer(producer.context)) return false;
    const auto destination = producer.context.rd;
    return (pipeline::operation::reads_rs1(consumer.context.opcode) &&
            consumer.context.rs1 == destination) ||
           (pipeline::operation::reads_rs2(consumer.context.opcode) &&
            consumer.context.rs2 == destination);
}

[[nodiscard]] auto forwarded_integer(const CycleInstructionSlot& producer, RegId source)
    -> std::optional<Register> {
    if (!producer.valid || !producer.executed || source == RegId::Zero) return std::nullopt;
    const auto effects = pipeline::build_writeback_effects(producer.context);
    if (!effects.integer_write.enabled || effects.integer_write.destination != source) {
        return std::nullopt;
    }
    if (producer.context.opcode == isa::Opcode::Load && !producer.memory_complete) {
        return std::nullopt;
    }
    return effects.integer_write.value;
}

}  // namespace

void CPU::run_ca_pipeline_cycle(Machine& machine) {
    auto& pipe = ca_pipeline;
    const bool three_stage =
        pipeline_sim.config.pipeline_type == pipeline::PipelineType::ThreeStage;
    pipe.retired_this_cycle = false;
    pipe.retired.clear();
    pipe.data_hazard_stall = false;
    pipe.control_flush = false;
    ca_state.retired_this_cycle = false;
    ca_state.waiting_for_interconnect = false;
    if (!pipe.initialized) {
        pipe.fetch_pc = state_.pc;
        pipe.initialized = true;
    }

    auto run_with_context = [&](CycleInstructionSlot& slot, auto&& operation) -> bool {
        std::swap(pipeline_context, slot.context);
        const bool result = operation();
        std::swap(pipeline_context, slot.context);
        return result;
    };
    auto cancel_port = [&](simrv::memory::TlPort port) {
        machine.memory_.system_bus().cancel_source(simrv::memory::make_tl_source(
            static_cast<HartId>(state_.mhartid), port));
    };
    auto flush_to = [&](Address pc) {
        pipe.flush_younger();
        pipe.control_flush = true;
        pipe.fetch_pc = pc;
        cancel_port(simrv::memory::TlPort::Instruction);
        ca_state.instruction_fill.reset();
        ca_state.instruction_walk.reset();
        pipe.frontend_blocked = false;
    };
    auto trap_at_retirement = [&](CycleInstructionSlot& slot) {
        const auto cause =
            slot.context.pending_exception.value_or(ExceptionCode::MisalignedFetch);
        const auto tval = slot.context.pending_tval;
        cancel_port(simrv::memory::TlPort::Instruction);
        cancel_port(simrv::memory::TlPort::Data);
        pipe.reset();
        ca_state.reset_instruction();
        raise_exception(static_cast<TrapCause>(cause), tval);
        pipe.fetch_pc = state_.pc;
        pipe.initialized = true;
    };
    auto can_resolve_dependency = [&](const CycleInstructionSlot& consumer,
                                      const CycleInstructionSlot& producer) {
        if (!has_raw_dependency(consumer, producer)) return true;
        if (!pipeline_sim.config.enable_forwarding) return false;
        const auto destination = producer.context.rd;
        const bool source1_ready =
            !pipeline::operation::reads_rs1(consumer.context.opcode) ||
            consumer.context.rs1 != destination ||
            forwarded_integer(producer, consumer.context.rs1).has_value();
        const bool source2_ready =
            !pipeline::operation::reads_rs2(consumer.context.opcode) ||
            consumer.context.rs2 != destination ||
            forwarded_integer(producer, consumer.context.rs2).has_value();
        return source1_ready && source2_ready;
    };
    auto apply_forwarding = [&](pipeline::PipelineContext& consumer) {
        if (!pipeline_sim.config.enable_forwarding) return;
        // M is younger than W and therefore wins if both target the same register.
        if (const auto value = forwarded_integer(pipe.writeback, consumer.rs1)) {
            consumer.rrs1 = *value;
        }
        if (const auto value = forwarded_integer(pipe.writeback, consumer.rs2)) {
            consumer.rrs2 = *value;
        }
        if (const auto value = forwarded_integer(pipe.memory, consumer.rs1)) {
            consumer.rrs1 = *value;
        }
        if (const auto value = forwarded_integer(pipe.memory, consumer.rs2)) {
            consumer.rrs2 = *value;
        }
    };

    // Retire first so all younger stages observe the architecturally committed state.
    auto& writeback = pipe.writeback;
    if (writeback.valid) {
        if (writeback.remaining_latency > 0) {
            --writeback.remaining_latency;
            return;
        }
        bool ready = true;
        if (writeback.serializing && !writeback.executed) {
            ready = run_with_context(writeback, [&] {
                fetch_operands(machine);
                if (pipeline_context.pending_exception.has_value()) return false;
                if (!execute_stage(machine)) return false;
                writeback.executed = true;
                return true;
            });
        }
        if (ready && (three_stage || writeback.serializing) && !writeback.memory_complete) {
            const auto misses_before = dcache.miss_count();
            ready = run_with_context(writeback, [&] {
                if (!memory_stage(machine)) return false;
                if (ca_state.waiting_for_interconnect) return false;
                writeback.memory_complete = true;
                return true;
            });
            if (dcache.miss_count() != misses_before) {
                writeback.dcache_miss = true;
            }
            if (!ready && ca_state.waiting_for_interconnect) return;
        }
        if (!ready || writeback.context.pending_exception.has_value()) {
            trap_at_retirement(writeback);
            return;
        }
        const bool retired = run_with_context(writeback, [&] {
            return writeback_stage(machine) && commit_stage(machine);
        });
        if (!retired) {
            trap_at_retirement(writeback);
            return;
        }
        const bool was_serializing = writeback.serializing;
        pipe.retired = writeback;
        writeback.clear();
        pipe.retired_this_cycle = true;
        ca_state.retired_this_cycle = true;
        if (was_serializing) {
            pipe.fetch_pc = state_.pc;
            pipe.frontend_blocked = false;
        }
    }

    // Memory stage owns the single per-hart data transaction until completion.
    auto& memory = pipe.memory;
    if (!three_stage && memory.valid && !writeback.valid) {
        if (memory.serializing) {
            writeback = std::move(memory);
            memory.clear();
        } else if (memory.remaining_latency > 0) {
            --memory.remaining_latency;
        } else {
            if (!memory.memory_complete) {
                const auto misses_before = dcache.miss_count();
                const bool success = run_with_context(memory, [&] { return memory_stage(machine); });
                if (dcache.miss_count() != misses_before) {
                    memory.dcache_miss = true;
                }
                if (ca_state.waiting_for_interconnect) return;
                memory.memory_complete = true;
                if (!success && !memory.context.pending_exception.has_value()) {
                    memory.context.pending_exception = ExceptionCode::FaultLoad;
                }
            }
            if (memory.remaining_latency == 0) {
                writeback = std::move(memory);
                memory.clear();
            }
        }
    }

    // Execute is side-effect-free for non-serializing instructions.
    auto& execute = pipe.execute;
    if (!three_stage && execute.valid && !memory.valid) {
        if (execute.remaining_latency > 0) {
            --execute.remaining_latency;
        } else {
            if (!execute.serializing && !execute.executed) {
                const Register saved_pc = state_.pc;
                state_.pc = execute.context.cpc;
                (void)run_with_context(execute, [&] { return execute_stage(machine); });
                state_.pc = saved_pc;
                execute.executed = true;

                if (is_control(execute.context)) {
                    const Address sequential =
                        execute.context.cpc + (execute.context.cinsn != 0u ? 2 : 4);
                    flush_to(execute.context.tkn ? execute.context.jmp_pc : sequential);
                }
            }
            memory = std::move(execute);
            execute.clear();
        }
    }

    // Decode waits for unresolved producers. This is conservative no-forwarding behavior;
    // forwarding is added by replacing these stalls with typed producer values.
    auto& decode = pipe.decode;
    if (decode.valid && !execute.valid) {
        const bool hazard = !can_resolve_dependency(decode, memory) ||
                            !can_resolve_dependency(decode, writeback);
        pipe.data_hazard_stall = hazard;
        const bool serial_wait = decode.serializing && (memory.valid || writeback.valid);
        if (!hazard && !serial_wait) {
            auto latency_minus_one = [](uint32_t latency) { return latency > 0 ? latency - 1 : 0; };
            if (three_stage) {
                if (!decode.executed && !decode.serializing) {
                    (void)run_with_context(decode, [&] {
                        fetch_operands(machine);
                        apply_forwarding(pipeline_context);
                        if (pipeline_context.pending_exception.has_value()) return false;
                        return execute_stage(machine);
                    });
                    decode.executed = true;
                    if (pipeline::operation::is_multiply(decode.context.op_id)) {
                        decode.remaining_latency =
                            latency_minus_one(pipeline_sim.config.mul_latency);
                    } else if (pipeline::operation::is_divide_or_remainder(decode.context.op_id)) {
                        decode.remaining_latency =
                            latency_minus_one(pipeline_sim.config.div_latency);
                    }
                    if (is_control(decode.context)) {
                        const Address sequential =
                            decode.context.cpc + (decode.context.cinsn != 0u ? 2 : 4);
                        // The resolving instruction itself occupies the combined ID/EX stage.
                        // Only the younger fetch-stage instruction is squashed.
                        pipe.fetch.clear();
                        pipe.control_flush = true;
                        pipe.fetch_pc = decode.context.tkn ? decode.context.jmp_pc : sequential;
                        cancel_port(simrv::memory::TlPort::Instruction);
                        ca_state.instruction_fill.reset();
                        ca_state.instruction_walk.reset();
                        pipe.frontend_blocked = false;
                    }
                }
                if (decode.remaining_latency > 0) {
                    --decode.remaining_latency;
                } else if (!writeback.valid) {
                    if (decode.serializing && decode.context.opcode == isa::Opcode::System) {
                        decode.remaining_latency = pipeline_sim.config.csr_flush_penalty;
                    } else if (decode.serializing &&
                               decode.context.opcode == isa::Opcode::MiscMem) {
                        decode.remaining_latency = pipeline_sim.config.fence_flush_penalty;
                    }
                    writeback = std::move(decode);
                    decode.clear();
                }
            } else {
                (void)run_with_context(decode, [&] {
                    fetch_operands(machine);
                    apply_forwarding(pipeline_context);
                    return !pipeline_context.pending_exception.has_value();
                });
                execute = std::move(decode);
                decode.clear();

                if (pipeline::operation::is_multiply(execute.context.op_id)) {
                    execute.remaining_latency = latency_minus_one(pipeline_sim.config.mul_latency);
                } else if (pipeline::operation::is_divide_or_remainder(execute.context.op_id)) {
                    execute.remaining_latency = latency_minus_one(pipeline_sim.config.div_latency);
                } else if (pipeline::operation::is_fp_divide_or_sqrt(execute.context.op_id)) {
                    execute.remaining_latency = latency_minus_one(pipeline_sim.config.fp_div_latency);
                } else if (pipeline::operation::is_fp_alu(execute.context.op_id)) {
                    execute.remaining_latency = latency_minus_one(pipeline_sim.config.fp_alu_latency);
                } else if (execute.context.opcode == isa::Opcode::System) {
                    execute.remaining_latency = pipeline_sim.config.csr_flush_penalty;
                } else if (execute.context.opcode == isa::Opcode::MiscMem) {
                    execute.remaining_latency = pipeline_sim.config.fence_flush_penalty;
                }
            }
        }
    }

    auto& fetch = pipe.fetch;
    if (fetch.valid && fetch.remaining_latency > 0) {
        --fetch.remaining_latency;
    }
    if (fetch.valid && fetch.remaining_latency == 0 && !decode.valid) {
        decode = std::move(fetch);
        fetch.clear();
    }

    if (!fetch.valid && !pipe.frontend_blocked) {
        const Register committed_pc = state_.pc;
        state_.pc = pipe.fetch_pc;
        std::swap(pipeline_context, fetch.context);
        const auto misses_before = icache.miss_count();
        const bool fetched = fetch_stage(machine, pipe.fetch_pc);
        const bool waiting = ca_state.waiting_for_interconnect;
        std::swap(pipeline_context, fetch.context);
        state_.pc = committed_pc;
        if (icache.miss_count() != misses_before) {
            ca_state.icache_miss = true;
        }
        if (waiting) return;

        fetch.valid = true;
        if (!fetched) {
            fetch.serializing = true;
            pipe.frontend_blocked = true;
        } else {
            std::swap(pipeline_context, fetch.context);
            decode_fields(machine);
            std::swap(pipeline_context, fetch.context);
            fetch.serializing = is_serializing(fetch.context);
            const Address width = fetch.context.cinsn != 0u ? 2 : 4;
            pipe.fetch_pc = fetch.context.cpc + width;
            pipe.frontend_blocked = fetch.serializing || is_control(fetch.context);
            fetch.icache_miss = ca_state.icache_miss;
            ca_state.icache_miss = false;
            fetch.tlb_miss = fetch.context.tlb_miss;
        }
    }
}

}  // namespace simrv::core
