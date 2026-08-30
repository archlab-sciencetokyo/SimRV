/**
 * @file InspectorPaneStats.cpp
 * @brief Performance statistics and pipeline counters pane rendering.
 */
#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <vector>

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/debug/GdbStub.hpp"
#include "simrv/debug/SpikeLockstep.hpp"
#include "simrv/pipeline/PipelineConfig.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/panels/InspectorPane.hpp"
#include "simrv/util/FormatUtil.hpp"
#include "simrv/xlen/Helpers.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::tui {

namespace {

auto compact_elf_symbol(std::string symbol, int max_columns) -> std::string {
    max_columns = std::max(1, max_columns);
    if (get_display_width(symbol) <= max_columns) return symbol;

    // SymbolTable formats in-function locations as "name + 0xoffset". Preserve that suffix so
    // a compact debug row still says where execution is within the resolved ELF symbol.
    const std::size_t offset_pos = symbol.rfind(" + 0x");
    const std::string suffix = offset_pos == std::string::npos ? "" : symbol.substr(offset_pos);
    const int suffix_columns = get_display_width(suffix);
    if (suffix_columns >= max_columns - 1) {
        return suffix.empty() ? "…" : "…" + suffix.substr(0, static_cast<size_t>(max_columns - 1));
    }

    const int prefix_columns = max_columns - suffix_columns - 1;
    return symbol.substr(0, static_cast<size_t>(prefix_columns)) + "…" + suffix;
}

auto make_progress_bar(double ratio, int width, const std::string& color_code) -> std::string {
    int filled = static_cast<int>(ratio * width);
    if (filled < 0) filled = 0;
    if (filled > width) filled = width;
    std::string bar;
    bar += color_code;
    for (int i = 0; i < filled; ++i) {
        bar += "█";
    }
    bar += kThemeMuted;
    for (int i = filled; i < width; ++i) {
        bar += "░";
    }
    bar += "\033[0m";
    return bar;
}

}  // namespace

namespace {

auto format_speed(uint64_t kips) -> std::string {
    return kips >= 1000 ? std::format("{:.2f} MIPS", static_cast<double>(kips) / 1000.0)
                        : std::format("{:.1f} KIPS", static_cast<double>(kips));
}

}  // namespace

auto InspectorPane::performance_row_count() const -> int {
    return machine_.runtime_profile.is_cycle_mode() ? 9 : 5;
}

auto InspectorPane::performance_start_row(bool single_column) const -> int {
    if (single_column) return 32;
    return page_ == TuiRegPage::PIPELINE ? 25 : 21;
}

auto InspectorPane::render_machine_performance_stats(const simrv::core::CPU& cpu, int stats_row,
                                                     int width) -> std::string {
    const auto simulated_seconds = static_cast<double>(cpu.clint_mmio.mtime.load()) / 10000000.0;
    if (stats_row == 0) return section_line("Performance", width);
    if (stats_row == 1) {
        return render_pair("retired", simrv::util::format_with_commas(cpu.e_icount), kThemeMint,
                           "sim", std::format("{:.3f} s", simulated_seconds), kThemeSky, width / 2,
                           width - width / 2, width < 45 ? 0 : 7);
    }
    if (stats_row == 2) {
        return render_pair("engine", std::string(machine_.runtime_profile.execution_name()),
                           kThemeVal, "host", std::format("{:.3f} s", active_runtime_), kThemeMint,
                           width / 2, width - width / 2, width < 45 ? 0 : 7);
    }
    if (stats_row == 3) {
        uint64_t top = max_kips_;
        for (uint64_t value : kips_history_) top = std::max(top, value);
        return render_pair("speed", format_speed(kips_), kThemeMint, "peak", format_speed(top),
                           kThemeSky, width / 2, width - width / 2, width < 45 ? 0 : 7);
    }
    if (stats_row == 4) {
        const auto average = active_runtime_ > 0.0
                                 ? static_cast<uint64_t>(static_cast<double>(cpu.e_icount) /
                                                         1000.0 / active_runtime_)
                                 : 0;
        if (width >= 52) {
            const bool compact = width < 70;
            const std::string average_label = compact ? "avg" : "average";
            const std::string suffix =
                std::format("]  {}: {}", average_label, format_speed(average));
            const int available = width - 11 - get_display_width(suffix);
            const int spark_width = std::max(8, std::min(available, (width * 2) / 5));
            return format_to_width(
                std::format(" {}{:<7}\033[0m: [{}{}\033[0m] {}{}: {}\033[0m", kThemeText, "trend",
                            kThemeSky, get_sparkline_string(spark_width), kThemeSky, average_label,
                            format_speed(average)),
                width);
        }
        return make_field("average", format_speed(average), kThemeSky, width < 45 ? 0 : 7);
    }
    return format_to_width("", width);
}

auto InspectorPane::render_sampled_machine_performance_stats(
    const simrv::core::TuiExecutionSnapshot& snapshot, int stats_row, int width) -> std::string {
    const auto simulated_seconds = static_cast<double>(snapshot.timer_ticks) / 10000000.0;
    if (stats_row == 0) return section_line("Performance", width);
    if (stats_row == 1) {
        return render_pair("retired", simrv::util::format_with_commas(snapshot.instruction_count),
                           kThemeMint, "sim", std::format("{:.3f} s", simulated_seconds), kThemeSky,
                           width / 2, width - width / 2, width < 45 ? 0 : 7);
    }
    if (stats_row == 2) {
        return render_pair("engine", std::string(machine_.runtime_profile.execution_name()),
                           kThemeVal, "host", std::format("{:.3f} s", active_runtime_), kThemeMint,
                           width / 2, width - width / 2, width < 45 ? 0 : 7);
    }
    if (stats_row == 3) {
        uint64_t top = max_kips_;
        for (uint64_t value : kips_history_) top = std::max(top, value);
        return render_pair("speed", format_speed(kips_), kThemeMint, "peak", format_speed(top),
                           kThemeSky, width / 2, width - width / 2, width < 45 ? 0 : 7);
    }
    if (stats_row == 4) {
        const auto average =
            active_runtime_ > 0.0
                ? static_cast<uint64_t>(static_cast<double>(snapshot.instruction_count) / 1000.0 /
                                        active_runtime_)
                : 0;
        if (width >= 52) {
            const bool compact = width < 70;
            const std::string average_label = compact ? "avg" : "average";
            const std::string suffix =
                std::format("]  {}: {}", average_label, format_speed(average));
            const int available = width - 11 - get_display_width(suffix);
            const int spark_width = std::max(8, std::min(available, (width * 2) / 5));
            return format_to_width(
                std::format(" {}{:<7}\033[0m: [{}{}\033[0m] {}{}: {}\033[0m", kThemeText, "trend",
                            kThemeSky, get_sparkline_string(spark_width), kThemeSky, average_label,
                            format_speed(average)),
                width);
        }
        return make_field("average", format_speed(average), kThemeSky, width < 45 ? 0 : 7);
    }
    return format_to_width("", width);
}

auto InspectorPane::render_cycle_accurate_stats(const simrv::core::CPU& cpu, int stats_row,
                                                int width) -> std::string {
    const int half = width / 2;
    const int label_pad = width < 45 ? 0 : 7;
    const uint64_t cycles = cpu.clint_mmio.mcycle;
    const auto simulated_seconds = static_cast<double>(cpu.clint_mmio.mtime.load()) / 10000000.0;
    if (stats_row == 0) return section_line("Performance · Cycle Accurate", width);
    if (stats_row == 1) {
        return render_pair("retired", simrv::util::format_with_commas(cpu.e_icount), kThemeMint,
                           "sim", std::format("{:.3f} s", simulated_seconds), kThemeSky, half,
                           width - half, label_pad);
    }
    if (stats_row == 2) {
        return render_pair("engine", std::string(machine_.runtime_profile.execution_name()),
                           kThemeVal, "host", std::format("{:.3f} s", active_runtime_), kThemeMint,
                           half, width - half, label_pad);
    }
    if (stats_row == 3) {
        uint64_t top = max_kips_;
        for (uint64_t value : kips_history_) top = std::max(top, value);
        return render_pair("speed", format_speed(kips_), kThemeMint, "peak", format_speed(top),
                           kThemeSky, half, width - half, label_pad);
    }
    if (stats_row == 4) {
        const auto average = active_runtime_ > 0.0
                                 ? static_cast<uint64_t>(static_cast<double>(cpu.e_icount) /
                                                         1000.0 / active_runtime_)
                                 : 0;
        if (width >= 52) {
            const bool compact = width < 70;
            const std::string average_label = compact ? "avg" : "average";
            const std::string suffix =
                std::format("]  {}: {}", average_label, format_speed(average));
            const int available = width - 11 - get_display_width(suffix);
            const int spark_width = std::max(8, std::min(available, (width * 2) / 5));
            return format_to_width(
                std::format(" {}{:<7}\033[0m: [{}{}\033[0m] {}{}: {}\033[0m", kThemeText, "trend",
                            kThemeSky, get_sparkline_string(spark_width), kThemeSky, average_label,
                            format_speed(average)),
                width);
        }
        return make_field("average", format_speed(average), kThemeSky, width < 45 ? 0 : 7);
    }
    if (stats_row == 5) {
        const double ipc = cycles == 0 ? 0.0 : static_cast<double>(cpu.e_icount) / cycles;
        const double cpi = cpu.e_icount == 0 ? 0.0 : static_cast<double>(cycles) / cpu.e_icount;
        return render_pair("IPC", std::format("{:.2f}", ipc), kThemeMint, "CPI",
                           std::format("{:.2f}", cpi), kThemePeach, half, width - half, label_pad);
    }
    if (stats_row == 6 || stats_row == 7) {
        const bool instruction_cache = stats_row == 6;
        const uint64_t hits = instruction_cache ? cpu.icache.hit_count() : cpu.dcache.hit_count();
        const uint64_t misses =
            instruction_cache ? cpu.icache.miss_count() : cpu.dcache.miss_count();
        const uint64_t total = hits + misses;
        const double hit_rate = total == 0 ? 0.0 : static_cast<double>(hits) / total;
        // The cache inspector owns raw hit/miss counts.  Keep this summary visual and bounded so
        // it remains legible beside the terminal pane at common split-screen widths.
        const std::string label = instruction_cache ? "I-cache" : "D-cache";
        const std::string percent = std::format("{:4.1f}%", hit_rate * 100.0);
        const int available = width - 11 - 2 - get_display_width(percent);
        const int bar_width = std::max(1, std::min(available, (width * 2) / 5));
        const char* color = instruction_cache ? kThemeSky : kThemePink;
        return format_to_width(
            std::format(" {}{:<7}\033[0m: [{}] {}{}\033[0m", kThemeText, label,
                        make_progress_bar(hit_rate, bar_width, color), color, percent),
            width);
    }
    if (stats_row == 8) {
        const uint64_t data = cpu.pipeline_sim.data_hazard_stalls();
        const uint64_t control = cpu.pipeline_sim.control_hazard_bubbles();
        const uint64_t cache = cpu.pipeline_sim.icache_stalls() + cpu.pipeline_sim.dcache_stalls();
        const auto [name, count] = std::max(
            {std::pair{"data", data}, std::pair{"control", control}, std::pair{"cache", cache}},
            [](const auto& lhs, const auto& rhs) { return lhs.second < rhs.second; });
        const double ratio = cycles == 0 ? 0.0 : static_cast<double>(count) * 100.0 / cycles;
        return make_field("limit", std::format("{} ({:.1f}%)", name, ratio), kThemePeach,
                          width < 45 ? 0 : 7);
    }
    return format_to_width("", width);
}

auto InspectorPane::render_debug_state(int debug_row, int width) -> std::string {
    auto const& cpu = current_cpu();
    auto const& st = cpu.state();
    int col_width = width / 2;
    int right_width = width - col_width;
    int const label_pad = width < 64 ? 0 : 11;

    if (debug_row == 0) {
        return section_line("Debug · Live", width);
    }
    if (debug_row == 1) {
        std::string sym = machine_.symbol_table().lookup(st.pc);
        // Keep the raw architectural PC separate from the ELF resolution.  The prior
        // <symbol> form hid the address and made an offset look like part of the symbol name.
        const bool resolved = !sym.empty();
        if (!resolved) sym = "no matching ELF symbol";
        // render_pair reserves eleven cells for the label and separator in the right column.
        sym = compact_elf_symbol(std::move(sym), std::max(1, right_width - 11));
        return render_pair(
            "PC",
            std::format("0x{:0{}x}", static_cast<uint64_t>(st.pc), simrv::xlen::kIsXLen64 ? 16 : 8),
            kThemeMint, "ELF sym", sym, resolved ? kThemePeach : kThemeMuted, col_width,
            right_width, label_pad);
    }
    if (debug_row == 2) {
        return render_pair(
            "breakpoints",
            std::to_string(machine_.breakpoint_manager().get_pc_breakpoints().size()),
            machine_.breakpoint_manager().get_pc_breakpoints().empty() ? kThemeMuted : kThemePeach,
            "watchpoints", std::to_string(machine_.breakpoint_manager().get_watchpoints().size()),
            machine_.breakpoint_manager().get_watchpoints().empty() ? kThemeMuted : kThemePeach,
            col_width, right_width, label_pad);
    }
    int optional_row = 3;
    if (machine_.is_paused() &&
        machine_.stop_reason() != simrv::core::Machine::StopReason::Running) {
        if (debug_row == optional_row) {
            return make_field(
                "stop", std::string(simrv::core::Machine::stop_reason_name(machine_.stop_reason())),
                kThemePeach, label_pad);
        }
        ++optional_row;
    }
    if (machine_.debugger() || machine_.lockstep()) {
        if (debug_row == optional_row) {
            const bool gdb_enabled = machine_.debugger() != nullptr;
            const bool lockstep_enabled = machine_.lockstep() != nullptr;
            return render_pair(
                gdb_enabled ? "gdb stub" : "",
                gdb_enabled ? (machine_.debugger()->is_connected() ? "connected" : "listening")
                            : "",
                kThemeMint, lockstep_enabled ? "lockstep" : "", lockstep_enabled ? "active" : "",
                kThemeMint, col_width, right_width, label_pad);
        }
        ++optional_row;
    }
    if (machine_.trap_log_enabled() && debug_row == optional_row) {
        std::string tohost_str = std::format("0x{:x}", machine_.tohost.load());
        return render_pair("tohost", tohost_str,
                           machine_.tohost.load() != 0 ? kThemePeach : kThemeVal, "traplog",
                           "active", kThemeMint, col_width, right_width, label_pad);
    }
    return format_to_width("", width);
}

auto InspectorPane::debug_state_row_count() const -> int {
    int rows = 3;  // heading, PC/ELF resolution, and configured stop points
    if (machine_.is_paused() && machine_.stop_reason() != simrv::core::Machine::StopReason::Running)
        ++rows;
    if (machine_.debugger() || machine_.lockstep()) ++rows;
    if (machine_.trap_log_enabled()) ++rows;
    return rows;
}

auto InspectorPane::render_perf_or_debug(const simrv::core::CPU& cpu, int logical_row, int width,
                                         bool single_column) -> std::string {
    int const total_logical_rows = get_total_rows(width);
    int const debug_rows = machine_.debug_diagnostics_enabled() ? debug_state_row_count() : 0;
    int const adj_base_rows = total_logical_rows - debug_rows;

    if (machine_.debug_diagnostics_enabled() && logical_row >= adj_base_rows &&
        logical_row < total_logical_rows) {
        return render_debug_state(logical_row - adj_base_rows, width);
    }

    const int stats_start = performance_start_row(single_column);
    if (logical_row < stats_start) return format_to_width("", width);
    const int stats_row = logical_row - stats_start;
    return machine_.runtime_profile.is_cycle_mode()
               ? render_cycle_accurate_stats(cpu, stats_row, width)
               : render_machine_performance_stats(cpu, stats_row, width);
}

auto InspectorPane::get_sparkline_string(int width) -> std::string {
    if (kips_history_.empty()) {
        std::string res(static_cast<std::size_t>(width), ' ');
        return res;
    }
    uint64_t max_val = 1;
    for (auto val : kips_history_) {
        if (val > max_val) max_val = val;
    }

    std::string s;
    int history_size = static_cast<int>(kips_history_.size());
    int pad = width - history_size;
    if (pad > 0) {
        s += std::string(static_cast<std::size_t>(pad), ' ');
    }

    constexpr std::array<const char*, 8> blocks = {" ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    int start_hist = (history_size > width) ? (history_size - width) : 0;
    for (int i = start_hist; i < history_size; ++i) {
        double ratio = static_cast<double>(kips_history_.at(static_cast<std::size_t>(i))) /
                       static_cast<double>(max_val);
        int block_idx = static_cast<int>(ratio * 7.0);
        if (block_idx < 0) block_idx = 0;
        if (block_idx > 7) block_idx = 7;
        s += blocks.at(static_cast<std::size_t>(block_idx));
    }
    return s;
}

}  // namespace simrv::tui
