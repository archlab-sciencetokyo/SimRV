/**
 * @file ExecuteUnit.hpp
 * @brief SimRV declarations.
 */
#pragma once

#include "Define.hpp"

struct FpExecResult {
    Register int_wb_data = 0;
    FloatingRegister fp_wb_data = 0;
    bool int_wb_enable = false;
    bool fp_wb_enable = false;
};

class ExecuteUnit {
   public:
    Register aluInt(Register in1, Register in2, Instruction funct3, Instruction funct7) const;
    Instruction branchTaken(Register in1, Register in2, Instruction funct3) const;
    Register aluAmo(Register in1, Register in2, Instruction funct5) const;
    CSRValue csrWriteValue(CSRValue rcsr, Register rrs1, Instruction imm, Instruction funct3) const;
    FpExecResult fusedFp(Opcode opcode, Word fmt, Word rs1, Word rs2, Word rs3, Word rm,
                         const FloatingRegister* freg, CSRValue& fcsr) const;
    FpExecResult opFp(Word funct7, Word funct3, Word rs2_field, Word rs1, Word rs2, Register rrs1,
                      const FloatingRegister* freg, CSRValue& fcsr) const;
};
