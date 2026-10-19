#pragma once

#include <array>

#include "simrv/CompilerHints.hpp"
#include "simrv/isa/InstructionFields.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/pipeline/DecodedInstruction.hpp"
#include "simrv/pipeline/OperationInfo.hpp"
#include "simrv/xlen/RegisterTypes.hpp"

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

[[nodiscard]] constexpr auto is_cfu(isa::OperationId op) noexcept -> bool {
    return info(op).execution_class == ExecutionClass::Cfu;
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

consteval auto generate_static_op_traits() {
    std::array<DependencyTraits, isa::kOperationIdCount> table{};
    for (size_t i = 0; i < isa::kOperationIdCount; ++i) {
        const auto op = static_cast<isa::OperationId>(i);
        const auto inf = info(op);
        DependencyTraits dt{};
        dt.writes_int = (inf.operands.rd == RegBank::Integer);
        dt.writes_fp = (inf.operands.rd == RegBank::Float);
        dt.writes_vec = (inf.operands.rd == RegBank::Vector);
        dt.reads_rs1_int = (inf.operands.rs1 == RegBank::Integer);
        dt.reads_rs2_int = (inf.operands.rs2 == RegBank::Integer);
        dt.reads_rs1_fp = (inf.operands.rs1 == RegBank::Float);
        dt.reads_rs2_fp = (inf.operands.rs2 == RegBank::Float);
        dt.reads_rs3_fp = (inf.operands.rs3 == RegBank::Float);
        const bool is_atomic = (inf.memory == MemoryAccessKind::Atomic);
        const bool is_load = (inf.memory == MemoryAccessKind::Load);
        const bool is_store = (inf.memory == MemoryAccessKind::Store);
        const bool is_amo_load =
            is_atomic && (op != isa::OperationId::SC_W && op != isa::OperationId::SC_D);
        dt.is_mem_load = (is_load || is_amo_load) && (inf.operands.rd == RegBank::Integer);
        dt.is_mem_store = is_store || (is_atomic && !is_load);
        dt.is_control = (inf.control != ControlFlowKind::None);
        const bool vector = (op >= isa::OperationId::VSETVLI && op <= isa::OperationId::VWSLL_VI);
        dt.is_serializing = vector || (inf.side_effects & SideEffectFlags::Serializing);
        dt.is_cfu = (inf.execution_class == ExecutionClass::Cfu);
        table[i] = dt;
    }
    return table;
}

inline constexpr auto kStaticOpTraitsTable = generate_static_op_traits();

[[nodiscard]] constexpr auto make_dependency_traits(isa::OperationId op_id, isa::Opcode opcode,
                                                    simrv::xlen::RegId rd,
                                                    isa::Funct5Amo funct5) noexcept
    -> DependencyTraits {
    if (simrv::compiler::likely(op_id != isa::UNKNOWN)) {
        const auto idx = static_cast<size_t>(op_id);
        if (simrv::compiler::likely(idx < kStaticOpTraitsTable.size())) {
            DependencyTraits dt = kStaticOpTraitsTable[idx];
            if (simrv::compiler::unlikely(rd == simrv::xlen::RegId::Zero)) {
                dt.writes_int = false;
                dt.is_mem_load = false;
            }
            if (simrv::compiler::unlikely(opcode == isa::Opcode::System ||
                                          opcode == isa::Opcode::MiscMem)) {
                dt.is_serializing = true;
            }
            return dt;
        }
    }

    DependencyTraits dt{};
    const bool fp_dst = (opcode == isa::Opcode::LoadFp || opcode == isa::Opcode::MAdd ||
                         opcode == isa::Opcode::MSub || opcode == isa::Opcode::NMAdd ||
                         opcode == isa::Opcode::NMSub || opcode == isa::Opcode::OpFp);
    const bool fp_rs1 = (opcode == isa::Opcode::OpFp || opcode == isa::Opcode::MAdd ||
                         opcode == isa::Opcode::MSub || opcode == isa::Opcode::NMSub ||
                         opcode == isa::Opcode::NMAdd);
    const bool fp_rs2 = (opcode == isa::Opcode::StoreFp || opcode == isa::Opcode::OpFp ||
                         opcode == isa::Opcode::MAdd || opcode == isa::Opcode::MSub ||
                         opcode == isa::Opcode::NMSub || opcode == isa::Opcode::NMAdd);
    const bool writes_int_calc = [&]() {
        if (rd == simrv::xlen::RegId::Zero) return false;
        switch (opcode) {
            case isa::Opcode::Branch:
            case isa::Opcode::Store:
            case isa::Opcode::StoreFp:
            case isa::Opcode::MiscMem:
                return false;
            default:
                return !fp_dst;
        }
    }();
    dt.writes_int = writes_int_calc;
    dt.writes_fp = fp_dst;
    dt.reads_rs1_int = reads_rs1(opcode) && !fp_rs1;
    dt.reads_rs2_int = reads_rs2(opcode) && !fp_rs2;
    dt.reads_rs1_fp = reads_rs1(opcode) && fp_rs1;
    dt.reads_rs2_fp = reads_rs2(opcode) && fp_rs2;
    dt.reads_rs3_fp = reads_rs3(opcode);
    dt.is_mem_load = (opcode == isa::Opcode::Load) ||
                     (opcode == isa::Opcode::Amo && funct5 != isa::Funct5Amo::Sc);
    dt.is_mem_store = (opcode == isa::Opcode::Store || opcode == isa::Opcode::StoreFp);
    dt.is_control = (opcode == isa::Opcode::Branch || opcode == isa::Opcode::Jal ||
                     opcode == isa::Opcode::Jalr);
    dt.is_serializing = (opcode == isa::Opcode::System || opcode == isa::Opcode::MiscMem);
    return dt;
}

[[nodiscard]] constexpr auto make_dependency_traits(isa::OperationId op_id, isa::Opcode opcode,
                                                    simrv::xlen::RegId rd, uint32_t funct5) noexcept
    -> DependencyTraits {
    return make_dependency_traits(op_id, opcode, rd, static_cast<isa::Funct5Amo>(funct5));
}

}  // namespace simrv::pipeline::operation
