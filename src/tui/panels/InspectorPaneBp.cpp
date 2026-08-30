/**
 * @file InspectorPaneBp.cpp
 * @brief Implements Branch Predictor Inspector and Telemetry for TUI Left Pane.
 */
#include <algorithm>
#include <bitset>
#include <format>
#include <string>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/pipeline/BranchPredictor.hpp"
#include "simrv/pipeline/PipelineSim.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/panels/InspectorPane.hpp"
#include "simrv/util/FormatUtil.hpp"

namespace simrv::tui {

auto InspectorPane::render_bp_stats(const simrv::core::CPU& cpu, int logical_row, int col_width,
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
            std::string text = (width < 45) ? "Enable Cycle-Accurate mode [,]"
                                            : "Enable cycle mode [,] or --mode cycle-accurate";
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

    const auto& bp = cpu.branch_predictor;
    const auto& stats = bp.stats();
    const auto& cfg = bp.config();

    if (logical_row == 0) {
        return section_line("Branch Predictor & Speculation", width);
    }
    if (logical_row == 1) {
        return format_to_width(
            std::format("  {}Model:\033[0m {}{}\033[0m  {}BHT:\033[0m {}{}\033[0m  {}BTB:\033[0m "
                        "{}{}\033[0m  {}RAS:\033[0m {}{}\033[0m",
                        kThemeText, kThemeMint, simrv::pipeline::to_string(cfg.type), kThemeText,
                        kThemeVal, cfg.bht_entries, kThemeText, kThemeVal, cfg.btb_entries,
                        kThemeText, kThemeVal, cfg.ras_entries),
            width);
    }
    if (logical_row == 2) {
        const double overall_acc = stats.overall_accuracy();
        const double mpki =
            (cpu.e_icount > 0)
                ? (static_cast<double>(stats.direction_misses + stats.target_misses) * 1000.0 /
                   static_cast<double>(cpu.e_icount))
                : 0.0;
        return format_to_width(
            std::format(
                "  {}Accuracy:\033[0m {}{:5.2f}%\033[0m  {}MPKI:\033[0m {}{:4.2f}\033[0m  "
                "{}Flushes:\033[0m {}{}\033[0m",
                kThemeText,
                overall_acc >= 90.0 ? kThemeMint : (overall_acc >= 75.0 ? kThemeVal : kThemeCoral),
                overall_acc, kThemeText, kThemeVal, mpki, kThemeText, kThemeCoral,
                simrv::util::format_with_commas(cpu.pipeline_sim.control_hazard_bubbles())),
            width);
    }
    if (logical_row == 3) {
        return section_line("Conditional Branches", width);
    }
    if (logical_row == 4) {
        return format_to_width(
            std::format(
                "  {}Evaluated:\033[0m {}{:<7}\033[0m  {}Dir Hits:\033[0m {}{:<7}\033[0m  "
                "{}Misses:\033[0m {}{}\033[0m",
                kThemeText, kThemeVal, simrv::util::format_with_commas(stats.conditional_branches),
                kThemeText, kThemeMint, simrv::util::format_with_commas(stats.direction_hits),
                kThemeText, kThemeCoral, simrv::util::format_with_commas(stats.direction_misses)),
            width);
    }
    if (logical_row == 5) {
        return format_to_width(
            std::format(
                "  {}Dir Accuracy:\033[0m {}{:5.2f}%\033[0m  {}Predictions:\033[0m {}{}\033[0m",
                kThemeText, stats.direction_accuracy() >= 90.0 ? kThemeMint : kThemeVal,
                stats.direction_accuracy(), kThemeText, kThemeVal,
                simrv::util::format_with_commas(stats.direction_predictions)),
            width);
    }
    if (logical_row == 6) {
        return section_line("Target Prediction (BTB & RAS)", width);
    }
    if (logical_row == 7) {
        return format_to_width(
            std::format(
                "  {}Direct JAL:\033[0m {}{:<6}\033[0m  {}Indirect JALR:\033[0m {}{}\033[0m",
                kThemeText, kThemeVal, simrv::util::format_with_commas(stats.direct_jumps),
                kThemeText, kThemeVal, simrv::util::format_with_commas(stats.indirect_jumps)),
            width);
    }
    if (logical_row == 8) {
        return format_to_width(
            std::format(
                "  {}BTB Hits:\033[0m {}{:<8}\033[0m  {}BTB Hit Rate:\033[0m {}{:5.2f}%\033[0m",
                kThemeText, kThemeMint, simrv::util::format_with_commas(stats.btb_hits), kThemeText,
                kThemeVal, stats.btb_hit_rate()),
            width);
    }
    if (logical_row == 9) {
        return format_to_width(
            std::format("  {}RAS Pops:\033[0m {}{:<8}\033[0m  {}RAS Hits:\033[0m {}{:<6}\033[0m  "
                        "{}Acc:\033[0m {}{:5.2f}%\033[0m",
                        kThemeText, kThemeVal, simrv::util::format_with_commas(stats.ras_pops),
                        kThemeText, kThemeMint, simrv::util::format_with_commas(stats.ras_hits),
                        kThemeText, kThemeMint, stats.ras_accuracy()),
            width);
    }
    if (logical_row == 10) {
        return section_line("Hardware State Visualizer", width);
    }
    if (logical_row == 11) {
        const uint32_t ghr = bp.ghr();
        const uint32_t bits = std::clamp(cfg.ghr_bits, 1u, 16u);
        std::string ghr_str;
        for (int b = static_cast<int>(bits) - 1; b >= 0; --b) {
            ghr_str.push_back(((ghr >> b) & 1u) != 0u ? '1' : '0');
        }
        return format_to_width(
            std::format("  {}GHR (History):\033[0m {}0b{}\033[0m", kThemeText, kThemeMint, ghr_str),
            width);
    }
    if (logical_row == 12) {
        const size_t depth = bp.ras_depth();
        const auto top = bp.ras_peek();
        std::string top_str =
            top.has_value() ? std::format("0x{:08x}", static_cast<uint32_t>(*top)) : "<empty>";
        return format_to_width(
            std::format("  {}RAS Depth:\033[0m {}{}/{}\033[0m  {}Top:\033[0m {}{}\033[0m",
                        kThemeText, kThemeVal, depth, cfg.ras_entries, kThemeText, kThemeVal,
                        top_str),
            width);
    }
    if (logical_row == 13) {
        const auto dist = bp.bht_distribution();
        return format_to_width(
            std::format(
                "  {}BHT Bias:\033[0m {}SN:{}\033[0m {}WN:{}\033[0m {}WT:{}\033[0m {}ST:{}\033[0m",
                kThemeText, kThemeCoral, dist[0], kThemeText, dist[1], kThemeText, dist[2],
                kThemeMint, dist[3]),
            width);
    }
    if (logical_row == 14) {
        return section_line("Status: Active Speculation Enabled", width);
    }

    return format_to_width("", width);
}

}  // namespace simrv::tui
