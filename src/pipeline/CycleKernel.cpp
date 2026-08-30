#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/pipeline/OperationTraits.hpp"
#include "simrv/tui/Tui.hpp"

namespace simrv::core {

namespace {

using pipeline::CycleInstructionSlot;

[[nodiscard]] auto is_control(const pipeline::PipelineContext& context) -> bool {
    return context.opcode == isa::Opcode::Branch || context.opcode == isa::Opcode::Jal ||
           context.opcode == isa::Opcode::Jalr;
}

[[nodiscard]] auto is_serializing(const pipeline::PipelineContext& context) -> bool {
    const auto opcode = context.opcode;
    const bool vector =
        context.op_id >= isa::OperationId::VSETVLI && context.op_id <= isa::OperationId::VWSLL_VI;
    return vector || opcode == isa::Opcode::System || opcode == isa::Opcode::MiscMem;
}

[[nodiscard]] auto writes_integer(const pipeline::PipelineContext& context) -> bool {
    if (context.rd == RegId::Zero) return false;
    switch (context.opcode) {
        case isa::Opcode::Branch:
        case isa::Opcode::Store:
        case isa::Opcode::StoreFp:
        case isa::Opcode::MiscMem:
            return false;
        default:
            return !isa::is_destination_fp(context.opcode, context.op_id);
    }
}

[[nodiscard]] auto has_raw_dependency(const CycleInstructionSlot& consumer,
                                      const CycleInstructionSlot& producer) -> bool {
    if (!consumer.valid || !producer.valid || !producer.writes_int ||
        producer.wb_dest == RegId::Zero)
        return false;
    const auto destination = producer.wb_dest;
    return (pipeline::operation::reads_rs1(consumer.context.opcode) &&
            consumer.context.rs1 == destination) ||
           (pipeline::operation::reads_rs2(consumer.context.opcode) &&
            consumer.context.rs2 == destination);
}

[[nodiscard]] auto forwarded_integer(const CycleInstructionSlot& producer, RegId source)
    -> std::optional<Register> {
    if (!producer.valid || !producer.wb_valid || source == RegId::Zero ||
        producer.wb_dest != source) {
        return std::nullopt;
    }
    return producer.wb_val;
}

}  // namespace

void CPU::run_ca_pipeline_cycle(Machine& machine) {
    auto& pipe = ca_pipeline;
    const bool three_stage =
        pipeline_sim.config.pipeline_type == pipeline::PipelineType::ThreeStage;
    // The retired slot is presentation state: fast CA only consumes the retirement bit, while
    // observable snapshots and TUI tracing need the full instruction context.  Avoid copying a
    // complete PipelineContext on every retirement when neither consumer is active.
    const bool retain_retired_slot =
        pipeline_sim.config.record_snapshots &&
        (!machine.tui_enabled() ||
         (machine.tui_controller() && machine.tui_controller()->captures_execution_detail()));
    pipe.retired_this_cycle = false;
    if (retain_retired_slot) pipe.retired->clear();
    pipe.data_hazard_stall = false;
    pipe.control_flush = false;
    ca_state.retired_this_cycle = false;
    ca_state.waiting_for_interconnect = false;
    if (!pipe.initialized) {
        pipe.fetch_pc = state_.pc;
        pipe.initialized = true;
    }

    auto run_with_context = [&](CycleInstructionSlot& slot, auto&& operation) -> bool {
        active_context_ = &slot.context;
        const bool result = operation();
        active_context_ = &pipeline_context;
        return result;
    };
    auto cancel_port = [&](simrv::memory::TlPort port) {
        machine.memory_.system_bus().cancel_source(
            simrv::memory::make_tl_source(static_cast<HartId>(state_.mhartid), port));
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
        const auto cause = slot.context.pending_exception.value_or(ExceptionCode::MisalignedFetch);
        const auto tval = slot.context.pending_tval;
        state_.pc = slot.context.cpc.raw();
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
        const auto destination = producer.wb_dest;
        const bool source1_ready = !pipeline::operation::reads_rs1(consumer.context.opcode) ||
                                   consumer.context.rs1 != destination ||
                                   forwarded_integer(producer, consumer.context.rs1).has_value();
        const bool source2_ready = !pipeline::operation::reads_rs2(consumer.context.opcode) ||
                                   consumer.context.rs2 != destination ||
                                   forwarded_integer(producer, consumer.context.rs2).has_value();
        return source1_ready && source2_ready;
    };
    auto apply_forwarding = [&](pipeline::PipelineContext& consumer) {
        if (!pipeline_sim.config.enable_forwarding) return;
        // M is younger than W and therefore wins if both target the same register.
        if (const auto value = forwarded_integer(*pipe.writeback, consumer.rs1)) {
            consumer.rrs1 = *value;
        }
        if (const auto value = forwarded_integer(*pipe.writeback, consumer.rs2)) {
            consumer.rrs2 = *value;
        }
        if (!three_stage) {
            if (const auto value = forwarded_integer(*pipe.memory, consumer.rs1)) {
                consumer.rrs1 = *value;
            }
            if (const auto value = forwarded_integer(*pipe.memory, consumer.rs2)) {
                consumer.rrs2 = *value;
            }
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
                    if (writeback->context.opcode == isa::Opcode::Load) {
                        writeback->wb_valid = false;
                    } else if (writeback->writes_int && writeback->wb_dest != RegId::Zero) {
                        writeback->wb_val = (writeback->context.opcode == isa::Opcode::System &&
                                             writeback->context.funct3 != isa::Funct3::Priv)
                                                ? writeback->context.rcsr
                                                : writeback->context.wb_data;
                        writeback->wb_valid = true;
                    }
                    return true;
                });
            }
            if (ready && (three_stage || writeback->serializing) && !writeback->memory_complete) {
                const auto misses_before = dcache.miss_count();
                ready = run_with_context(*writeback, [&] {
                    if (!memory_stage(machine)) return false;
                    if (ca_state.waiting_for_interconnect) return false;
                    writeback->memory_complete = true;
                    if (writeback->context.opcode == isa::Opcode::Load) {
                        writeback->wb_val = writeback->context.mem_rdata;
                        writeback->wb_valid =
                            (writeback->writes_int && writeback->wb_dest != RegId::Zero);
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
            const bool retired = run_with_context(
                *writeback, [&] { return writeback_stage(machine) && commit_stage(machine); });
            if (!retired) {
                trap_at_retirement(*writeback);
                return;
            }
            const bool was_serializing = writeback->serializing;
            if (retain_retired_slot) *pipe.retired = *writeback;
            writeback->invalidate();
            pipe.retired_this_cycle = true;
            ca_state.retired_this_cycle = true;
            if (was_serializing) {
                pipe.fetch_pc = state_.pc;
                pipe.frontend_blocked = false;
            }
        }
    }

    // Memory stage owns the single per-hart data transaction until completion.
    auto* memory = pipe.memory;
    if (!three_stage && memory->valid && !pipe.writeback->valid) {
        if (memory->serializing) {
            std::swap(pipe.writeback, pipe.memory);
            pipe.memory->invalidate();
        } else if (memory->remaining_latency > 0) {
            --memory->remaining_latency;
        } else {
            if (!memory->memory_complete) {
                const auto misses_before = dcache.miss_count();
                const bool success =
                    run_with_context(*memory, [&] { return memory_stage(machine); });
                if (dcache.miss_count() != misses_before) {
                    memory->dcache_miss = true;
                }
                if (ca_state.waiting_for_interconnect) return;
                memory->memory_complete = true;
                if (memory->context.opcode == isa::Opcode::Load) {
                    memory->wb_val = memory->context.mem_rdata;
                    memory->wb_valid = (memory->writes_int && memory->wb_dest != RegId::Zero);
                }
                if (!success && !memory->context.pending_exception.has_value()) {
                    memory->context.pending_exception = ExceptionCode::FaultLoad;
                }
            }
            if (memory->remaining_latency == 0) {
                std::swap(pipe.writeback, pipe.memory);
                pipe.memory->invalidate();
            }
        }
    }

    // Execute is side-effect-free for non-serializing instructions.
    auto* execute = pipe.execute;
    if (!three_stage && execute->valid && !pipe.memory->valid) {
        if (execute->remaining_latency > 0) {
            --execute->remaining_latency;
        } else {
            if (!execute->serializing && !execute->executed) {
                const Register saved_pc = state_.pc;
                state_.pc = execute->context.cpc.raw();
                (void)run_with_context(*execute, [&] { return execute_stage(machine); });
                state_.pc = saved_pc;
                execute->executed = true;
                if (execute->context.opcode == isa::Opcode::Load) {
                    execute->wb_valid = false;
                } else if (execute->writes_int && execute->wb_dest != RegId::Zero) {
                    execute->wb_val = (execute->context.opcode == isa::Opcode::System &&
                                       execute->context.funct3 != isa::Funct3::Priv)
                                          ? execute->context.rcsr
                                          : execute->context.wb_data;
                    execute->wb_valid = true;
                }

                if (is_control(execute->context)) {
                    const Address sequential =
                        (execute->context.cpc + (execute->context.cinsn != 0u ? 2 : 4)).raw();
                    const Address target =
                        execute->context.tkn ? execute->context.jmp_pc : sequential;

                    const simrv::pipeline::BranchFeedback feedback{
                        .pc = execute->context.cpc.raw(),
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
                        execute->context.tkn && (target != execute->prediction.predicted_target);
                    const bool mispredict = dir_mispredict || target_mispredict;

                    if (mispredict || pipe.frontend_blocked) {
                        branch_predictor.restore_speculation(execute->prediction);
                        flush_to(target);
                    }
                }
            }
            std::swap(pipe.memory, pipe.execute);
            pipe.execute->invalidate();
        }
    }

    // Decode waits for unresolved producers. This is conservative no-forwarding behavior;
    // forwarding is added by replacing these stalls with typed producer values.
    auto* decode = pipe.decode;
    if (decode->valid && !pipe.execute->valid) {
        const bool hazard = !can_resolve_dependency(*decode, *pipe.memory) ||
                            !can_resolve_dependency(*decode, *pipe.writeback);
        pipe.data_hazard_stall = hazard;
        const bool serial_wait =
            decode->serializing && (pipe.memory->valid || pipe.writeback->valid);
        if (!hazard && !serial_wait) {
            auto latency_minus_one = [](uint32_t latency) { return latency > 0 ? latency - 1 : 0; };
            if (three_stage) {
                if (!decode->executed && !decode->serializing) {
                    (void)run_with_context(*decode, [&] {
                        fetch_operands(machine);
                        apply_forwarding(active_context());
                        if (active_context().pending_exception.has_value()) return false;
                        return execute_stage(machine);
                    });
                    decode->executed = true;
                    if (decode->context.opcode == isa::Opcode::Load) {
                        decode->wb_valid = false;
                    } else if (decode->writes_int && decode->wb_dest != RegId::Zero) {
                        decode->wb_val = (decode->context.opcode == isa::Opcode::System &&
                                          decode->context.funct3 != isa::Funct3::Priv)
                                             ? decode->context.rcsr
                                             : decode->context.wb_data;
                        decode->wb_valid = true;
                    }
                    if (pipeline::operation::is_multiply(decode->context.op_id)) {
                        decode->remaining_latency =
                            latency_minus_one(pipeline_sim.config.mul_latency);
                    } else if (pipeline::operation::is_divide_or_remainder(decode->context.op_id)) {
                        decode->remaining_latency =
                            latency_minus_one(pipeline_sim.config.div_latency);
                    }
                    if (is_control(decode->context)) {
                        const Address sequential =
                            (decode->context.cpc + (decode->context.cinsn != 0u ? 2 : 4)).raw();
                        const Address target =
                            decode->context.tkn ? decode->context.jmp_pc : sequential;

                        const simrv::pipeline::BranchFeedback feedback{
                            .pc = decode->context.cpc.raw(),
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
                            decode->context.tkn && (target != decode->prediction.predicted_target);
                        const bool mispredict = dir_mispredict || target_mispredict;

                        if (mispredict || pipe.frontend_blocked) {
                            branch_predictor.restore_speculation(decode->prediction);
                            // The resolving instruction itself occupies the combined ID/EX stage.
                            // Only the younger fetch-stage instruction is squashed.
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
            } else {
                (void)run_with_context(*decode, [&] {
                    fetch_operands(machine);
                    apply_forwarding(active_context());
                    return !active_context().pending_exception.has_value();
                });
                auto* ex_slot = pipe.decode;
                std::swap(pipe.execute, pipe.decode);
                pipe.decode->invalidate();

                if (pipeline::operation::is_multiply(ex_slot->context.op_id)) {
                    ex_slot->remaining_latency = latency_minus_one(pipeline_sim.config.mul_latency);
                } else if (pipeline::operation::is_divide_or_remainder(ex_slot->context.op_id)) {
                    ex_slot->remaining_latency = latency_minus_one(pipeline_sim.config.div_latency);
                } else if (pipeline::operation::is_fp_divide_or_sqrt(ex_slot->context.op_id)) {
                    ex_slot->remaining_latency =
                        latency_minus_one(pipeline_sim.config.fp_div_latency);
                } else if (pipeline::operation::is_fp_alu(ex_slot->context.op_id)) {
                    ex_slot->remaining_latency =
                        latency_minus_one(pipeline_sim.config.fp_alu_latency);
                } else if (ex_slot->context.opcode == isa::Opcode::System) {
                    ex_slot->remaining_latency = pipeline_sim.config.csr_flush_penalty;
                } else if (ex_slot->context.opcode == isa::Opcode::MiscMem) {
                    ex_slot->remaining_latency = pipeline_sim.config.fence_flush_penalty;
                }
            }
        }
    }

    auto* fetch = pipe.fetch;
    if (fetch->valid && fetch->remaining_latency > 0) {
        --fetch->remaining_latency;
    }
    if (fetch->valid && fetch->remaining_latency == 0 && !pipe.decode->valid) {
        std::swap(pipe.decode, pipe.fetch);
        pipe.fetch->invalidate();
        fetch = pipe.fetch;
    }

    if (!fetch->valid && !pipe.frontend_blocked) {
        const Register committed_pc = state_.pc;
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
            decode_fields(machine);
            fetch->serializing = is_serializing(fetch->context);
            fetch->writes_int = writes_integer(fetch->context);
            fetch->wb_dest = fetch->context.rd;
            fetch->wb_valid = false;
            fetch->wb_val = 0;
            const Address width = fetch->context.cinsn != 0u ? 2 : 4;
            const Address sequential = (fetch->context.cpc + width).raw();

            if (is_control(fetch->context)) {
                fetch->prediction =
                    branch_predictor.predict(fetch->context.cpc.raw(), fetch->context);
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
}

}  // namespace simrv::core
