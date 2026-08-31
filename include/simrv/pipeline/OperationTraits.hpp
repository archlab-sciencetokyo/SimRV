#pragma once

#include "simrv/isa/Base.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/pipeline/OperationInfo.hpp"

namespace simrv::pipeline::operation {

[[nodiscard]] constexpr auto is_multiply(isa::OperationId op) noexcept -> bool {
    return info(op).execution_class == ExecutionClass::Multiply;
}

[[nodiscard]] constexpr auto is_divide_or_remainder(isa::OperationId op) noexcept -> bool {
    return info(op).execution_class == ExecutionClass::DivideOrRemainder;
}

[[nodiscard]] constexpr auto is_fp_divide_or_sqrt(isa::OperationId op) noexcept -> bool {
    return info(op).execution_class == ExecutionClass::FpDivideOrSqrt;
}

[[nodiscard]] constexpr auto is_fp_alu(isa::OperationId op) noexcept -> bool {
    return info(op).execution_class == ExecutionClass::FpAlu;
}

[[nodiscard]] constexpr auto rd_bank(isa::OperationId op) noexcept -> RegBank {
    return info(op).operands.rd;
}

[[nodiscard]] constexpr auto rs1_bank(isa::OperationId op) noexcept -> RegBank {
    return info(op).operands.rs1;
}

[[nodiscard]] constexpr auto rs2_bank(isa::OperationId op) noexcept -> RegBank {
    return info(op).operands.rs2;
}

[[nodiscard]] constexpr auto rs3_bank(isa::OperationId op) noexcept -> RegBank {
    return info(op).operands.rs3;
}

[[nodiscard]] constexpr auto writes_rd(isa::OperationId op) noexcept -> bool {
    return rd_bank(op) != RegBank::None;
}

[[nodiscard]] constexpr auto writes_integer(isa::OperationId op) noexcept -> bool {
    return rd_bank(op) == RegBank::Integer;
}

[[nodiscard]] constexpr auto writes_float(isa::OperationId op) noexcept -> bool {
    return rd_bank(op) == RegBank::Float;
}

[[nodiscard]] constexpr auto writes_vector(isa::OperationId op) noexcept -> bool {
    return rd_bank(op) == RegBank::Vector;
}

[[nodiscard]] constexpr auto reads_rs1(isa::OperationId op) noexcept -> bool {
    return rs1_bank(op) != RegBank::None;
}

[[nodiscard]] constexpr auto reads_rs2(isa::OperationId op) noexcept -> bool {
    return rs2_bank(op) != RegBank::None;
}

[[nodiscard]] constexpr auto reads_rs3(isa::OperationId op) noexcept -> bool {
    return rs3_bank(op) != RegBank::None;
}

[[nodiscard]] constexpr auto is_rs1_int(isa::OperationId op) noexcept -> bool {
    return rs1_bank(op) == RegBank::Integer;
}

[[nodiscard]] constexpr auto is_rs2_int(isa::OperationId op) noexcept -> bool {
    return rs2_bank(op) == RegBank::Integer;
}

[[nodiscard]] constexpr auto is_rs3_int(isa::OperationId op) noexcept -> bool {
    return rs3_bank(op) == RegBank::Integer;
}

[[nodiscard]] constexpr auto is_rs1_fp(isa::OperationId op) noexcept -> bool {
    return rs1_bank(op) == RegBank::Float;
}

[[nodiscard]] constexpr auto is_rs2_fp(isa::OperationId op) noexcept -> bool {
    return rs2_bank(op) == RegBank::Float;
}

[[nodiscard]] constexpr auto is_rs3_fp(isa::OperationId op) noexcept -> bool {
    return rs3_bank(op) == RegBank::Float;
}

[[nodiscard]] constexpr auto is_load(isa::OperationId op) noexcept -> bool {
    return info(op).memory == MemoryAccessKind::Load;
}

[[nodiscard]] constexpr auto is_store(isa::OperationId op) noexcept -> bool {
    return info(op).memory == MemoryAccessKind::Store;
}

[[nodiscard]] constexpr auto is_atomic(isa::OperationId op) noexcept -> bool {
    return info(op).memory == MemoryAccessKind::Atomic;
}

[[nodiscard]] constexpr auto is_memory(isa::OperationId op) noexcept -> bool {
    return info(op).memory != MemoryAccessKind::None;
}

[[nodiscard]] constexpr auto is_control(isa::OperationId op) noexcept -> bool {
    return info(op).control != ControlFlowKind::None;
}

[[nodiscard]] constexpr auto is_branch(isa::OperationId op) noexcept -> bool {
    return info(op).control == ControlFlowKind::Branch;
}

[[nodiscard]] constexpr auto is_jump(isa::OperationId op) noexcept -> bool {
    return info(op).control == ControlFlowKind::Jump || info(op).control == ControlFlowKind::Jalr;
}

[[nodiscard]] constexpr auto is_serializing(isa::OperationId op) noexcept -> bool {
    return info(op).side_effects & SideEffectFlags::Serializing;
}

[[nodiscard]] constexpr auto has_side_effects(isa::OperationId op) noexcept -> bool {
    return info(op).side_effects != SideEffectFlags::None;
}

// Opcode-level query fallbacks for raw opcodes
[[nodiscard]] constexpr auto is_load(isa::Opcode opcode) noexcept -> bool {
    return opcode == isa::Opcode::Load || opcode == isa::Opcode::LoadFp;
}

[[nodiscard]] constexpr auto is_store(isa::Opcode opcode) noexcept -> bool {
    return opcode == isa::Opcode::Store || opcode == isa::Opcode::StoreFp;
}

[[nodiscard]] constexpr auto reads_rs1(isa::Opcode opcode) noexcept -> bool {
    using enum isa::Opcode;
    switch (opcode) {
        case Op:
        case Op32:
        case OpImm:
        case OpImm32:
        case Load:
        case LoadFp:
        case Store:
        case StoreFp:
        case Branch:
        case Jalr:
        case System:
        case Amo:
        case OpFp:
        case MAdd:
        case MSub:
        case NMAdd:
        case NMSub:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] constexpr auto reads_rs2(isa::Opcode opcode) noexcept -> bool {
    using enum isa::Opcode;
    switch (opcode) {
        case Op:
        case Op32:
        case Store:
        case StoreFp:
        case Branch:
        case Amo:
        case OpFp:
        case MAdd:
        case MSub:
        case NMAdd:
        case NMSub:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] constexpr auto reads_rs3(isa::Opcode opcode) noexcept -> bool {
    return opcode == isa::Opcode::MAdd || opcode == isa::Opcode::MSub ||
           opcode == isa::Opcode::NMAdd || opcode == isa::Opcode::NMSub;
}

}  // namespace simrv::pipeline::operation
