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
class CPU;
struct ArchState;
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
    [[nodiscard]] auto get_row_uncached(int logical_row, int width) -> std::string;
    [[nodiscard]] auto get_explain_rows(int width) -> std::vector<std::string>;

    [[nodiscard]] auto is_single_column(int width) const -> bool;
    [[nodiscard]] auto get_total_rows(int width) -> int;
    [[nodiscard]] auto render_active_spinner(int logical_row, int width) -> std::string;
    [[nodiscard]] auto render_registers_single_column(const simrv::core::ArchState& st, int logical_row, int width) -> std::string;
    [[nodiscard]] auto render_registers_double_column(const simrv::core::ArchState& st, int logical_row, int col_width, int right_width) -> std::string;
    [[nodiscard]] auto render_registers_or_pipeline(const simrv::core::CPU& cpu, const simrv::core::ArchState& st, int logical_row, int col_width, int right_width, int width, bool single_column) -> std::string;
    [[nodiscard]] auto render_system_or_pipeline_extended(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width, bool single_column) -> std::string;
    [[nodiscard]] auto render_perf_or_debug(const simrv::core::CPU& cpu, int logical_row, int width, bool single_column) -> std::string;
    [[nodiscard]] auto render_pipeline_stages(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string;
    [[nodiscard]] auto render_cache_stats(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string;
    [[nodiscard]] auto render_pipeline_timeline(const simrv::core::CPU& cpu, int logical_row, int width) -> std::string;
    [[nodiscard]] auto render_pipeline_stages_cycle_accurate(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string;
    [[nodiscard]] auto render_pipeline_stages_ca_core(const simrv::core::CPU& cpu, int stage_idx, int width) -> std::string;
    [[nodiscard]] auto render_pipeline_stages_ca_hazards(const simrv::core::CPU& cpu, int stage_idx, int col_width, int right_width) -> std::string;
    [[nodiscard]] auto render_pipeline_stages_ca_pred(const simrv::core::CPU& cpu, int stage_idx, int width) -> std::string;
    [[nodiscard]] auto render_pipeline_stages_functional(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string;
    [[nodiscard]] auto render_pipeline_stages_functional_low(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string;
    [[nodiscard]] auto render_pipeline_stages_functional_low_part1(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string;
    [[nodiscard]] auto render_pipeline_stages_functional_low_part2(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string;
    [[nodiscard]] auto render_pipeline_stages_functional_high(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string;
    [[nodiscard]] auto render_system_state(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string;
    [[nodiscard]] auto render_machine_performance_stats(const simrv::core::CPU& cpu, int adj_logical_row, int width) -> std::string;
    [[nodiscard]] auto render_machine_performance_stats_core(const simrv::core::CPU& cpu, int adj_logical_row, int width) -> std::string;
    [[nodiscard]] auto render_machine_performance_stats_sys(const simrv::core::CPU& cpu, int adj_logical_row, int width) -> std::string;
    [[nodiscard]] auto render_cycle_accurate_stats(const simrv::core::CPU& cpu, int adj_logical_row, int width) -> std::string;
    [[nodiscard]] auto render_cycle_accurate_core_stats(const simrv::core::CPU& cpu, int adj_logical_row, int width) -> std::string;
    [[nodiscard]] auto render_cycle_accurate_hazard_stats(const simrv::core::CPU& cpu, int adj_logical_row, int width) -> std::string;
    [[nodiscard]] auto render_cycle_accurate_mix_stats(const simrv::core::CPU& cpu, int adj_logical_row, int width) -> std::string;
    [[nodiscard]] auto render_cycle_accurate_hw_info(const simrv::core::CPU& cpu, int adj_logical_row, int width) -> std::string;
    [[nodiscard]] auto render_debug_state(int logical_row, int width) -> std::string;
    [[nodiscard]] auto section_line(const std::string& title, int width) -> std::string;
    [[nodiscard]] auto make_field(const std::string& label, const std::string& value, const char* value_color, int label_pad) -> std::string;
    [[nodiscard]] auto render_pair(const std::string& l1, const std::string& v1, const char* c1,
                                  const std::string& l2, const std::string& v2, const char* c2,
                                  int col_width, int right_width, int label_pad) -> std::string;
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
