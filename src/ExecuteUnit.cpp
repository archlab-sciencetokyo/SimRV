/**
 * @file ExecuteUnit.cpp
 * @brief SimRV implementation unit.
 */
#include "ExecuteUnit.hpp"

uint32_t ExecuteUnit::aluInt(uint32_t in1, uint32_t in2, uint32_t funct3, uint32_t funct7) const {
    return ALU_IM(in1, in2, funct3, funct7);
}

uint32_t ExecuteUnit::branchTaken(uint32_t in1, uint32_t in2, uint32_t funct3) const {
    return ALU_B(in1, in2, funct3);
}

uint32_t ExecuteUnit::aluAmo(uint32_t in1, uint32_t in2, uint32_t funct5) const {
    return ALU_A(in1, in2, funct5);
}

uint32_t ExecuteUnit::csrWriteValue(uint32_t rcsr, uint32_t rrs1, uint32_t imm,
                                    uint32_t funct3) const {
    return ALU_C(rcsr, rrs1, imm, funct3);
}
