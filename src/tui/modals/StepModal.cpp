/**
 * @file StepModal.cpp
 * @brief Implementation of StepSize and Speed simulation modal dialog handling.
 */
#include "simrv/tui/modals/StepModal.hpp"

#include <charconv>
#include <format>
#include "simrv/tui/TuiTheme.hpp"

namespace simrv::tui::modals {

void StepModal::open(ModalType type, std::string& input, uint64_t step_granularity,
                     uint64_t step_delay_us) {
    input.clear();
    if (type == ModalType::SetStepSize) {
        input = std::to_string(step_granularity);
    } else if (type == ModalType::SetSpeed) {
        uint64_t hz = step_delay_us > 0 ? (1000000 / step_delay_us) : 0;
        input = std::to_string(hz);
    }
}

auto StepModal::submit(ModalType type, const std::string& input,
                       std::atomic<uint64_t>& step_granularity,
                       std::atomic<uint64_t>& step_delay_us,
                       const std::function<void()>& on_speed_changed_cb,
                       const std::function<void(const std::string&)>& notice_cb) -> bool {
    if (input.empty()) {
        if (notice_cb) notice_cb("Invalid input: value cannot be empty");
        return false;
    }

    uint64_t val = 0;
    auto result = std::from_chars(input.data(), input.data() + input.size(), val);
    if (result.ec != std::errc{}) {
        if (notice_cb) notice_cb(std::format("Invalid numeric value: '{}'", input));
        return false;
    }

    if (type == ModalType::SetStepSize) {
        if (val > 0) {
            step_granularity.store(val, std::memory_order_relaxed);
        } else {
            if (notice_cb) notice_cb("Step size must be greater than 0");
            return false;
        }
    } else if (type == ModalType::SetSpeed) {
        uint64_t delay = (val > 0) ? (1000000 / val) : 0;
        step_delay_us.store(delay, std::memory_order_relaxed);
        if (on_speed_changed_cb) on_speed_changed_cb();
    }
    return true;
}

void StepModal::render(ModalType type, std::vector<std::string>& content_rows,
                        const std::string& input) {
    if (type == ModalType::SetStepSize) {
        content_rows.push_back(std::format("{}Enter Step Count N (instructions):\033[0m", kThemeText));
        content_rows.push_back(std::format("  \033[1m>\033[0m {}{}_\033[0m", kThemeMint, input));
    } else if (type == ModalType::SetSpeed) {
        content_rows.push_back(std::format("{}Enter Target Frequency (Hz, 0=Max):\033[0m", kThemeText));
        content_rows.push_back(std::format("  \033[1m>\033[0m {}{}_\033[0m", kThemeMint, input));
    }
}

}  // namespace simrv::tui::modals
