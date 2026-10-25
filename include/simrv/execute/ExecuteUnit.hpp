/**
 * @file ExecuteUnit.hpp
 * @brief Unified execution unit facade aggregating modular extension subunits.
 */
#pragma once

#include "simrv/execute/CfuUnit.hpp"
#include "simrv/execute/ExecuteUnitFp.hpp"
#include "simrv/execute/ExecuteUnitInt.hpp"
#include "simrv/execute/ExecuteUnitVector.hpp"
#include "simrv/execute/ExecuteUnitZb.hpp"

namespace simrv::execute {

/**
 * @class ExecuteUnit
 * @brief Backward-compatible facade coordinating execution across all RISC-V extension subunits.
 */
class ExecuteUnit {
   public:
    static inline auto aluInt(simrv::xlen::Register in1, simrv::xlen::Register in2,
                              isa::OperationId op_id, unsigned xlen = simrv::xlen::kXLenBits)
        -> simrv::xlen::Register {
        return ExecuteUnitInt::aluInt(in1, in2, op_id, xlen);
    }

    static inline auto aluIntW(simrv::xlen::Register in1, simrv::xlen::Register in2,
                               isa::OperationId op_id) -> simrv::xlen::Register {
        return ExecuteUnitInt::aluIntW(in1, in2, op_id);
    }

    static inline auto aluInt32(simrv::xlen::Register in1, simrv::xlen::Register in2,
                                isa::OperationId op_id) -> simrv::xlen::Register {
        return ExecuteUnitInt::aluInt32(in1, in2, op_id);
    }

    static inline auto branchTaken(simrv::xlen::Register in1, simrv::xlen::Register in2,
                                   isa::Funct3 funct3, unsigned xlen = simrv::xlen::kXLenBits)
        -> bool {
        return ExecuteUnitInt::branchTaken(in1, in2, funct3, xlen);
    }

    static inline auto aluAmo(simrv::xlen::Register in1, simrv::xlen::Register in2,
                              isa::Funct5Amo funct5, isa::Funct3 funct3) -> simrv::xlen::Register {
        return ExecuteUnitInt::aluAmo(in1, in2, funct5, funct3);
    }

    static inline auto csrWriteValue(simrv::xlen::CsrValue rcsr, simrv::xlen::Register rrs1,
                                     simrv::xlen::ImmValue imm, isa::Funct3 funct3)
        -> std::expected<simrv::xlen::CsrValue, simrv::xlen::TrapCause> {
        return ExecuteUnitInt::csrWriteValue(rcsr, rrs1, imm, funct3);
    }

    static inline auto fusedFp(isa::Opcode opcode, FpFmt fmt, simrv::xlen::FpRegId rs1,
                               simrv::xlen::FpRegId rs2, simrv::xlen::FpRegId rs3,
                               isa::RoundingMode rm, const simrv::xlen::FloatingRegister* freg,
                               simrv::xlen::CsrValue& fcsr) -> FpExecResult {
        return ExecuteUnitFp::fusedFp(opcode, fmt, rs1, rs2, rs3, rm, freg, fcsr);
    }

    static inline auto opFp(simrv::xlen::Funct7 funct7, isa::Funct3 funct3,
                            simrv::xlen::FpRegId rs1, simrv::xlen::FpRegId rs2,
                            simrv::xlen::Register rrs1, const simrv::xlen::FloatingRegister* freg,
                            simrv::xlen::CsrValue& fcsr) -> FpExecResult {
        return ExecuteUnitFp::opFp(funct7, funct3, rs1, rs2, rrs1, freg, fcsr);
    }

    static inline auto aluIntZb(simrv::xlen::Register in1, simrv::xlen::Register in2,
                                isa::OperationId op_id, unsigned xlen = simrv::xlen::kXLenBits)
        -> simrv::xlen::Register {
        return ExecuteUnitZb::aluIntZb(in1, in2, op_id, xlen);
    }

    static inline auto aluIntZbW(simrv::xlen::Register in1, simrv::xlen::Register in2,
                                 isa::OperationId op_id) -> simrv::xlen::Register {
        return ExecuteUnitZb::aluIntZbW(in1, in2, op_id);
    }

    static inline void execute_vector(core::CPU& cpu, memory::MemorySubsystem& mem,
                                      isa::OperationId op_id, simrv::xlen::Instruction ir) {
        ExecuteUnitVector::execute_vector(cpu, mem, op_id, ir);
    }

    static inline void execute_vector(core::CPU& cpu, core::Machine& machine,
                                      isa::OperationId op_id, simrv::xlen::Instruction ir) {
        ExecuteUnitVector::execute_vector(cpu, machine, op_id, ir);
    }
};

}  // namespace simrv::execute
