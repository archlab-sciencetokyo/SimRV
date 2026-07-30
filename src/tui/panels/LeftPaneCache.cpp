/**
 * @file LeftPaneCache.cpp
 * @brief Implements Cache Statistics and Set & Way Inspector for TUI Left Pane.
 */
#include "simrv/tui/panels/LeftPane.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/util/FormatUtil.hpp"
#include <format>
#include <string>
#include <algorithm>

namespace simrv::tui {

auto LeftPane::render_cache_stats(const simrv::core::CPU& cpu, int logical_row, int col_width,
                                  int right_width) -> std::string {
    int const width = col_width + right_width;
    if (!machine_.s_cycle_accurate) {
        switch (logical_row) {
            case 0:
                return section_line("Cache — Not Available", width);
            case 2:
                return format_to_width(
                    std::format("  {}Cache simulation is disabled in high-performance mode.\033[0m",
                                kThemeMuted),
                    width);
            case 3:
                return format_to_width(
                    std::format("  {}Relaunch SimRV with {}--cycle-accurate\033[0m{} (-c) to "
                                "enable.\033[0m",
                                kThemeMuted, kThemeVal, kThemeMuted),
                    width);
            default:
                return format_to_width("", width);
        }
    }

    auto const& ic = cpu.icache;
    auto const& dc = cpu.dcache;

    auto make_bar = [](double ratio, int bar_width) -> std::string {
        int filled = static_cast<int>(ratio * bar_width);
        if (filled < 0) filled = 0;
        if (filled > bar_width) filled = bar_width;
        std::string bar = kThemeMint;
        for (int i = 0; i < filled; ++i) {
            bar += "█";
        }
        bar += kThemeMuted;
        for (int i = filled; i < bar_width; ++i) {
            bar += "░";
        }
        bar += "\033[0m";
        return bar;
    };

    switch (logical_row) {
        case 0:
            return section_line("L1 Cache Performance & Replacements", width);
        case 1: {
            uint64_t h = ic.hit_count(), m = ic.miss_count(), r = ic.replacement_count();
            uint64_t tot = h + m;
            double mr = (tot == 0) ? 0.0 : 100.0 * static_cast<double>(m) / static_cast<double>(tot);
            return format_to_width(
                std::format("  {}ICache\033[0m Hits: {}{:<7}\033[0m Miss: {}{:<6}\033[0m MR: {}{:>5.1f}%\033[0m Evict: {}{:<5}\033[0m",
                            kThemeText, kThemeMint, simrv::util::format_with_commas(h),
                            kThemeCoral, simrv::util::format_with_commas(m),
                            kThemePeach, mr, kThemeSky, simrv::util::format_with_commas(r)),
                width);
        }
        case 2: {
            uint64_t h = dc.hit_count(), m = dc.miss_count(), r = dc.replacement_count();
            uint64_t tot = h + m;
            double mr = (tot == 0) ? 0.0 : 100.0 * static_cast<double>(m) / static_cast<double>(tot);
            return format_to_width(
                std::format("  {}DCache\033[0m Hits: {}{:<7}\033[0m Miss: {}{:<6}\033[0m MR: {}{:>5.1f}%\033[0m Evict: {}{:<5}\033[0m",
                            kThemeText, kThemeMint, simrv::util::format_with_commas(h),
                            kThemeCoral, simrv::util::format_with_commas(m),
                            kThemePeach, mr, kThemeSky, simrv::util::format_with_commas(r)),
                width);
        }
        case 3: {
            uint64_t ih = ic.hit_count(), im = ic.miss_count();
            uint64_t itot = ih + im;
            double iratio = (itot == 0) ? 1.0 : static_cast<double>(ih) / static_cast<double>(itot);

            uint64_t dh = dc.hit_count(), dm = dc.miss_count();
            uint64_t dtot = dh + dm;
            double dratio = (dtot == 0) ? 1.0 : static_cast<double>(dh) / static_cast<double>(dtot);

            int bar_w = std::max(4, (width - 46) / 2);
            std::string ibar = make_bar(iratio, bar_w);
            std::string dbar = make_bar(dratio, bar_w);
            return format_to_width(
                std::format("  {}IC\033[0m [{}] {:>5.1f}%  │  {}DC\033[0m [{}] {:>5.1f}%",
                            kThemeText, ibar, iratio * 100.0, kThemeText, dbar, dratio * 100.0),
                width);
        }
        case 4: {
            std::string target_name = (cache_inspect_type_ == 0) ? "L1 ICache" : "L1 DCache";
            return section_line(std::format("Set & Way Inspector — {} Set #{:02d} (0-15)", target_name, cache_inspect_set_), width);
        }
        case 5: {
            return format_to_width(
                std::format("  {}Use \033[1m[↑/↓]\033[0m or \033[1m[j/k]\033[0m Set, \033[1m[←/→]\033[0m I/D-Cache, \033[1m[0-3]\033[0m or \033[1m[w]\033[0m Way\033[0m",
                            kThemeMuted),
                width);
        }
        case 6:
        case 7:
        case 8:
        case 9: {
            uint32_t way_idx = static_cast<uint32_t>(logical_row - 6);
            uint32_t set_idx = static_cast<uint32_t>(cache_inspect_set_);
            bool is_selected_way = (static_cast<int>(way_idx) == cache_inspect_way_);

            bool is_icache = (cache_inspect_type_ == 0);
            bool valid = is_icache ? ic.is_line_valid(set_idx, way_idx) : dc.is_line_valid(set_idx, way_idx);
            Address tag = is_icache ? ic.get_line_tag(set_idx, way_idx) : dc.get_line_tag(set_idx, way_idx);
            uint64_t lru = is_icache ? ic.get_line_last_used(set_idx, way_idx) : dc.get_line_last_used(set_idx, way_idx);

            uint32_t last_set = is_icache ? ic.last_accessed_set() : dc.last_accessed_set();
            bool last_hit = is_icache ? ic.last_access_was_hit() : dc.last_access_was_hit();
            uint32_t last_repl_set = is_icache ? ic.last_replaced_set() : dc.last_replaced_set();
            uint32_t last_repl_way = is_icache ? ic.last_replaced_way() : dc.last_replaced_way();

            bool is_replaced_way = (set_idx == last_repl_set && way_idx == last_repl_way);

            std::string status_tag;
            if (!valid) {
                status_tag = std::format("{}INVALID\033[0m           ", kThemeMuted);
            } else {
                status_tag = std::format("{}0x{:016x}\033[0m", kThemeVal, tag);
            }

            std::string cursor_marker = is_selected_way ? std::format("\033[1m{}▶\033[0m ", kThemeMint) : "  ";

            std::string way_str = is_selected_way ? std::format("\033[1;7mWay #{}\033[0m", way_idx) : std::format("{}Way #{}\033[0m", kThemeText, way_idx);
            std::string way_prefix = std::format("{} [V:{}] Tag: {}",
                                                 way_str,
                                                 valid ? std::format("{}1\033[0m", kThemeMint) : std::format("{}0\033[0m", kThemeMuted),
                                                 status_tag);

            uint32_t last_hit_way_idx = is_icache ? ic.last_hit_way() : dc.last_hit_way();
            bool is_hit_way = (set_idx == last_set && last_hit && way_idx == last_hit_way_idx);

            std::string highlight;
            if (is_replaced_way) {
                // The replacement way after a miss — show MISS ► REPLACED together
                highlight = std::format("  \033[1m{}◄ MISS\033[0m {}▸ REPLACED\033[0m", kThemeCoral, kThemePeach);
            } else if (is_hit_way) {
                highlight = std::format("  \033[1m{}◄ HIT\033[0m", kThemeMint);
            }

            std::string line_str = std::format("{}{}  LRU:{:<8}{}", cursor_marker, way_prefix, lru, highlight);
            return format_to_width(line_str, width);
        }
        case 10: {
            auto set_idx = static_cast<uint32_t>(cache_inspect_set_);
            auto way_idx = static_cast<uint32_t>(cache_inspect_way_);
            bool is_icache = (cache_inspect_type_ == 0);
            const auto* line_data = is_icache ? ic.get_line_data(set_idx, way_idx) : dc.get_line_data(set_idx, way_idx);
            bool valid = is_icache ? ic.is_line_valid(set_idx, way_idx) : dc.is_line_valid(set_idx, way_idx);

            std::string hex_str;

            if (line_data != nullptr && valid) {
                for (size_t b = 0; b < 16 && b < line_data->size(); ++b) {
                    uint8_t byte_val = std::to_integer<uint8_t>(line_data->at(b));
                    hex_str += std::format("{}{:02x} ", kThemeVal, byte_val);
                }
                return format_to_width(
                    std::format("  {}Data (Way #{}): {}\033[0m", kThemeText, way_idx, hex_str),
                    width);
            } else {
                return format_to_width(
                    std::format("  {}Data (Way #{}): {}<Empty / Invalid Line>\033[0m",
                                kThemeText, way_idx, kThemeMuted),
                    width);
            }
        }
        case 11:
            return section_line("Set Occupancy Map & Replacement Log", width);
        case 12:
        case 13:
        case 14: {
            int const base_set = (logical_row - 12) * 6;
            bool is_ic = (cache_inspect_type_ == 0);
            const auto& target_cache = is_ic ? static_cast<const simrv::cache::BaseCache<64, 32, 4>&>(ic)
                                             : static_cast<const simrv::cache::BaseCache<64, 32, 4>&>(dc);

            auto make_set_str = [this](auto const& cache, int set_idx) -> std::string {
                if (set_idx >= 16) return "";
                bool const is_selected = (set_idx == cache_inspect_set_);
                bool const is_last = (static_cast<uint32_t>(set_idx) == cache.last_accessed_set());
                bool const was_hit = cache.last_access_was_hit();

                std::string set_prefix;
                if (is_selected) {
                    set_prefix = std::format("\033[1;7m{:02d}\033[0m:[", set_idx);
                } else if (is_last) {
                    if (was_hit) {
                        set_prefix = std::format("\033[1m{}{:02d}:\033[0m[", kThemeMint, set_idx);
                    } else {
                        set_prefix = std::format("\033[1m{}{:02d}:\033[0m[", kThemeCoral, set_idx);
                    }
                } else {
                    set_prefix = std::format("{}{:02d}:\033[0m[", kThemeMuted, set_idx);
                }

                std::string s = set_prefix;
                for (uint32_t w = 0; w < 4; ++w) {
                    if (cache.is_line_valid(set_idx, w)) {
                        s += std::format("{}#\033[0m", kThemeMint);
                    } else {
                        s += std::format("{}.\033[0m", kThemeMuted);
                    }
                }
                s += "]";
                return s;
            };

            std::string sets_row;
            for (int s = base_set; s < base_set + 6 && s < 16; ++s) {
                sets_row += make_set_str(target_cache, s) + " ";
            }
            return format_to_width("  " + sets_row, width);
        }
        case 15: {
            bool is_ic = (cache_inspect_type_ == 0);
            const auto& cache = is_ic ? static_cast<const simrv::cache::BaseCache<64, 32, 4>&>(ic)
                                      : static_cast<const simrv::cache::BaseCache<64, 32, 4>&>(dc);
            Address ev_tag = cache.last_evicted_tag();
            std::string ev_str = (ev_tag == ~Address{0}) ? "None" : std::format("0x{:016x}", ev_tag);
            uint32_t r_set = cache.last_replaced_set();
            uint32_t r_way = cache.last_replaced_way();
            std::string r_loc = (r_set == 0xFFFFFFFF) ? "None" : std::format("Set #{}, Way #{}", r_set, r_way);

            return format_to_width(
                std::format("  {}Last Evicted Tag:\033[0m {}  │  {}Location:\033[0m {}",
                            kThemeText, ev_str, kThemeText, r_loc),
                width);
        }
        default:
            return format_to_width("", width);
    }
}

} // namespace simrv::tui
