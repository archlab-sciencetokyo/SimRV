/**
 * @file InspectorPaneLive.cpp
 * @brief Live, snapshot-backed activity and performance summary.
 */
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

#include "simrv/core/Machine.hpp"
#include "simrv/core/PlatformTiming.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/panels/InspectorPane.hpp"
#include "simrv/util/FormatUtil.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::tui {

namespace {

auto privilege_label(simrv::xlen::PrivilegeLevel privilege) -> std::string_view {
    switch (privilege) {
        case simrv::xlen::PrivilegeLevel::User:
            return "U";
        case simrv::xlen::PrivilegeLevel::Supervisor:
            return "S";
        case simrv::xlen::PrivilegeLevel::Machine:
        default:
            return "M";
    }
}

auto format_rate(uint64_t kips) -> std::string {
    return kips >= 1000 ? std::format("{:.2f} MIPS", static_cast<double>(kips) / 1000.0)
                        : std::format("{:.1f} KIPS", static_cast<double>(kips));
}

auto format_duration(double seconds) -> std::string { return std::format("{:.2f}s", seconds); }

}  // namespace

auto InspectorPane::render_live_overview_row(int logical_row, int width) -> std::string {
    const int hart_count = static_cast<int>(machine_.num_harts());
    if (logical_row == 0) {
        const auto state_label = paused_ ? "PAUSED" : "RUNNING";
        const auto state_color = paused_ ? kThemePeach : kThemeMint;
        const auto mode_label = machine_.runtime_profile.is_cycle_mode() ? "CA" : "IA";
        return format_to_width(
            std::format(" {}● {}{}\033[0m  {}{}\033[0m  {} harts", state_color, state_label,
                        kThemeText, kThemeVal, mode_label, hart_count),
            width);
    }
    if (logical_row == 1) return section_line("Live Activity", width);

    const int first_hart_row = 2;
    const int totals_title_row = first_hart_row + hart_count;
    if (logical_row >= first_hart_row && logical_row < totals_title_row) {
        const auto hart = static_cast<size_t>(logical_row - first_hart_row);
        if (hart >= live_harts_.size()) return format_to_width("", width);
        const auto& snapshot = live_harts_[hart];
        const bool selected = hart == selected_hart_;
        const std::string hart_label = std::format("H{}", hart);
        const unsigned pc_digits = simrv::xlen::kIsXLen64 ? 16 : 8;
        const std::string pc =
            width >= 36 ? std::format("PC 0x{:0{}x}", static_cast<uint64_t>(snapshot.pc), pc_digits)
                        : std::format("0x{:x}", static_cast<uint64_t>(snapshot.pc));
        const std::string location =
            snapshot.symbol.empty() ? pc : std::format("{} <{}>", pc, snapshot.symbol);
        const auto color = selected ? kThemeSky : kThemeText;
        return format_to_width(
            std::format(" {}{}{}\033[0m  {}{}\033[0m  {}{}\033[0m", color, selected ? ">" : " ",
                        hart_label, kThemeMuted, privilege_label(snapshot.privilege), kThemeText,
                        location),
            width);
    }
    if (logical_row == totals_title_row) return section_line("Totals & Performance", width);

    uint64_t instructions = 0;
    uint64_t cycles = 0;
    for (const auto& hart : live_harts_) {
        instructions += hart.instructions;
        cycles += hart.cycles;
    }
    const int retired_row = totals_title_row + 1;
    if (logical_row == retired_row) {
        return render_pair("Retired Σ", simrv::util::format_with_commas(instructions), kThemeMint,
                           "Hart cycles Σ", simrv::util::format_with_commas(cycles), kThemeSky,
                           width / 2, width - width / 2, width < 55 ? 0 : 11);
    }
    if (logical_row == retired_row + 1) {
        const auto average = active_runtime_ > 0.0
                                 ? static_cast<uint64_t>(static_cast<double>(instructions) /
                                                         1000.0 / active_runtime_)
                                 : 0;
        return render_pair("Recent", format_rate(kips_), kThemeMint, "Average",
                           format_rate(average), kThemeSky, width / 2, width - width / 2,
                           width < 55 ? 0 : 8);
    }
    if (logical_row == retired_row + 2) {
        const double ipc = cycles == 0 ? 0.0 : static_cast<double>(instructions) / cycles;
        const double cpi = instructions == 0 ? 0.0 : static_cast<double>(cycles) / instructions;
        return render_pair("IPC", std::format("{:.2f}", ipc), kThemeMint, "CPI",
                           std::format("{:.2f}", cpi), kThemePeach, width / 2, width - width / 2,
                           width < 55 ? 0 : 8);
    }
    if (logical_row == retired_row + 3) {
        const auto timer_ticks = live_harts_.empty() ? 0 : live_harts_.front().timer_ticks;
        const auto guest_seconds = simrv::core::timing::timer_ticks_to_seconds(timer_ticks);
        return render_pair("Host", format_duration(active_runtime_), kThemeMint, "Guest",
                           format_duration(guest_seconds), kThemeSky, width / 2, width - width / 2,
                           width < 55 ? 0 : 8);
    }
    if (logical_row == retired_row + 4) {
        return render_pair("Engine", std::string(machine_.runtime_profile.execution_name()),
                           kThemeVal, "Peak", format_rate(max_kips_), kThemePeach, width / 2,
                           width - width / 2, width < 55 ? 0 : 8);
    }
    return format_to_width("", width);
}

}  // namespace simrv::tui
