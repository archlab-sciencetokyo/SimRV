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
    ::OperationId op_id = ::UNKNOWN;
    Opcode opcode = static_cast<Opcode>(0);
    RegId rd = static_cast<RegId>(0);
    RegId rs1 = static_cast<RegId>(0);
    RegId rs2 = static_cast<RegId>(0);
    Funct3 funct3 = static_cast<Funct3>(0);
    Funct5Amo funct5 = static_cast<Funct5Amo>(0);
    Word funct7 = 0;
    Word funct12 = 0;
    ImmValue imm = 0;
    std::optional<ExceptionCode> pending_exception;
    CSRValue pending_tval = 0;
};

} // namespace simrv::pipeline
