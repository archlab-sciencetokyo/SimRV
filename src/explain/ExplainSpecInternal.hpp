/**
 * @file ExplainSpecInternal.hpp
 * @brief Internal helpers for modular specification tables across scalar and vector extensions.
 */
#pragma once

#include <array>
#include <string_view>
#include <utility>

#include "simrv/isa/OperationId.hpp"

namespace simrv::explain::detail {

using OpDescTable =
    std::array<std::pair<std::string_view, std::string_view>, simrv::isa::kOperationIdCount>;

void populate_scalar_descriptions(OpDescTable& arr);
void populate_vector_descriptions(OpDescTable& arr);

}  // namespace simrv::explain::detail
