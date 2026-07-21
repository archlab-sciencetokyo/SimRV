/**
 * @file TuiModal.cpp
 * @brief Implementation of TUI Modal dialogs and stable overlay rendering.
 */

#include "simrv/tui/TuiModal.hpp"
#include <charconv>
#include <format>
#include "simrv/core/Machine.hpp"
#include "simrv/tui/LeftPane.hpp"
#include "simrv/tui/TuiTheme.hpp"

namespace simrv::tui {

TuiModal::TuiModal(simrv::core::Machine& machine) : machine_(machine) {}

void TuiModal::open(ModalType type, LeftPane* left_pane, uint64_t step_granularity, uint64_t step_delay_us) {
    active_modal_ = type;
    input_.clear();
    switch (type) {
        case ModalType::SetBreakpoint:
            input_ = std::format("0x{:08x}", machine_.cpu.state().pc);
            break;
        case ModalType::SetStepSize:
            input_ = std::to_string(step_granularity);
            break;
        case ModalType::SetSpeed: {
            uint64_t hz = step_delay_us > 0 ? (1000000 / step_delay_us) : 0;
            input_ = std::to_string(hz);
            break;
        }
        case ModalType::InspectAddress:
            input_ = std::format("0x{:08x}", left_pane ? left_pane->get_inspect_addr() : 0);
            break;
        default:
            break;
    }
}

void TuiModal::close() {
    active_modal_ = ModalType::None;
    input_.clear();
}

void TuiModal::submit(LeftPane* left_pane,
                      std::atomic<uint64_t>& step_granularity,
                      std::atomic<uint64_t>& step_delay_us,
                      const std::function<void(TuiRegPage)>& set_reg_page_cb,
                      const std::function<void(const std::string&)>& set_status_override_cb) {
    if (active_modal_ == ModalType::None) return;
    std::string text = input_;
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.erase(text.begin());
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();

    if (!text.empty() && active_modal_ != ModalType::Help) {
        switch (active_modal_) {
            case ModalType::SetBreakpoint: {
                Address addr = 0;
                bool ok = false;
                if (text.starts_with("0x") || text.starts_with("0X")) {
                    auto result = std::from_chars(text.data() + 2, text.data() + text.size(), addr, 16);
                    ok = (result.ec == std::errc{});
                } else if (std::all_of(text.begin(), text.end(), ::isxdigit)) {
                    auto result = std::from_chars(text.data(), text.data() + text.size(), addr, 16);
                    ok = (result.ec == std::errc{});
                } else {
                    auto sym_opt = machine_.symbols.lookup_name(text);
                    if (sym_opt.has_value()) {
                        addr = *sym_opt;
                        ok = true;
                    }
                }
                if (ok) {
                    machine_.breakpoints.add_pc_breakpoint(addr);
                    set_status_override_cb(std::format("Breakpoint set at 0x{:08x}", addr));
                } else {
                    set_status_override_cb(std::format("Symbol/address not found: {}", text));
                }
                break;
            }
            case ModalType::SetStepSize: {
                uint64_t val = 0;
                auto result = std::from_chars(text.data(), text.data() + text.size(), val);
                if (result.ec == std::errc{} && val > 0) {
                    step_granularity.store(val, std::memory_order_relaxed);
                    set_status_override_cb(std::format("StepN set to {} instructions", val));
                } else {
                    set_status_override_cb(std::format("Invalid step size: {}", text));
                }
                break;
            }
            case ModalType::SetSpeed: {
                uint64_t val = 0;
                auto result = std::from_chars(text.data(), text.data() + text.size(), val);
                if (result.ec == std::errc{}) {
                    if (val == 0) {
                        step_delay_us.store(0, std::memory_order_relaxed);
                        set_status_override_cb("Speed set to Max (no delay)");
                    } else {
                        uint64_t delay = 1000000 / val;
                        step_delay_us.store(delay, std::memory_order_relaxed);
                        set_status_override_cb(std::format("Speed set to {} Hz (delay: {}us)", val, delay));
                    }
                } else {
                    set_status_override_cb(std::format("Invalid speed frequency: {}", text));
                }
                break;
            }
            case ModalType::InspectAddress: {
                Address addr = 0;
                bool ok = false;
                if (text.starts_with("0x") || text.starts_with("0X")) {
                    auto result = std::from_chars(text.data() + 2, text.data() + text.size(), addr, 16);
                    ok = (result.ec == std::errc{});
                } else if (std::all_of(text.begin(), text.end(), ::isxdigit)) {
                    auto result = std::from_chars(text.data(), text.data() + text.size(), addr, 16);
                    ok = (result.ec == std::errc{});
                } else {
                    auto sym_opt = machine_.symbols.lookup_name(text);
                    if (sym_opt.has_value()) {
                        addr = *sym_opt;
                        ok = true;
                    }
                }
                if (ok) {
                    if (left_pane) {
                        left_pane->set_inspect_addr(addr);
                    }
                    if (set_reg_page_cb) {
                        set_reg_page_cb(TuiRegPage::STACK);
                    }
                    set_status_override_cb(std::format("Inspecting memory at 0x{:08x}", addr));
                } else {
                    set_status_override_cb(std::format("Invalid address/symbol: {}", text));
                }
                break;
            }
            default:
                break;
        }
    }
    active_modal_ = ModalType::None;
    input_.clear();
}

void TuiModal::render_overlay(std::vector<std::string>& lines, int term_width, int term_height) const {
    if (active_modal_ == ModalType::None || lines.empty()) return;

    bool is_help = (active_modal_ == ModalType::Help);
    int box_w = is_help ? std::min(76, term_width - 4) : std::min(58, term_width - 4);
    if (box_w < 35) return;

    std::string m_bg = kThemeModalBg;
    std::string m_rst = std::string("\033[0m") + m_bg;

    std::vector<std::string> content_rows;
    auto add_row = [&](const std::string& formatted_str) {
        std::string s = formatted_str;
        std::string target = "\033[0m";
        size_t pos = 0;
        while ((pos = s.find(target, pos)) != std::string::npos) {
            s.replace(pos, target.length(), m_rst);
            pos += m_rst.length();
        }
        content_rows.push_back(s);
    };

    std::string title;
    switch (active_modal_) {
        case ModalType::SetBreakpoint:
            title = " SET BREAKPOINT ";
            add_row(std::format("{}Enter PC Address (hex) or Symbol:\033[0m", kThemeText));
            add_row(std::format("  \033[1m>\033[0m {}{}_\033[0m", kThemeMint, input_));
            break;
        case ModalType::SetStepSize:
            title = " SET STEP SIZE (N) ";
            add_row(std::format("{}Enter Step Count N (instructions):\033[0m", kThemeText));
            add_row(std::format("  \033[1m>\033[0m {}{}_\033[0m", kThemeMint, input_));
            break;
        case ModalType::SetSpeed:
            title = " SET SIMULATION FREQUENCY ";
            add_row(std::format("{}Enter Target Frequency (Hz, 0=Max):\033[0m", kThemeText));
            add_row(std::format("  \033[1m>\033[0m {}{}_\033[0m", kThemeMint, input_));
            break;
        case ModalType::InspectAddress:
            title = " INSPECT MEMORY ADDRESS ";
            add_row(std::format("{}Enter Target Address (hex) or Symbol:\033[0m", kThemeText));
            add_row(std::format("  \033[1m>\033[0m {}{}_\033[0m", kThemeMint, input_));
            break;
        case ModalType::Help:
            title = " SIMULATOR KEYBOARD SHORTCUTS ";
            if (term_height < 32 && box_w >= 70) {
                // Dual-column layout for small screen height
                struct Shortcut { const char* key; const char* desc; };
                static constexpr std::array<Shortcut, 23> help_items = {{
                    {"[s] / [Space]", "Step 1 inst"},
                    {"[n]",          "Step N insts"},
                    {"[b]",          "Undo step"},
                    {"[:]",          "Set PC Breakpoint"},
                    {"[k]",          "Toggle PC Breakpoint"},
                    {"[g]",          "Set N Step Size"},
                    {"[f]",          "Set Frequency (Hz)"},
                    {"[m]",          "Inspect Memory"},
                    {"[c] / [Ctrl-P]", "Run / Pause"},
                    {"[Tab]",        "Cycle Layout"},
                    {"[r] / [Alt-r]", "Cycle Registers"},
                    {"[l] / [Alt-l]", "Cycle Tool Tabs"},
                    {"[p] / [Alt-p]", "Cycle Right Pane"},
                    {"[e] / [Alt-e]", "Jump Explainer"},
                    {"[v] / [Alt-v]", "Toggle Trace"},
                    {"[u/d] / [PgUp/Dn]", "Scroll Logs"},
                    {"[w/s] / [Alt-w/s]", "Scroll Regs"},
                    {"[ [ / ] ]",     "Resize Pane"},
                    {"[F1] / [h] / [?]", "Show Help"},
                    {"[Alt-h]",      "High Contrast"},
                    {"[Alt-t]",      "Sakura Pastel"},
                    {"[Esc]",        "Close Modal"},
                    {"[Ctrl-Q]",     "Quit Simulator"}
                }};
                std::size_t half = (help_items.size() + 1) / 2;
                for (std::size_t i = 0; i < half; ++i) {
                    std::string left_item = std::format("{}{:<17}\033[0m {}{:<18}\033[0m", kThemeSky, help_items[i].key, kThemeText, help_items[i].desc);
                    std::string right_item;
                    if (i + half < help_items.size()) {
                        const auto& r = help_items[i + half];
                        right_item = std::format("{}{:<17}\033[0m {}{}\033[0m", kThemeSky, r.key, kThemeText, r.desc);
                    }
                    add_row(std::format("{} │ {}", left_item, right_item));
                }
            } else {
                // Full single-column layout for taller screens
                add_row(std::format(" {}{:<22}\033[0m {}Single instruction step\033[0m", kThemeSky, "[s] / [Space]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Step N instructions\033[0m", kThemeSky, "[n]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Undo / Step back 1 instruction\033[0m", kThemeSky, "[b]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Set PC Breakpoint modal\033[0m", kThemeSky, "[:]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Toggle PC breakpoint at current PC\033[0m", kThemeSky, "[k]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Set Step Size (N) modal\033[0m", kThemeSky, "[g]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Set Speed Frequency (Hz) modal\033[0m", kThemeSky, "[f]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Inspect Memory Address modal\033[0m", kThemeSky, "[m]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Run / Pause simulation loop\033[0m", kThemeSky, "[c] / [Ctrl-P]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Cycle TUI panel layout\033[0m", kThemeSky, "[Tab]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Cycle register sub-views (GPR/FPR/VEC)\033[0m", kThemeSky, "[r] / [Alt-r]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Cycle tool tabs (Pipe/Cache/Trace/Exp/Stack)\033[0m", kThemeSky, "[l] / [Alt-l]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Cycle Right Pane mode\033[0m", kThemeSky, "[p] / [Alt-p]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Jump to Explainer / Trap Details\033[0m", kThemeSky, "[e] / [Alt-e]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Toggle trace recording\033[0m", kThemeSky, "[v] / [Alt-v]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Scroll trace / log views\033[0m", kThemeSky, "[u/d] / [Up/Dn/PgUp/Dn]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Scroll registers view\033[0m", kThemeSky, "[w/s] / [Alt-w/s]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Adjust left pane width\033[0m", kThemeSky, "[ [ / ] ] / [Left/Right]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Show this help dialog\033[0m", kThemeSky, "[F1] / [h] / [?]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Toggle High Contrast theme\033[0m", kThemeSky, "[Alt-h]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Toggle Sakura Pastel theme\033[0m", kThemeSky, "[Alt-t]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Close modal dialog\033[0m", kThemeSky, "[Esc]", kThemeText));
            }
            break;
        default:
            break;
    }

    std::vector<std::string> box_lines;
    int inner_w = box_w - 2;

    auto pad_center = [&](const std::string& text, int width) -> std::string {
        int text_len = get_display_width(text);
        if (text_len >= width) return format_to_width(text, width);
        int pad_l = (width - text_len) / 2;
        int pad_r = width - text_len - pad_l;
        return std::string(static_cast<size_t>(pad_l), ' ') + text + std::string(static_cast<size_t>(pad_r), ' ');
    };

    auto pad_left = [&](const std::string& text, int width) -> std::string {
        int text_len = get_display_width(text);
        if (text_len >= width) return format_to_width(text, width);
        return text + std::string(static_cast<size_t>(width - text_len), ' ');
    };

    std::string top_border = std::format("{}{}╔{}{}{}╗\033[0m", m_bg, kThemeBorder, pad_center(std::format("═ \033[1m{}{}{}{} ═", kThemeSky, title, m_rst, kThemeBorder), inner_w), m_bg, kThemeBorder);
    box_lines.push_back(top_border);
    box_lines.push_back(std::format("{}{}║{}{}{}║\033[0m", m_bg, kThemeBorder, pad_center("", inner_w), m_bg, kThemeBorder));

    for (const auto& row : content_rows) {
        box_lines.push_back(std::format("{}{}║{}{}{}║\033[0m", m_bg, kThemeBorder, pad_left(" " + row, inner_w), m_bg, kThemeBorder));
    }

    box_lines.push_back(std::format("{}{}║{}{}{}║\033[0m", m_bg, kThemeBorder, pad_center("", inner_w), m_bg, kThemeBorder));
    if (!is_help) {
        box_lines.push_back(std::format("{}{}║{}{}{}║\033[0m", m_bg, kThemeBorder, pad_center(std::format("{}[Enter]\033[0m{} {}Submit  |  {}[Esc]\033[0m{} {}Cancel", kThemeVal, m_rst, kThemeMuted, kThemeVal, m_rst, kThemeMuted), inner_w), m_bg, kThemeBorder));
    } else {
        box_lines.push_back(std::format("{}{}║{}{}{}║\033[0m", m_bg, kThemeBorder, pad_center(std::format("{}Press {}[Esc]\033[0m{}, {}[Enter]\033[0m{}, {}[h]\033[0m{} or {}[F1]\033[0m{} to close", kThemeMuted, kThemeVal, m_rst, kThemeVal, m_rst, kThemeVal, m_rst, kThemeVal, m_rst), inner_w), m_bg, kThemeBorder));
    }
    box_lines.push_back(std::format("{}{}╚{}╝\033[0m", m_bg, kThemeBorder, make_repeated_string("═", inner_w)));

    int box_h = static_cast<int>(box_lines.size());
    int start_y = (static_cast<int>(lines.size()) - box_h) / 2;
    int start_x = (term_width - box_w) / 2;
    if (start_y < 0) start_y = 0;
    if (start_x < 0) start_x = 0;

    for (size_t i = 0; i < box_lines.size(); ++i) {
        int r = start_y + static_cast<int>(i);
        if (r >= 0 && static_cast<size_t>(r) < lines.size()) {
            lines[r] = overlay_string(lines[r], box_lines[i], start_x, box_w);
        }
    }
}

}  // namespace simrv::tui
