/**
 * @file ExecuteUnitZk.hpp
 * @brief RISC-V Scalar Cryptography (Zknd, Zkne, Zknh, Zksed, Zksh) execution unit.
 */
#pragma once

#include <cstdint>

#include "simrv/CompilerHints.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::execute {

/**
 * @class ExecuteUnitZk
 * @brief Stateless scalar cryptography execution engine.
 */
class ExecuteUnitZk {
   public:
    [[nodiscard]] static auto aluIntZk(simrv::xlen::Register in1, simrv::xlen::Register in2,
                                       isa::OperationId op_id,
                                       unsigned xlen = simrv::xlen::kXLenBits,
                                       uint32_t funct7 = 0) noexcept -> simrv::xlen::Register;

    static void set_current_funct7(uint32_t funct7) noexcept;
    [[nodiscard]] static auto get_current_funct7() noexcept -> uint32_t;
};

}  // namespace simrv::execute
