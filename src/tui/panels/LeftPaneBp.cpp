/**
 * @file LeftPaneBp.cpp
 * @brief Implements Branch Predictor Inspector for TUI Left Pane.
 */
#include <format>
#include <string>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/pipeline/PipelineSim.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/panels/LeftPane.hpp"
#include "simrv/util/FormatUtil.hpp"

namespace simrv::tui {

auto LeftPane::render_bp_stats(const simrv::core::CPU& cpu, int logical_row, int col_width,
                               int right_width) -> std::string {
    int const width = col_width + right_width;

    if (!machine_.runtime_profile.is_cycle_mode()) {
        if (logical_row == 0) {
            return section_line("Branch Predictor Inspector", width);
        }
        if (logical_row == 4) {
            std::string text = "\033[1;31m[!] CYCLE-ACCURATE MODE INACTIVE\033[0m";
            int text_w = get_display_width(text);
            int spaces = std::max(1, (width - text_w) / 2);
            return format_to_width(std::string(static_cast<std::size_t>(spaces), ' ') + text,
                                   width);
        }
        if (logical_row == 6) {
            std::string text =
                (width < 45) ? "Enable Cycle-Accurate mode [,]" : "Enable cycle mode [,] or --ca";
            int text_w = get_display_width(text);
            int spaces = std::max(1, (width - text_w) / 2);
            return format_to_width(
                std::string(static_cast<std::size_t>(spaces), ' ') + kThemeMuted + text + "\033[0m",
                width);
        }
        if (logical_row == 14) {
            return section_line("Status: Predictor Disabled (Functional)", width);
        }
        return format_to_width("", width);
    }

    if (logical_row == 0) {
        return section_line("Control-Hazard Inspector", width);
    }
    if (logical_row == 1) {
        return format_to_width(
            std::format("  {}Policy:\033[0m {}Resolve in execute; block fetch\033[0m", kThemeText,
                        kThemeMint),
            width);
    }
    if (logical_row == 2) {
        return format_to_width(
            std::format("  {}Control flushes:\033[0m {}{}\033[0m", kThemeText, kThemeCoral,
                        simrv::util::format_with_commas(cpu.pipeline_sim.control_hazard_bubbles())),
            width);
    }
    if (logical_row == 3) {
        return section_line("Prediction State", width);
    }
    if (logical_row == 5) {
        return format_to_width(
            std::format("  {}BHT/BTB disabled in current CA policies\033[0m", kThemeMuted), width);
    }

    return format_to_width("", width);
}

}  // namespace simrv::tui
