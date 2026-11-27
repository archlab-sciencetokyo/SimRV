/**
 * @file ToolPickerModal.cpp
 * @brief Categorized keyboard-driven tool picker modal for multi-column workbench slots.
 */
#include "simrv/tui/modals/ToolPickerModal.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <span>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/modals/ModalComponents.hpp"

namespace simrv::tui::modals {

auto ToolPickerModal::make_filter(const simrv::core::Machine& machine, size_t hart,
                                  bool display_graphics_supported) -> ToolFilter {
    const bool cycle_mode = machine.runtime_profile.is_cycle_mode();
    uint64_t misa = 0;
    if (machine.num_harts() > 0) {
        const size_t effective_hart = std::min(hart, machine.num_harts() - 1);
        misa = machine.hart(effective_hart).state().misa;
    }
    const bool has_fp = (misa & ((1ULL << ('f' - 'a')) | (1ULL << ('d' - 'a')))) != 0;
    const bool has_vector = (misa & (1ULL << ('v' - 'a'))) != 0;
    return ToolFilter{.cycle_accurate = cycle_mode,
                      .has_fp = has_fp,
                      .has_vector = has_vector,
                      .display_graphics_supported = display_graphics_supported};
}

auto ToolPickerModal::all_tools(ToolFilter filter) -> std::vector<ToolEntry> {
    std::vector<const TuiPageInfo*> ordered;
    ordered.reserve(std::size(kTuiPages));
    for (const auto& info : kTuiPages) {
        if (!filter.is_available(info)) continue;
        ordered.push_back(&info);
    }
    std::ranges::sort(ordered, {}, &TuiPageInfo::picker_order);

    std::vector<ToolEntry> entries;
    entries.reserve(ordered.size());
    for (const auto* info : ordered) {
        entries.push_back({.page = info->page,
                           .accelerator = info->accelerator,
                           .label = info->picker_label.data(),
                           .description = info->description.data(),
                           .group = info->group});
    }
    return entries;
}

auto ToolPickerModal::total_tool_count(ToolFilter filter) -> size_t {
    return all_tools(filter).size();
}

auto ToolPickerModal::content_row_count(ToolFilter filter) -> int {
    const auto tools = all_tools(filter);
    if (tools.empty()) return 6;
    int group_headers = 0;
    auto current_grp = static_cast<TuiCategoryGroup>(99);
    for (const auto& tool : tools) {
        if (tool.group != current_grp) {
            current_grp = tool.group;
            ++group_headers;
        }
    }
    return 2 + group_headers + static_cast<int>(tools.size()) + 2;
}

auto ToolPickerModal::tool_at_index(size_t index, ToolFilter filter) -> std::optional<TuiRegPage> {
    const auto list = all_tools(filter);
    if (index < list.size()) {
        return list[index].page;
    }
    return std::nullopt;
}

auto ToolPickerModal::tool_index_at_row(int content_row, ToolFilter filter)
    -> std::optional<size_t> {
    const auto tools = all_tools(filter);
    int row = 2;
    for (size_t i = 0; i < tools.size(); ++i) {
        if (i == 0 || tools[i].group != tools[i - 1].group) {
            ++row;
        }
        if (row == content_row) {
            return i;
        }
        ++row;
    }
    return std::nullopt;
}

auto ToolPickerModal::row_for_tool_index(size_t index, ToolFilter filter) -> int {
    const auto tools = all_tools(filter);
    if (index >= tools.size()) {
        return 3;
    }
    int row = 2;
    for (size_t i = 0; i <= index; ++i) {
        if (i == 0 || tools[i].group != tools[i - 1].group) {
            ++row;
        }
        if (i == index) {
            return row;
        }
        ++row;
    }
    return 3;
}

auto ToolPickerModal::find_by_accelerator(char c, ToolFilter filter) -> std::optional<TuiRegPage> {
    const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const auto& item : all_tools(filter)) {
        if (item.accelerator == lower) {
            return item.page;
        }
    }
    return std::nullopt;
}

void ToolPickerModal::render(std::vector<std::string>& /*content_rows*/,
                             const std::function<void(const std::string&)>& add_row_cb,
                             int slot_idx, int cursor, TuiRegPage current_page, int /*term_height*/,
                             int /*box_w*/, int num_slots, ToolFilter filter) {
    // Row 0: Column selection tab bar
    static constexpr std::array<std::string_view, 4> kTabLabels = {"Column 1", "Column 2",
                                                                   "Column 3", "Column 4"};
    int const effective_slots = std::clamp(num_slots, 1, 4);
    std::vector<std::string_view> tabs;
    tabs.reserve(static_cast<size_t>(effective_slots));
    for (int s = 0; s < effective_slots; ++s) {
        tabs.push_back(kTabLabels.at(static_cast<size_t>(s)));
    }
    add_row_cb(build_modal_tab_bar(tabs, static_cast<size_t>(slot_idx)));

    // Row 1: Target slot description
    add_row_cb(std::format(" Assign a tool to Column {} (currently: {}):", slot_idx + 1,
                           get_page_name(current_page)));

    const auto tools = all_tools(filter);
    auto current_grp = static_cast<TuiCategoryGroup>(99);

    for (size_t i = 0; i < tools.size(); ++i) {
        const auto& tool = tools[i];
        if (tool.group != current_grp) {
            current_grp = tool.group;
            add_row_cb(
                std::format(" \033[1;36m── {} ──\033[0m", get_category_picker_header(current_grp)));
        }

        const bool is_cursor = (static_cast<int>(i) == cursor);
        const bool is_active = (tool.page == current_page);

        std::string badge = std::format("[{}]", tool.accelerator);
        std::string active_tag = is_active ? " \033[32m[Active]\033[0m" : "";

        if (is_cursor) {
            add_row_cb(std::format(" \033[1;7m {:<4} {:<22} \033[0m {}{:<40}\033[0m{}", badge,
                                   tool.label, kThemeMuted, tool.description, active_tag));
        } else {
            add_row_cb(std::format("  \033[1;33m{:<4}\033[0m {:<22} {}{:<40}\033[0m{}", badge,
                                   tool.label, kThemeMuted, tool.description, active_tag));
        }
    }

    // Row 19: Blank spacing row
    add_row_cb("");
    // Row 20: Clickable footer action row
    add_row_cb(build_modal_footer({{.key = "[Enter / Click]", .action = "Assign Tool"},
                                   {.key = "[Up/Down]", .action = "Navigate"},
                                   {.key = "[Tab / Click]", .action = "Target Col"},
                                   {.key = "[Esc / q]", .action = "Cancel"}}));
}

void ToolPickerModal::render_content(std::vector<std::string>& content_rows, int width,
                                     int height) {
    auto add_row = [&](std::string s) { content_rows.push_back(std::move(s)); };
    render(content_rows, add_row, slot_idx_, cursor_, current_page_, height, width, num_slots_,
           filter_);
}

auto ToolPickerModal::cursor_row() const -> int {
    return row_for_tool_index(static_cast<size_t>(cursor_), filter_);
}

void ToolPickerModal::scroll_to_top() { cursor_ = 0; }

void ToolPickerModal::scroll_to_bottom() {
    const int count = static_cast<int>(total_tool_count(filter_));
    if (count > 0) cursor_ = count - 1;
}

void ToolPickerModal::handle_wheel(int delta) {
    const int count = static_cast<int>(total_tool_count(filter_));
    if (count <= 0) return;
    cursor_ = (cursor_ + delta) % count;
    if (cursor_ < 0) cursor_ += count;
}

auto ToolPickerModal::handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction {
    if (byte == 27 || key == simrv::tui::TuiKey::Esc || byte == 'q' || byte == 'Q') {
        return framework::ModalAction::Close;
    }
    if (key == simrv::tui::TuiKey::Tab) {
        if (num_slots_ > 0) slot_idx_ = (slot_idx_ + 1) % num_slots_;
        return framework::ModalAction::Continue;
    }
    if (key == simrv::tui::TuiKey::BackTab) {
        if (num_slots_ > 0) slot_idx_ = (slot_idx_ + num_slots_ - 1) % num_slots_;
        return framework::ModalAction::Continue;
    }
    if (key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline || byte == ' ') {
        return framework::ModalAction::Submit;
    }
    char acc = static_cast<char>(std::tolower(static_cast<unsigned char>(byte)));
    if (key == simrv::tui::TuiKey::y || key == simrv::tui::TuiKey::Y) acc = 'y';
    auto found_page = find_by_accelerator(acc, filter_);
    if (found_page.has_value()) {
        const auto tools = all_tools(filter_);
        for (size_t i = 0; i < tools.size(); ++i) {
            if (tools[i].page == *found_page) {
                cursor_ = static_cast<int>(i);
                break;
            }
        }
        return framework::ModalAction::Submit;
    }
    return framework::ModalAction::Continue;
}

auto ToolPickerModal::handle_control_click(int content_row, size_t action_index)
    -> framework::ModalAction {
    if (content_row == 0) {
        if (num_slots_ > 0)
            slot_idx_ = std::clamp(static_cast<int>(action_index), 0, num_slots_ - 1);
        return framework::ModalAction::Continue;
    }
    if (action_index == 0) {
        return framework::ModalAction::Submit;
    }
    if (action_index == 1) {
        handle_wheel(1);
        return framework::ModalAction::Continue;
    }
    if (action_index == 2) {
        if (num_slots_ > 0) slot_idx_ = (slot_idx_ + 1) % num_slots_;
        return framework::ModalAction::Continue;
    }
    if (action_index == 3) {
        return framework::ModalAction::Close;
    }
    return framework::ModalAction::Continue;
}

auto ToolPickerModal::handle_click(framework::ScreenCoord local_pt) -> framework::ModalAction {
    int content_row = local_pt.y - 1;
    auto tool_idx = tool_index_at_row(content_row, filter_);
    if (tool_idx.has_value()) {
        cursor_ = static_cast<int>(*tool_idx);
        return framework::ModalAction::Submit;
    }
    return framework::ModalAction::Continue;
}

}  // namespace simrv::tui::modals
