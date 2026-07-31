/**
 * @file HelpModal.cpp
 * @brief Implementation of Help shortcuts modal overlay rendering.
 */
#include "simrv/tui/modals/HelpModal.hpp"

#include <array>
#include <format>

#include "simrv/core/BuildInfo.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::tui::modals {

namespace {

struct Shortcut {
    const char* key;
    const char* desc;
};

}  // namespace

void HelpModal::render(std::vector<std::string>& content_rows,
                       const std::function<void(const std::string&)>& add_row_cb, int term_height,
                       int box_w) {
    (void)content_rows;
    add_row_cb(
        std::format(" \033[1mSimRV Version:\033[0m \033[1;36m{}\033[0m  \033[90m(RV{})\033[0m",
                    simrv::buildinfo::kVersion, simrv::xlen::kXLenBits));
    add_row_cb("");
    if (term_height < 32 && box_w >= 70) {
        // Dual-column layout for small screen height
        static const auto help_items =
            std::to_array<Shortcut>({{"[s] / [Space]", "Step 1 inst"},
                                     {"[b]", "Undo step / Backstep"},
                                     {"[c] / [Space]", "Run / Pause"},
                                     {"[o] / [Alt-o]", "Load Binary / Disk"},
                                     {"[,] / [Alt-s]", "Simulator Settings"},
                                     {"[y]", "CA System Config"},
                                     {"[:]", "Set PC Breakpoint"},
                                     {"[w]", "Set Watchpoint"},
                                     {"[m]", "Manage Break/Watchpoints"},
                                     {"[k]", "Toggle PC Breakpoint"},
                                     {"[f]", "Set Frequency (Hz)"},
                                     {"[i]", "Inspect Memory"},
                                     {"[Tab]", "Cycle Layout"},
                                     {"[r] / [Alt-r]", "Cycle Registers"},
                                     {"[l] / [Alt-l]", "Cycle Tool Tabs"},
                                     {"[p] / [Alt-p]", "Cycle Right Pane"},
                                     {"[e] / [Alt-e]", "Jump Explainer"},
                                     {"[v] / [Alt-v]", "Toggle Trace"},
                                     {"[u/d] / [PgUp/Dn]", "Scroll Logs"},
                                     {"[Alt-w/s]", "Scroll Regs"},
                                     {"[ [ / ] ]", "Resize Pane"},
                                     {"[F1] / [h] / [?]", "Show Help"},
                                     {"[Alt-h]", "High Contrast"},
                                     {"[Alt-t]", "Sakura Pastel"},
                                     {"[Esc]", "Close Modal"},
                                     {"[q] / [Ctrl-Q]", "Quit Simulator"}});
        std::size_t half = (help_items.size() + 1) / 2;
        for (std::size_t i = 0; i < half; ++i) {
            std::string left_item = std::format("\033[1m{}{:<17}\033[0m {}{:<18}\033[0m", kThemeSky,
                                                help_items[i].key, kThemeText, help_items[i].desc);
            std::string right_item;
            if (i + half < help_items.size()) {
                const auto& r = help_items[i + half];
                right_item = std::format("\033[1m{}{:<17}\033[0m {}{}\033[0m", kThemeSky, r.key,
                                         kThemeText, r.desc);
            }
            add_row_cb(std::format("{} │ {}", left_item, right_item));
        }
    } else {
        // Full single-column layout for taller screens
        add_row_cb(std::format(" \033[1m{}{:<22}\033[0m {}Single instruction step\033[0m",
                               kThemeSky, "[s] / [Space]", kThemeText));
        add_row_cb(std::format(" \033[1m{}{:<22}\033[0m {}Undo / Step back 1 instruction\033[0m",
                               kThemeSky, "[b]", kThemeText));
        add_row_cb(std::format(" \033[1m{}{:<22}\033[0m {}Load Program Binary or Disk modal\033[0m",
                               kThemeSky, "[o] / [Alt-o]", kThemeText));
        add_row_cb(std::format(" \033[1m{}{:<22}\033[0m {}Simulator Settings modal\033[0m",
                               kThemeSky, "[,]", kThemeText));
        add_row_cb(std::format(" \033[1m{}{:<22}\033[0m {}CA System Config modal\033[0m", kThemeSky,
                               "[y]", kThemeText));
        add_row_cb(std::format(" \033[1m{}{:<22}\033[0m {}Set PC Breakpoint modal\033[0m",
                               kThemeSky, "[:]", kThemeText));
        add_row_cb(std::format(" \033[1m{}{:<22}\033[0m {}Set Memory Watchpoint modal\033[0m",
                               kThemeSky, "[w]", kThemeText));
        add_row_cb(
            std::format(" \033[1m{}{:<22}\033[0m {}Toggle PC breakpoint at current PC\033[0m",
                        kThemeSky, "[k]", kThemeText));
        add_row_cb(std::format(" \033[1m{}{:<22}\033[0m {}Set Speed Frequency (Hz) modal\033[0m",
                               kThemeSky, "[f]", kThemeText));
        add_row_cb(std::format(" \033[1m{}{:<22}\033[0m {}Inspect Memory Address modal\033[0m",
                               kThemeSky, "[i]", kThemeText));
        add_row_cb(std::format(" \033[1m{}{:<22}\033[0m {}Run / Pause simulation loop\033[0m",
                               kThemeSky, "[c] / [Ctrl-P]", kThemeText));
        add_row_cb(std::format(" \033[1m{}{:<22}\033[0m {}Cycle TUI panel layout\033[0m", kThemeSky,
                               "[Tab]", kThemeText));
        add_row_cb(
            std::format(" \033[1m{}{:<22}\033[0m {}Cycle register sub-views (GPR/FPR/VEC)\033[0m",
                        kThemeSky, "[r] / [Alt-r]", kThemeText));
        add_row_cb(std::format(
            " \033[1m{}{:<22}\033[0m {}Cycle tool tabs (Pipe/Cache/Trace/Exp/Stack)\033[0m",
            kThemeSky, "[l] / [Alt-l]", kThemeText));
        add_row_cb(std::format(" \033[1m{}{:<22}\033[0m {}Cycle Right Pane mode\033[0m", kThemeSky,
                               "[p] / [Alt-p]", kThemeText));
        add_row_cb(std::format(" \033[1m{}{:<22}\033[0m {}Jump to Explainer / Trap Details\033[0m",
                               kThemeSky, "[e] / [Alt-e]", kThemeText));
        add_row_cb(std::format(" \033[1m{}{:<22}\033[0m {}Toggle trace recording\033[0m", kThemeSky,
                               "[v] / [Alt-v]", kThemeText));
        add_row_cb(std::format(" \033[1m{}{:<22}\033[0m {}Scroll trace / log views\033[0m",
                               kThemeSky, "[u/d] / [Up/Dn/PgUp/Dn]", kThemeText));
        add_row_cb(std::format(" \033[1m{}{:<22}\033[0m {}Scroll registers view\033[0m", kThemeSky,
                               "[Alt-w/s]", kThemeText));
        add_row_cb(std::format(" \033[1m{}{:<22}\033[0m {}Adjust left pane width\033[0m", kThemeSky,
                               "[ [ / ] ]", kThemeText));
        add_row_cb(std::format(" \033[1m{}{:<22}\033[0m {}Show this help dialog\033[0m", kThemeSky,
                               "[F1] / [h] / [?]", kThemeText));
        add_row_cb(std::format(" \033[1m{}{:<22}\033[0m {}Toggle High Contrast theme\033[0m",
                               kThemeSky, "[Alt-h]", kThemeText));
        add_row_cb(std::format(" \033[1m{}{:<22}\033[0m {}Toggle Sakura Pastel theme\033[0m",
                               kThemeSky, "[Alt-t]", kThemeText));
        add_row_cb(std::format(" \033[1m{}{:<22}\033[0m {}Close modal dialog\033[0m", kThemeSky,
                               "[Esc]", kThemeText));
    }
}

}  // namespace simrv::tui::modals
