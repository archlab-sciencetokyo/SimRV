/**
 * @file InspectionReport.hpp
 * @brief Deterministic, paused-state inspection report serialization.
 */
#pragma once

#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace simrv::core {
class Machine;
}

namespace simrv::tui {

enum class InspectionReportWriteStatus { Written, WouldOverwrite };

struct MissionEvidence {
    std::string lesson_id;
    std::vector<std::string> reached_checkpoints;
    struct CheckpointDetail {
        std::string symbol;
        std::string message;
    };
    std::vector<CheckpointDetail> checkpoint_details;
};

/// Build a versioned JSON report from a quiescent machine and its stable TUI telemetry.
[[nodiscard]] auto make_inspection_report(const simrv::core::Machine& machine, size_t hart,
                                          std::span<const std::string> recent_trace,
                                          const MissionEvidence* mission = nullptr) -> std::string;

/// Write a report without replacing an existing file unless overwrite is explicitly true.
[[nodiscard]] auto write_inspection_report(const std::filesystem::path& path,
                                           std::string_view report, bool overwrite)
    -> std::expected<InspectionReportWriteStatus, std::string>;

}  // namespace simrv::tui
