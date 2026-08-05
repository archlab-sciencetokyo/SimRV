#pragma once

#include <expected>

#include "simrv/Define.hpp"

namespace simrv::core {
class CPU;
class Machine;
}  // namespace simrv::core

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
    /// Execute integer ALU or M-extension or B-extension operation.
    static auto aluInt(Register in1, Register in2, isa::OperationId op_id,
                       unsigned xlen = simrv::xlen::kXLenBits) -> Register;
    /// Execute RV64 W-class integer operations with 32-bit result semantics.
    static auto aluIntW(Register in1, Register in2, isa::OperationId op_id) -> Register;
    /// Evaluate branch condition and return taken flag.
    static auto branchTaken(Register in1, Register in2, isa::Funct3 funct3,
                            unsigned xlen = simrv::xlen::kXLenBits) -> bool;
    /// Execute AMO arithmetic/logic result function.
    static auto aluAmo(Register in1, Register in2, isa::Funct5Amo funct5, isa::Funct3 funct3)
        -> Register;
    /// Compute CSR write value for CSR instruction variants.
    static auto csrWriteValue(CSRValue rcsr, Register rrs1, ImmValue imm, isa::Funct3 funct3)
        -> std::expected<CSRValue, TrapCause>;
    /// Execute fused floating-point multiply-add family.
    static auto fusedFp(isa::Opcode opcode, Word fmt, Word rs1, Word rs2, Word rs3, Word rm,
                        const FloatingRegister* freg, CSRValue& fcsr) -> FpExecResult;
    /// Execute non-fused floating-point operations and conversions.
    static auto opFp(Word funct7, isa::Funct3 funct3, Word rs1, Word rs2, Register rrs1,
                     const FloatingRegister* freg, CSRValue& fcsr) -> FpExecResult;
    /// Execute vector instructions.
    static void execute_vector(core::CPU& cpu, core::Machine& machine, isa::OperationId op_id,
                               Instruction ir);

   private:
    static auto aluInt32(Register in1, Register in2, isa::OperationId op_id) -> Register;
    static auto aluIntB(Register in1, Register in2, isa::OperationId op_id,
                        unsigned xlen = simrv::xlen::kXLenBits) -> Register;
    static auto aluIntBW(Register in1, Register in2, isa::OperationId op_id) -> Register;

    static void execute_vector_config(core::CPU& cpu, isa::OperationId op_id, Instruction ir,
                                      RegId rd, RegId rs1, RegId rs2);

    static void execute_vector_memory(core::CPU& cpu, core::Machine& machine,
                                      isa::OperationId op_id, RegId rd, RegId rs1, RegId rs2,
                                      bool vm, uint32_t vl, uint32_t sew);

    static void execute_vector_integer(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs1,
                                       RegId rs2, bool vm, uint32_t vl, uint32_t sew,
                                       Register rs1_val, int32_t simm5);

    static void execute_vector_fixed_point(core::CPU& cpu, isa::OperationId op_id, RegId rd,
                                           RegId rs1, RegId rs2, bool vm, uint32_t vl, uint32_t sew,
                                           Register rs1_val, int32_t simm5);

    static void execute_vector_float(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs1,
                                     RegId rs2, bool vm, uint32_t vl, uint32_t sew);

    static void execute_vector_permute(core::CPU& cpu, isa::OperationId op_id, RegId rd, RegId rs1,
                                       RegId rs2, bool vm, uint32_t vl, uint32_t sew,
                                       Register rs1_val, int32_t simm5);
};

}  // namespace simrv::execute
