/**
 * @file ToolPickerModal.hpp
 * @brief Categorized keyboard-driven tool picker modal for multi-column workbench slots.
 */
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "simrv/tui/TuiTypes.hpp"

namespace simrv::tui::modals {

struct ToolEntry {
    TuiRegPage page;
    char accelerator;
    const char* label;
    const char* description;
    TuiCategoryGroup group;
};

class ToolPickerModal {
   public:
    [[nodiscard]] static auto all_tools() -> const std::vector<ToolEntry>&;
    [[nodiscard]] static auto find_by_accelerator(char c) -> std::optional<TuiRegPage>;
    [[nodiscard]] static auto tool_at_index(size_t index) -> std::optional<TuiRegPage>;
    [[nodiscard]] static auto tool_index_at_row(int content_row) -> std::optional<size_t>;
    [[nodiscard]] static auto row_for_tool_index(size_t index) -> int;
    [[nodiscard]] static auto total_tool_count() -> size_t;
    static void render(std::vector<std::string>& content_rows,
                       const std::function<void(const std::string&)>& add_row_cb, int slot_idx,
                       int cursor, TuiRegPage current_page, int term_height, int box_w,
                       int num_slots = 3);
};

}  // namespace simrv::tui::modals
