/**
 * @file ExecuteUnit.cpp
 * @brief SimRV implementation unit.
 */
#include "ExecuteUnit.hpp"

#include "Module.hpp"

Register ExecuteUnit::aluInt(Register in1, Register in2, Instruction funct3,
                             Instruction funct7) const {
    return ALU_IM(in1, in2, funct3, funct7);
}

Instruction ExecuteUnit::branchTaken(Register in1, Register in2, Instruction funct3) const {
    return ALU_B(in1, in2, funct3);
}

Register ExecuteUnit::aluAmo(Register in1, Register in2, Instruction funct5) const {
    return ALU_A(in1, in2, funct5);
}

CSRValue ExecuteUnit::csrWriteValue(CSRValue rcsr, Register rrs1, Instruction imm,
                                    Instruction funct3) const {
    return ALU_C(rcsr, rrs1, imm, funct3);
}
