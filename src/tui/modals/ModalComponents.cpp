/**
 * @file ModalComponents.cpp
 * @brief Dynamic, reusable components for modal headers, footers, tabs, and input forms.
 */
#include "simrv/tui/modals/ModalComponents.hpp"

#include <algorithm>
#include <cctype>
#include <format>

#include "simrv/tui/framework/Components.hpp"

namespace simrv::tui::modals {

namespace {

constexpr std::string_view kControlRowMarker = "\033[0;0m";
constexpr std::string_view kControlGroupMarker = "\033[0;0m\033[0;0m";

[[nodiscard]] auto unbox_key(std::string_view key) -> std::string_view {
    if (key.size() >= 2 && key.front() == '[' && key.back() == ']') {
        return key.substr(1, key.size() - 2);
    }
    return key;
}

[[nodiscard]] auto capitalize_key_tokens(std::string_view key) -> std::string {
    std::string result(key);
    for (std::size_t i = 0; i < result.size(); ++i) {
        const bool starts_token = i == 0 || result[i - 1] == '/' ||
                                  std::isspace(static_cast<unsigned char>(result[i - 1])) != 0;
        const bool ends_token = i + 1 == result.size() || result[i + 1] == '/' ||
                                std::isspace(static_cast<unsigned char>(result[i + 1])) != 0;
        if (starts_token && ends_token &&
            std::isalpha(static_cast<unsigned char>(result[i])) != 0) {
            result[i] = static_cast<char>(
                std::toupper(static_cast<unsigned char>(result[i])));
        }
    }
    return result;
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
        case ModalType::Help:
            return {.title = " SIMULATOR KEYBOARD SHORTCUTS ", .is_wide = true};
        case ModalType::Notice:
            return {.title = is_notice_error ? std::format(" ❌ {} ", notice_title)
                                             : std::format(" ⚠️  {} ", notice_title),
                    .is_wide = true};
        case ModalType::PlatformChangeConfirm:
            return {.title = " ⚙️  PLATFORM PROFILE CHANGE: RELOAD REQUIRED ", .is_wide = true};
        case ModalType::LayoutPresets:
            return {.title = " WORKBENCH LAYOUT PRESETS ", .is_wide = true};
        case ModalType::ToolPicker:
            return {.title = " SELECT COLUMN TOOL [o] ", .is_wide = true};
        case ModalType::SaveCpuConfig:
            return {.title = " SAVE CPU MODEL CONFIGURATION (.cfg) ", .is_wide = false};
        case ModalType::LoadCpuConfig:
            return {.title = " LOAD CPU MODEL CONFIGURATION (.cfg) ", .is_wide = false};
        case ModalType::LoadFilePicker:
            return {.title = " SELECT FILE ", .is_wide = true};
        case ModalType::MemoryDump:
            return {.title = " MEMORY DUMP & HEX INSPECTOR [d] ", .is_wide = true};
        case ModalType::SwitchHart:
            return {.title = " MULTI-HART SMP SWITCHER [H/n] ", .is_wide = true};
        case ModalType::LogViewer:
            return {.title = " SIMULATOR LOG VIEWER [L] ", .is_wide = true};
        case ModalType::MissionObjective:
            return {.title = " LESSON TASKS / GUIDE [F12] ", .is_wide = true};
        case ModalType::None:
        default:
            return {.title = "", .is_wide = false};
    }
}

auto format_action_hint(std::string_view key, std::string_view action, std::string_view color)
    -> std::string {
    return std::format("\033[1;7m{} {} \033[0m {}{}\033[0m", color,
                       capitalize_key_tokens(unbox_key(key)), kThemeMuted, action);
}

auto build_modal_footer(std::span<const ModalActionHint> hints) -> std::string {
    if (hints.empty()) {
        return "";
    }
    const std::string_view sep =
        framework::active_render_context().density == framework::Density::Compact ? " " : " · ";

    std::string out(kControlRowMarker);
    for (size_t i = 0; i < hints.size(); ++i) {
        if (i > 0) {
            out += std::format("{}{}\033[0m", kThemeMuted, sep);
            out += kControlGroupMarker;
        }
        out += format_action_hint(hints[i].key, hints[i].action, hints[i].color);
    }
    return out;
}

auto build_modal_footer(std::initializer_list<ModalActionHint> hints) -> std::string {
    return build_modal_footer(std::span<const ModalActionHint>(hints.begin(), hints.size()));
}

auto build_modal_tab_bar(std::span<const std::string_view> tabs, size_t active_tab) -> std::string {
    const std::string_view sep =
        framework::active_render_context().density == framework::Density::Compact ? " " : " · ";

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
    const bool compact = framework::active_render_context().density == framework::Density::Compact;
    int const left = compact ? 0 : std::max(0, (inner_width - row_width) / 2);
    const std::string_view separator = compact ? " " : " · ";
    const int separator_width = static_cast<int>(separator.size());
    const int right = std::max(0, inner_width - row_width - left);

    framework::RenderedControl result;
    result.text = framework::align_row({.text = std::move(row),
                                        .alignment = compact ? framework::HorizontalAlign::Left
                                                             : framework::HorizontalAlign::Center,
                                        .role = framework::Role::Accent,
                                        .kind = framework::RowKind::Controls},
                                       inner_width)
                      .text;
    result.width = inner_width;

    // The invisible group marker avoids confusing spaces inside padded keycaps or compound
    // labels with the visible action separator.
    std::size_t segment_start = 0;
    int span_start = left;
    std::size_t index = 0;
    while (segment_start <= result.text.size()) {
        const auto marker_pos = result.text.find(kControlGroupMarker, segment_start);
        const bool has_next = marker_pos != std::string::npos;
        const auto segment_end = has_next ? marker_pos : result.text.size();
        int segment_width = get_display_width(
            std::string_view(result.text).substr(segment_start, segment_end - segment_start));
        if (index == 0) segment_width = std::max(0, segment_width - left);
        if (has_next) segment_width = std::max(0, segment_width - separator_width);
        if (!has_next) segment_width = std::max(0, segment_width - right);
        if (segment_width > 0 && span_start < inner_width) {
            const int visible_width = std::min(segment_width, inner_width - span_start);
            result.spans.push_back({.start = span_start,
                                    .width = visible_width,
                                    .id = std::to_string(index)});
        }
        if (!has_next) break;
        span_start += segment_width + separator_width;
        segment_start = marker_pos + kControlGroupMarker.size();
        ++index;
    }
    return result;
}

auto build_section_divider(std::string_view title, std::string_view color) -> std::string {
    if (framework::active_render_context().density == framework::Density::Compact)
        return std::format("{}{}\033[0m", color, title);
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
