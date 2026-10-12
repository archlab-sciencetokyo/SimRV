/**
 * @file ExecuteUnitZb.hpp
 * @brief Canonical Bitmanipulation execution unit (Zba, Zbb, Zbc, Zbs).
 */
#pragma once

#include "simrv/isa/OperationId.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::execute {

/**
 * @class ExecuteUnitZb
 * @brief Stateless bitmanipulation execution routines for RISC-V Zb standard extensions.
 */
class ExecuteUnitZb {
   public:
    /// Execute standard bit-manipulation operations across XLEN bits.
    static auto aluIntZb(simrv::xlen::Register in1, simrv::xlen::Register in2,
                         isa::OperationId op_id, unsigned xlen = simrv::xlen::kXLenBits)
        -> simrv::xlen::Register;

    /// Execute 32-bit W-class bit-manipulation operations on RV64.
    static auto aluIntZbW(simrv::xlen::Register in1, simrv::xlen::Register in2,
                          isa::OperationId op_id) -> simrv::xlen::Register;
};

/// Shorthand alias matching the traditional RISC-V B extension designation.
using ExecuteUnitB = ExecuteUnitZb;

}  // namespace simrv::execute
