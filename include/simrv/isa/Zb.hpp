/**
 * @file Zb.hpp
 * @brief Bit-manipulation extension-family classification.
 */
#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "simrv/isa/OperationId.hpp"

namespace simrv::isa {

/** Return whether an operation belongs to the implemented Zb family. */
[[nodiscard]] constexpr auto is_zb_operation(OperationId op_id) noexcept -> bool {
    return is_zb_op(op_id);
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

/** Return whether a name is one of the standardized scalar Zb extensions or shorthand. */
[[nodiscard]] auto is_zb_extension(std::string_view name) -> bool;

/**
 * Add a standardized Zb extension to a preset extension list.
 *
 * The `zb` shorthand expands to the architectural set `zba,zbb,zbc,zbs`.
 * Names are normalized to lower case and duplicates are suppressed.
 */
void add_zb_extension(std::vector<std::string>& extensions, std::string_view name);

}  // namespace simrv::isa
