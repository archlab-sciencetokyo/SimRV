#pragma once
#include <expected>

#include "simrv/isa/Amo.hpp"
#include "simrv/isa/Base.hpp"
#include "simrv/isa/Fp.hpp"
#include "simrv/isa/FpTypes.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/xlen/RegisterTypes.hpp"

namespace simrv::core {
class CPU;
class Machine;
}  // namespace simrv::core

namespace simrv::memory {
class MemorySubsystem;
}  // namespace simrv::memory

namespace simrv::execute {

/**
 * @struct FpExecResult
 * @brief Aggregated writeback signals produced by FP execution helpers.
 */
struct FpExecResult {
    simrv::xlen::Register int_wb_data = 0;
    simrv::xlen::FloatingRegister fp_wb_data = 0;
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
    static auto aluInt(simrv::xlen::Register in1, simrv::xlen::Register in2, isa::OperationId op_id,
                       unsigned xlen = simrv::xlen::kXLenBits) -> simrv::xlen::Register;
    /// Execute RV64 W-class integer operations with 32-bit result semantics.
    static auto aluIntW(simrv::xlen::Register in1, simrv::xlen::Register in2,
                        isa::OperationId op_id) -> simrv::xlen::Register;
    /// Evaluate branch condition and return taken flag.
    static auto branchTaken(simrv::xlen::Register in1, simrv::xlen::Register in2,
                            isa::Funct3 funct3, unsigned xlen = simrv::xlen::kXLenBits) -> bool;
    /// Execute AMO arithmetic/logic result function.
    static auto aluAmo(simrv::xlen::Register in1, simrv::xlen::Register in2, isa::Funct5Amo funct5,
                       isa::Funct3 funct3) -> simrv::xlen::Register;
    /// Compute CSR write value for CSR instruction variants.
    static auto csrWriteValue(simrv::xlen::CSRValue rcsr, simrv::xlen::Register rrs1,
                              simrv::xlen::ImmValue imm, isa::Funct3 funct3)
        -> std::expected<simrv::xlen::CSRValue, simrv::xlen::TrapCause>;
    /// Execute fused floating-point multiply-add family.
    static auto fusedFp(isa::Opcode opcode, FpFmt fmt, simrv::xlen::FpRegId rs1,
                        simrv::xlen::FpRegId rs2, simrv::xlen::FpRegId rs3, isa::RoundingMode rm,
                        const simrv::xlen::FloatingRegister* freg, simrv::xlen::CSRValue& fcsr)
        -> FpExecResult;
    static auto fusedFp(isa::Opcode opcode, simrv::xlen::Word fmt, simrv::xlen::Word rs1,
                        simrv::xlen::Word rs2, simrv::xlen::Word rs3, simrv::xlen::Word rm,
                        const simrv::xlen::FloatingRegister* freg, simrv::xlen::CSRValue& fcsr)
        -> FpExecResult {
        return fusedFp(opcode, static_cast<FpFmt>(fmt), static_cast<simrv::xlen::FpRegId>(rs1),
                       static_cast<simrv::xlen::FpRegId>(rs2),
                       static_cast<simrv::xlen::FpRegId>(rs3), static_cast<isa::RoundingMode>(rm),
                       freg, fcsr);
    }
    /// Execute non-fused floating-point operations and conversions.
    static auto opFp(simrv::xlen::Funct7 funct7, isa::Funct3 funct3, simrv::xlen::FpRegId rs1,
                     simrv::xlen::FpRegId rs2, simrv::xlen::Register rrs1,
                     const simrv::xlen::FloatingRegister* freg, simrv::xlen::CSRValue& fcsr)
        -> FpExecResult;
    static auto opFp(simrv::xlen::Word funct7, isa::Funct3 funct3, simrv::xlen::Word rs1,
                     simrv::xlen::Word rs2, simrv::xlen::Register rrs1,
                     const simrv::xlen::FloatingRegister* freg, simrv::xlen::CSRValue& fcsr)
        -> FpExecResult {
        return opFp(static_cast<simrv::xlen::Funct7>(funct7), funct3,
                    static_cast<simrv::xlen::FpRegId>(rs1), static_cast<simrv::xlen::FpRegId>(rs2),
                    rrs1, freg, fcsr);
    }
    /// Execute vector instructions.
    static void execute_vector(core::CPU& cpu, memory::MemorySubsystem& mem, isa::OperationId op_id,
                               simrv::xlen::Instruction ir);
    static void execute_vector(core::CPU& cpu, core::Machine& machine, isa::OperationId op_id,
                               simrv::xlen::Instruction ir);

   private:
    static auto aluInt32(simrv::xlen::Register in1, simrv::xlen::Register in2,
                         isa::OperationId op_id) -> simrv::xlen::Register;
    static auto aluIntZb(simrv::xlen::Register in1, simrv::xlen::Register in2,
                         isa::OperationId op_id, unsigned xlen = simrv::xlen::kXLenBits)
        -> simrv::xlen::Register;
    static auto aluIntZbW(simrv::xlen::Register in1, simrv::xlen::Register in2,
                          isa::OperationId op_id) -> simrv::xlen::Register;

    static void execute_vector_config(core::CPU& cpu, isa::OperationId op_id,
                                      simrv::xlen::Instruction ir, simrv::xlen::RegId rd,
                                      simrv::xlen::RegId rs1, simrv::xlen::RegId rs2);

    static void execute_vector_memory(core::CPU& cpu, memory::MemorySubsystem& mem,
                                      isa::OperationId op_id, simrv::xlen::RegId rd,
                                      simrv::xlen::RegId rs1, simrv::xlen::RegId rs2, bool vm,
                                      uint32_t vl, uint32_t sew, uint32_t nfields,
                                      uint32_t field_registers);

    [[nodiscard]] static auto execute_vector_integer(core::CPU& cpu, isa::OperationId op_id,
                                                     simrv::xlen::RegId rd, simrv::xlen::RegId rs1,
                                                     simrv::xlen::RegId rs2, bool vm, uint32_t vl,
                                                     uint32_t sew, simrv::xlen::Register rs1_val,
                                                     int32_t simm5) -> bool;

    static void execute_vector_fixed_point(core::CPU& cpu, isa::OperationId op_id,
                                           simrv::xlen::RegId rd, simrv::xlen::RegId rs1,
                                           simrv::xlen::RegId rs2, bool vm, uint32_t vl,
                                           uint32_t sew, simrv::xlen::Register rs1_val,
                                           int32_t simm5);

    static void execute_vector_float(core::CPU& cpu, isa::OperationId op_id, simrv::xlen::RegId rd,
                                     simrv::xlen::RegId rs1, simrv::xlen::RegId rs2, bool vm,
                                     uint32_t vl, uint32_t sew);

    static void execute_vector_permute(core::CPU& cpu, isa::OperationId op_id,
                                       simrv::xlen::RegId rd, simrv::xlen::RegId rs1,
                                       simrv::xlen::RegId rs2, bool vm, uint32_t vl, uint32_t sew,
                                       simrv::xlen::Register rs1_val, int32_t simm5);
};

}  // namespace simrv::execute
