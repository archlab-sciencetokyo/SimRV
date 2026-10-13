/**
 * @file ExecuteUnitZk.hpp
 * @brief Canonical Scalar Cryptography execution unit (Zkn, Zks, Zk).
 */
#pragma once

#include "simrv/execute/ExecuteUnitZb.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/isa/Zk.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::execute {

/**
 * @class ExecuteUnitZk
 * @brief Stateless scalar cryptography execution routines for RISC-V Zk standard extensions.
 *
 * Reuses Zbkb/Zbkc building blocks implemented in ExecuteUnitZb and provides
 * algorithmic acceleration entry points.
 */
class ExecuteUnitZk {
   public:
    /// Query whether an operation belongs to the scalar crypto extension subset.
    [[nodiscard]] static constexpr auto is_crypto_operation(isa::OperationId op_id) noexcept
        -> bool {
        return isa::is_zkn_operation(op_id);
    }

    /// Execute crypto building block via Zb bitmanip evaluation.
    static auto aluCrypto(simrv::xlen::Register in1, simrv::xlen::Register in2,
                          isa::OperationId op_id, unsigned xlen = simrv::xlen::kXLenBits)
        -> simrv::xlen::Register {
        return ExecuteUnitZb::aluIntZb(in1, in2, op_id, xlen);
    }
};

/// Shorthand alias matching the traditional RISC-V K extension designation.
using ExecuteUnitK = ExecuteUnitZk;

}  // namespace simrv::execute
