/**
 * @file InspectionReport.hpp
 * @brief Deterministic, paused-state inspection report serialization.
 */
#pragma once

#include <expected>
#include <filesystem>
#include <span>
#include <string>

namespace simrv::core {
class Machine;
}

namespace simrv::tui {

enum class InspectionReportWriteStatus { Written, WouldOverwrite };

/// Build a versioned JSON report from a quiescent machine and its stable TUI telemetry.
[[nodiscard]] auto make_inspection_report(const simrv::core::Machine& machine, size_t hart,
                                          std::span<const std::string> recent_trace) -> std::string;

/// Write a report without replacing an existing file unless overwrite is explicitly true.
[[nodiscard]] auto write_inspection_report(const std::filesystem::path& path,
                                           std::string_view report, bool overwrite)
    -> std::expected<InspectionReportWriteStatus, std::string>;

}  // namespace simrv::tui
