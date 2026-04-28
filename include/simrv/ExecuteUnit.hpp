/**
 * @file ExecuteUnit.hpp
 * @brief Integer/FP execute helper interfaces.
 */
#pragma once

#include "Define.hpp"

/**
 * @struct FpExecResult
 * @brief Aggregated writeback signals produced by FP execution helpers.
 */
struct FpExecResult {
    Register int_wb_data = 0;
    FloatingRegister fp_wb_data = 0;
    bool int_wb_enable = false;
    bool fp_wb_enable = false;
};

/**
 * @class ExecuteUnit
 * @brief Stateless arithmetic and control helper routines used by EX stage.
 */
class ExecuteUnit {
   public:
    /// Execute integer ALU or M-extension operation.
    static auto aluInt(Register in1, Register in2, Instruction funct3, Instruction funct7)
        -> Register;
    /// Evaluate branch condition and return taken flag.
    static auto branchTaken(Register in1, Register in2, Instruction funct3) -> Instruction;
    /// Execute AMO arithmetic/logic result function.
    static auto aluAmo(Register in1, Register in2, Instruction funct5) -> Register;
    /// Compute CSR write value for CSR instruction variants.
    static auto csrWriteValue(CSRValue rcsr, Register rrs1, Instruction imm, Instruction funct3)
        -> CSRValue;
    /// Execute fused floating-point multiply-add family.
    static auto fusedFp(Opcode opcode, Word fmt, Word rs1, Word rs2, Word rs3, Word rm,
                        const FloatingRegister* freg, CSRValue& fcsr) -> FpExecResult;
    /// Execute non-fused floating-point operations and conversions.
    static auto opFp(Word funct7, Word funct3, Word rs2_field, Word rs1, Word rs2, Register rrs1,
                     const FloatingRegister* freg, CSRValue& fcsr) -> FpExecResult;
};
