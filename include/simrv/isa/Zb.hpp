/**
 * @file Zb.hpp
 * @brief Bit-manipulation extension-family classification.
 */
#pragma once

#include <utility>

#include "simrv/isa/OperationId.hpp"

namespace simrv::isa {

/** Return whether an operation belongs to the implemented Zb family. */
[[nodiscard]] constexpr auto is_zb_operation(OperationId op_id) noexcept -> bool {
    const auto value = std::to_underlying(op_id);
    return value >= std::to_underlying(OperationId::SH1ADD) &&
           value <= std::to_underlying(OperationId::PACKW);
}

/** Return whether an operation is a Zbkb/Zbkc building block used by the Zkn aggregate. */
[[nodiscard]] constexpr auto is_zb_crypto_operation(OperationId op_id) noexcept -> bool {
    switch (op_id) {
        case OperationId::CLMUL:
        case OperationId::CLMULH:
        case OperationId::CLMULR:
        case OperationId::PACK:
        case OperationId::PACKW:
            return true;
        default:
            return false;
    }
}

}  // namespace simrv::isa
