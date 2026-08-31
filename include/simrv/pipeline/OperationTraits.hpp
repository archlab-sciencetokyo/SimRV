#pragma once

#include "simrv/isa/Base.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/pipeline/OperationInfo.hpp"

namespace simrv::pipeline::operation {

[[nodiscard]] constexpr auto is_multiply(isa::OperationId op) -> bool {
    return info(op).execution_class == ExecutionClass::Multiply;
}

[[nodiscard]] constexpr auto is_divide_or_remainder(isa::OperationId op) -> bool {
    return info(op).execution_class == ExecutionClass::DivideOrRemainder;
}

[[nodiscard]] constexpr auto is_fp_divide_or_sqrt(isa::OperationId op) -> bool {
    return info(op).execution_class == ExecutionClass::FpDivideOrSqrt;
}

[[nodiscard]] constexpr auto is_fp_alu(isa::OperationId op) -> bool {
    return info(op).execution_class == ExecutionClass::FpAlu;
}

[[nodiscard]] constexpr auto is_load(isa::Opcode opcode) -> bool {
    return opcode == isa::Opcode::Load || opcode == isa::Opcode::LoadFp;
}

[[nodiscard]] constexpr auto is_store(isa::Opcode opcode) -> bool {
    return opcode == isa::Opcode::Store || opcode == isa::Opcode::StoreFp;
}

[[nodiscard]] constexpr auto reads_rs1(isa::Opcode opcode) -> bool {
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

[[nodiscard]] constexpr auto reads_rs2(isa::Opcode opcode) -> bool {
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

[[nodiscard]] constexpr auto reads_rs3(isa::Opcode opcode) -> bool {
    return opcode == isa::Opcode::MAdd || opcode == isa::Opcode::MSub ||
           opcode == isa::Opcode::NMAdd || opcode == isa::Opcode::NMSub;
}

}  // namespace simrv::pipeline::operation
