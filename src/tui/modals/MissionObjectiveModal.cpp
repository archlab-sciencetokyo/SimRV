/**
 * @file MissionObjectiveModal.cpp
 * @brief Implementation of mission guidance and objective modal.
 */
#include "simrv/tui/modals/MissionObjectiveModal.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <vector>

#include "simrv/core/Machine.hpp"
#include "simrv/debug/SymbolTable.hpp"
#include "simrv/tui/TuiMission.hpp"
#include "simrv/tui/TuiModal.hpp"
#include "simrv/tui/modals/ModalComponents.hpp"

namespace simrv::tui::modals {

void MissionObjectiveModal::render_content(std::vector<std::string>& content_rows, int /*width*/,
                                           int /*height*/) {
    auto add_row = [&](std::string s) { content_rows.push_back(std::move(s)); };
    const auto guidance = mission_ != nullptr ? mission_->guidance() : std::nullopt;
    if (guidance) {
        if (!mission_->goal().empty()) {
            add_row("Goal: " + std::string(mission_->goal()));
        } else if (!mission_->description().empty()) {
            add_row("Lesson: " + std::string(mission_->description()));
        }
        const bool complete = mission_->completed();
        const auto step_number =
            std::min(mission_->step() + (complete ? 0U : 1U), mission_->step_count());
        add_row(
            std::format("Step {}/{} · {}", step_number, mission_->step_count(), guidance->title));
        add_row("Learn: " + (guidance->learn.empty() ? guidance->title : guidance->learn));
        add_row("Task: " + guidance->prompt);
        if (!guidance->hint.empty()) {
            if (hint_level_ == 0) {
                add_row("Hint [y to reveal]");
            } else if (hint_level_ == 1) {
                add_row("Hint: " + guidance->hint + " [y for why]");
            } else {
                add_row("Why: " + guidance->why + " [y to hide]");
            }
        } else if (hint_level_ == 2) {
            add_row("Why: " + guidance->why + " [y to hide]");
        } else {
            add_row("Why [y to reveal]");
        }
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
        add_row(
            complete
                ? std::format("Mission complete · {} of {} checkpoints reached",
                              mission_->reached_checkpoint_count(), mission_->checkpoint_count())
                : (!has_checkpoint
                       ? "No checkpoint for this instruction"
                       : (reached
                              ? (reached_ahead
                                     ? std::format("Reached early · {} (after step {})",
                                                   checkpoint->symbol, checkpoint->after_step)
                                     : std::format("Checkpoint reached · {}", checkpoint->symbol))
                              : std::format("Waiting for {} · after step {}", checkpoint->symbol,
                                            checkpoint->after_step))));
        if (checkpoint_symbol_missing) {
            add_row("This lesson checkpoint is missing from the loaded program.");
            add_row("Ask your instructor to provide the matching program file.");
        }
        if (reached && !checkpoint->message.empty()) {
            add_row(" " + checkpoint->message);
        }
        if (guest_stopped_for_checkpoint) {
            add_row("The program stopped before reaching this checkpoint.");
            add_row("Next: restart with r, or press Enter to inspect the stopped state.");
        } else if (!complete && has_checkpoint && !reached && !checkpoint_symbol_missing) {
            add_row(
                std::format("Next: press F5 to run to {}. This does not advance the instruction.",
                            checkpoint->symbol));
        } else if (!complete && reached_ahead) {
            add_row(
                std::format("Next: press Space to continue reading through step {}. The checkpoint "
                            "is already reached.",
                            checkpoint->after_step));
        } else if (!complete && reached && final_step) {
            add_row("Next: press Space to finish the lesson.");
        } else if (!complete && reached) {
            add_row("Next: press Space for the next instruction.");
        } else if (!complete && !has_checkpoint && !final_step) {
            add_row("Next: press Space for the next instruction.");
        }
        add_row("");
        if (complete) {
            add_row(mission_->can_previous_step()
                        ? " [Enter / Esc] Workbench   [p] Previous   [r] Restart"
                        : " [Enter / Esc] Workbench   [r] Restart");
            add_row(" [q / F10] Exit simulator");
        } else if (guest_stopped_for_checkpoint) {
            add_row(" [r] Restart   [Enter / Esc] Inspect state   [p] Previous");
        } else {
            add_row(
                has_checkpoint && !reached && !checkpoint_symbol_missing
                    ? " [F5] Run   [F6] Step   [Space] Next   [Enter / Esc] Hide   [y] Hint"
                    : (checkpoint_symbol_missing
                           ? " [F6] Step   [Space] Next   [Enter / Esc] Hide   [y] Hint"
                           : (final_step ? " [Space] Finish   [Enter / Esc] Hide   [y] Hint"
                                         : (reached ? " [Space] Next instruction   [Enter / Esc] "
                                                      "Hide   [y] Hint"
                                                    : " [Space] Next instruction   [F5] Run   "
                                                      "[Enter / Esc] Hide   [y] Hint"))));
            if (mission_->can_previous_step())
                add_row(" [p] Previous   [r] Restart   [F9 / ?] Glossary");
            else
                add_row(" [r] Restart   [F9 / ?] Glossary");
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
