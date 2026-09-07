/**
 * @file TuiMission.hpp
 * @brief External, non-assessed classroom missions for the TUI.
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/tui/TuiTypes.hpp"

namespace simrv::tui {

enum class MissionStatus : uint8_t { Inactive, Active, LoadError, ProgramMismatch, Complete };

struct MissionGuidance {
    std::string title;
    std::string prompt;
    std::string why;
    TuiRegPage destination = TuiRegPage::GPR;
    int glossary_topic = 0;
};

/// Tracks a versioned external classroom mission. It records no student identity or grades.
class MissionProgress {
   public:
    void configure(std::string_view mission_path, std::string_view binary_path);
    void restart();
    void dismiss() noexcept { status_.store(MissionStatus::Inactive, std::memory_order_relaxed); }
    void observe_symbol(std::string_view exact_symbol);

    [[nodiscard]] auto status() const noexcept -> MissionStatus {
        return status_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] auto enabled() const noexcept -> bool {
        return status() == MissionStatus::Active;
    }
    [[nodiscard]] auto completed() const noexcept -> bool {
        return status() == MissionStatus::Complete;
    }
    [[nodiscard]] auto step() const noexcept -> size_t {
        return step_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] auto guidance() const -> std::optional<MissionGuidance>;
    [[nodiscard]] auto detail() const noexcept -> std::string_view { return detail_; }

   private:
    struct MissionStep {
        std::string symbol;
        MissionGuidance guidance;
    };

    [[nodiscard]] auto load(std::string_view mission_path) -> bool;
    std::string mission_path_;
    std::string expected_program_;
    std::string detail_;
    std::vector<MissionStep> steps_;
    std::atomic<MissionStatus> status_{MissionStatus::Inactive};
    std::atomic<size_t> step_{0};
};

}  // namespace simrv::tui
