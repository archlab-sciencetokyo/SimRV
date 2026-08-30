#pragma once

#include "simrv/isa/Base.hpp"
#include "simrv/isa/OperationId.hpp"

namespace simrv::pipeline::operation {

[[nodiscard]] constexpr auto is_multiply(isa::OperationId op) -> bool {
    using enum isa::OperationId;
    switch (op) {
        case MUL:
        case MULH:
        case MULHSU:
        case MULHU:
        case MULW:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] constexpr auto is_divide_or_remainder(isa::OperationId op) -> bool {
    using enum isa::OperationId;
    switch (op) {
        case DIV:
        case DIVU:
        case REM:
        case REMU:
        case DIVW:
        case DIVUW:
        case REMW:
        case REMUW:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] constexpr auto is_fp_divide_or_sqrt(isa::OperationId op) -> bool {
    using enum isa::OperationId;
    switch (op) {
        case FDIV_S:
        case FSQRT_S:
        case FDIV_D:
        case FSQRT_D:
            return true;
        default:
            return false;
    }
}

[[nodiscard]] constexpr auto is_fp_alu(isa::OperationId op) -> bool {
    using enum isa::OperationId;
    switch (op) {
        case FADD_S:
        case FSUB_S:
        case FMUL_S:
        case FMADD_S:
        case FMSUB_S:
        case FNMADD_S:
        case FNMSUB_S:
        case FADD_D:
        case FSUB_D:
        case FMUL_D:
        case FMADD_D:
        case FMSUB_D:
        case FNMADD_D:
        case FNMSUB_D:
        case FMIN_S:
        case FMAX_S:
        case FMIN_D:
        case FMAX_D:
        case FSGNJ_S:
        case FSGNJN_S:
        case FSGNJX_S:
        case FSGNJ_D:
        case FSGNJN_D:
        case FSGNJX_D:
        case FCVT_S_D:
        case FCVT_D_S:
        case FCVT_W_S:
        case FCVT_WU_S:
        case FCVT_S_W:
        case FCVT_S_WU:
        case FCVT_L_S:
        case FCVT_LU_S:
        case FCVT_S_L:
        case FCVT_S_LU:
        case FCVT_W_D:
        case FCVT_WU_D:
        case FCVT_D_W:
        case FCVT_D_WU:
        case FCVT_L_D:
        case FCVT_LU_D:
        case FCVT_D_L:
        case FCVT_D_LU:
            return true;
        default:
            return false;
    }
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
            return true;
        default:
            return false;
    }
}

}  // namespace simrv::pipeline::operation
