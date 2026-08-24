/**
 * @file ModalComponents.cpp
 * @brief Dynamic, reusable components for modal headers, footers, tabs, and input forms.
 */
#include "simrv/tui/modals/ModalComponents.hpp"

#include <format>

namespace simrv::tui::modals {

auto get_modal_metadata(ModalType type, bool is_notice_error, std::string_view notice_title)
    -> ModalMetadata {
    switch (type) {
        case ModalType::SetBreakpoint:
            return {.title = " SET BREAKPOINT ", .is_wide = false};
        case ModalType::SetWatchpoint:
            return {.title = " SET WATCHPOINT ", .is_wide = false};
        case ModalType::ManageBreakpoints:
            return {.title = " MANAGE BREAKPOINTS & WATCHPOINTS ", .is_wide = false};
        case ModalType::SetSpeed:
            return {.title = " SET SIMULATION FREQUENCY ", .is_wide = false};
        case ModalType::InspectAddress:
            return {.title = " INSPECT MEMORY ADDRESS ", .is_wide = false};
        case ModalType::LoadBinary:
            return {.title = " LOAD PROGRAM BINARY ", .is_wide = false};
        case ModalType::LoadDiskImage:
            return {.title = " LOAD DISK IMAGE (Optional) ", .is_wide = false};
        case ModalType::Glossary:
            return {.title = " ARCHITECTURE GLOSSARY & CONCEPTS [?] ", .is_wide = true};
        case ModalType::Settings:
            return {.title = " SIMULATOR SETTINGS & CONFIGURATION ", .is_wide = true};
        case ModalType::ConfigureMisa:
            return {.title = " CONFIGURE CPU MISA & EXTENSIONS ", .is_wide = true};
        case ModalType::ConfigureSystem:
            return {.title = " PIPELINE & MICROARCHITECTURE CONFIGURATION ", .is_wide = true};
        case ModalType::Help:
            return {.title = " SIMULATOR KEYBOARD SHORTCUTS ", .is_wide = true};
        case ModalType::Notice:
            return {.title = is_notice_error ? std::format(" ❌ {} ", notice_title)
                                             : std::format(" ⚠️  {} ", notice_title),
                    .is_wide = true};
        case ModalType::PlatformChangeConfirm:
            return {.title = " ⚙️  PLATFORM PROFILE CHANGE: RELOAD REQUIRED ", .is_wide = true};
        case ModalType::None:
        default:
            return {.title = "", .is_wide = false};
    }
}

auto format_action_hint(std::string_view key, std::string_view action, std::string_view color)
    -> std::string {
    return std::format("\033[1m{}{}\033[0m {}{}\033[0m", color, key, kThemeMuted, action);
}

auto build_modal_footer(std::span<const ModalActionHint> hints) -> std::string {
    if (hints.empty()) {
        return "";
    }
    const auto style = get_active_theme_style();
    const bool is_ansi = (style == TuiThemeStyle::ClassicAnsi);
    const std::string_view sep = is_ansi ? " | " : " │ ";

    std::string out;
    for (size_t i = 0; i < hints.size(); ++i) {
        if (i > 0) {
            out += std::format("{}{}\033[0m", kThemeMuted, sep);
        }
        out += format_action_hint(hints[i].key, hints[i].action, hints[i].color);
    }
    return out;
}

auto build_modal_footer(std::initializer_list<ModalActionHint> hints) -> std::string {
    return build_modal_footer(std::span<const ModalActionHint>(hints.begin(), hints.size()));
}

auto build_modal_tab_bar(std::span<const std::string_view> tabs, size_t active_tab) -> std::string {
    const auto style = get_active_theme_style();
    const bool is_ansi = (style == TuiThemeStyle::ClassicAnsi);
    const std::string_view sep = is_ansi ? " | " : " │ ";

    std::string out = "  ";
    for (size_t i = 0; i < tabs.size(); ++i) {
        if (i > 0) {
            out += sep;
        }
        if (i == active_tab) {
            out += std::format("\033[7m [{}] {} \033[0m", i + 1, tabs[i]);
        } else {
            out += std::format(" {}[{}] {}\033[0m ", kThemeMuted, i + 1, tabs[i]);
        }
    }
    return out;
}

auto build_section_divider(std::string_view title, std::string_view color) -> std::string {
    return std::format("{}\033[1;35m── {} ──\033[0m", color, title);
}

auto build_menu_item_row(std::string_view label, std::string_view value, bool is_selected,
                         int label_width) -> std::string {
    const std::string prefix = is_selected ? std::format("{}>\033[0m ", kThemeMint) : "  ";
    const std::string name_str =
        std::format("{}{:<{}}\033[0m", is_selected ? "\033[1;37m" : kThemeText, label, label_width);
    return std::format("{}{} : {}", prefix, name_str, value);
}

void build_text_input_rows(std::vector<std::string>& content_rows, std::string_view prompt,
                           std::string_view input, std::string_view hint) {
    content_rows.push_back(std::format("{}{}\033[0m", kThemeText, prompt));
    content_rows.push_back(std::format("  \033[1m>\033[0m {}{}_\033[0m", kThemeMint, input));
    if (!hint.empty()) {
        content_rows.push_back(std::format("{}{}\033[0m", kThemeMuted, hint));
    }
}

}  // namespace simrv::tui::modals
