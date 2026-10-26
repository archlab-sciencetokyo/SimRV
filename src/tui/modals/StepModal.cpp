/**
 * @file StepModal.cpp
 * @brief Implementation of StepSize and Speed simulation modal dialog handling.
 */
#include "simrv/tui/modals/StepModal.hpp"

#include <charconv>
#include <format>

#include "simrv/tui/modals/ModalComponents.hpp"

namespace simrv::tui::modals {

void StepModal::render_content(std::vector<std::string>& content_rows, int /*width*/,
                               int /*height*/) {
    render(ModalType::SetSpeed, content_rows, input_);
}

auto StepModal::handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction {
    if (byte == 27 || key == simrv::tui::TuiKey::Esc) {
        return framework::ModalAction::Close;
    }
    if (key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline) {
        return framework::ModalAction::Submit;
    }
    if (byte == 8 || byte == 127 || key == simrv::tui::TuiKey::Backspace) {
        if (!input_.empty()) input_.pop_back();
        return framework::ModalAction::Continue;
    }
    if (byte >= 32 && byte <= 126) {
        input_.push_back(static_cast<char>(byte));
        return framework::ModalAction::Continue;
    }
    return framework::ModalAction::Continue;
}

auto StepModal::handle_control_click(int /*content_row*/, size_t action_index)
    -> framework::ModalAction {
    return action_index == 0 ? framework::ModalAction::Submit : framework::ModalAction::Close;
}

auto StepModal::handle_click(framework::ScreenCoord local_pt) -> framework::ModalAction {
    (void)local_pt;
    return framework::ModalAction::Continue;
}

void StepModal::open(ModalType type, std::string& input, uint64_t step_delay_us) {
    input.clear();
    if (type == ModalType::SetSpeed) {
        uint64_t hz = step_delay_us > 0 ? ((1'000'000 + step_delay_us / 2) / step_delay_us) : 0;
        input = std::to_string(hz);
    }
}

auto StepModal::submit(ModalType type, const std::string& input,
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

    if (type == ModalType::SetSpeed) {
        uint64_t delay = (val > 0) ? (1 + (999'999 / val)) : 0;
        step_delay_us.store(delay, std::memory_order_relaxed);
        if (on_speed_changed_cb) on_speed_changed_cb();
    }
    return true;
}

void StepModal::render(ModalType type, std::vector<std::string>& content_rows,
                       const std::string& input) {
    if (type == ModalType::SetSpeed) {
        build_text_input_rows(content_rows, "Enter Target Frequency (Hz, 0=Max):", input,
                              "Examples: 1000 (1kHz), 1000000 (1MHz), 0 (maximum speed)");
        content_rows.push_back("  At 60 Hz or slower, detailed values stay visible.");
        content_rows.push_back(
            "  Above 60 Hz, fast-changing detail uses a spinner; sampled counters stay live.");
        content_rows.push_back("");
        content_rows.push_back(build_modal_footer(
            {{.key = "[Enter]", .action = "Apply"}, {.key = "[Esc]", .action = "Cancel"}}));
    }
}

}  // namespace simrv::tui::modals
