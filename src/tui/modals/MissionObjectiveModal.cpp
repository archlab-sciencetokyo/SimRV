/**
 * @file MissionObjectiveModal.cpp
 * @brief Implementation of mission guidance and objective modal.
 */
#include "simrv/tui/modals/MissionObjectiveModal.hpp"

#include <algorithm>
#include <format>
#include <iterator>
#include <string>
#include <vector>

#include "simrv/core/Machine.hpp"
#include "simrv/debug/SymbolTable.hpp"
#include "simrv/tui/TuiMission.hpp"
#include "simrv/tui/TuiModal.hpp"
#include "simrv/tui/framework/Theme.hpp"
#include "simrv/tui/framework/Text.hpp"
#include "simrv/tui/modals/ModalComponents.hpp"

namespace simrv::tui::modals {

namespace {

[[nodiscard]] auto format_inline_text(std::string_view text, std::string_view value_color)
    -> std::string {
    std::string result;
    result.reserve(text.size() + 24);
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] == '`') {
            const auto close = text.find('`', i + 1);
            if (close != std::string_view::npos) {
                result += std::format("\033[1m{}{}\033[0m", value_color,
                                      text.substr(i + 1, close - i - 1));
                i = close + 1;
                continue;
            }
        }
        if (text.substr(i).starts_with("**")) {
            const auto close = text.find("**", i + 2);
            if (close != std::string_view::npos) {
                result += std::format("\033[1m{}\033[0m", text.substr(i + 2, close - i - 2));
                i = close + 2;
                continue;
            }
        }
        result.push_back(text[i++]);
    }
    return result;
}

}  // namespace

void MissionObjectiveModal::render_content(std::vector<std::string>& content_rows, int width,
                                           int /*height*/) {
    const int text_width = std::max(1, width - 4);
    auto add_row = [&](std::string s) {
        auto wrapped = framework::wrap_text(s, text_width, 2);
        content_rows.insert(content_rows.end(), std::make_move_iterator(wrapped.begin()),
                            std::make_move_iterator(wrapped.end()));
    };
    const auto palette = framework::active_theme().palette;
    auto add_labeled_row = [&](std::string_view label, std::string_view text,
                               std::string_view color) {
        add_row(std::format("{}\033[1m{}:\033[0m {}", color, label,
                            format_inline_text(text, palette.value)));
    };
    auto add_status_row = [&](std::string_view text, std::string_view color) {
        add_row(std::format("{}{}\033[0m", color, text));
    };
    auto add_footer = [&](std::initializer_list<ModalActionHint> hints) {
        content_rows.push_back(build_modal_footer(hints));
    };
    const auto guidance = mission_ != nullptr ? mission_->guidance() : std::nullopt;
    if (guidance) {
        const bool has_context = !mission_->description().empty() || !mission_->goal().empty();
        if (!mission_->description().empty())
            add_labeled_row("Scenario", mission_->description(), palette.accent);
        if (!mission_->goal().empty()) add_labeled_row("Goal", mission_->goal(), palette.success);
        if (has_context) add_row("");
        const bool complete = mission_->completed();
        const auto step_number =
            std::min(mission_->step() + (complete ? 0U : 1U), mission_->step_count());
        add_row(std::format("{}\033[1mStep {}/{} · {}\033[0m", palette.accent, step_number,
                            mission_->step_count(), guidance->title));
        add_labeled_row("Learn", guidance->learn.empty() ? guidance->title : guidance->learn,
                        palette.value);
        add_row("");
        add_labeled_row("Task", guidance->prompt, palette.text);
        add_row("");
        if (!guidance->hint.empty()) {
            if (hint_level_ == 0) {
                add_labeled_row("Hint", "Press Y to reveal.", palette.warning);
            } else if (hint_level_ == 1) {
                add_labeled_row("Hint", guidance->hint + "  [Y for why]", palette.warning);
            } else {
                add_labeled_row("Why", guidance->why + "  [Y to hide]", palette.muted);
            }
        } else if (hint_level_ == 2) {
            add_labeled_row("Why", guidance->why + "  [Y to hide]", palette.muted);
        } else {
            add_labeled_row("Why", "Press Y to reveal.", palette.muted);
        }
        add_row("");
        const auto checkpoint = mission_->checkpoint_progress();
        const bool has_checkpoint = checkpoint.has_value();
        const bool reached = checkpoint && checkpoint->reached;
        const bool checkpoint_symbol_missing =
            checkpoint && !reached && machine_ && !machine_->binary_path().empty() &&
            !machine_->symbol_table().lookup_name(checkpoint->symbol);
        const bool final_step = mission_->is_final_step();
        const bool reached_ahead = reached && checkpoint->after_step > mission_->step() + 1;
        const bool guest_stopped_for_checkpoint =
            !complete && has_checkpoint && !reached && machine_ && machine_->is_shutdown_;
        if (complete) {
            add_status_row(std::format("✓ Lesson complete · {} of {} checkpoints reached",
                                       mission_->reached_checkpoint_count(),
                                       mission_->checkpoint_count()),
                           palette.success);
        } else if (!has_checkpoint) {
            add_status_row("No checkpoint for this lesson step.", palette.muted);
        } else if (reached) {
            add_status_row(reached_ahead
                               ? std::format("✓ Checkpoint reached early · {} (after step {})",
                                             checkpoint->symbol, checkpoint->after_step)
                               : std::format("✓ Checkpoint reached · {}", checkpoint->symbol),
                           palette.success);
        } else {
            add_status_row(std::format("○ Checkpoint waiting · {} (after step {})",
                                       checkpoint->symbol, checkpoint->after_step),
                           palette.warning);
        }
        if (checkpoint_symbol_missing) {
            add_status_row("This checkpoint is not in the loaded program.", palette.danger);
            add_row("Ask your instructor for the matching program file.");
        }
        if (reached && !checkpoint->message.empty()) {
            add_labeled_row("Checkpoint note", checkpoint->message, palette.value);
        }
        if (guest_stopped_for_checkpoint) {
            add_status_row("The program stopped before reaching this checkpoint.", palette.danger);
        }
        add_row("");
        const std::string hint_action =
            !guidance->hint.empty()
                ? (hint_level_ == 0 ? "Show hint"
                   : hint_level_ == 1 ? "Show why"
                                      : "Hide why")
                : (hint_level_ == 2 ? "Hide why" : "Show why");
        if (complete) {
            add_status_row("Guest execution is stopped. The final state is safe to review.",
                           palette.success);
            if (mission_->can_previous_step()) {
                add_footer({{.key = "[Enter / Esc]", .action = "Workbench"},
                            {.key = "[P]", .action = "Previous", .color = kThemePink}});
            } else {
                add_footer({{.key = "[Enter / Esc]", .action = "Workbench"}});
            }
            add_footer({{.key = "[R]", .action = "Restart", .color = kThemeMint},
                        {.key = "[Q / F10]", .action = "Exit", .color = kThemeCoral}});
        } else if (guest_stopped_for_checkpoint) {
            add_footer({{.key = "[R]", .action = "Restart", .color = kThemeMint},
                        {.key = "[Enter / Esc]", .action = "Inspect state"},
                        {.key = "[P]", .action = "Previous", .color = kThemePink}});
        } else if (!mission_->enabled()) {
            add_footer({{.key = "[Enter / Esc]", .action = "Close"}});
        } else if (!mission_->can_advance_step() && has_checkpoint) {
            if (checkpoint_symbol_missing) {
                add_footer({{.key = "[F6]", .action = "Step", .color = kThemePeach},
                            {.key = "[Enter / Esc]", .action = "Hide"},
                            {.key = "[Y]", .action = hint_action, .color = kThemePink}});
            } else {
                add_footer({{.key = "[F5]", .action = "Run to checkpoint", .color = kThemePeach},
                            {.key = "[F6]", .action = "Step", .color = kThemePeach},
                            {.key = "[Enter / Esc]", .action = "Hide"},
                            {.key = "[Y]", .action = hint_action, .color = kThemePink}});
            }
        } else {
            if (has_checkpoint && !reached && !checkpoint_symbol_missing) {
                add_footer({{.key = "[Space / N]",
                             .action = final_step ? "Finish lesson" : "Next lesson step",
                             .color = kThemeSky},
                            {.key = "[F5]", .action = "Run to checkpoint",
                             .color = kThemePeach}});
            } else {
                add_footer({{.key = "[Space / N]",
                             .action = final_step ? "Finish lesson" : "Next lesson step",
                             .color = kThemeSky}});
            }
            add_footer({{.key = "[Enter / Esc]", .action = "Hide"},
                        {.key = "[Y]", .action = hint_action, .color = kThemePink}});
            if (mission_->can_previous_step()) {
                add_footer({{.key = "[P]", .action = "Previous / Undo", .color = kThemePink},
                            {.key = "[R]", .action = "Restart", .color = kThemeMint},
                            {.key = "[F9 / ?]", .action = "Glossary"}});
            } else {
                add_footer({{.key = "[R]", .action = "Restart", .color = kThemeMint},
                            {.key = "[F9 / ?]", .action = "Glossary"}});
            }
        }
    } else {
        add_row("No active mission objective.");
        add_row(modals::build_modal_footer({{.key = "[Esc]", .action = "Close"}}));
    }
}

auto MissionObjectiveModal::handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction {
    if (byte == 'y' || byte == 'Y') {
        if (host_) host_->cycle_mission_hint();
        return framework::ModalAction::Continue;
    }
    if (byte == 27 || key == simrv::tui::TuiKey::Esc || key == simrv::tui::TuiKey::Enter ||
        key == simrv::tui::TuiKey::Newline) {
        return framework::ModalAction::Close;
    }
    return framework::ModalAction::Continue;
}

auto MissionObjectiveModal::handle_control_click(int /*content_row*/, size_t /*action_index*/)
    -> framework::ModalAction {
    return framework::ModalAction::Close;
}

auto MissionObjectiveModal::handle_click(framework::ScreenCoord /*local_pt*/)
    -> framework::ModalAction {
    return framework::ModalAction::Continue;
}

}  // namespace simrv::tui::modals
