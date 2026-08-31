#pragma once

#include "simrv/Define.hpp"
#include "simrv/pipeline/PipelineContext.hpp"

namespace simrv::pipeline {

enum class RegisterBank : uint8_t { Integer, FloatingPoint };

struct RegisterWriteEffect {
    RegisterBank bank = RegisterBank::Integer;
    RegId destination = RegId::Zero;
    uint64_t value = 0;
    bool enabled = false;
};

/// Architectural changes produced by the writeback stage, before they are applied.
struct WritebackEffects {
    RegisterWriteEffect integer_write{};
    RegisterWriteEffect floating_write{.bank = RegisterBank::FloatingPoint};
    bool increments_instruction_count = false;
    bool marks_floating_point_dirty = false;
};

// Called once for every retired instruction.  Keeping the small classification in the caller
// avoids a hot out-of-line call in both IA and CA writeback paths.
[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto build_writeback_effects(
    const PipelineContext& context) -> WritebackEffects {
    WritebackEffects effects{};
    if (context.pending_exception.has_value()) return effects;

    effects.increments_instruction_count = true;
    if (context.op_id >= isa::OperationId::VSETVLI && context.op_id <= isa::OperationId::VWSLL_VI) {
        return effects;
    }

    const auto opcode = static_cast<isa::Opcode>(context.opcode);
    const auto funct5 = static_cast<isa::Funct5Amo>(context.funct5);
    const auto funct3 = static_cast<isa::Funct3>(context.funct3);

    auto& integer = effects.integer_write;
    integer.destination = context.rd;
    if (opcode == isa::Opcode::Load ||
        (opcode == isa::Opcode::Amo && funct5 != isa::Funct5Amo::Sc)) {
        integer.value = context.mem_rdata;
        integer.enabled = true;
    } else if (opcode == isa::Opcode::System && funct3 != isa::Funct3::Priv) {
        integer.value = context.rcsr;
        integer.enabled = true;
    } else if ((opcode == isa::Opcode::Amo && funct5 == isa::Funct5Amo::Sc) ||
               opcode == isa::Opcode::Lui || opcode == isa::Opcode::Auipc ||
               opcode == isa::Opcode::Jal || opcode == isa::Opcode::Jalr ||
               opcode == isa::Opcode::Op || opcode == isa::Opcode::OpImm ||
               opcode == isa::Opcode::Op32 || opcode == isa::Opcode::OpImm32 ||
               (opcode == isa::Opcode::OpFp && context.int_wb_from_fp)) {
        integer.value = context.wb_data;
        integer.enabled = true;
    }

    auto& floating = effects.floating_write;
    floating.destination = context.rd;
    if (opcode == isa::Opcode::LoadFp) {
        floating.value = context.fp_mem_rdata;
        floating.enabled = true;
    } else if ((opcode == isa::Opcode::OpFp || opcode == isa::Opcode::MAdd ||
                opcode == isa::Opcode::MSub || opcode == isa::Opcode::NMAdd ||
                opcode == isa::Opcode::NMSub) &&
               context.fp_wb_enable) {
        floating.value = context.fp_wb_data;
        floating.enabled = true;
    }
    effects.marks_floating_point_dirty = floating.enabled;
    return effects;
}

}  // namespace simrv::pipeline
