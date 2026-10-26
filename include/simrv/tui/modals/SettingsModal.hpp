/**
 * @file SettingsModal.hpp
 * @brief Modal dialog handler for simulator settings configuration.
 */
#pragma once

#include <algorithm>
#include <array>
#include <functional>
#include <string>
#include <vector>

#include "simrv/core/MachineConfig.hpp"
#include "simrv/tui/TuiModal.hpp"
#include "simrv/tui/framework/Modal.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::tui {
class TuiModal;
}

namespace simrv::tui::modals {

constexpr std::array<int, 14> kGeneralSettingRows = {5,  6,  7,  8,  9,  12, 13,
                                                     14, 17, 18, 19, 22, 25, 28};
constexpr int kGeneralSettingsContentRows = 29;

[[nodiscard]] constexpr inline auto general_setting_row(int item) noexcept -> int {
    return (item >= 0 && static_cast<size_t>(item) < kGeneralSettingRows.size())
               ? kGeneralSettingRows[static_cast<size_t>(item)]
               : 0;
}

[[nodiscard]] inline auto general_setting_at_row(int row) noexcept -> int {
    const auto it = std::ranges::find(kGeneralSettingRows, row);
    return it == kGeneralSettingRows.end()
               ? -1
               : static_cast<int>(std::distance(kGeneralSettingRows.begin(), it));
}

class SettingsModal : public framework::IModal {
   public:
    explicit SettingsModal(TuiModal* host, simrv::core::Machine* machine, SettingsDraft& draft)
        : host_(host), machine_(machine), draft_(draft) {}

    SettingsModal() : host_(nullptr), machine_(nullptr), draft_(owned_draft_) {}

    [[nodiscard]] auto title() const -> std::string override {
        return "Simulator Settings & Microarchitecture Configuration";
    }
    [[nodiscard]] auto type() const -> ModalType override { return ModalType::Settings; }

    void render_content(std::vector<std::string>& content_rows, int width, int height) override;
    auto handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction override;
    auto handle_control_click(int content_row, size_t action_index)
        -> framework::ModalAction override;
    auto handle_click(framework::ScreenCoord local_pt) -> framework::ModalAction override;
    auto handle_click(framework::ScreenCoord local_pt, framework::ScreenSize box_size)
        -> framework::ModalAction override;

    [[nodiscard]] auto cursor_row() const -> int override;
    void scroll_to_top() override;
    void scroll_to_bottom() override;
    void handle_wheel(int delta) override;
    auto handle_horizontal(int delta) -> bool override {
        adjust_setting(draft_, delta);
        return true;
    }

    static void open(SettingsDraft& draft, const simrv::core::Machine& machine,
                     framework::DensityMode density_mode = framework::DensityMode::Auto);
    static void move_cursor(SettingsDraft& draft, int delta);
    static void move_cursor(int& cursor, int delta);
    static void cycle_tab(SettingsDraft& draft, int delta = 1);
    static void set_tab(SettingsDraft& draft, uint8_t tab);
    static void adjust_setting(SettingsDraft& draft, int dir,
                               const simrv::core::Machine* machine = nullptr);
    static void adjust_setting(SettingsDraft& draft, int index, int dir,
                               const simrv::core::Machine* machine = nullptr);
    static void toggle_setting(SettingsDraft& draft, const simrv::core::Machine* machine = nullptr);
    static void toggle_setting(SettingsDraft& draft, int index,
                               const simrv::core::Machine* machine = nullptr);
    static void push_digit(SettingsDraft& draft, std::string& input, char c);
    static void pop_digit(SettingsDraft& draft, std::string& input);
    static void apply_isa_preset(SettingsDraft& draft, int preset_idx);
    static auto submit(const SettingsDraft& draft, simrv::core::Machine& machine,
                       const std::function<void(TuiRegPage)>& set_reg_page_cb,
                       const std::function<void(simrv::core::MachineConfig, SysConfigDraft, bool)>&
                           confirm_reconfiguration = {}) -> bool;
    static void render(std::vector<std::string>& content_rows,
                       const std::function<void(const std::string&)>& add_row_cb,
                       const SettingsDraft& draft, const simrv::core::Machine& machine);

   private:
    SettingsDraft owned_draft_;
    TuiModal* host_;
    simrv::core::Machine* machine_;
    SettingsDraft& draft_;
};

}  // namespace simrv::tui::modals
