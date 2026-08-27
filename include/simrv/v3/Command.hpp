// SPDX-License-Identifier: MIT
#pragma once

#include <optional>
#include <span>

namespace simrv::v3 {

/// Returns an exit code when argv is a v3 subcommand, otherwise nullopt for legacy handling.
[[nodiscard]] auto try_run_command(std::span<char* const> args) -> std::optional<int>;

}  // namespace simrv::v3
