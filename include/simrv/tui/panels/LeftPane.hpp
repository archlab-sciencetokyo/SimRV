/**
 * @file LeftPane.hpp
 * @brief OOP Widget for TUI Left Pane (Registers, Pipeline, Cache, Trace, Stack, Explainer & Log).
 */
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "simrv/core/RegisterFile.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/tui/TuiWidget.hpp"

namespace simrv::core {
class Machine;
class CPU;
struct ArchState;
}  // namespace simrv::core

namespace simrv::tui {

/// Canonical ABI register names shared by all register pane sub-units.
inline constexpr std::array<const char*, 32> kRegNames = {
    "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0/fp", "s1", "a0",
    "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3",    "s4", "s5",
    "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5",    "t6"};

/// Canonical FP ABI register names shared by all register pane sub-units.
inline constexpr std::array<const char*, 32> kFpRegNames = {
    "ft0", "ft1", "ft2", "ft3", "ft4",  "ft5",  "ft6", "ft7", "fs0",  "fs1", "fa0",
    "fa1", "fa2", "fa3", "fa4", "fa5",  "fa6",  "fa7", "fs2", "fs3",  "fs4", "fs5",
    "fs6", "fs7", "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"};

class LeftPane : public TuiWidget {
   public:
    explicit LeftPane(simrv::core::Machine& machine) : machine_(machine) {
        cached_gpr_.fill(0);
        cached_fpr_.fill(0);
        cached_vec_.fill({});
    }
    ~LeftPane() override = default;

    [[nodiscard]] auto render_row(int row_idx, int width) -> std::string override;

    void set_page(TuiRegPage page) { page_ = page; }
    [[nodiscard]] auto get_page() const -> TuiRegPage { return page_; }

    void update_cache();
    void set_kips(uint64_t kips) { kips_ = kips; }
    void set_max_kips(uint64_t max_kips) { max_kips_ = max_kips; }
    void set_kips_history(const std::vector<uint64_t>& history) { kips_history_ = history; }
    void set_paused(bool paused) { paused_ = paused; }
    void set_visible_rows(int rows) { visible_rows_ = rows; }
    void set_active_runtime(double secs) { active_runtime_ = secs; }
    void scroll(int lines);
    void reset_scroll() { scroll_offset_ = 0; }
    [[nodiscard]] auto get_scroll_offset() const -> int { return scroll_offset_; }
    void set_inspect_addr(Register addr) { inspect_addr_ = addr; }
    [[nodiscard]] auto get_inspect_addr() const -> Register { return inspect_addr_; }
    void set_explain_pc(Register pc) { explain_pc_ = pc; }
    [[nodiscard]] auto get_explain_pc() const -> Register { return explain_pc_; }
    void set_trace_buffer(const std::vector<std::string>* trace_buf) { trace_buffer_ = trace_buf; }
    void set_log_lines(std::vector<std::string> log_lines) { log_lines_ = std::move(log_lines); }
    void set_previous_page(TuiRegPage p) { previous_page_ = p; }
    [[nodiscard]] auto get_previous_page() const -> std::optional<TuiRegPage> {
        return previous_page_;
    }
    [[nodiscard]] auto get_pipeline_pc_at_row(int logical_row) const -> Register;
    [[nodiscard]] auto get_register_value_at_row(int logical_row, int col_x, int pane_width) const
        -> std::optional<Register>;
    [[nodiscard]] auto get_stack_addr_at_row(int logical_row) const -> std::optional<Register>;
    [[nodiscard]] auto is_running_label_click(int logical_row, int col, int width) const -> bool;
    [[nodiscard]] auto get_text_in_range(int start_row, int start_col, int end_row, int end_col, int width) -> std::string;

    void select_next_cache_set(int delta) {
        constexpr int kNumSets = 16;
        cache_inspect_set_ = (cache_inspect_set_ + delta + kNumSets) % kNumSets;
    }
    void toggle_cache_inspect_type() { cache_inspect_type_ = 1 - cache_inspect_type_; }
    void select_cache_way(int way) {
        if (way >= 0 && way < 4) {
            cache_inspect_way_ = way;
        }
    }
    void cycle_cache_way(int delta) { cache_inspect_way_ = (cache_inspect_way_ + delta + 4) % 4; }
    [[nodiscard]] auto get_cache_inspect_set() const -> int { return cache_inspect_set_; }
    [[nodiscard]] auto get_cache_inspect_type() const -> int { return cache_inspect_type_; }
    [[nodiscard]] auto get_cache_inspect_way() const -> int { return cache_inspect_way_; }

    [[nodiscard]] auto get_tab_at_col(int col) const -> std::optional<TuiRegPage>;

   private:
    std::optional<TuiRegPage> previous_page_;
    [[nodiscard]] auto render_tab_bar(int width) const -> std::string;
    [[nodiscard]] auto render_trace_row(int logical_row, int width) -> std::string;
    [[nodiscard]] auto render_log_bottom_row(int row_idx, int num_rows, int width) -> std::string;

    [[nodiscard]] auto get_sparkline_string(int width) -> std::string;
    [[nodiscard]] auto get_row_uncached(int logical_row, int width) -> std::string;
    [[nodiscard]] auto get_explain_rows(int width) -> std::vector<std::string>;

    [[nodiscard]] auto is_single_column(int width) const -> bool;
    [[nodiscard]] auto get_total_rows(int width) -> int;
    [[nodiscard]] auto get_running_label_start_row() const -> int;
    [[nodiscard]] auto render_active_spinner(int logical_row, int width) -> std::string;
    [[nodiscard]] auto render_registers_single_column(const simrv::core::ArchState& st,
                                                      int logical_row, int width) -> std::string;
    [[nodiscard]] auto render_registers_double_column(const simrv::core::ArchState& st,
                                                      int logical_row, int col_width,
                                                      int right_width) -> std::string;
    [[nodiscard]] auto render_registers_or_pipeline(const simrv::core::CPU& cpu,
                                                    const simrv::core::ArchState& st,
                                                    int logical_row, int col_width, int right_width,
                                                    int width, bool single_column) -> std::string;
    [[nodiscard]] auto render_system_or_pipeline_extended(const simrv::core::CPU& cpu,
                                                          int logical_row, int col_width,
                                                          int right_width, bool single_column)
        -> std::string;
    [[nodiscard]] auto render_perf_or_debug(const simrv::core::CPU& cpu, int logical_row, int width,
                                            bool single_column) -> std::string;
    [[nodiscard]] auto render_pipeline_stages(const simrv::core::CPU& cpu, int logical_row,
                                              int col_width, int right_width) -> std::string;
    [[nodiscard]] auto render_cache_stats(const simrv::core::CPU& cpu, int logical_row,
                                          int col_width, int right_width) -> std::string;
    [[nodiscard]] auto render_tlb_stats(const simrv::core::CPU& cpu, int logical_row, int col_width,
                                        int right_width) -> std::string;
    [[nodiscard]] auto render_bp_stats(const simrv::core::CPU& cpu, int logical_row, int col_width,
                                       int right_width) -> std::string;
    [[nodiscard]] auto render_hazard_stats(const simrv::core::CPU& cpu, int logical_row,
                                           int col_width, int right_width) -> std::string;
    [[nodiscard]] auto render_io_stats(const simrv::core::CPU& cpu, int logical_row, int col_width,
                                       int right_width) -> std::string;
    [[nodiscard]] auto render_stack_frame(const simrv::core::CPU& cpu, int logical_row,
                                          int col_width, int right_width) -> std::string;
    [[nodiscard]] auto translate_safe(const simrv::core::CPU& cpu, Register vaddr) const
        -> std::optional<Register>;
    [[nodiscard]] auto render_pipeline_timeline(const simrv::core::CPU& cpu, int logical_row,
                                                int width) -> std::string;
    [[nodiscard]] auto render_pipeline_stages_cycle_accurate(const simrv::core::CPU& cpu,
                                                             int logical_row, int col_width,
                                                             int right_width) -> std::string;
    [[nodiscard]] auto render_pipeline_stages_ca_core(const simrv::core::CPU& cpu, int stage_idx,
                                                      int width) -> std::string;
    [[nodiscard]] auto render_pipeline_stages_ca_hazards(const simrv::core::CPU& cpu, int stage_idx,
                                                         int col_width, int right_width)
        -> std::string;
    [[nodiscard]] auto render_pipeline_stages_ca_pred(const simrv::core::CPU& cpu, int stage_idx,
                                                      int width) -> std::string;
    [[nodiscard]] auto render_pipeline_stages_functional(const simrv::core::CPU& cpu,
                                                         int logical_row, int col_width,
                                                         int right_width) -> std::string;
    [[nodiscard]] auto render_pipeline_stages_functional_low(const simrv::core::CPU& cpu,
                                                             int logical_row, int col_width,
                                                             int right_width) -> std::string;
    [[nodiscard]] auto render_pipeline_stages_functional_low_part1(const simrv::core::CPU& cpu,
                                                                   int logical_row, int col_width,
                                                                   int right_width) -> std::string;
    [[nodiscard]] auto render_pipeline_stages_functional_low_part2(const simrv::core::CPU& cpu,
                                                                   int logical_row, int col_width,
                                                                   int right_width) -> std::string;
    [[nodiscard]] auto render_pipeline_stages_functional_high(const simrv::core::CPU& cpu,
                                                              int logical_row, int col_width,
                                                              int right_width) -> std::string;
    [[nodiscard]] auto render_system_state(const simrv::core::CPU& cpu, int logical_row,
                                           int col_width, int right_width) -> std::string;
    [[nodiscard]] auto render_machine_performance_stats(const simrv::core::CPU& cpu,
                                                        int adj_logical_row, int width)
        -> std::string;
    [[nodiscard]] auto render_machine_performance_stats_core(const simrv::core::CPU& cpu,
                                                             int adj_logical_row, int width)
        -> std::string;
    [[nodiscard]] auto render_machine_performance_stats_sys(const simrv::core::CPU& cpu,
                                                            int adj_logical_row, int width)
        -> std::string;
    [[nodiscard]] auto render_cycle_accurate_stats(const simrv::core::CPU& cpu, int adj_logical_row,
                                                   int width) -> std::string;
    [[nodiscard]] auto render_cycle_accurate_core_stats(const simrv::core::CPU& cpu,
                                                        int adj_logical_row, int width)
        -> std::string;
    [[nodiscard]] auto render_cycle_accurate_hazard_stats(const simrv::core::CPU& cpu,
                                                          int adj_logical_row, int width)
        -> std::string;
    [[nodiscard]] auto render_cycle_accurate_mix_stats(const simrv::core::CPU& cpu,
                                                       int adj_logical_row, int width)
        -> std::string;
    [[nodiscard]] auto render_cycle_accurate_hw_info(const simrv::core::CPU& cpu,
                                                     int adj_logical_row, int width) -> std::string;
    [[nodiscard]] auto render_debug_state(int logical_row, int width) -> std::string;
    [[nodiscard]] auto section_line(const std::string& title, int width) -> std::string;
    [[nodiscard]] auto make_field(const std::string& label, const std::string& value,
                                  const char* value_color, int label_pad) -> std::string;
    [[nodiscard]] auto render_pair(const std::string& l1, const std::string& v1, const char* c1,
                                   const std::string& l2, const std::string& v2, const char* c2,
                                   int col_width, int right_width, int label_pad) -> std::string;
    simrv::core::Machine& machine_;
    TuiRegPage page_ = TuiRegPage::GPR;
    bool paused_ = true;

    std::array<Register, 32> cached_gpr_{};
    std::array<uint64_t, 32> cached_fpr_{};
    std::array<simrv::core::VectorRegister, 32> cached_vec_{};
    std::array<std::string, 80> cached_left_rows_;
    int last_width_ = 0;

    uint64_t kips_ = 0;
    uint64_t max_kips_ = 0;
    std::vector<uint64_t> kips_history_;
    int visible_rows_ = 25;
    int scroll_offset_ = 0;
    double active_runtime_ = 0.0;
    Register inspect_addr_ = 0;
    Register explain_pc_ = 0;
    const std::vector<std::string>* trace_buffer_ = nullptr;
    std::vector<std::string> log_lines_;
    int cache_inspect_type_ = 0;  // 0: ICache, 1: DCache
    int cache_inspect_set_ = 0;   // 0 .. 15
    int cache_inspect_way_ = 0;   // 0 .. 3
};

}  // namespace simrv::tui
