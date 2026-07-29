/**
 * @file StatusBar.hpp
 * @brief OOP Widget for TUI headers, footers, and stats.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include "simrv/tui/TuiWidget.hpp"
#include "simrv/tui/Tui.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::tui {

enum class TuiFooterAction : uint8_t {
    Step,
    StepBack,
    StepN,
    CycleRegs,
    CycleTools,
    SetBreakpoint,
    SetWatchpoint,
    TogglePcBreakpoint,
    SetStepSize,
    SetSpeed,
    InspectMem,
    LoadBinary,
    ToggleHelp,
    RunPause,
    Quit,
    CycleLayout,
    TogglePanel,
    ToggleTrace,
    OpenSettings,
    ConfigureMisa,
    ConfigureSystem,
    ManageBreakpoints
};

class StatusBar : public TuiWidget {
   public:
    explicit StatusBar(simrv::core::Machine& machine);

    void set_paused(bool paused) { paused_ = paused; }
    void set_status_override(const std::string& status) { status_override_ = status; }
    void update_kips(uint64_t current_kips);

    void set_active_page(TuiRegPage page) { active_page_ = page; }
    void set_scroll_offset(int offset) { scroll_offset_ = offset; }
    void set_layout(TuiLayout layout) { layout_ = layout; }
    void set_pane_widths(int left, int right) { left_width_ = left; right_width_ = right; }
    void set_right_panel_mode(TuiRightPanelMode mode) { right_panel_mode_ = mode; }
    void set_trace_enabled(bool enabled) { trace_enabled_ = enabled; }

    [[nodiscard]] auto is_pos_on_status_badge(int x, int width) const -> bool;
    [[nodiscard]] auto is_pos_on_right_panel_mode(int x) const -> bool;
    [[nodiscard]] auto get_footer_action_at_col(int col, int row_idx = 0) const -> std::optional<TuiFooterAction>;
    [[nodiscard]] auto render_row(int row_idx, int width) -> std::string override;

   private:
    simrv::core::Machine& machine_;
    bool paused_ = true;
    bool trace_enabled_ = false;
    std::string status_override_;
    TuiRegPage active_page_ = TuiRegPage::GPR;
    TuiLayout layout_ = TuiLayout::Split;
    TuiRightPanelMode right_panel_mode_ = TuiRightPanelMode::Terminal;
    int scroll_offset_ = 0;
    int left_width_ = 0;
    int right_width_ = 0;

    uint64_t last_icount_ = 0;
    uint64_t kips_ = 0;
};

}  // namespace simrv::tui
