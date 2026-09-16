#pragma once

#include "simrv/isa/Base.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/pipeline/DecodedInstruction.hpp"
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

[[nodiscard]] constexpr auto make_dependency_traits(isa::OperationId op_id, isa::Opcode opcode,
                                                    RegId rd, uint32_t funct5) noexcept
    -> DependencyTraits {
    DependencyTraits dt{};
    if (op_id != isa::UNKNOWN) {
        dt.writes_int = writes_integer(op_id) && (rd != RegId::Zero);
        dt.writes_fp = writes_float(op_id);
        dt.reads_rs1_int = is_rs1_int(op_id);
        dt.reads_rs2_int = is_rs2_int(op_id);
        dt.reads_rs1_fp = is_rs1_fp(op_id);
        dt.reads_rs2_fp = is_rs2_fp(op_id);
        dt.reads_rs3_fp = is_rs3_fp(op_id);
        const bool is_amo_load = (is_atomic(op_id) && op_id != isa::OperationId::SC_W &&
                                  op_id != isa::OperationId::SC_D);
        dt.is_mem_load = (is_load(op_id) || is_amo_load) && dt.writes_int;
        dt.is_control = is_control(op_id);
        const bool vector =
            op_id >= isa::OperationId::VSETVLI && op_id <= isa::OperationId::VWSLL_VI;
        dt.is_serializing = vector || is_serializing(op_id) || opcode == isa::Opcode::System ||
                            opcode == isa::Opcode::MiscMem;
        dt.is_cfu = is_cfu(op_id);
    } else {
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
            if (rd == RegId::Zero) return false;
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
                         (opcode == isa::Opcode::Amo &&
                          static_cast<isa::Funct5Amo>(funct5) != isa::Funct5Amo::Sc);
        dt.is_control = (opcode == isa::Opcode::Branch || opcode == isa::Opcode::Jal ||
                         opcode == isa::Opcode::Jalr);
        dt.is_serializing = (opcode == isa::Opcode::System || opcode == isa::Opcode::MiscMem);
    }
    return dt;
}

}  // namespace simrv::pipeline::operation
