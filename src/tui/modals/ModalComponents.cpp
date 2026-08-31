/**
 * @file ModalComponents.cpp
 * @brief Dynamic, reusable components for modal headers, footers, tabs, and input forms.
 */
#include "simrv/tui/modals/ModalComponents.hpp"

#include <algorithm>
#include <format>

#include "simrv/tui/framework/Components.hpp"

namespace simrv::tui::modals {

namespace {

constexpr std::string_view kControlRowMarker = "\033[0;0m";

[[nodiscard]] auto unbox_key(std::string_view key) -> std::string_view {
    if (key.size() >= 2 && key.front() == '[' && key.back() == ']') {
        return key.substr(1, key.size() - 2);
    }
    return key;
}

[[nodiscard]] auto unbox_value(std::string_view value) -> std::string {
    std::string result(value);
    std::size_t open = std::string::npos;
    for (std::size_t i = 0; i < result.size(); ++i) {
        if (result[i] == '[' && (i == 0 || result[i - 1] != '\033')) {
            open = i;
            break;
        }
    }
    if (open == std::string::npos) return result;
    auto const close = result.find(']', open + 1);
    if (close == std::string::npos) return result;
    result.erase(close, 1);
    result.erase(open, 1);
    return result;
}

}  // namespace

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
    return std::format("\033[1;7m{} {} \033[0m {}{}\033[0m", color, unbox_key(key), kThemeMuted,
                       action);
}

auto build_modal_footer(std::span<const ModalActionHint> hints) -> std::string {
    if (hints.empty()) {
        return "";
    }
    constexpr std::string_view sep = " · ";

    std::string out(kControlRowMarker);
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
    constexpr std::string_view sep = " · ";

    std::string out(kControlRowMarker);
    for (size_t i = 0; i < tabs.size(); ++i) {
        if (i > 0) {
            out += std::format("{}{}\033[0m", kThemeMuted, sep);
        }
        if (i == active_tab) {
            out += std::format("\033[1;7m{} {} \033[0m \033[1;4m{}{}\033[0m", kThemeSky, i + 1,
                               kThemeSky, tabs[i]);
        } else {
            out += std::format("\033[7m{} {} \033[0m {}{}\033[0m", kThemeMuted, i + 1, kThemeMuted,
                               tabs[i]);
        }
    }
    return out;
}

auto align_modal_control_row(std::string row, int inner_width) -> std::string {
    return layout_modal_control_row(std::move(row), inner_width).text;
}

auto layout_modal_control_row(std::string row, int inner_width) -> framework::RenderedControl {
    auto const marker = row.find(kControlRowMarker);
    if (marker == std::string::npos) {
        return {.text = std::move(row), .width = inner_width, .spans = {}};
    }
    row.erase(marker, kControlRowMarker.size());
    int const row_width = get_display_width(row);
    int const left = std::max(0, (inner_width - row_width) / 2);

    framework::RenderedControl result;
    result.text = framework::align_row({.text = std::move(row),
                                        .alignment = framework::HorizontalAlign::Center,
                                        .role = framework::Role::Accent,
                                        .kind = framework::RowKind::Controls},
                                       inner_width)
                      .text;
    result.width = inner_width;

    // Action groups are separated by a three-cell themed midpoint. Their measured spans include
    // both the filled keycap and its label, matching the visual click affordance.
    std::string plain;
    bool escape = false;
    bool csi = false;
    for (char ch : result.text) {
        if (ch == '\033') {
            escape = true;
            csi = false;
        } else if (escape) {
            if (!csi && ch == '[')
                csi = true;
            else if (!csi || (ch >= '@' && ch <= '~'))
                escape = csi = false;
        } else {
            plain.push_back(ch);
        }
    }
    constexpr std::string_view separator = " · ";
    while (!plain.empty() && plain.back() == ' ') plain.pop_back();
    int start = left;
    std::size_t byte_start = static_cast<std::size_t>(left);
    std::size_t index = 0;
    while (byte_start < plain.size()) {
        auto const end = plain.find(separator, byte_start);
        std::string_view const group = std::string_view(plain).substr(
            byte_start, end == std::string::npos ? std::string::npos : end - byte_start);
        int const group_width = get_display_width(group);
        result.spans.push_back(
            {.start = start, .width = group_width, .id = std::to_string(index++)});
        if (end == std::string::npos) break;
        start += group_width + 3;
        byte_start = end + separator.size();
    }
    return result;
}

auto build_section_divider(std::string_view title, std::string_view color) -> std::string {
    return std::format("{}\033[1;35m── {} ──\033[0m", color, title);
}

auto build_menu_item_row(std::string_view label, std::string_view value, bool is_selected,
                         int label_width) -> std::string {
    const std::string name_str =
        is_selected ? std::format("\033[1;7m{} {:<{}} \033[0m", kThemeSky, label, label_width)
                    : std::format(" {}{:<{}} \033[0m", kThemeText, label, label_width);
    return std::format("{} : {}", name_str, unbox_value(value));
}

void build_text_input_rows(std::vector<std::string>& content_rows, std::string_view prompt,
                           std::string_view input, std::string_view hint) {
    content_rows.push_back(std::format("{}{}\033[0m", kThemeText, prompt));
    content_rows.push_back(
        std::format("  \033[1;7m{} INPUT \033[0m {}{}_\033[0m", kThemeSky, kThemeText, input));
    if (!hint.empty()) {
        content_rows.push_back(std::format("{}{}\033[0m", kThemeMuted, hint));
    }
}

}  // namespace simrv::tui::modals
