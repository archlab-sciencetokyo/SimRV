/**
 * @file RegisterPane.hpp
 * @brief OOP Widget for Register and Pipeline visualizer.
 */
#pragma once

#include <vector>
#include <array>
#include <string>
#include "simrv/tui/TuiWidget.hpp"
#include "simrv/tui/Tui.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::tui {

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
    void set_visible_rows(int rows) { visible_rows_ = rows; }
    void set_active_runtime(double secs) { active_runtime_ = secs; }
    void scroll(int lines);
    void reset_scroll() { scroll_offset_ = 0; }
    [[nodiscard]] auto get_scroll_offset() const -> int { return scroll_offset_; }
    
   private:
    [[nodiscard]] auto get_sparkline_string(int width) -> std::string;
    [[nodiscard]] auto get_row_uncached(int row_idx, int pane_width) -> std::string;
    [[nodiscard]] auto get_explain_rows(int width) -> std::vector<std::string>;
    simrv::core::Machine& machine_;
    TuiRegPage page_ = TuiRegPage::GPR;
    bool paused_ = true;
    
    std::array<Register, 32> cached_gpr_;
    std::array<uint64_t, 32> cached_fpr_;
    std::array<uint64_t, 32> cached_vec_;
    std::array<std::string, 80> cached_left_rows_;
    int last_width_ = 0;
    
    uint64_t kips_ = 0;
    std::vector<uint64_t> kips_history_;
    int visible_rows_ = 25;
    int scroll_offset_ = 0;
    double active_runtime_ = 0.0;
};

}  // namespace simrv::tui
