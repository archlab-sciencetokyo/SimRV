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

    if (!machine_.s_cycle_accurate) {
        if (logical_row == 0) {
            return section_line("Branch Predictor Inspector", width);
        }
        if (logical_row == 4) {
            std::string text = "\033[1;38;5;203m⚠️ CYCLE-ACCURATE MODE INACTIVE\033[0m";
            int spaces = std::max(0, (width - 32) / 2);
            return format_to_width(std::string(spaces, ' ') + text, width);
        }
        if (logical_row == 6) {
            std::string text = "Enable Cycle-Accurate mode [,] or -ca";
            int spaces = std::max(0, (width - static_cast<int>(text.length())) / 2);
            return format_to_width(std::string(spaces, ' ') + text, width);
        }
        if (logical_row == 14) {
            return section_line("Status: Predictor Disabled (Functional)", width);
        }
        return format_to_width("", width);
    }

    auto const state = cpu.pipeline_sim.save_state();
    auto const bp_type = cpu.pipeline_sim.config.bp_type;

    std::string bp_type_str = "2-Bit Bimodal";
    switch (bp_type) {
        case simrv::pipeline::BranchPredictorType::StaticNotTaken:
            bp_type_str = "Static NT";
            break;
        case simrv::pipeline::BranchPredictorType::StaticTaken:
            bp_type_str = "Static T";
            break;
        case simrv::pipeline::BranchPredictorType::OneBitBimodal:
            bp_type_str = "1-Bit Bimodal";
            break;
        case simrv::pipeline::BranchPredictorType::TwoBitBimodal:
            bp_type_str = "2-Bit Bimodal";
            break;
        case simrv::pipeline::BranchPredictorType::Gshare:
            bp_type_str = "Gshare";
            break;
    }

    uint64_t total_branches = cpu.pipeline_sim.cycle_count();
    uint64_t mispredicted = state.stats.control_hazard_bubbles;
    uint64_t correct = (total_branches >= mispredicted) ? (total_branches - mispredicted) : 0;
    double acc = (total_branches == 0)
                     ? 100.0
                     : 100.0 * static_cast<double>(correct) / static_cast<double>(total_branches);
    uint64_t penalty_cycles = mispredicted * 2;

    if (logical_row == 0) {
        return section_line("Branch Predictor & BHT Inspector", width);
    }
    if (logical_row == 1) {
        return format_to_width(
            std::format("  {}Type:\033[0m {}{:<13}\033[0m │ {}Acc:\033[0m {}{:5.1f}%\033[0m │ "
                        "{}Pen:\033[0m {}{}\033[0m",
                        kThemeText, kThemeMint, bp_type_str, kThemeText,
                        (acc >= 90.0 ? kThemeMint : (acc >= 75.0 ? kThemePeach : kThemeCoral)), acc,
                        kThemeText, kThemeCoral, simrv::util::format_with_commas(penalty_cycles)),
            width);
    }
    if (logical_row == 2) {
        return format_to_width(
            std::format("  {}Branches:\033[0m {}{:<6}\033[0m │ {}Correct:\033[0m {}{:<6}\033[0m │ "
                        "{}Mispred:\033[0m {}{}\033[0m",
                        kThemeText, kThemeVal, simrv::util::format_with_commas(total_branches),
                        kThemeText, kThemeMint, simrv::util::format_with_commas(correct),
                        kThemeText, kThemeCoral, simrv::util::format_with_commas(mispredicted)),
            width);
    }
    if (logical_row == 3) {
        return section_line("Branch History Table (BHT 2-Bit Counter Map)", width);
    }

    // Display 4 BHT entries per row to guarantee compact length (<44 chars)
    if (logical_row >= 4 && logical_row <= 12) {
        int row_idx = logical_row - 4;  // 0 to 8
        int entries_per_row = 4;
        int start_idx = row_idx * entries_per_row;
        std::string line = "  ";

        for (int i = 0; i < entries_per_row; ++i) {
            int bht_idx = start_idx + i;
            if (bht_idx >= 256) break;
            uint8_t bht_val = state.branch_history_table[static_cast<size_t>(bht_idx)];

            const char* color = kThemeMuted;
            const char* label = "00(SN)";
            switch (bht_val) {
                case 0:
                    color = kThemeMuted;
                    label = "00(SN)";
                    break;
                case 1:
                    color = kThemePeach;
                    label = "01(WN)";
                    break;
                case 2:
                    color = kThemeSky;
                    label = "10(WT)";
                    break;
                case 3:
                    color = kThemeMint;
                    label = "11(ST)";
                    break;
                default:
                    break;
            }
            line += std::format("{}[{:02x}:{} {}{}\033[0m] ", kThemeText, bht_idx, kThemeText,
                                color, label);
        }
        return format_to_width(line, width);
    }

    if (logical_row == 13) {
        return section_line("BHT: 256 entries │ BTB: 64 targets", width);
    }

    return format_to_width("", width);
}

}  // namespace simrv::tui
