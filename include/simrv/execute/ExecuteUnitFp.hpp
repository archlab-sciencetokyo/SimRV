/**
 * @file ExecuteUnitFp.hpp
 * @brief Canonical Floating-Point execution unit (RV32F/RV64F single, RV32D/RV64D double).
 */
#pragma once

#include "simrv/isa/Fp.hpp"
#include "simrv/isa/FpTypes.hpp"
#include "simrv/isa/InstructionFields.hpp"
#include "simrv/xlen/RegisterTypes.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

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
 * @class ExecuteUnitFp
 * @brief Stateless floating-point arithmetic, conversion, and comparison helpers.
 */
class ExecuteUnitFp {
   public:
    /// Execute fused floating-point multiply-add family.
    static auto fusedFp(isa::Opcode opcode, FpFmt fmt, simrv::xlen::FpRegId rs1,
                        simrv::xlen::FpRegId rs2, simrv::xlen::FpRegId rs3, isa::RoundingMode rm,
                        const simrv::xlen::FloatingRegister* freg, simrv::xlen::CsrValue& fcsr)
        -> FpExecResult;

    /// Execute non-fused floating-point operations and conversions.
    static auto opFp(simrv::xlen::Funct7 funct7, isa::Funct3 funct3, simrv::xlen::FpRegId rs1,
                     simrv::xlen::FpRegId rs2, simrv::xlen::Register rrs1,
                     const simrv::xlen::FloatingRegister* freg, simrv::xlen::CsrValue& fcsr)
        -> FpExecResult;
};

}  // namespace simrv::execute
