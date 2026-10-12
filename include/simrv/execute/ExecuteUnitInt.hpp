/**
 * @file ExecuteUnitInt.hpp
 * @brief Canonical Base Integer (I), Multiply/Divide (M), Atomics (A), and CSR execution unit.
 */
#pragma once

#include <expected>

#include "simrv/isa/Amo.hpp"
#include "simrv/isa/InstructionFields.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::execute {

/**
 * @class ExecuteUnitInt
 * @brief Stateless arithmetic, branch, atomic, and CSR evaluation for integer pipeline stages.
 */
class ExecuteUnitInt {
   public:
    /// Execute integer ALU or M-extension operation.
    static auto aluInt(simrv::xlen::Register in1, simrv::xlen::Register in2, isa::OperationId op_id,
                       unsigned xlen = simrv::xlen::kXLenBits) -> simrv::xlen::Register;

    /// Execute RV64 W-class integer operations with 32-bit result semantics.
    static auto aluIntW(simrv::xlen::Register in1, simrv::xlen::Register in2,
                        isa::OperationId op_id) -> simrv::xlen::Register;

    /// Execute 32-bit integer arithmetic.
    static auto aluInt32(simrv::xlen::Register in1, simrv::xlen::Register in2,
                         isa::OperationId op_id) -> simrv::xlen::Register;

    /// Evaluate branch condition and return taken flag.
    static auto branchTaken(simrv::xlen::Register in1, simrv::xlen::Register in2,
                            isa::Funct3 funct3, unsigned xlen = simrv::xlen::kXLenBits) -> bool;

    /// Execute AMO arithmetic/logic result function.
    static auto aluAmo(simrv::xlen::Register in1, simrv::xlen::Register in2, isa::Funct5Amo funct5,
                       isa::Funct3 funct3) -> simrv::xlen::Register;

    /// Compute CSR write value for CSR instruction variants.
    static auto csrWriteValue(simrv::xlen::CsrValue rcsr, simrv::xlen::Register rrs1,
                              simrv::xlen::ImmValue imm, isa::Funct3 funct3)
        -> std::expected<simrv::xlen::CsrValue, simrv::xlen::TrapCause>;
};

}  // namespace simrv::execute
