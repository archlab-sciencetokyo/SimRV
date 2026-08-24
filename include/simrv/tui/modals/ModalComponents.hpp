/**
 * @file ModalComponents.hpp
 * @brief Dynamic, reusable components for modal headers, footers, tabs, and input forms.
 */
#pragma once

#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/tui/TuiModal.hpp"
#include "simrv/tui/TuiTheme.hpp"

namespace simrv::tui::modals {

struct ModalActionHint {
    std::string key;
    std::string action;
    std::string color = kThemeSky;
};

struct ModalMetadata {
    std::string title{};
    std::string subtitle{};
    bool is_wide = false;
};

/**
 * @brief Dynamic registry returning metadata (title, width, headers) for all modal types.
 */
auto get_modal_metadata(ModalType type, bool is_notice_error = false,
                        std::string_view notice_title = "") -> ModalMetadata;

/**
 * @brief Format a single filled action keycap followed by its plain-text label.
 */
auto format_action_hint(std::string_view key, std::string_view action,
                        std::string_view color = kThemeSky) -> std::string;

/**
 * @brief Formats a unified footer action row with themed midpoint separators.
 */
auto build_modal_footer(std::span<const ModalActionHint> hints) -> std::string;
auto build_modal_footer(std::initializer_list<ModalActionHint> hints) -> std::string;

/**
 * @brief Formats an aligned horizontal tab bar with active tab highlighted.
 */
auto build_modal_tab_bar(std::span<const std::string_view> tabs, size_t active_tab) -> std::string;

/**
 * @brief Resolve a shared tab/footer control row against the modal's final inner width.
 */
auto align_modal_control_row(std::string row, int inner_width) -> std::string;
auto layout_modal_control_row(std::string row, int inner_width)
    -> framework::RenderedControl;

/**
 * @brief Formats a styled section category divider banner, e.g. "── ISA Extensions ──".
 */
auto build_section_divider(std::string_view title, std::string_view color = kThemeText)
    -> std::string;

/**
 * @brief Formats an aligned key-value / selectable menu row with pointer.
 */
auto build_menu_item_row(std::string_view label, std::string_view value, bool is_selected,
                         int label_width = 29) -> std::string;

/**
 * @brief Formats prompt and cursor input rows for text dialogs.
 */
void build_text_input_rows(std::vector<std::string>& content_rows, std::string_view prompt,
                           std::string_view input, std::string_view hint = "");

}  // namespace simrv::tui::modals
