/**
 * @file Tui.hpp
 * @brief Interactive TUI console dashboard for RTOS execution mode.
 */
#pragma once

#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "simrv/Define.hpp"
#include "simrv/core/TelemetrySink.hpp"
#include "simrv/isa/Base.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/tui/LogBuffer.hpp"
#include "simrv/tui/TuiBackend.hpp"
#include "simrv/tui/TuiInputRouter.hpp"
#include "simrv/tui/TuiKey.hpp"
#include "simrv/tui/TuiLayoutPolicy.hpp"
#include "simrv/tui/TuiMission.hpp"
#include "simrv/tui/TuiModal.hpp"
#include "simrv/tui/TuiTypes.hpp"
#include "simrv/tui/VirtualTerminal.hpp"
#include "simrv/tui/panels/StatusBar.hpp"
#include "simrv/util/UniqueFd.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::tui {

extern volatile std::sig_atomic_t g_resized;  // NOLINT(avoid-non-const-global-variables)

/**
 * @class Tui
 * @brief Handles ANSI-based split-screen rendering, scrolling, and status display for RTOS mode.
 */
enum class SelectionPane : uint8_t { None, InspectorPane, TerminalPane };

struct SelectionState {
    SelectionPane pane = SelectionPane::None;
    int start_x = -1;
    int start_y = -1;
    int end_x = -1;
    int end_y = -1;
    bool is_selecting = false;
    bool is_active = false;
};

class InspectorPane;
class TerminalPane;
class StatusBar;

class Tui : public core::ITelemetrySink, public core::IConsoleSink {
    friend struct TuiTestAccess;

   public:
    static constexpr size_t kTraceBufferSize = 200;
    static constexpr size_t kFlightRecorderHarts = 16;
    static constexpr uint64_t kDetailedExecutionMaxHz = 100;
    explicit Tui(simrv::core::Machine& machine);
    ~Tui() override;

    void clear_selection();
    void copy_active_selection();
    void copy_to_clipboard(std::string_view text);
    [[nodiscard]] auto get_selection_state() const -> const SelectionState& { return selection_; }

    void initialize();
    void shutdown();
    void render(bool force = false);
    void handle_char_write(char ch) override;
    void print_log(const std::string& msg);

    void start_ui_thread() override;
    void stop_ui_thread() override;
    void trigger_immediate_render();
    [[nodiscard]] auto is_ui_thread_running() const -> bool override {
        return ui_running_.load(std::memory_order_relaxed);
    }

    void pause_loop() override;
    void unpause_loop() override;
    [[nodiscard]] auto is_tui_paused() const -> bool {
        return paused_.load(std::memory_order_relaxed);
    }

    void set_paused(bool p) override;
    [[nodiscard]] auto is_paused() const -> bool override {
        return paused_.load(std::memory_order_relaxed);
    }
    /// Rich per-instruction state is useful while stopped or deliberately stepped slowly.  At
    /// higher rates the UI renders sampled state instead, keeping the simulator hot path lean.
    [[nodiscard]] auto captures_execution_detail() const -> bool override {
        return is_trace_active() || is_paused() ||
               step_delay_us_.load(std::memory_order_relaxed) >=
                   (1'000'000U / kDetailedExecutionMaxHz);
    }
    [[nodiscard]] auto step_delay_us() const -> uint64_t override {
        return step_delay_us_.load(std::memory_order_relaxed);
    }
    void set_step_delay_us(uint64_t delay_us) override {
        step_delay_us_.store(delay_us, std::memory_order_relaxed);
    }
    void set_sim_thread_sleeping(bool s) override {
        sim_thread_is_sleeping_.store(s, std::memory_order_relaxed);
    }
    [[nodiscard]] auto is_sim_thread_sleeping() const -> bool {
        return sim_thread_is_sleeping_.load(std::memory_order_relaxed);
    }
    void on_cycle_completed_slow();
    void on_cycle_completed() override {
        if (simrv::compiler::unlikely(step_delay_us_.load(std::memory_order_relaxed) > 0)) {
            on_cycle_completed_slow();
        }
    }
    void reset_speed_history();

    std::atomic<uint64_t> step_delay_us_{0};
    std::atomic<uint32_t> tui_target_fps_{30};

    void set_target_fps(uint32_t fps) override {
        tui_target_fps_.store(fps > 0 ? fps : 30, std::memory_order_relaxed);
    }
    [[nodiscard]] auto target_fps() const -> uint32_t override {
        return tui_target_fps_.load(std::memory_order_relaxed);
    }

    void open_modal(ModalType type) {
        if (!is_paused()) {
            pause_loop();
        }
        modal_.open(type, inspector_pane_.get(), step_delay_us_.load(std::memory_order_relaxed));
        set_paused(true);
        render(true);
    }
    void close_modal() {
        modal_.close();
        render(true);
    }
    void submit_modal() {
        modal_.submit(
            inspector_pane_.get(), step_delay_us_, [this](TuiRegPage page) { set_reg_page(page); },
            [this](const std::string& status) { set_status_override(status); },
            [this]() { reset_speed_history(); });
        render(true);
    }
    [[nodiscard]] auto is_modal_active() const -> bool { return modal_.is_active(); }
    [[nodiscard]] auto get_active_modal() const -> ModalType { return modal_.get_type(); }

    void set_status_override(const std::string& status) override {
        status_override_ = status;
        status_override_expires_at_ =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(2500);
    }
    void set_persistent_status_override(const std::string& status) override {
        status_override_ = status;
        status_override_expires_at_ = std::chrono::steady_clock::time_point::max();
    }
    void clear_status_override() {
        status_override_.clear();
        status_override_expires_at_ = {};
    }

    void cycle_layout();
    void apply_layout_preset(LayoutPreset preset);
    void focus_next_slot();
    void focus_prev_slot();
    [[nodiscard]] auto is_terminal_focused() const noexcept -> bool { return !is_paused(); }
    [[nodiscard]] auto focused_slot() const noexcept -> size_t { return focused_slot_index_; }
    [[nodiscard]] auto get_workbench_slots() const noexcept -> const std::vector<WorkbenchSlot>& {
        return workbench_slots_;
    }
    void set_workbench_slot_page(size_t slot_idx, TuiRegPage page);
    /// Cycle the tool page for the given workbench slot (wraps within the slot's category group).
    void cycle_slot_page(size_t slot_idx);
    [[nodiscard]] auto focused_page() const -> TuiRegPage;

    void cycle_reg_page();
    void cycle_tool_page();
    void set_reg_page(TuiRegPage page);
    void toggle_explain();
    void cycle_right_panel_mode();
    void record_instruction(Register pc, simrv::isa::Opcode opcode, simrv::isa::OperationId op_id,
                            uint8_t rd, Register rd_val, uint8_t rs1, Register rs1_val, uint8_t rs2,
                            Register rs2_val, int64_t imm, uint8_t hart) override;
    void record_flight_instruction(Register pc, simrv::isa::Opcode opcode,
                                   simrv::isa::OperationId op_id, uint8_t hart) override;
    /// Export the selected hart's paused architectural state and recent trace as JSON.
    void export_inspection_report();
    /// Toggle the interactive Student Guide used while paused.
    void toggle_student_guide();
    /// Perform the context-sensitive action currently proposed by the Student Guide.
    void activate_student_guide_suggestion();
    [[nodiscard]] auto is_student_guide_enabled() const -> bool { return student_guide_enabled_; }
    void dismiss_mission();
    void restart_mission();
    [[nodiscard]] auto mission_progress() const noexcept -> const MissionProgress& {
        return mission_;
    }
    // Source-compatible wrappers for the former "learn mode" API.
    void toggle_learn_mode() { toggle_student_guide(); }
    [[nodiscard]] auto is_learn_mode_enabled() const -> bool { return is_student_guide_enabled(); }
    [[nodiscard]] auto is_trace_active() const -> bool override {
        return trace_or_livetrace_active_.load(std::memory_order_relaxed);
    }
    void scroll(int lines);
    void reset_scroll();
    void scroll_inspector(int lines);
    void reset_scroll_inspector();
    [[nodiscard]] auto get_scroll_offset() const -> int { return scroll_offset_; }
    [[nodiscard]] auto get_right_panel_mode() const -> TuiRightPanelMode {
        return right_panel_mode_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] auto get_pane_width() const -> int { return pane_width_cached_; }
    [[nodiscard]] auto get_layout() const -> TuiLayout { return layout_; }
    void adjust_inspector_width(int delta);

    /// Toggle the unified run/terminal-attachment state.
    void toggle_run_state();
    /// Toggle execution mode (IA <-> CA) dynamically without reloading.
    void toggle_execution_mode();
    [[nodiscard]] auto is_terminal_attached() const -> bool { return !is_paused(); }

    [[nodiscard]] auto selected_hart() const -> size_t { return selected_hart_; }
    void select_next_hart();

    void handle_mouse(int x, int y, int b);

   private:
    void sync_workbench_slots();
    struct TraceRecord {
        Register pc = 0;
        simrv::isa::Opcode opcode{};
        simrv::isa::OperationId op_id{};
        uint8_t rd = 0;
        Register rd_val = 0;
        uint8_t rs1 = 0;
        Register rs1_val = 0;
        uint8_t rs2 = 0;
        Register rs2_val = 0;
        int64_t imm = 0;
        uint64_t sequence = 0;
        uint8_t hart = 0;
        bool detailed = false;
    };

    simrv::core::Machine& machine_;

    /// Deliver one terminal byte to the configured guest console endpoint.
    void write_guest_input(uint8_t byte);
    TuiModal modal_;

    std::unique_ptr<InspectorPane> inspector_pane_;
    std::unique_ptr<TerminalPane> terminal_pane_;
    std::unique_ptr<StatusBar> status_bar_;

    int pane_width_cached_ = 62;
    int cached_num_rows_ = 20;
    [[nodiscard]] auto get_terminal_pane_start_line(int num_rows) const -> int;
    [[nodiscard]] auto is_sixel_supported() const -> bool { return sixel_supported_; }

    int user_inspector_width_{-1};
    int cell_width_px_ = 8;
    int cell_height_px_ = 16;
    bool sixel_supported_{false};
    VirtualTerminal vt_;
    LogBuffer log_buffer_;
    std::vector<std::string> trace_buffer_;
    std::vector<std::string> lines_to_draw_;
    std::vector<std::string> terminal_rows_cache_;
    uint64_t terminal_rows_generation_ = 0;
    int terminal_rows_width_ = 0;
    int terminal_rows_count_ = 0;
    int terminal_rows_start_ = 0;
    std::vector<std::string> last_screen_lines_;
    std::atomic<bool> paused_{true};
    bool student_guide_enabled_{false};
    MissionProgress mission_;
    bool inspection_overwrite_armed_{false};
    std::vector<WorkbenchSlot> workbench_slots_{{TuiRegPage::GPR, 0}, {TuiRegPage::CONSOLE, 0}};
    size_t focused_slot_index_ = 0;
    size_t selected_hart_ = 0;
    TuiLayout layout_ = TuiLayout::Split;
    std::atomic<TuiRightPanelMode> right_panel_mode_{TuiRightPanelMode::Terminal};
    std::atomic<uint64_t> current_instruction_count_{0};
    std::atomic<bool> trace_or_livetrace_active_{false};
    bool render_in_progress_{false};
    std::string status_override_;
    std::chrono::steady_clock::time_point status_override_expires_at_{};
    int scroll_offset_{0};
    SelectionState selection_;
    std::chrono::steady_clock::time_point last_speed_update_{std::chrono::steady_clock::now()};
    uint64_t last_icount_ = 0;
    uint64_t speed_ips_ = 0;
    uint64_t kips_ = 0;
    uint64_t max_kips_ = 0;
    std::vector<uint64_t> kips_history_;

    // Active runtime tracking
    std::chrono::microseconds runtime_duration_{0};
    std::chrono::steady_clock::time_point last_runtime_tick_{};

    struct FlightRing {
        std::array<TraceRecord, kTraceBufferSize> records{};
        std::atomic<uint64_t> write_sequence{0};
    };
    std::array<FlightRing, kFlightRecorderHarts> flight_rings_{};
    std::atomic<uint64_t> flight_sequence_{0};
    std::array<uint64_t, kFlightRecorderHarts> rendered_flight_sequences_{};

    // Dedicated UI render and input thread
    std::jthread ui_thread_;
    std::atomic<bool> ui_running_{false};
    std::atomic<bool> render_requested_{false};
    std::atomic<bool> full_render_requested_{false};
    std::shared_ptr<LocalTuiBackend> backend_;
    simrv::util::UniqueFd ui_wake_;
    bool frame_dirty_ = true;
    std::array<uint8_t, 256> input_bytes_{};
    size_t input_pos_ = 0;
    size_t input_size_ = 0;

    // Thread-safe queues for decoupling writes from simulation
    std::string tx_buffer_;
    std::queue<std::string> log_fifo_;
    mutable std::mutex tui_mutex_;
    mutable std::mutex io_mutex_;

    std::string esc_buf_;
    std::atomic<bool> sim_thread_is_sleeping_{false};
    std::thread::id main_thread_id_;
    std::atomic<bool> processing_ui_input_{false};
    int cached_term_width_ = 0;
    int cached_term_height_ = 0;
    std::chrono::steady_clock::time_point last_draw_time_{};

    void ui_render_loop(const std::stop_token& stop_token);
    // The dedicated UI thread is the sole stdin reader and renderer. Keeping these private
    // prevents simulation and launcher code from racing terminal input or escape parsing.
    void update();
    void update_cache();

    auto consume_control_sequence(uint8_t first_byte) -> bool;
    auto parse_sgr_mouse(const std::string& seq, int& b, int& x, int& y) -> bool;
    auto poll_keyboard(uint8_t& byte_out) -> bool;
    auto format_trace_record(const TraceRecord& rec) -> std::string;
    auto update_trace_active_cache() -> void;
    void drain_trace_records();

    void execute_footer_action(TuiFooterAction action);
    void execute_header_action(HeaderHitResult hit);
    auto handle_alt_key(char key, uint8_t byte) -> bool;
    auto handle_arrow_key_sequence() -> bool;
    auto handle_modal_keyboard_input(uint8_t byte, TuiKey key) -> bool;
    bool handle_modal_settings(ModalType mtype, uint8_t byte, TuiKey key);
    bool handle_modal_breakpoint(ModalType mtype, uint8_t byte, TuiKey key);
    auto handle_normal_keyboard_input(uint8_t byte, TuiKey key) -> void;
    auto handle_debug_keyboard_input(TuiKey key) -> bool;
    bool handle_speed_keyboard_input(TuiKey key);
    bool handle_navigation_keyboard_input(uint8_t byte, TuiKey key);
    auto handle_mouse_inspector(int x, int y, int b, bool multi_column = false) -> void;
    void format_trace_inst(const TraceRecord& rec, const std::string& op_name, bool rd_fp,
                           bool rs1_fp, bool rs2_fp, std::string& inst_str,
                           std::string& side_effect);
    void render_update_speed(std::chrono::steady_clock::time_point now);
    void render_build_lines(int inspector_width, int terminal_width, int num_rows,
                            TuiRightPanelMode panel_mode);
    void render_draw_sixel(int inspector_width, int terminal_width, int num_rows,
                           std::string& update_cmds);
    void init_terminal_raw_mode();
    void detect_terminal_sixel_support(const std::string& resp);
};

}  // namespace simrv::tui
