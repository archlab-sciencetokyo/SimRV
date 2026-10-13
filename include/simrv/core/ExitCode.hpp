/**
 * @file ExitCode.hpp
 * @brief Canonical host-level process exit codes for SimRV.
 */
#pragma once

#include <cstdint>

namespace simrv::core {

/**
 * @brief Host exit codes returned to the OS environment.
 *
 * Distinguishes host-level CLI usage, configuration, or initialization errors
 * from guest simulation outcomes. When a guest target terminates cleanly via
 * sys_exit or SBI poweroff, the guest process status is passed through directly.
 */
enum class HostExitCode : int {
    Success = 0,
    GeneralError = 1,
    UsageError = 2,
    ConfigurationError = 3,
    InitializationError = 4,
    ExecutionError = 5,
    CheckpointError = 6,
    IpcError = 7
};

[[nodiscard]] constexpr auto to_exit_code(HostExitCode code) noexcept -> int {
    return static_cast<int>(code);
}

}  // namespace simrv::core
