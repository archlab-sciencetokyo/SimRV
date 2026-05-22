#pragma once

#include <expected>

#include "simrv/Define.hpp"

namespace simrv::execute {

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
    static auto aluInt(Register in1, Register in2, Funct3 funct3, Instruction funct7)
        -> Register;
    /// Execute RV64 W-class integer operations with 32-bit result semantics.
    static auto aluIntW(Opcode opcode, Register in1, Register in2, Funct3 funct3,
                        Instruction funct7) -> Register;
    /// Evaluate branch condition and return taken flag.
    static auto branchTaken(Register in1, Register in2, Funct3 funct3) -> bool;
    /// Execute AMO arithmetic/logic result function.
    static auto aluAmo(Register in1, Register in2, Funct5Amo funct5, Funct3 funct3) -> Register;
    /// Compute CSR write value for CSR instruction variants.
    static auto csrWriteValue(CSRValue rcsr, Register rrs1, ImmValue imm, Funct3 funct3)
        -> std::expected<CSRValue, TrapCause>;
    /// Execute fused floating-point multiply-add family.
    static auto fusedFp(Opcode opcode, Word fmt, Word rs1, Word rs2, Word rs3, Word rm,
                        const FloatingRegister* freg, CSRValue& fcsr) -> FpExecResult;
    /// Execute non-fused floating-point operations and conversions.
    static auto opFp(Word funct7, Funct3 funct3, Word rs1, Word rs2, Register rrs1,
                     const FloatingRegister* freg, CSRValue& fcsr) -> FpExecResult;
};

}  // namespace simrv::execute
