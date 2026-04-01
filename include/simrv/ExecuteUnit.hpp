/**
 * @file ExecuteUnit.hpp
 * @brief SimRV declarations.
 */
#pragma once

#include "Define.hpp"

class ExecuteUnit {
   public:
    Register aluInt(Register in1, Register in2, Instruction funct3, Instruction funct7) const;
    Instruction branchTaken(Register in1, Register in2, Instruction funct3) const;
    Register aluAmo(Register in1, Register in2, Instruction funct5) const;
    CSRValue csrWriteValue(CSRValue rcsr, Register rrs1, Instruction imm, Instruction funct3) const;
};
