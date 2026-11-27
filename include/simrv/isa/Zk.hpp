/**
 * @file Zk.hpp
 * @brief Scalar cryptography extension naming and operation integration.
 */
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "simrv/isa/OperationId.hpp"
#include "simrv/isa/Zb.hpp"

namespace simrv::isa {

/** Return whether a name is one of the standardized scalar Zk extensions. */
[[nodiscard]] auto is_zk_extension(std::string_view name) -> bool;

/**
 * Add a standardized Zk extension to a preset extension list.
 *
 * The `zk` shorthand expands to the architectural aggregate `zkn,zkr,zkt`.
 * Names are normalized to lower case and duplicates are suppressed.
 */
void add_zk_extension(std::vector<std::string>& extensions, std::string_view name);

/** Return whether the operation is implemented as part of the current Zkn subset. */
[[nodiscard]] auto is_zkn_operation(OperationId op_id) noexcept -> bool;

}  // namespace simrv::isa
