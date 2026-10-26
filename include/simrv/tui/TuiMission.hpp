/**
 * @file TuiMission.hpp
 * @brief External, non-assessed classroom missions for the TUI.
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/tui/TuiTypes.hpp"

namespace simrv::tui {

enum class MissionStatus : uint8_t { Inactive, Active, LoadError, ProgramMismatch, Complete };

struct MissionGuidance {
    std::string title;
    std::string learn;
    std::string prompt;
    std::string hint;
    std::string why;
    TuiRegPage destination = TuiRegPage::GPR;
    std::vector<TuiRegPage> panels;
    int glossary_topic = 0;
};

struct MissionCheckpointProgress {
    std::string symbol;
    std::string message;
    size_t after_step = 0;
    bool reached = false;
};

/// Tracks a versioned external classroom mission. It records no student identity or grades.
class MissionProgress {
   public:
    void configure(std::string_view mission_path, std::string_view binary_path);
    void restart();
    void dismiss() noexcept {
        status_.store(MissionStatus::Inactive, std::memory_order_relaxed);
        checkpoint_reached_.store(false, std::memory_order_relaxed);
        revision_.fetch_add(1, std::memory_order_relaxed);
    }
    void observe_symbol(std::string_view exact_symbol);
    [[nodiscard]] auto advance_step() -> bool;
    [[nodiscard]] auto can_advance_step() const noexcept -> bool;
    [[nodiscard]] auto previous_step() -> bool;
    [[nodiscard]] auto can_previous_step() const noexcept -> bool;
    [[nodiscard]] auto checkpoint_reached() const noexcept -> bool {
        return checkpoint_reached_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] auto revision() const noexcept -> uint64_t {
        return revision_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] auto status() const noexcept -> MissionStatus {
        return status_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] auto enabled() const noexcept -> bool {
        return status() == MissionStatus::Active;
    }
    [[nodiscard]] auto completed() const noexcept -> bool {
        return status() == MissionStatus::Complete;
    }
    [[nodiscard]] auto is_final_step() const noexcept -> bool {
        return enabled() && step() + 1 >= steps_.size();
    }
    [[nodiscard]] auto step() const noexcept -> size_t {
        return step_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] auto step_count() const noexcept -> size_t { return steps_.size(); }
    [[nodiscard]] auto speed_hz() const noexcept -> uint64_t;
    [[nodiscard]] auto guidance() const -> std::optional<MissionGuidance>;
    [[nodiscard]] auto detail() const noexcept -> std::string_view { return detail_; }
    [[nodiscard]] auto description() const noexcept -> std::string_view { return description_; }
    [[nodiscard]] auto goal() const noexcept -> std::string_view { return goal_; }
    [[nodiscard]] auto lesson_id() const noexcept -> std::string_view { return lesson_id_; }
    [[nodiscard]] auto reached_checkpoint_events() const -> std::vector<std::string>;
    [[nodiscard]] auto checkpoint_symbol() const noexcept -> std::string_view;
    [[nodiscard]] auto checkpoint_message() const noexcept -> std::string_view;
    [[nodiscard]] auto checkpoint_progress() const -> std::optional<MissionCheckpointProgress>;
    [[nodiscard]] auto checkpoints_progress() const -> std::vector<MissionCheckpointProgress>;
    [[nodiscard]] auto checkpoint_count() const noexcept -> size_t { return checkpoints_.size(); }
    [[nodiscard]] auto reached_checkpoint_count() const -> size_t;

   private:
    struct MissionStep {
        std::string id;
        MissionGuidance guidance;
        std::optional<uint64_t> speed_hz;
    };
    struct MissionCheckpoint {
        std::string symbol;
        std::string message;
        size_t after_step = 0;
    };

    [[nodiscard]] auto load(std::string_view mission_path) -> bool;
    std::string mission_path_;
    std::string lesson_id_;
    std::string expected_program_;
    std::string description_;
    std::string goal_;
    std::string detail_;
    uint64_t speed_hz_ = 30;
    std::vector<MissionStep> steps_;
    std::vector<MissionCheckpoint> checkpoints_;
    mutable std::mutex checkpoint_events_mutex_;
    std::vector<std::string> checkpoint_events_;
    std::atomic<MissionStatus> status_{MissionStatus::Inactive};
    std::atomic<size_t> step_{0};
    std::atomic<bool> checkpoint_reached_{false};
    std::atomic<uint64_t> revision_{0};
};

}  // namespace simrv::tui
