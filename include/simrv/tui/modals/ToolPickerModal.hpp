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
#include "simrv/tui/framework/Modal.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::tui::modals {

struct ToolFilter {
    bool cycle_accurate = true;
    bool has_fp = true;
    bool has_vector = true;

    [[nodiscard]] constexpr auto is_available(const TuiPageInfo& info) const noexcept -> bool {
        if (info.cycle_accurate_only && !cycle_accurate) return false;
        if (info.floating_point_only && !has_fp) return false;
        if (info.vector_only && !has_vector) return false;
        return true;
    }
};

struct ToolEntry {
    TuiRegPage page;
    char accelerator;
    const char* label;
    const char* description;
    TuiCategoryGroup group;
};

class ToolPickerModal : public framework::IModal {
   public:
    explicit ToolPickerModal(int& slot_idx, int& cursor, TuiRegPage current_page, int& num_slots,
                             ToolFilter filter = {})
        : slot_idx_(slot_idx),
          cursor_(cursor),
          current_page_(current_page),
          num_slots_(num_slots),
          filter_(filter) {}

    ToolPickerModal()
        : slot_idx_(owned_slot_),
          cursor_(owned_cursor_),
          current_page_(TuiRegPage::GPR),
          num_slots_(owned_num_slots_),
          filter_({}) {}

    [[nodiscard]] auto title() const -> std::string override { return "Workbench Slot Tools"; }
    [[nodiscard]] auto type() const -> ModalType override { return ModalType::ToolPicker; }

    void render_content(std::vector<std::string>& content_rows, int width, int height) override;
    auto handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction override;
    auto handle_control_click(int content_row, size_t action_index)
        -> framework::ModalAction override;
    auto handle_click(framework::ScreenCoord local_pt) -> framework::ModalAction override;

    [[nodiscard]] auto cursor_row() const -> int override;
    void scroll_to_top() override;
    void scroll_to_bottom() override;
    void handle_wheel(int delta) override;
    auto handle_horizontal(int delta) -> bool override {
        if (num_slots_ > 0) slot_idx_ = (slot_idx_ + delta) % num_slots_;
        if (slot_idx_ < 0) slot_idx_ += num_slots_;
        return true;
    }

    [[nodiscard]] static auto make_filter(const simrv::core::Machine& machine, size_t hart = 0)
        -> ToolFilter;
    [[nodiscard]] static auto all_tools(ToolFilter filter = {}) -> std::vector<ToolEntry>;
    [[nodiscard]] static auto find_by_accelerator(char c, ToolFilter filter = {})
        -> std::optional<TuiRegPage>;
    [[nodiscard]] static auto tool_at_index(size_t index, ToolFilter filter = {})
        -> std::optional<TuiRegPage>;
    [[nodiscard]] static auto tool_index_at_row(int content_row, ToolFilter filter = {})
        -> std::optional<size_t>;
    [[nodiscard]] static auto row_for_tool_index(size_t index, ToolFilter filter = {}) -> int;
    [[nodiscard]] static auto total_tool_count(ToolFilter filter = {}) -> size_t;
    [[nodiscard]] static auto content_row_count(ToolFilter filter = {}) -> int;
    static void render(std::vector<std::string>& content_rows,
                       const std::function<void(const std::string&)>& add_row_cb, int slot_idx,
                       int cursor, TuiRegPage current_page, int term_height, int box_w,
                       int num_slots = 3, ToolFilter filter = {});

   private:
    int owned_slot_ = 0;
    int owned_cursor_ = 0;
    int owned_num_slots_ = 3;
    int& slot_idx_;
    int& cursor_;
    TuiRegPage current_page_;
    int& num_slots_;
    ToolFilter filter_;
};

}  // namespace simrv::tui::modals
