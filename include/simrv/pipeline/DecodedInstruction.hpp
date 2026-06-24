#pragma once

#include <optional>
#include "simrv/Define.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::pipeline {

struct DecodedInstruction {
    Register cpc = 0;
    Instruction ir = 0;
    Instruction ir_org = 0;
    Instruction cinsn = 0;
    isa::OperationId op_id = isa::UNKNOWN;
    isa::Opcode opcode = static_cast<isa::Opcode>(0);
    RegId rd = static_cast<RegId>(0);
    RegId rs1 = static_cast<RegId>(0);
    RegId rs2 = static_cast<RegId>(0);
    isa::Funct3 funct3 = static_cast<isa::Funct3>(0);
    isa::Funct5Amo funct5 = static_cast<isa::Funct5Amo>(0);
    Word funct7 = 0;
    Word funct12 = 0;
    ImmValue imm = 0;
    std::optional<ExceptionCode> pending_exception;
    CSRValue pending_tval = 0;

    constexpr void copy_from(const DecodedInstruction& other) {
        *this = other;
    }
};

} // namespace simrv::pipeline
