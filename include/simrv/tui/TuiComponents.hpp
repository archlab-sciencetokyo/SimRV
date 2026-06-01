/**
 * @file TuiComponents.hpp
 * @brief OOP Widgets for the TUI system.
 */
#pragma once

#include <string>
#include <vector>
#include <chrono>

#include "simrv/tui/TuiWidget.hpp"
#include "simrv/tui/Tui.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::tui {


/**
 * @class RegisterPane
 * @brief Renders architectural state (GPR, FPR, VEC, Pipeline).
 */

#include <vector>
#include <array>
#include <vector>



class RegisterPane : public TuiWidget {
   public:
    explicit RegisterPane(simrv::core::Machine& machine) : machine_(machine) {
        cached_gpr_.fill(0);
        cached_fpr_.fill(0);
        cached_vec_.fill(0);
    }
    ~RegisterPane() override = default;

    [[nodiscard]] auto render_row(int row_idx, int width) -> std::string override;

    void set_page(TuiRegPage page) { page_ = page; }
    [[nodiscard]] auto get_page() const -> TuiRegPage { return page_; }
    
    void update_cache();
    void set_kips(uint64_t kips) { kips_ = kips; }
    void set_kips_history(const std::vector<uint64_t>& history) { kips_history_ = history; }
    void set_paused(bool paused) { paused_ = paused; }
    
   private:
    [[nodiscard]] auto get_sparkline_string(int width) -> std::string;
    [[nodiscard]] auto get_row_uncached(int row_idx, int pane_width) -> std::string;
    simrv::core::Machine& machine_;
    TuiRegPage page_ = TuiRegPage::GPR;
    bool paused_ = true;
    
    std::array<uint32_t, 32> cached_gpr_;
    std::array<uint64_t, 32> cached_fpr_;
    std::array<uint64_t, 32> cached_vec_; // Or whatever type it uses
    std::array<std::string, 25> cached_left_rows_;
    
    uint64_t kips_ = 0;
    std::vector<uint64_t> kips_history_;
};


/**
 * @class ConsolePane
 * @brief Renders standard log output with scrollback.
 */

class ConsolePane : public TuiWidget {
   public:
    ConsolePane() = default;
    ~ConsolePane() override = default;

    [[nodiscard]] auto render_row(int row_idx, int width) -> std::string override;
    
    void set_lines(const std::vector<std::string>& lines) { lines_ = lines; }
    void set_scroll_offset(int offset) { scroll_offset_ = offset; }
    
   private:
    std::vector<std::string> lines_;
    int scroll_offset_ = 0;
};


/**
 * @class StatusBar
 * @brief Renders the top and bottom header lines.
 */
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

    [[nodiscard]] auto render_row(int row_idx, int width) -> std::string override;

   private:
    [[nodiscard]] auto get_sparkline_string(int width) -> std::string;

    simrv::core::Machine& machine_;
    bool paused_ = true;
    std::string status_override_;
    TuiRegPage active_page_ = TuiRegPage::GPR;
    TuiLayout layout_ = TuiLayout::Split;
    int scroll_offset_ = 0;
    int left_width_ = 0;
    int right_width_ = 0;

    std::vector<uint64_t> kips_history_;
    uint64_t last_icount_ = 0;
};

}  // namespace simrv::device
