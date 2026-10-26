#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/CpuConfigParser.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/TrapController.hpp"
#include "simrv/debug/BreakpointManager.hpp"
#include "simrv/debug/SymbolTable.hpp"
#include "simrv/isa/Common.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/pipeline/OperationInfo.hpp"
#include "simrv/pipeline/Scoreboard.hpp"
#include "simrv/tui/InspectionReport.hpp"
#include "simrv/tui/LogBuffer.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/tui/TuiDrawer.hpp"
#include "simrv/tui/TuiFrameRenderer.hpp"
#include "simrv/tui/TuiGuidance.hpp"
#include "simrv/tui/TuiInputRouter.hpp"
#include "simrv/tui/TuiKeybindings.hpp"
#include "simrv/tui/TuiLayoutPolicy.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/VirtualTerminal.hpp"
#include "simrv/tui/framework/Framework.hpp"
#include "simrv/tui/framework/Layout.hpp"
#include "simrv/tui/modals/AddressModal.hpp"
#include "simrv/tui/modals/GlossaryModal.hpp"
#include "simrv/tui/modals/HartModal.hpp"
#include "simrv/tui/modals/HelpModal.hpp"
#include "simrv/tui/modals/LoadModal.hpp"
#include "simrv/tui/modals/LogModal.hpp"
#include "simrv/tui/modals/MemoryModal.hpp"
#include "simrv/tui/modals/ModalComponents.hpp"
#include "simrv/tui/modals/SettingsModal.hpp"
#include "simrv/tui/modals/SystemConfigModal.hpp"
#include "simrv/tui/modals/ToolPickerModal.hpp"
#include "simrv/tui/panels/InspectorPane.hpp"
#include "simrv/tui/panels/StatusBar.hpp"
#include "simrv/tui/panels/TerminalPane.hpp"
#include "simrv/util/CliParser.hpp"
#include "simrv/util/InstructionExplainer.hpp"

namespace simrv::tui {
struct TuiTestAccess {
    static auto arrow(Tui& tui, std::string_view sequence) -> bool {
        tui.esc_buf_ = sequence;
        return tui.handle_arrow_key_sequence();
    }
    static auto modal(Tui& tui) -> TuiModal& { return tui.modal_; }
    static void set_cached_term_width(Tui& tui, int w) { tui.cached_term_width_ = w; }
    static void set_cached_term_height(Tui& tui, int h) { tui.cached_term_height_ = h; }
    static void set_pipeline_detail_visible(Tui& tui, bool visible) {
        tui.pipeline_or_detail_visible_.store(visible, std::memory_order_relaxed);
    }
    static void set_paused(Tui& tui, bool paused) {
        tui.paused_.store(paused, std::memory_order_relaxed);
    }
    static void set_mission_breakpoint(Tui& tui, simrv::xlen::Address address) {
        tui.machine_.breakpoint_manager().add_pc_breakpoint(address,
                                                            simrv::debug::BreakpointOwner::Mission);
        tui.mission_breakpoint_addr_ = address;
    }
    static void configure_mission(Tui& tui, std::string_view path, std::string_view binary) {
        tui.mission_.configure(path, binary);
        tui.mission_revision_seen_ = tui.mission_.revision();
    }
    static void observe_mission_symbol(Tui& tui, std::string_view symbol) {
        tui.mission_.observe_symbol(symbol);
    }
    static auto mission_progress(Tui& tui) -> MissionProgress& { return tui.mission_; }
    static void apply_mission_speed(Tui& tui) { tui.apply_mission_speed(); }
    static void apply_mission_layout(Tui& tui) { tui.apply_mission_guidance_layout(); }
    static void refresh_mission_layout_notice(Tui& tui) { tui.refresh_mission_layout_notice(); }
    static void drain_trace(Tui& tui) { tui.drain_trace_records(); }
    static auto trace_rows(const Tui& tui) -> const std::vector<std::string>& {
        return tui.trace_buffer_;
    }
    static void init_panes(Tui& tui, simrv::core::Machine& machine) {
        tui.status_bar_ = std::make_unique<StatusBar>(machine, &tui);
        tui.inspector_pane_ = std::make_unique<InspectorPane>(machine, &tui);
        tui.terminal_pane_ = std::make_unique<TerminalPane>();
    }
    static void set_last_draw_time(Tui& tui, std::chrono::steady_clock::time_point t) {
        tui.last_draw_time_ = t;
    }
    static void set_ui_running(Tui& tui, bool r) {
        tui.ui_running_.store(r, std::memory_order_relaxed);
    }
    static auto last_screen_lines(const Tui& tui) -> const std::vector<std::string>& {
        return tui.last_screen_lines_;
    }
    static auto inspector(Tui& tui) -> InspectorPane* { return tui.inspector_pane_.get(); }
    static void update_cache(Tui& tui) { tui.update_cache(); }
    static auto status_override(const Tui& tui) -> const std::string& {
        return tui.status_override_;
    }
    static auto workbench_slots(const Tui& tui) -> const std::vector<WorkbenchSlot>& {
        return tui.workbench_.slots();
    }
    static void set_focused_slot(Tui& tui, size_t slot) {
        tui.workbench_.focus(slot);
        tui.focused_slot_index_ = tui.workbench_.focused_index();
    }
    static auto column_widths(const Tui& tui, int width) -> framework::ColumnWidths {
        return tui.column_widths(width);
    }
    static auto layout(const Tui& tui) -> TuiLayout { return tui.layout_; }
    static void set_layout(Tui& tui, TuiLayout layout) { tui.layout_ = layout; }
    static auto status_bar(const Tui& tui) -> StatusBar* { return tui.status_bar_.get(); }
    static auto handle_modal_key(Tui& tui, uint8_t byte, TuiKey key) -> bool {
        return tui.handle_modal_keyboard_input(byte, key);
    }
    static void handle_normal_key(Tui& tui, uint8_t byte, TuiKey key) {
        tui.handle_normal_keyboard_input(byte, key);
    }
    static auto search_active(const Tui& tui) -> bool { return tui.terminal_search_active_; }
    static auto search_query(const Tui& tui) -> const std::string& {
        return tui.terminal_search_query_;
    }
    static void inject_input(Tui& tui, std::string_view input) {
        tui.input_size_ = std::min(input.size(), tui.input_bytes_.size());
        tui.input_pos_ = 0;
        std::copy_n(input.data(), tui.input_size_, tui.input_bytes_.data());
    }
    static auto consume_control_seq(Tui& tui, std::string_view seq) -> bool {
        if (!seq.empty() && seq[0] == '\x1b') {
            inject_input(tui, seq.substr(1));
            return tui.consume_control_sequence(static_cast<uint8_t>(seq[0]));
        }
        return false;
    }
    static auto handle_alt_key(Tui& tui, char key, uint8_t byte) -> bool {
        return tui.handle_alt_key(key, byte);
    }
    static auto selection(const Tui& tui) -> const SelectionState& { return tui.selection_; }
    static void set_sixel_supported(Tui& tui, bool s) { tui.sixel_supported_ = s; }
    static void set_sixel_rendered(Tui& tui, bool rendered) { tui.sixel_rendered_ = rendered; }
    static auto sixel_framebuffer_signature(const Tui& tui) -> uint64_t {
        return tui.sixel_framebuffer_signature_;
    }
    static auto compute_sixel_dimensions(int panel_cols, int body_rows, int cell_width_px,
                                         int cell_height_px, int source_width = 800,
                                         int source_height = 600) {
        return Tui::compute_sixel_dimensions(panel_cols, body_rows, cell_width_px, cell_height_px,
                                             source_width, source_height);
    }
    static void render_draw_sixel(Tui& tui, int panel_x, int display_width, int display_rows,
                                  std::string& update_cmds) {
        tui.render_draw_sixel(panel_x, display_width, display_rows, update_cmds);
    }
    static auto display_coords_to_fb(const Tui& tui, int x, int y, size_t col_idx, int tw, int th) {
        return tui.display_coords_to_fb(x, y, col_idx, tw, th);
    }
    static auto display_pixels_to_fb(const Tui& tui, int x, int y, size_t col_idx, int tw, int th,
                                     int tpw = 0, int tph = 0) {
        return tui.display_pixels_to_fb(x, y, col_idx, tw, th, tpw, tph);
    }
    static void handle_display_mouse(Tui& tui, int x, int y, int b, size_t col_idx, int tw,
                                     int th) {
        tui.handle_display_mouse(x, y, b, col_idx, tw, th);
    }
    static void set_workbench_slots(Tui& tui, std::vector<WorkbenchSlot> slots) {
        tui.workbench_.set_slots(std::move(slots));
    }
    static void set_preferences_path(Tui& tui, std::filesystem::path path) {
        tui.preferences_path_ = std::move(path);
    }
    static auto get_terminal_pane_start_line(const Tui& tui, int num_rows) -> int {
        return tui.get_terminal_pane_start_line(num_rows);
    }
    static auto vt(Tui& tui) -> VirtualTerminal& { return tui.vt_; }
    static void set_scroll_offset(Tui& tui, int offset) { tui.scroll_offset_ = offset; }
    static void set_user_column_widths(Tui& tui, framework::ColumnWidthOverrides widths) {
        tui.user_column_widths_ = widths;
    }
};

}  // namespace simrv::tui

namespace {

auto failures = 0;

[[nodiscard]] auto fnv1a64(std::string_view text) -> std::uint64_t {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char byte : text) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_uart_search_ctrl_f_and_escape_cancel() {
    simrv::core::Machine machine;
    simrv::tui::Tui tui(machine);
    simrv::tui::TuiTestAccess::init_panes(tui, machine);
    simrv::tui::TuiTestAccess::set_workbench_slots(tui, {{simrv::tui::TuiRegPage::CONSOLE, 0}});

    simrv::tui::TuiTestAccess::handle_normal_key(tui, 0x06, simrv::tui::TuiKey::CtrlF);
    expect(simrv::tui::TuiTestAccess::search_active(tui), "Ctrl-F starts UART scrollback search");
    simrv::tui::TuiTestAccess::handle_normal_key(tui, 'b', simrv::tui::TuiKey::b);
    expect(simrv::tui::TuiTestAccess::search_query(tui) == "b", "UART search records typed query");

    // Exercise the same Escape parser path used by the terminal input loop.
    simrv::tui::TuiTestAccess::consume_control_seq(tui, "\033");
    expect(!simrv::tui::TuiTestAccess::search_active(tui), "Escape cancels active UART search");
    expect(simrv::tui::TuiTestAccess::search_query(tui).empty(),
           "cancelling UART search clears its query");
}

auto strip_ansi(const std::string& rendered) -> std::string {
    std::string plain;
    bool in_escape = false;
    bool in_csi = false;
    for (char ch : rendered) {
        if (ch == '\033') {
            in_escape = true;
            in_csi = false;
        } else if (in_escape) {
            if (!in_csi && ch == '[') {
                in_csi = true;
            } else if (!in_csi || (ch >= '@' && ch <= '~')) {
                in_escape = false;
                in_csi = false;
            }
        } else {
            plain.push_back(ch);
        }
    }
    return plain;
}

auto plain_line(const simrv::tui::VirtualTerminal& terminal, int row, int width) -> std::string {
    return strip_ansi(terminal.get_line_as_string(row, width));
}

void test_terminal_controls() {
    simrv::tui::VirtualTerminal terminal(8, 3);
    std::string response;
    terminal.set_response_callback([&response](std::string_view bytes) { response.append(bytes); });
    const uint64_t initial_generation = terminal.generation();
    terminal.write_string("abc\rZ\n12\tX");
    expect(terminal.generation() == initial_generation + 1,
           "one UART chunk advances the terminal generation once");
    expect(terminal.get_cursor_x() == 0, "writing at the final tab stop wraps to column zero");
    expect(terminal.get_cursor_y() == 2, "writing at the final tab stop advances one row");
    expect(plain_line(terminal, 0, 8).starts_with("Zbc"), "carriage return overwrites row start");
    expect(plain_line(terminal, 1, 8).starts_with("12"), "newline preserves subsequent text");

    terminal.write_string("\033[2;3Hq\033[31;1mR\033[0m");
    expect(plain_line(terminal, 1, 8).substr(2, 2) == "qR", "CSI positioning and SGR text work");
    terminal.write_string("\033[?25l");
    expect(!terminal.is_cursor_visible(), "private CSI hides the cursor");
    terminal.write_string("\033[?25h");
    expect(terminal.is_cursor_visible(), "private CSI shows the cursor");
    terminal.write_string("\033[6n");
    expect(response == "\033[2;5R", "cursor-position query returns a PTY-style response");
    response.clear();
    terminal.write_string("\033[c\033[18t");
    expect(response == "\033[?1;2c\033[8;3;8t",
           "terminal capability and window-size queries receive deterministic replies");
}

void test_tui_preferences_round_trip() {
    using namespace simrv::tui;
    using simrv::tui::framework::DensityMode;
    const auto root = std::filesystem::temp_directory_path() / "simrv-tui-preferences-test";
    const auto path = root / "tui.conf";
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);

    TuiPreferences expected;
    expected.layout = TuiLayout::ThreeColumn;
    expected.slots = {{TuiRegPage::TRACE, 3}, {TuiRegPage::BUS, 1}, {TuiRegPage::GPR, 0}};
    expected.focused_slot = 1;
    expected.inspector_width = 42;
    expected.column_widths = {42, 35, 28, -1};
    expected.theme = TuiThemeStyle::SakuraPastel;
    expected.high_contrast = true;
    expected.density_mode = DensityMode::Compact;
    expect(expected.save(path), "TUI preferences save successfully");

    TuiPreferences actual;
    expect(actual.load(path), "TUI preferences load successfully");
    expect(actual.layout == expected.layout && actual.slots.size() == expected.slots.size() &&
               actual.slots[0].page == expected.slots[0].page &&
               actual.slots[0].scroll_offset == expected.slots[0].scroll_offset &&
               actual.slots[2].page == TuiRegPage::GPR &&
               actual.focused_slot == expected.focused_slot &&
               actual.column_widths == expected.column_widths && actual.theme == expected.theme &&
               actual.high_contrast && actual.density_mode == expected.density_mode,
           "TUI layout and presentation options survive a save and reload");
    {
        std::ofstream old_file(path);
        old_file << "version=1\n";
    }
    TuiPreferences old_preferences;
    expect(old_preferences.load(path) && old_preferences.density_mode == DensityMode::Auto,
           "older preference files default panel density to Auto");
    std::filesystem::remove_all(root, ignored);

    const auto tui_path = root / "live-tui.conf";
    (void)setenv("SIMRV_TUI_PREFERENCES", tui_path.c_str(), 1);
    {
        simrv::core::Machine machine;
        Tui tui(machine);
        TuiTestAccess::set_layout(tui, TuiLayout::ThreeColumn);
        TuiTestAccess::set_workbench_slots(
            tui, {{TuiRegPage::TRACE, 2}, {TuiRegPage::BUS, 1}, {TuiRegPage::CONSOLE, 0}});
        TuiTestAccess::set_focused_slot(tui, 1);
        tui.save_preferences();
    }
    {
        simrv::core::Machine machine;
        Tui tui(machine);
        expect(TuiTestAccess::layout(tui) == TuiLayout::ThreeColumn &&
                   tui.get_workbench_slots().size() == 3 && tui.focused_slot() == 1 &&
                   tui.get_workbench_slots()[0].page == TuiRegPage::TRACE &&
                   tui.get_workbench_slots()[0].scroll_offset == 2,
               "a new TUI session restores its saved workbench layout");
    }
    (void)setenv("SIMRV_TUI_PREFERENCES", "off", 1);
    std::filesystem::remove_all(root, ignored);
}

void test_density_resolution() {
    using namespace simrv::tui::framework;
    expect(resolve_density(DensityMode::Auto, 59, 40) == Density::Compact,
           "Auto uses compact density for a narrow surface");
    expect(resolve_density(DensityMode::Auto, 80, 23) == Density::Compact,
           "Auto uses compact density for a short surface");
    expect(resolve_density(DensityMode::Auto, 80, 24) == Density::Comfortable,
           "Auto uses comfortable density when the surface has room");
    expect(resolve_density(DensityMode::Compact, 120, 40) == Density::Compact,
           "forced Compact overrides surface geometry");
    expect(resolve_density(DensityMode::Comfortable, 20, 10) == Density::Comfortable,
           "forced Comfortable overrides surface geometry");
}

void test_terminal_scrollback_and_selection() {
    simrv::tui::VirtualTerminal terminal(4, 2);
    terminal.write_string("one\ntwo\ntri");
    expect(terminal.get_scrollback_size() >= 1, "overflow creates scrollback");
    expect(terminal.get_text_in_range(0, 0, 1, 2).find("one") != std::string::npos,
           "selection includes scrollback text");

    const uint64_t generation_before_resize = terminal.generation();
    terminal.resize(6, 3);
    expect(terminal.generation() == generation_before_resize + 1,
           "geometry changes invalidate cached terminal rows");
    expect(terminal.get_cursor_x() < 6 && terminal.get_cursor_y() < 3,
           "resize keeps the cursor in bounds");
    terminal.write_string("\033c");
    expect(terminal.get_cursor_x() == 0 && terminal.get_cursor_y() == 0,
           "terminal reset restores the cursor origin");

    simrv::tui::SelectionState sel{};
    expect(sel.start_x == -1 && sel.end_x == -1, "selection starts uninitialized");
    expect(sel.col_start_x == 2, "default column start x is 2");
    expect(sel.content_start_y == 4, "default content start y is 4");
    expect(!sel.is_active && !sel.is_selecting, "selection is inactive by default");
}

void test_utf8_and_theme_helpers() {
    simrv::tui::VirtualTerminal terminal(4, 1);
    terminal.write_string(
        "A\xE2\x98\x83"
        "B");
    expect(terminal.get_cursor_x() == 3, "a UTF-8 sequence occupies one terminal cell");
    expect(simrv::tui::get_display_width("\033[31mred\033[0m") == 3,
           "display width ignores ANSI styling");
    expect(simrv::tui::get_display_width("A\xE2\x98\x83"
                                         "B") == 3,
           "display width counts UTF-8 code points");
    expect(simrv::tui::get_display_width("A界B") == 4,
           "display width counts East Asian wide characters as two cells");
    expect(simrv::tui::get_display_width("e\xCC\x81") == 1,
           "combining marks do not shift following columns");
    expect(simrv::tui::get_display_width("🙂") == 2,
           "emoji occupy the two cells used by supported terminals");
    const std::string clipped_wide = simrv::tui::format_to_width("A界B", 2);
    expect(simrv::tui::get_display_width(clipped_wide) == 2,
           "wide-character truncation preserves the requested row width");
    expect(clipped_wide.find("界") == std::string::npos,
           "truncation never emits half of a two-cell character");
    const std::string over_wide = simrv::tui::overlay_string("A界BC", "XX", 1, 2);
    expect(simrv::tui::get_display_width(over_wide) == 5 &&
               over_wide.find("AXXBC") != std::string::npos,
           "overlay replacement preserves width across a complete wide character");
    const std::string inside_wide = simrv::tui::overlay_string("A界BC", "XX", 2, 2);
    expect(simrv::tui::get_display_width(inside_wide) == 5,
           "overlay replacement preserves width when its edge crosses a wide character");
    expect(simrv::tui::make_repeated_string("═", 3) == "═══", "border repetition is exact");

    simrv::tui::set_high_contrast(true);
    expect(simrv::tui::is_high_contrast(), "high-contrast theme updates shared state");
    simrv::tui::set_high_contrast(false);
    expect(!simrv::tui::is_high_contrast(), "adaptive theme clears high contrast");
}

void test_key_registry() {
    const auto bindings = simrv::tui::Keybindings::all();
    expect(bindings.size() == 43, "all key actions have registry entries");
    std::set<simrv::tui::KeyAction> actions;
    std::set<char> claimed_chars;
    const auto categories = simrv::tui::Keybindings::categories();
    for (const auto& binding : bindings) {
        actions.insert(binding.action);
        expect(!binding.key_display.empty(), "key display is not empty");
        expect(!binding.help_label.empty(), "help description is not empty");
        expect(&simrv::tui::Keybindings::get(binding.action) == &binding,
               "action lookup returns the canonical registry entry");
        expect(std::ranges::any_of(categories,
                                   [&binding](const auto& category) {
                                       return category.category == binding.category;
                                   }),
               "every registered action uses a declared category");
        bool has_key_match = false;
        if (binding.primary_char != '\0') {
            has_key_match = true;
            expect(claimed_chars.insert(binding.primary_char).second,
                   "canonical primary bindings do not collide");
            expect(simrv::tui::Keybindings::match(
                       static_cast<simrv::tui::TuiKey>(binding.primary_char)) == binding.action,
                   "primary key dispatch matches its help descriptor");
        }
        if (binding.alt_char != '\0' && binding.alt_char != binding.primary_char) {
            has_key_match = true;
            expect(claimed_chars.insert(binding.alt_char).second,
                   "canonical alternate bindings do not collide");
            expect(simrv::tui::Keybindings::match(
                       static_cast<simrv::tui::TuiKey>(binding.alt_char)) == binding.action,
                   "alternate key dispatch matches its help descriptor");
        }
        for (const auto key : binding.special_keys) {
            if (key == simrv::tui::TuiKey::None) continue;
            has_key_match = true;
            expect(simrv::tui::Keybindings::match(key) == binding.action,
                   "special key dispatch matches its help descriptor");
        }
        if (binding.alt_sequence != '\0') {
            has_key_match = true;
            expect(simrv::tui::Keybindings::match_alt(binding.alt_sequence) == binding.action,
                   "Alt sequence dispatch matches its help descriptor");
        }
        if (binding.alt_sequence_alt != '\0') {
            has_key_match = true;
            expect(simrv::tui::Keybindings::match_alt(binding.alt_sequence_alt) == binding.action,
                   "alternate Alt sequence dispatch matches its help descriptor");
        }
        expect(has_key_match, "every registered action has a key match");
    }
    expect(actions.size() == bindings.size(), "key actions are unique");
    expect(simrv::tui::Keybindings::get_help_key(simrv::tui::KeyAction::Quit).find("Ctrl-Q") !=
               std::string::npos,
           "global quit is documented by the canonical help descriptor");
    expect(simrv::tui::Keybindings::get_help_key(simrv::tui::KeyAction::RunPause).find("Ctrl-P") !=
               std::string::npos,
           "global pause is documented by the canonical help descriptor");

    expect(simrv::tui::Keybindings::get_help_key(simrv::tui::KeyAction::Help).find("F1") !=
               std::string::npos,
           "canonical F1 is Help");
    expect(simrv::tui::Keybindings::get_help_key(simrv::tui::KeyAction::Settings).find("F2") !=
               std::string::npos,
           "canonical F2 is Settings");
    expect(simrv::tui::Keybindings::get_help_key(simrv::tui::KeyAction::CycleRegPage).find("F3") !=
               std::string::npos,
           "canonical F3 is CycleRegPage");
    expect(simrv::tui::Keybindings::get_help_key(simrv::tui::KeyAction::CycleRegPage).find("[r]") ==
               std::string::npos,
           "legacy r register alias is removed");
    expect(
        simrv::tui::Keybindings::get_help_key(simrv::tui::KeyAction::CycleToolPage).find("F11") !=
            std::string::npos,
        "canonical F11 is CycleToolPage");
    expect(
        simrv::tui::Keybindings::get_help_key(simrv::tui::KeyAction::CycleToolPage).find("[l]") ==
            std::string::npos,
        "legacy l tools alias is removed");
    expect(simrv::tui::Keybindings::get_help_key(simrv::tui::KeyAction::OpenLayoutPresets)
                   .find("F4") != std::string::npos,
           "canonical F4 is Layout Presets");
    expect(simrv::tui::Keybindings::get_help_key(simrv::tui::KeyAction::RunPause).find("F5") !=
               std::string::npos,
           "canonical F5 is Run/Pause");
    expect(simrv::tui::Keybindings::get_help_key(simrv::tui::KeyAction::Step).find("F6") !=
               std::string::npos,
           "canonical F6 is Step");
    expect(simrv::tui::Keybindings::get_help_key(simrv::tui::KeyAction::TogglePcBreakpoint)
                   .find("F8") != std::string::npos,
           "canonical F8 is TogglePcBreakpoint");
    expect(simrv::tui::Keybindings::get_help_key(simrv::tui::KeyAction::OpenGlossary).find("F9") !=
               std::string::npos,
           "canonical F9 is Architecture Glossary");
    expect(simrv::tui::Keybindings::get_help_key(simrv::tui::KeyAction::Quit).find("F10") !=
               std::string::npos,
           "canonical F10 is Quit");
    expect(simrv::tui::Keybindings::get_help_key(simrv::tui::KeyAction::CheckMissionObjective) ==
               "[F12]",
           "F12 is the only registered mission modal shortcut");
    expect(simrv::tui::Keybindings::get_help_key(simrv::tui::KeyAction::Step).contains("Space") &&
               simrv::tui::Keybindings::match(simrv::tui::TuiKey::Space) ==
                   simrv::tui::KeyAction::Step,
           "Space retains simulator stepping outside the mission modal");
    expect(!simrv::tui::Keybindings::match(static_cast<simrv::tui::TuiKey>('y')),
           "y is not registered as a global mission action");

    constexpr simrv::tui::TuiFooterAction footer_actions[] = {
        simrv::tui::TuiFooterAction::Step,
        simrv::tui::TuiFooterAction::CycleRegs,
        simrv::tui::TuiFooterAction::CycleTools,
        simrv::tui::TuiFooterAction::SetBreakpoint,
        simrv::tui::TuiFooterAction::SetWatchpoint,
        simrv::tui::TuiFooterAction::TogglePcBreakpoint,
        simrv::tui::TuiFooterAction::SetSpeed,
        simrv::tui::TuiFooterAction::InspectMem,
        simrv::tui::TuiFooterAction::LoadBinary,
        simrv::tui::TuiFooterAction::ToggleHelp,
        simrv::tui::TuiFooterAction::RunPause,
        simrv::tui::TuiFooterAction::Quit,
        simrv::tui::TuiFooterAction::OpenSettings,
        simrv::tui::TuiFooterAction::ManageBreakpoints,
        simrv::tui::TuiFooterAction::Reboot,
        simrv::tui::TuiFooterAction::SwitchHart,
        simrv::tui::TuiFooterAction::ToggleTheme,
        simrv::tui::TuiFooterAction::ToggleExecutionMode,
        simrv::tui::TuiFooterAction::AddColumn,
        simrv::tui::TuiFooterAction::CloseColumn,
        simrv::tui::TuiFooterAction::FocusNextPane,
        simrv::tui::TuiFooterAction::FocusPrevPane,
    };
    for (const auto footer_action : footer_actions) {
        const auto key_action = simrv::tui::key_action_for_footer(footer_action);
        expect(!simrv::tui::Keybindings::get_footer_text(key_action).empty(),
               "every clickable footer action has a canonical label");
    }

    bool threw = false;
    try {
        (void)simrv::tui::Keybindings::get(static_cast<simrv::tui::KeyAction>(255));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    expect(threw, "invalid key actions fail instead of silently mapping to Step");

    using simrv::tui::ActionContext;
    using simrv::tui::KeyAction;
    expect(!claimed_chars.contains('b') && !claimed_chars.contains('B'),
           "removed backstep keys remain unbound");
    expect(!claimed_chars.contains('g') && !claimed_chars.contains('G'),
           "removed general guidance keys remain unbound");
    ActionContext paused{.paused = true, .image_loaded = true, .cycle_accurate = true};
    expect(simrv::tui::Keybindings::is_available(KeyAction::Step, paused),
           "step is available for a paused loaded image");
    auto running = paused;
    running.paused = false;
    expect(!simrv::tui::Keybindings::is_available(KeyAction::InspectAddress, running),
           "inspection requires a paused machine");
    expect(!simrv::tui::Keybindings::is_available(KeyAction::CheckMissionObjective, paused),
           "mission progress key is unavailable outside classroom missions");
    paused.mission_active = true;
    expect(simrv::tui::Keybindings::is_available(KeyAction::CheckMissionObjective, paused),
           "mission progress key becomes available for an active mission");
    expect(simrv::tui::Keybindings::unavailable_reason(KeyAction::InspectAddress, running) ==
               "Pause the simulator first",
           "disabled actions explain how to become available");
    expect(simrv::tui::Keybindings::is_available(KeyAction::SetBreakpoint, paused),
           "breakpoints are available without a debug-mode gate");
    expect(simrv::tui::Keybindings::is_available(KeyAction::InspectAddress, paused),
           "architectural memory inspection is always available while paused");
    auto modal = paused;
    modal.modal_active = true;
    expect(simrv::tui::Keybindings::is_available(KeyAction::Quit, modal),
           "quit remains available over a modal");
    expect(!simrv::tui::Keybindings::is_available(KeyAction::Step, modal),
           "modal input does not leak into simulation controls");
}

void test_sysconfig_modal_modes() {
    using simrv::tui::SettingsDraft;
    using simrv::tui::SysConfigDraft;
    using simrv::tui::modals::SettingsModal;
    using simrv::tui::modals::SystemConfigModal;

    // Test Cycle-Accurate mode behavior
    SysConfigDraft ca_draft;
    ca_draft.cycle_accurate = true;
    ca_draft.mul_latency = 3;
    int cursor = 0;

    SystemConfigModal::move_cursor(ca_draft, cursor, 1);
    expect(cursor == 1, "CA mode advances cursor across pipeline settings");

    SystemConfigModal::adjust_setting(ca_draft, 0, 1);
    expect(ca_draft.pipeline_type == 1, "CA mode allows adjusting pipeline model");

    SystemConfigModal::adjust_setting(ca_draft, 1, 5);
    expect(ca_draft.mul_latency == 8, "CA mode allows mutating modeled execution latency");

    // Test Functional (IA) mode behavior for CA modal
    SysConfigDraft ia_draft;
    ia_draft.cycle_accurate = false;
    ia_draft.mul_latency = 3;
    cursor = 0;

    SystemConfigModal::move_cursor(ia_draft, cursor, 1);
    expect(cursor == 0, "IA mode has no navigable CA pipeline items");

    // Test SMP configuration in SettingsModal
    SettingsDraft settings_draft;
    SettingsModal::adjust_setting(settings_draft, 2, 1);
    expect(settings_draft.density_mode == simrv::tui::framework::DensityMode::Compact,
           "Settings modal can select compact panel density");
    SettingsModal::adjust_setting(settings_draft, 2, 1);
    expect(settings_draft.density_mode == simrv::tui::framework::DensityMode::Comfortable,
           "Settings modal cycles panel density choices");
    settings_draft.num_harts = 1;
    settings_draft.smp_quantum = 1000;
    settings_draft.smp_multi_threaded = false;
    int s_cursor = 0;

    SettingsModal::move_cursor(s_cursor, 3);
    expect(s_cursor == 3, "Settings modal advances cursor to active SMP Hart Count");

    SettingsModal::adjust_setting(settings_draft, 3, 3);
    expect(settings_draft.num_harts == 4, "Settings modal adjusts SMP active core count");

    SettingsModal::adjust_setting(settings_draft, 4, 1);
    expect(settings_draft.smp_multi_threaded == true,
           "Settings modal toggles the SMP execution mode");

    SettingsModal::adjust_setting(settings_draft, 5, 1);
    expect(settings_draft.smp_quantum == 2500, "Settings modal advances SMP quantum level");

    simrv::core::Machine settings_machine;
    SettingsDraft submitted_draft;
    SettingsModal::open(submitted_draft, settings_machine);
    submitted_draft.num_harts = 4;
    expect(SettingsModal::submit(submitted_draft, settings_machine, [](simrv::tui::TuiRegPage) {}),
           "Settings modal accepts an SMP hart-count update");
    const auto staged_settings = settings_machine.take_staged_reconfiguration();
    expect(settings_machine.reboot_requested && staged_settings.has_value() &&
               staged_settings->execution.num_harts == 4,
           "Settings modal stages hart-count changes for reboot");

    simrv::core::Machine live_mode_machine;
    SettingsDraft mode_draft;
    SettingsModal::open(mode_draft, live_mode_machine);
    expect((mode_draft.misa.to_misa_val() & (1ULL << ('x' - 'a'))) == 0,
           "Settings modal does not advertise unsupported non-standard extensions");
    mode_draft.cycle_accurate = true;
    mode_draft.sys_config.cycle_accurate = true;
    expect(SettingsModal::submit(mode_draft, live_mode_machine, [](simrv::tui::TuiRegPage) {}),
           "Settings modal accepts live switch to Cycle-Accurate mode");
    expect(live_mode_machine.reboot_requested &&
               live_mode_machine.take_staged_reconfiguration().has_value(),
           "Switching to cycle-accurate mode stages its CPU model for restart");
    expect(live_mode_machine.runtime_profile.is_cycle_mode(),
           "Machine runtime profile successfully updated to cycle-accurate mode");

    simrv::core::Machine cancelled_mode_machine;
    SettingsDraft cancelled_mode_draft;
    SettingsModal::open(cancelled_mode_draft, cancelled_mode_machine);
    cancelled_mode_draft.cycle_accurate = true;
    cancelled_mode_draft.sys_config.cycle_accurate = true;
    bool requested_cycle_mode = false;
    expect(!SettingsModal::submit(
               cancelled_mode_draft, cancelled_mode_machine, [](simrv::tui::TuiRegPage) {},
               [&requested_cycle_mode](simrv::core::MachineConfig, SysConfigDraft, bool ca) {
                   requested_cycle_mode = ca;
               }),
           "settings requiring a restart wait for confirmation");
    expect(requested_cycle_mode && cancelled_mode_machine.runtime_profile.is_instruction_mode() &&
               !cancelled_mode_machine.reboot_requested,
           "cancelling a settings restart leaves the active execution mode unchanged");

    simrv::core::Machine ia_mode_machine;
    SettingsDraft ia_mode_draft;
    SettingsModal::open(ia_mode_draft, ia_mode_machine);
    expect(SettingsModal::submit(ia_mode_draft, ia_mode_machine, [](simrv::tui::TuiRegPage) {}),
           "Settings modal keeps an unchanged Instruction-Accurate model active");
    expect(
        !ia_mode_machine.reboot_requested && ia_mode_machine.runtime_profile.is_instruction_mode(),
        "unchanged Instruction-Accurate configuration does not request a restart");

    // Verify render text in IA mode contains disabled note for CA options
    std::vector<std::string> rows;
    SystemConfigModal::render(
        rows, [&](const std::string& line) { rows.push_back(line); }, ia_draft, 0, "");
    bool found_disabled_note = false;
    for (const auto& r : rows) {
        if (r.find("Disabled in IA Mode") != std::string::npos ||
            r.find("Disabled (IA Mode)") != std::string::npos) {
            found_disabled_note = true;
            break;
        }
    }
    expect(found_disabled_note,
           "IA mode render explicitly surfaces disabled status for CA options");
}

void test_cpu_config_reload_confirmation_and_slow_mode() {
    using simrv::tui::ModalType;
    using simrv::tui::Tui;
    using simrv::tui::TuiKey;
    using simrv::tui::TuiTestAccess;

    const auto root = std::filesystem::temp_directory_path() / "simrv-cpu-config-confirm";
    std::error_code ignored;
    std::filesystem::create_directories(root, ignored);
    const auto unchanged_path = (root / "unchanged.cfg").string();
    const auto changed_path = (root / "changed.cfg").string();

    simrv::core::Machine unchanged_machine;
    const auto current_model = unchanged_machine.primary_hart().cpu_model_config;
    expect(simrv::core::save_cpu_config(unchanged_path, current_model),
           "write an unchanged CPU model for confirmation testing");
    Tui unchanged_tui(unchanged_machine);
    unchanged_tui.open_modal(ModalType::LoadCpuConfig);
    auto& unchanged_modal = TuiTestAccess::modal(unchanged_tui);
    unchanged_modal.open_load_cpu_config();
    while (!unchanged_modal.get_input().empty()) unchanged_modal.pop_char();
    for (char ch : unchanged_path) unchanged_modal.push_char(ch);
    unchanged_tui.submit_modal();
    expect(unchanged_tui.get_active_modal() == ModalType::Notice &&
               !unchanged_machine.reboot_requested,
           "loading an unchanged CPU model skips reload confirmation");

    auto changed_model = current_model;
    changed_model.pipeline.enable_forwarding = !changed_model.pipeline.enable_forwarding;
    expect(simrv::core::save_cpu_config(changed_path, changed_model),
           "write a changed CPU model for confirmation testing");

    auto prepare_changed_load = [&](Tui& tui) {
        tui.open_modal(ModalType::LoadCpuConfig);
        auto& modal = TuiTestAccess::modal(tui);
        modal.open_load_cpu_config();
        while (!modal.get_input().empty()) modal.pop_char();
        for (char ch : changed_path) modal.push_char(ch);
        tui.submit_modal();
    };

    simrv::core::Machine cancelled_machine;
    Tui cancelled_tui(cancelled_machine);
    prepare_changed_load(cancelled_tui);
    expect(cancelled_tui.get_active_modal() == ModalType::ConfigurationChangeConfirm &&
               !cancelled_machine.reboot_requested,
           "changed CPU model waits for confirmation before reboot");
    TuiTestAccess::handle_modal_key(cancelled_tui, 'D', static_cast<TuiKey>('D'));
    expect(!cancelled_machine.reboot_requested &&
               cancelled_machine.primary_hart().cpu_model_config.pipeline.enable_forwarding ==
                   current_model.pipeline.enable_forwarding,
           "discarding a CPU model leaves active execution configuration unchanged");

    simrv::core::Machine confirmed_machine;
    Tui confirmed_tui(confirmed_machine);
    prepare_changed_load(confirmed_tui);
    TuiTestAccess::handle_modal_key(confirmed_tui, 'R', static_cast<TuiKey>('R'));
    auto staged = confirmed_machine.take_staged_reconfiguration();
    expect(confirmed_machine.reboot_requested && staged.has_value() &&
               staged->cpu_model_config_override.has_value() &&
               staged->cpu_model_config_override->pipeline.enable_forwarding ==
                   changed_model.pipeline.enable_forwarding,
           "confirmed CPU model is staged for the simulator restart");

    simrv::core::Machine speed_machine;
    Tui speed_tui(speed_machine);
    simrv::tui::TuiTestAccess::set_paused(speed_tui, false);
    speed_tui.set_step_delay_us(16'666);
    expect(!speed_tui.captures_execution_detail(),
           "execution faster than 60 Hz uses sampled rather than detailed capture");
    speed_tui.set_step_delay_us(16'667);
    speed_tui.set_reg_page(simrv::tui::TuiRegPage::CONSOLE);
    expect(!speed_tui.captures_execution_detail(),
           "Console does not request pipeline detail capture through the DISASM alias");
    speed_tui.set_reg_page(simrv::tui::TuiRegPage::PIPELINE);
    TuiTestAccess::set_pipeline_detail_visible(speed_tui, true);
    expect(speed_tui.captures_execution_detail(),
           "execution at 60 Hz captures detail for visible pipeline views");
    speed_tui.set_step_delay_us(0);
    expect(!speed_tui.captures_execution_detail(),
           "unthrottled execution keeps the fast sampled path");

    speed_machine.publish_tui_execution_snapshot_for_testing();
    TuiTestAccess::init_panes(speed_tui, speed_machine);
    auto* speed_inspector = TuiTestAccess::inspector(speed_tui);
    speed_inspector->set_paused(false);
    speed_inspector->set_visible_rows(45);
    speed_inspector->set_page(simrv::tui::TuiRegPage::GPR);
    const auto running_detail = strip_ansi(speed_inspector->render_row(2, 80));
    const auto hidden_registers = strip_ansi(speed_inspector->render_row(3, 80));
    const auto live_counters = strip_ansi(speed_inspector->render_row(24, 80));
    expect(running_detail.contains("RUNNING") && !hidden_registers.contains("x1"),
           "unthrottled register detail is replaced by the running indicator");
    expect(live_counters.contains("retired"),
           "sampled performance counters remain visible in the same inspector panel");

    std::filesystem::remove_all(root, ignored);
}

void test_page_guidance() {
    using simrv::tui::TuiRegPage;
    expect(simrv::tui::get_page_info(TuiRegPage::GPR)->fast_mode_content ==
                   simrv::tui::TuiFastModeContent::FastChangingDetail &&
               simrv::tui::get_page_info(TuiRegPage::TRACE)->fast_mode_content ==
                   simrv::tui::TuiFastModeContent::Stable &&
               simrv::tui::get_page_info(TuiRegPage::CONSOLE)->fast_mode_content ==
                   simrv::tui::TuiFastModeContent::Stable,
           "fast-mode policy hides detailed content groups while sampled tools stay visible");
    constexpr TuiRegPage pages[] = {TuiRegPage::GPR,      TuiRegPage::FPR,     TuiRegPage::VEC,
                                    TuiRegPage::PIPELINE, TuiRegPage::CACHE,   TuiRegPage::TLB,
                                    TuiRegPage::BPRED,    TuiRegPage::HAZARD,  TuiRegPage::BUS,
                                    TuiRegPage::TRACE,    TuiRegPage::EXPLAIN, TuiRegPage::STACK,
                                    TuiRegPage::CONSOLE};
    for (auto page : pages) {
        auto const guidance = simrv::tui::guidance_for_page(page, true);
        expect(!guidance.title.empty(), "each inspection page has a guidance title");
        expect(!guidance.meaning.empty(), "each inspection page explains visible state");
        expect(!guidance.relationship.empty(), "each inspection page links state to execution");
        expect(!simrv::tui::Keybindings::get(guidance.next_action).key_display.empty(),
               "each inspection page links to a canonical action");
    }

    auto memory_guidance =
        simrv::tui::guidance_for_context({.page = TuiRegPage::GPR,
                                          .cycle_accurate = false,
                                          .instruction_valid = true,
                                          .operation = simrv::isa::OperationId::LW,
                                          .destination = 10});
    expect(memory_guidance.relationship.contains("address"),
           "memory instructions receive contextual address guidance");
    auto hazard_guidance = simrv::tui::guidance_for_context({.page = TuiRegPage::HAZARD,
                                                             .cycle_accurate = true,
                                                             .data_hazard_stalls = 3,
                                                             .control_hazard_bubbles = 2});
    expect(hazard_guidance.meaning.contains("3 data stalls") &&
               hazard_guidance.meaning.contains("2 control bubbles"),
           "cycle telemetry is summarized in contextual guidance");
    auto empty_guidance = simrv::tui::guidance_for_context({.image_loaded = false});
    expect(empty_guidance.next_action == simrv::tui::KeyAction::LoadBinary &&
               empty_guidance.meaning.contains("No program"),
           "an empty classroom session guides the student into the program loader");

    auto trap_guidance = simrv::tui::guidance_for_context(
        {.page = TuiRegPage::GPR, .has_pending_trap = true, .trap_cause = 2});
    expect(trap_guidance.title.contains("Trap") &&
               trap_guidance.meaning.contains("Illegal instruction"),
           "pending traps receive diagnostic guidance with trap cause name");
    expect(trap_guidance.glossary_topic == 5, "trap guidance references Privilege glossary topic");

    auto csr_guidance = simrv::tui::guidance_for_context(
        {.page = TuiRegPage::GPR, .is_csr_op = true, .csr_address = 0x180});
    expect(
        csr_guidance.title.contains("satp") && csr_guidance.meaning.contains("Address Translation"),
        "CSR operations receive educational metadata guidance");
    expect(csr_guidance.glossary_topic == 5, "CSR guidance references Privilege glossary topic");

    auto tlb_guidance_bare =
        simrv::tui::guidance_for_context({.page = TuiRegPage::TLB, .translation_enabled = false});
    expect(tlb_guidance_bare.meaning.contains("Bare mode"),
           "bare mode TLB explains disabled translation");
    auto tlb_guidance_paged =
        simrv::tui::guidance_for_context({.page = TuiRegPage::TLB, .translation_enabled = true});
    expect(tlb_guidance_paged.meaning.contains("active"), "paged TLB explains active translation");

    auto vec_guidance =
        simrv::tui::guidance_for_context({.page = TuiRegPage::VEC,
                                          .operation = simrv::isa::OperationId::VADD_VV,
                                          .is_vector_op = true});
    expect(vec_guidance.title.contains("Vector") && vec_guidance.meaning.contains("element lanes"),
           "vector instructions receive vector lane guidance");

    auto cfu_guidance = simrv::tui::guidance_for_context({.operation = simrv::isa::OperationId::CFU,
                                                          .image_loaded = true,
                                                          .is_cfu_op = true,
                                                          .cfu_funct7 = 0x05,
                                                          .cfu_funct3 = 4,
                                                          .cfu_has_plugin = false,
                                                          .cfu_latency = 1,
                                                          .cfu_stall_cycles = 0});
    expect(
        cfu_guidance.title.contains("Custom Function Unit") && cfu_guidance.title.contains("func4"),
        "CFU instruction receives dedicated student guidance with func index");
    expect(cfu_guidance.meaning.contains("rvproc_cfu") && cfu_guidance.glossary_topic == 1,
           "CFU student guidance references built-in hardware model and pipeline glossary");

    auto cfu_plugin_guidance =
        simrv::tui::guidance_for_context({.operation = simrv::isa::OperationId::CFU,
                                          .image_loaded = true,
                                          .is_cfu_op = true,
                                          .cfu_funct7 = 0x10,
                                          .cfu_funct3 = 1,
                                          .cfu_has_plugin = true,
                                          .cfu_plugin_path = "/path/libcfu.so",
                                          .cfu_latency = 3,
                                          .cfu_stall_cycles = 2});
    expect(cfu_plugin_guidance.meaning.contains("external accelerator plugin") &&
               cfu_plugin_guidance.meaning.contains("3 cycle latency"),
           "CFU plugin guidance reports external plugin and multi-cycle stall latency");
}

void test_cleanup_input_boundaries() {
    for (const auto* option : {"--bht-size", "--bht-entries", "--btb-size", "--btb-entries",
                               "--ras-size", "--ras-entries"}) {
        for (const auto* value : {"0", "1", "0x20", "4294967295", "4294967296", "-1", "1x"}) {
            std::array<std::string, 5> storage{"SimRV", option, value, "-m", "test.bin"};
            std::array<char*, 5> args{storage[0].data(), storage[1].data(), storage[2].data(),
                                      storage[3].data(), storage[4].data()};
            const bool valid = std::string_view(value) == "1" ||
                               std::string_view(value) == "0x20" ||
                               std::string_view(value) == "4294967295";
            expect(simrv::util::parse_command_line(args).has_value() == valid,
                   "size parsing preserves numeric boundaries");
        }
    }
    for (const auto* value : {"0", "1", "16", "17"}) {
        std::array<std::string, 5> storage{"SimRV", "--smp", value, "-m", "test.bin"};
        std::array<char*, 5> args{storage[0].data(), storage[1].data(), storage[2].data(),
                                  storage[3].data(), storage[4].data()};
        const bool valid = std::string_view(value) == "1" || std::string_view(value) == "16";
        expect(simrv::util::parse_command_line(args).has_value() == valid,
               "SMP hart count respects platform capacity");
    }
    for (const auto* name : {"x0", "r31", "f31", "v31", "fp", " X 1 "})
        expect(simrv::debug::parse_register_name(name).has_value(), "valid register spelling");
    for (const auto* name : {"x32", "f32", "v32", "x-1", "v+1", "f99999999999999999999999"})
        expect(!simrv::debug::parse_register_name(name),
               "invalid register rejected without throwing");
}

void test_mirrored_modal_arrows() {
    using namespace simrv::tui;
    simrv::core::Machine machine;
    Tui tui(machine);
    for (auto type : {ModalType::Glossary, ModalType::Settings, ModalType::ManageBreakpoints}) {
        for (std::string_view sequence :
             {"\033[A", "\033OA", "\033[B", "\033OB", "\033[C", "\033OC", "\033[D", "\033OD"}) {
            tui.open_modal(type);
            TuiModal reference(machine);
            reference.open(type, nullptr, tui.step_delay_us_.load());
            const char key = sequence.back();
            const int direction = key == 'A' || key == 'D' ? -1 : 1;
            const bool vertical = key == 'A' || key == 'B';
            if (vertical) {
                switch (type) {
                    case ModalType::Glossary:
                        reference.scroll_glossary_content(2 * direction);
                        break;
                    case ModalType::Settings:
                        reference.move_settings_cursor(direction);
                        break;
                    case ModalType::ManageBreakpoints:
                        reference.move_bp_cursor(direction);
                        break;
                    default:
                        break;
                }
            } else {
                switch (type) {
                    case ModalType::Glossary:
                        reference.move_glossary_topic(direction);
                        break;
                    case ModalType::Settings:
                        reference.adjust_setting_at_cursor(direction);
                        break;
                    default:
                        break;
                }
            }
            const bool handled = TuiTestAccess::arrow(tui, sequence);
            expect(handled == (vertical || type != ModalType::ManageBreakpoints),
                   "arrow routing preserves modal handling");
            std::vector<std::string> actual(40, std::string(120, ' ')), expected = actual;
            TuiTestAccess::modal(tui).render_overlay(actual, 120, 40);
            reference.render_overlay(expected, 120, 40);
            expect(actual == expected, "mirrored arrows preserve modal state and rendering");
        }
    }
}

void test_classroom_cli_defaults() {
    const auto mission_path = (std::filesystem::path(__FILE__).parent_path().parent_path() /
                               "examples/isa/lessons/control-flow-calls.mission")
                                  .string();
    std::array<std::string, 5> mission_storage = {"SimRV", "--mission", mission_path, "-m",
                                                  "control-flow-calls.elf"};
    std::array<char*, 5> mission_args{};
    for (size_t index = 0; index < mission_storage.size(); ++index) {
        mission_args[index] = mission_storage[index].data();
    }
    const auto mission_parsed = simrv::util::parse_command_line(mission_args);
    expect(mission_parsed.has_value() && mission_parsed->options.to_machine_config().tui.enabled &&
               mission_parsed->options.to_machine_config().tui.mission == mission_path,
           "--mission alone selects interactive classroom mode");

    std::array<std::string, 3> class_only_storage = {"SimRV", "--class", "-m"};
    std::array<char*, 3> class_only_args{};
    for (size_t index = 0; index < class_only_storage.size(); ++index) {
        class_only_args[index] = class_only_storage[index].data();
    }
    expect(!simrv::util::parse_command_line(class_only_args).has_value(),
           "the removed --class switch is not accepted");

    std::array<std::string, 5> mission_storage_with_report = {"SimRV", "--mission", mission_path,
                                                              "--inspection-output", "report.json"};
    std::array<char*, 5> report_args{};
    for (size_t index = 0; index < mission_storage_with_report.size(); ++index) {
        report_args[index] = mission_storage_with_report[index].data();
    }
    const auto report_parsed = simrv::util::parse_command_line(report_args);
    expect(report_parsed.has_value() &&
               report_parsed->options.to_machine_config().tui.inspection_output == "report.json",
           "inspection output path reaches typed machine configuration");

    std::array<std::string, 6> invalid_storage = {"SimRV",      "--cli", "--mission",
                                                  mission_path, "-m",    "demo.elf"};
    std::array<char*, 6> invalid_args{};
    for (size_t index = 0; index < invalid_args.size(); ++index) {
        invalid_args[index] = invalid_storage[index].data();
    }
    expect(!simrv::util::parse_command_line(invalid_args).has_value(),
           "mission rejects a non-TUI headless CLI invocation");
}

void test_load_modal_path_completion() {
    namespace fs = std::filesystem;
    const auto root = fs::temp_directory_path() /
                      ("simrv-path-completion-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root / "subdir");
    std::ofstream(root / "program.elf").put('x');

    auto file_prefix = (root / "pro").string();
    expect(simrv::tui::modals::LoadModal::complete_path(file_prefix),
           "load modal completes a unique file prefix");
    expect(file_prefix == (root / "program.elf").string(),
           "load modal inserts the matching file path");

    auto directory_prefix = (root / "sub").string();
    expect(simrv::tui::modals::LoadModal::complete_path(directory_prefix),
           "load modal completes a unique directory prefix");
    expect(directory_prefix == (root / "subdir").string() + fs::path::preferred_separator,
           "load modal appends a separator to completed directories");

    simrv::core::Machine machine;
    simrv::tui::TuiModal modal(machine);
    modal.open(simrv::tui::ModalType::LoadBinary, nullptr, 0);
    modal.set_input((root / "program").string());
    modal.open_file_picker();
    expect(modal.file_picker_active(), "file picker opens from a load modal");
    modal.file_picker_input(13, simrv::tui::TuiKey::Enter);
    expect(!modal.file_picker_active() && modal.get_input() == (root / "program.elf").string(),
           "file picker selects a matching file into the load path");

    simrv::core::Machine key_machine;
    simrv::tui::Tui key_tui(key_machine);
    simrv::tui::TuiTestAccess::modal(key_tui).open(simrv::tui::ModalType::LoadBinary, nullptr, 0);
    simrv::tui::TuiTestAccess::handle_modal_key(key_tui, 15, simrv::tui::TuiKey::CtrlO);
    expect(simrv::tui::TuiTestAccess::modal(key_tui).file_picker_active(),
           "Ctrl-O opens the load modal file browser");
    expect(simrv::tui::TuiTestAccess::consume_control_seq(key_tui, "\x1b") &&
               simrv::tui::TuiTestAccess::modal(key_tui).get_type() ==
                   simrv::tui::ModalType::LoadBinary,
           "Escape returns from the file browser to its load dialog");

    modal.open(simrv::tui::ModalType::LoadBinary, nullptr, 0);
    modal.set_input((root / "subdir").string() + fs::path::preferred_separator);
    modal.open_file_picker();
    modal.file_picker_input(13, simrv::tui::TuiKey::Enter);  // Select the leading ".." entry.
    modal.file_picker_input('p', simrv::tui::TuiKey::p);
    modal.file_picker_input('r', simrv::tui::TuiKey::r);
    modal.file_picker_input('o', simrv::tui::TuiKey::o);
    modal.file_picker_input(13, simrv::tui::TuiKey::Enter);
    expect(modal.get_input() == (root / "program.elf").string(),
           "the leading .. entry navigates to the parent directory");

    modal.open(simrv::tui::ModalType::LoadBinary, nullptr, 0);
    modal.toggle_load_mode();
    modal.toggle_load_mode();
    modal.set_input(root.string());
    modal.open_file_picker();
    modal.move_file_picker_cursor(1);  // Skip the "use this directory" entry to enter root.
    modal.file_picker_input(13, simrv::tui::TuiKey::Enter);
    modal.file_picker_input(13, simrv::tui::TuiKey::Enter);  // Select the current folder.
    expect(!modal.file_picker_active() && modal.get_input() == root.string(),
           "Buildroot mode file picker can select a directory as the image bundle");

    std::error_code ignored;
    fs::remove_all(root, ignored);
}

void test_control_flow_calls_mission() {
    simrv::tui::MissionProgress mission;
    const auto lesson = (std::filesystem::path(__FILE__).parent_path().parent_path() /
                         "examples/isa/lessons/control-flow-calls.mission")
                            .string();
    mission.configure(lesson, "/tmp/control-flow-calls.elf");
    expect(mission.enabled() && mission.step() == 0 && mission.speed_hz() == 30,
           "mission starts for its bundled ELF at the classroom default of 30 Hz");
    expect(mission.lesson_id() == "control-flow-calls",
           "mission preserves its educator-facing identifier for reports");
    expect(mission.description().contains("one branch"), "mission loads the lesson description");
    expect(mission.goal().contains("stack"), "mission loads the lesson goal");
    expect(mission.guidance()->title == "Follow the branch",
           "mission starts at branch objective without duplicate step prefix");
    expect(mission.guidance()->panels.size() == 3 &&
               mission.guidance()->panels.front() == simrv::tui::TuiRegPage::EXPLAIN &&
               mission.guidance()->panels.back() == simrv::tui::TuiRegPage::TRACE,
           "mission step provides a complete multi-panel workbench layout");

    const auto speed_lesson =
        std::filesystem::temp_directory_path() / "simrv-mission-speed-test.mission";
    {
        std::ofstream output(speed_lesson);
        output << "[mission]\nversion = \"1\"\nprogram = \"pace.elf\"\n"
                  "speed_hz = \"30\"\n\n"
                  "[step]\nid = \"default\"\ntitle = \"Default\"\n"
                  "prompt = \"Observe\"\nwhy = \"Learn\"\nview = \"registers\"\n\n"
                  "[step]\nid = \"override\"\ntitle = \"Override\"\n"
                  "prompt = \"Observe\"\nwhy = \"Learn\"\nview = \"registers\"\n"
                  "speed_hz = \"45\"\n";
    }
    simrv::tui::MissionProgress speed_mission;
    speed_mission.configure(speed_lesson.string(), "/tmp/pace.elf");
    expect(speed_mission.enabled() && speed_mission.speed_hz() == 30 &&
               speed_mission.advance_step() && speed_mission.speed_hz() == 45,
           "a mission step can override the mission-wide execution frequency");
    std::error_code speed_lesson_error;
    std::filesystem::remove(speed_lesson, speed_lesson_error);

    simrv::core::Machine machine;
    simrv::tui::TuiModal no_hint_modal(machine);
    no_hint_modal.set_mission_progress(&speed_mission);
    no_hint_modal.open_mission_objective();
    std::vector<std::string> no_hint_rows(20, std::string(100, ' '));
    no_hint_modal.render_overlay(no_hint_rows, 100, 20);
    std::string no_hint_render;
    for (const auto& row : no_hint_rows) no_hint_render += row;
    expect(no_hint_render.find("Why [y to reveal]") != std::string::npos,
           "steps without a short hint retain the existing rationale reveal");
    no_hint_modal.cycle_mission_hint();
    expect(no_hint_modal.mission_hint_level() == 2,
           "one y reveals the full rationale when no short hint is authored");

    simrv::tui::TuiModal modal(machine);
    modal.set_mission_progress(&mission);
    modal.open_mission_objective();
    expect(modal.get_type() == simrv::tui::ModalType::MissionObjective,
           "mission objective modal opens with the active lesson");
    std::vector<std::string> modal_rows(30, std::string(100, ' '));
    modal.render_overlay(modal_rows, 100, 30);
    std::string rendered_modal;
    for (const auto& row : modal_rows) rendered_modal += row;
    expect(rendered_modal.find("Goal: Use the PC") != std::string::npos,
           "mission modal includes the lesson goal");
    expect(rendered_modal.find("Step 1/6 · Follow the branch") != std::string::npos &&
               rendered_modal.find("Step 1 of 6") == std::string::npos,
           "mission modal formats step title cleanly without duplicate step prefix");
    expect(rendered_modal.find("Waiting for mission_loop · after step 1") != std::string::npos,
           "mission modal identifies the pending checkpoint boundary");
    expect(rendered_modal.find("Learn: Relate a branch decision") != std::string::npos &&
               rendered_modal.find("Relate a branch decision to the next program counter") !=
                   std::string::npos,
           "mission modal explains the learning target");
    expect(rendered_modal.find("Hint [y to reveal]") != std::string::npos,
           "mission modal explains how to reveal a hint");
    expect(rendered_modal.find("Next: press F5 to run to mission_loop") != std::string::npos,
           "mission modal states one direct next action");
    expect(rendered_modal.find("Flow:") == std::string::npos &&
               rendered_modal.find("checkpoints 0/6") == std::string::npos &&
               rendered_modal.find("At the branch, a0 is 2") == std::string::npos &&
               rendered_modal.find("conditional branch selects") == std::string::npos,
           "mission modal omits progress notation and unrevealed teaching content");
    modal.cycle_mission_hint();
    modal_rows.assign(30, std::string(100, ' '));
    modal.render_overlay(modal_rows, 100, 30);
    rendered_modal.clear();
    for (const auto& row : modal_rows) rendered_modal += row;
    expect(rendered_modal.find("At the branch, a0 is 2") != std::string::npos &&
               rendered_modal.find("conditional branch selects") == std::string::npos,
           "first y reveals the short hint before the rationale");
    modal.cycle_mission_hint();
    modal_rows.assign(30, std::string(100, ' '));
    modal.render_overlay(modal_rows, 100, 30);
    rendered_modal.clear();
    for (const auto& row : modal_rows) rendered_modal += row;
    expect(rendered_modal.find("conditional branch selects") != std::string::npos,
           "second y reveals the full rationale");
    modal.cycle_mission_hint();
    modal_rows.assign(30, std::string(100, ' '));
    modal.render_overlay(modal_rows, 100, 30);
    rendered_modal.clear();
    for (const auto& row : modal_rows) rendered_modal += row;
    expect(rendered_modal.find("At the branch, a0 is 2") == std::string::npos &&
               rendered_modal.find("conditional branch selects") == std::string::npos,
           "third y hides both hint levels");
    expect(!mission.can_advance_step() && mission.checkpoint_symbol() == "mission_loop",
           "the branch observation waits at the branch target checkpoint");
    mission.observe_symbol("mission_loop");
    expect(mission.checkpoint_reached() && mission.step() == 0,
           "reaching a separate checkpoint marks only its symbol state");
    mission.observe_symbol("mission_loop");
    expect(mission.reached_checkpoint_events() == std::vector<std::string>{"mission_loop"},
           "checkpoint evidence records each reached symbol once in order");
    modal_rows.assign(30, std::string(100, ' '));
    modal.render_overlay(modal_rows, 100, 30);
    rendered_modal.clear();
    for (const auto& row : modal_rows) rendered_modal += row;
    expect(rendered_modal.find("Checkpoint reached · mission_loop") != std::string::npos,
           "mission modal reports automatic checkpoint completion");
    expect(rendered_modal.find("The branch was taken; execution is now at the loop body.") !=
               std::string::npos,
           "mission modal displays the reached checkpoint's configured message");
    expect(mission.advance_step() && mission.step() == 1 && !mission.guidance()->learn.empty() &&
               mission.checkpoint_symbol() == "mission_loop_backedge",
           "acknowledging the branch opens the loop observation with its learning target");
    mission.observe_symbol("mission_loop_backedge");
    expect(mission.checkpoint_reached() && mission.advance_step() && mission.step() == 2 &&
               !mission.guidance()->learn.empty() &&
               mission.checkpoint_symbol() == "mission_before_call",
           "the loop back-edge checkpoint gates the next instruction");
    mission.observe_symbol("mission_before_call");
    expect(mission.checkpoint_reached() && mission.advance_step() && mission.step() == 3 &&
               !mission.guidance()->learn.empty() &&
               mission.checkpoint_symbol() == "mission_frame_ready",
           "the call setup checkpoint gates inspection of the callee");
    mission.observe_symbol("mission_frame_ready");
    expect(mission.checkpoint_reached() && mission.advance_step() && mission.step() == 4 &&
               !mission.guidance()->learn.empty() &&
               mission.checkpoint_symbol() == "mission_after_call",
           "the callee checkpoint gates tracing the return");
    mission.observe_symbol("mission_after_call");
    expect(mission.checkpoint_reached() && mission.advance_step() && mission.step() == 5 &&
               !mission.guidance()->learn.empty() && mission.checkpoint_symbol() == "mission_done",
           "the return checkpoint gates final result review");
    mission.observe_symbol("mission_done");
    expect(mission.enabled() && mission.checkpoint_reached() && mission.step() == 5,
           "final checkpoint is reached independently of the modal");
    expect(mission.advance_step(), "next instruction advances after final checkpoint");
    expect(mission.completed() && mission.step() == 6, "ordered checkpoints complete the mission");
    expect(
        mission.reached_checkpoint_events() ==
            std::vector<std::string>{"mission_loop", "mission_loop_backedge", "mission_before_call",
                                     "mission_frame_ready", "mission_after_call", "mission_done"},
        "TA evidence records all reached checkpoint symbols in order");
    expect(mission.guidance()->title.contains("complete"),
           "completed mission provides review guidance");

    const auto independent_lesson =
        std::filesystem::temp_directory_path() / "simrv-independent-checkpoint-test.mission";
    {
        std::ofstream output(independent_lesson);
        output << "[mission]\nversion = \"1\"\nprogram = \"independent.elf\"\n"
                  "goal = \"Understand the sequence\"\n\n"
                  "[step]\nid = \"first\"\ntitle = \"First\"\n"
                  "prompt = \"Observe first\"\nlearn = \"Learn the first concept\"\n"
                  "why = \"It matters\"\nview = \"registers\"\n\n"
                  "[step]\nid = \"second\"\ntitle = \"Second\"\n"
                  "prompt = \"Observe second\"\nwhy = \"It matters too\"\n"
                  "view = \"trace\"\n\n"
                  "[checkpoint]\nlabel = \"later_marker\"\nafter_step = 2\n"
                  "message = \"Later boundary\"\n";
    }
    simrv::tui::MissionProgress independent;
    independent.configure(independent_lesson.string(), "/tmp/independent.elf");
    expect(independent.enabled() && independent.guidance()->learn == "Learn the first concept" &&
               independent.checkpoint_progress()->after_step == 2 && independent.can_advance_step(),
           "optional learning target loads and a future checkpoint does not block earlier "
           "instruction");
    independent.observe_symbol("later_marker");
    expect(independent.step() == 0 && independent.checkpoint_reached() &&
               independent.checkpoint_progress()->reached && independent.can_advance_step(),
           "a future checkpoint is recorded early without moving the cursor and gates its own "
           "boundary");
    simrv::tui::TuiModal progress_modal(machine);
    progress_modal.set_mission_progress(&independent);
    progress_modal.open_mission_objective();
    std::vector<std::string> progress_rows(30, std::string(120, ' '));
    progress_modal.render_overlay(progress_rows, 120, 30);
    std::string progress_text;
    for (const auto& row : progress_rows) progress_text += row;
    expect(progress_text.find("Reached early · later_marker (after step 2)") != std::string::npos &&
               progress_text.find("continue reading through step 2") != std::string::npos &&
               progress_text.find("Flow:") == std::string::npos,
           "student guidance explains an early checkpoint without a dense progress strip");

    simrv::core::Machine recovery_machine;
    recovery_machine.is_shutdown_.store(true);
    recovery_machine.stop_reason_.store(simrv::core::Machine::StopReason::GuestCrash);
    simrv::tui::MissionProgress recovery_progress;
    recovery_progress.configure(independent_lesson.string(), "/tmp/independent.elf");
    simrv::tui::TuiModal recovery_modal(recovery_machine);
    recovery_modal.set_mission_progress(&recovery_progress);
    recovery_modal.open_mission_objective();
    std::vector<std::string> recovery_rows(30, std::string(120, ' '));
    recovery_modal.render_overlay(recovery_rows, 120, 30);
    std::string recovery_text;
    for (const auto& row : recovery_rows) recovery_text += row;
    expect(recovery_text.find("The program stopped before reaching this checkpoint.") !=
                   std::string::npos &&
               recovery_text.find("restart with r") != std::string::npos &&
               recovery_text.find("[r] Restart") != std::string::npos,
           "mission guidance offers recovery when the guest stops before a checkpoint");
    expect(independent.advance_step() && independent.step() == 1 && independent.can_advance_step(),
           "students can advance up to an already reached checkpoint boundary");
    expect(independent.advance_step() && independent.completed() &&
               independent.reached_checkpoint_count() == 1,
           "reaching the boundary allows completion with factual checkpoint progress");
    std::error_code independent_error;
    std::filesystem::remove(independent_lesson, independent_error);

    simrv::core::Machine layout_machine;
    simrv::tui::Tui layout_tui(layout_machine);
    simrv::tui::TuiTestAccess::init_panes(layout_tui, layout_machine);
    simrv::tui::TuiTestAccess::configure_mission(layout_tui, lesson, "/tmp/control-flow-calls.elf");
    simrv::tui::TuiTestAccess::set_cached_term_width(layout_tui, 100);
    simrv::tui::TuiTestAccess::set_cached_term_height(layout_tui, 24);
    simrv::tui::TuiTestAccess::apply_mission_layout(layout_tui);
    simrv::tui::TuiTestAccess::refresh_mission_layout_notice(layout_tui);
    if (auto* inspector = simrv::tui::TuiTestAccess::inspector(layout_tui)) {
        inspector->set_page(simrv::tui::TuiRegPage::GPR);
        inspector->set_visible_rows(18);
        const std::string drawer_row0 = inspector->render_row(16, 80);
        const std::string drawer_row1 = inspector->render_row(17, 80);
        expect(drawer_row0.contains("Mission 1/6: Follow the branch") &&
                   drawer_row0.contains("mission_loop") && drawer_row1.contains("Task:"),
               "primary inspector pane renders the 2-line mission drawer when a mission is active");
    }
    expect(layout_tui.get_workbench_slots().size() == 3 &&
               simrv::tui::TuiTestAccess::column_widths(layout_tui, 100).count == 2 &&
               simrv::tui::TuiTestAccess::status_override(layout_tui).contains("144x10"),
           "mission retains the full layout, shows fitting panels, and warns when it is too small");
    simrv::tui::TuiTestAccess::apply_mission_speed(layout_tui);
    expect(layout_tui.step_delay_us() == 33'334,
           "classroom mission configures the simulator to its readable default frequency");
    simrv::tui::TuiTestAccess::set_cached_term_width(layout_tui, 160);
    simrv::tui::TuiTestAccess::refresh_mission_layout_notice(layout_tui);
    expect(simrv::tui::TuiTestAccess::column_widths(layout_tui, 160).count == 3 &&
               simrv::tui::TuiTestAccess::status_override(layout_tui).contains("now fits"),
           "mission reports when the requested panel layout fits after resizing");
    auto& lesson_modal = simrv::tui::TuiTestAccess::modal(layout_tui);
    lesson_modal.open_mission_objective();
    simrv::tui::TuiTestAccess::handle_modal_key(layout_tui, '\r', simrv::tui::TuiKey::Enter);
    expect(layout_tui.get_active_modal() == simrv::tui::ModalType::None &&
               layout_tui.mission_progress().step() == 0,
           "Enter hides the mission modal without advancing or completing the instruction");
    simrv::tui::TuiTestAccess::handle_normal_key(layout_tui, 0, simrv::tui::TuiKey::F12);
    expect(layout_tui.get_active_modal() == simrv::tui::ModalType::MissionObjective,
           "F12 reopens the current mission instruction from the workbench");
    const auto status_before_f12 = simrv::tui::TuiTestAccess::status_override(layout_tui);
    simrv::tui::TuiTestAccess::handle_modal_key(layout_tui, 0, simrv::tui::TuiKey::F12);
    expect(layout_tui.get_active_modal() == simrv::tui::ModalType::MissionObjective &&
               simrv::tui::TuiTestAccess::status_override(layout_tui) == status_before_f12,
           "F12 inside the mission modal does not duplicate its visible checkpoint status");
    simrv::tui::TuiTestAccess::handle_modal_key(layout_tui, 27, simrv::tui::TuiKey::Esc);
    expect(layout_tui.get_active_modal() == simrv::tui::ModalType::None &&
               layout_tui.mission_progress().step() == 0,
           "Esc hides the mission modal with the same non-advancing behavior as Enter");
    lesson_modal.open_mission_objective();
    simrv::tui::TuiTestAccess::observe_mission_symbol(layout_tui, "mission_loop");
    simrv::tui::TuiTestAccess::handle_modal_key(layout_tui, ' ', simrv::tui::TuiKey::Space);
    const auto active_after_advance = layout_tui.get_active_modal();
    simrv::tui::TuiTestAccess::handle_modal_key(layout_tui, '\t', simrv::tui::TuiKey::Tab);
    expect(layout_tui.mission_progress().step() == 1 &&
               active_after_advance == simrv::tui::ModalType::MissionObjective &&
               layout_tui.focused_slot() == 0,
           "Space advances instruction while Tab remains unrelated to mission navigation");
    simrv::tui::TuiTestAccess::handle_modal_key(layout_tui, 'p', simrv::tui::TuiKey::p);
    expect(layout_tui.mission_progress().step() == 0 &&
               layout_tui.mission_progress().checkpoint_reached(),
           "p returns to the previous mission step and preserves its reached checkpoint state");
    simrv::tui::TuiTestAccess::handle_modal_key(layout_tui, 'n', simrv::tui::TuiKey::n);
    expect(layout_tui.mission_progress().step() == 1 &&
               !layout_tui.mission_progress().checkpoint_reached(),
           "n advances to the next mission step");

    layout_machine.set_resolved_boot_state(0x80000000, std::nullopt);
    layout_machine.primary_hart().state().pc = 0x80000004;
    layout_machine.primary_hart().state().regs.write(simrv::xlen::RegId::A0, 99);
    simrv::tui::TuiTestAccess::handle_modal_key(layout_tui, 0, simrv::tui::TuiKey::F5);
    expect(layout_tui.get_active_modal() == simrv::tui::ModalType::None && !layout_tui.is_paused(),
           "F5 inside the mission modal hides the modal and starts guest execution directly");
    simrv::tui::TuiTestAccess::observe_mission_symbol(layout_tui, "mission_loop_backedge");
    layout_tui.pause_loop();
    expect(layout_tui.get_active_modal() == simrv::tui::ModalType::None &&
               layout_tui.mission_progress().checkpoint_reached() &&
               simrv::tui::TuiTestAccess::status_override(layout_tui)
                   .contains("Checkpoint 'mission_loop_backedge' after step 2 reached"),
           "reaching a checkpoint keeps the workbench unobstructed and shows a status notice");
    simrv::tui::TuiTestAccess::handle_normal_key(layout_tui, 0, simrv::tui::TuiKey::F5);
    expect(layout_tui.mission_progress().step() == 1 && layout_tui.is_paused() &&
               simrv::tui::TuiTestAccess::status_override(layout_tui)
                   .contains("Press n for the next instruction"),
           "F5 cannot run past a reached checkpoint before the student advances the instruction");
    simrv::tui::TuiTestAccess::handle_normal_key(layout_tui, 'n', simrv::tui::TuiKey::n);
    expect(layout_tui.mission_progress().step() == 2 &&
               layout_tui.get_active_modal() == simrv::tui::ModalType::MissionObjective,
           "pressing n on the workbench after a checkpoint advances and opens the next objective");
    layout_tui.restart_mission();
    expect(layout_tui.mission_progress().step() == 0 &&
               layout_machine.primary_hart().state().pc == 0x80000000 &&
               layout_machine.primary_hart().state().regs.read(simrv::xlen::RegId::A0) == 0,
           "restarting a mission also resets guest architectural state to the entry point");

    simrv::core::Machine completion_machine;
    simrv::tui::Tui completion_tui(completion_machine);
    simrv::tui::TuiTestAccess::configure_mission(completion_tui, lesson,
                                                 "/tmp/control-flow-calls.elf");
    auto& completion_progress = simrv::tui::TuiTestAccess::mission_progress(completion_tui);
    while (completion_progress.step() < 5) {
        if (!completion_progress.can_advance_step())
            simrv::tui::TuiTestAccess::observe_mission_symbol(
                completion_tui, completion_progress.checkpoint_symbol());
        expect(completion_progress.advance_step(),
               "test setup advances to final lesson instruction");
    }
    simrv::tui::TuiTestAccess::observe_mission_symbol(completion_tui, "mission_done");
    simrv::tui::TuiTestAccess::modal(completion_tui).open_mission_objective();
    std::vector<std::string> final_modal_rows(30, std::string(100, ' '));
    simrv::tui::TuiTestAccess::modal(completion_tui).render_overlay(final_modal_rows, 100, 30);
    std::string final_modal_text;
    for (const auto& row : final_modal_rows) final_modal_text += row;
    expect(final_modal_text.find("Checkpoint reached · mission_done") != std::string::npos &&
               final_modal_text.find("[Space] Finish") != std::string::npos,
           "final checkpoint explains that Space completes the lesson for review");
    simrv::tui::TuiTestAccess::handle_modal_key(completion_tui, ' ', simrv::tui::TuiKey::Space);
    expect(completion_progress.completed(), "final instruction advances the mission to complete");
    expect(completion_machine.stop_reason() == simrv::core::Machine::StopReason::Running &&
               !completion_machine.is_shutdown_ &&
               simrv::tui::TuiTestAccess::modal(completion_tui).get_type() ==
                   simrv::tui::ModalType::MissionObjective,
           "mission completion leaves the paused simulator open for review");
    final_modal_rows.assign(30, std::string(100, ' '));
    simrv::tui::TuiTestAccess::modal(completion_tui).render_overlay(final_modal_rows, 100, 30);
    final_modal_text.clear();
    for (const auto& row : final_modal_rows) final_modal_text += row;
    expect(
        final_modal_text.find("6 of 6 checkpoints reached") != std::string::npos &&
            final_modal_text.find("press x while paused on the workbench") == std::string::npos &&
            final_modal_text.find("[r] Restart") != std::string::npos &&
            final_modal_text.find("[q / F10] Exit simulator") != std::string::npos,
        "completion screen offers review, restart, and an explicit simulator exit");
    simrv::tui::TuiTestAccess::handle_modal_key(completion_tui, 'q', simrv::tui::TuiKey::q);
    expect(completion_machine.stop_reason() == simrv::core::Machine::StopReason::GuestExit,
           "the completed mission screen exits only after an explicit quit action");

    mission.restart();
    expect(mission.enabled() && mission.step() == 0 && mission.reached_checkpoint_events().empty(),
           "mission restart resets local progress and checkpoint evidence");
    mission.dismiss();
    expect(
        mission.status() == simrv::tui::MissionStatus::Inactive && !mission.guidance().has_value(),
        "mission dismissal preserves free exploration without guidance");
    mission.configure(lesson, "calls-stack.elf");
    expect(mission.status() == simrv::tui::MissionStatus::ProgramMismatch &&
               mission.guidance().has_value(),
           "mission reports a bundled-program mismatch");
    mission.configure("/tmp/does-not-exist.mission", "control-flow-calls.elf");
    expect(
        mission.status() == simrv::tui::MissionStatus::LoadError && mission.guidance().has_value(),
        "mission reports an unavailable external lesson file");

    const auto invalid_lesson =
        std::filesystem::temp_directory_path() / "simrv-invalid-mission-validation.mission";
    {
        std::ofstream output(invalid_lesson);
        output << "[mission]\nversion = \"1\"\nprogram = \"demo.elf\"\n\n"
                  "[step]\nid = \"dup\"\ntitle = \"First\"\n"
                  "prompt = \"Observe\"\nwhy = \"Reason\"\nview = \"registers\"\n\n"
                  "[step]\nid = \"dup\"\ntitle = \"Second\"\n"
                  "prompt = \"Observe\"\nwhy = \"Reason\"\nview = \"registers\"\n";
    }
    mission.configure(invalid_lesson.string(), "/tmp/demo.elf");
    expect(mission.status() == simrv::tui::MissionStatus::LoadError &&
               mission.guidance()->prompt.contains("Duplicate [step] id"),
           "mission loader rejects duplicate step ids");
    {
        std::ofstream output(invalid_lesson);
        output << "[mission]\nversion = \"1\"\nprogram = \"demo.elf\"\n\n"
                  "[step]\nid = \"one\"\ntitle = \"First\"\n"
                  "prompt = \"Observe\"\nwhy = \"Reason\"\nview = \"registers\"\n"
                  "glossary = \"6\"\n";
    }
    mission.configure(invalid_lesson.string(), "/tmp/demo.elf");
    expect(mission.status() == simrv::tui::MissionStatus::LoadError &&
               mission.guidance()->prompt.contains("glossary"),
           "mission loader rejects out-of-range glossary topics");
    std::error_code invalid_error;
    std::filesystem::remove(invalid_lesson, invalid_error);
}

void test_drawer_stack_layout() {
    simrv::tui::TuiDrawerStack drawers;
    drawers.add({.id = simrv::tui::TuiDrawerId::Log,
                 .name = "Log",
                 .height = 3,
                 .render_row = [](int row, int) { return "log " + std::to_string(row); }});
    drawers.add({.id = simrv::tui::TuiDrawerId::Mission,
                 .name = "Mission",
                 .height = 2,
                 .render_row = [](int row, int) { return row == 0 ? "guide" : "guide body"; }});
    expect(drawers.height() == 5, "drawer stack reserves the combined open drawer height");
    expect(drawers.top_drawer_row(10) == 5, "top_drawer_row identifies start of drawer stack");
    expect(drawers.render_row(5, 10, 80) == "log 0" && drawers.render_row(7, 10, 80) == "log 2" &&
               drawers.render_row(8, 10, 80) == "guide" &&
               drawers.render_row(9, 10, 80) == "guide body",
           "drawer stack places reusable drawers in insertion order at the panel bottom");
    expect(!drawers.render_row(4, 10, 80), "drawer stack leaves panel rows outside its region");

    const auto log_span = drawers.find_span(simrv::tui::TuiDrawerId::Log, 10);
    const auto mission_span = drawers.find_span(simrv::tui::TuiDrawerId::Mission, 10);
    expect(log_span.has_value() && log_span->start_row == 5 && log_span->height == 3 &&
               log_span->is_header_row(5) && !log_span->is_header_row(6),
           "find_span returns exact Log drawer geometry and header row check");
    expect(mission_span.has_value() && mission_span->start_row == 8 && mission_span->height == 2 &&
               mission_span->contains(9) && !mission_span->contains(7),
           "find_span returns exact Mission drawer geometry");
    expect(drawers.drawer_at_row(6, 10)->id == simrv::tui::TuiDrawerId::Log &&
               drawers.drawer_at_row(8, 10)->id == simrv::tui::TuiDrawerId::Mission &&
               !drawers.drawer_at_row(4, 10).has_value(),
           "drawer_at_row hit-tests each drawer span accurately");

    // Verify interactive Mission drawer click and Log drawer toggle bounds in Tui
    const auto lesson = (std::filesystem::path(__FILE__).parent_path().parent_path() /
                         "examples/isa/lessons/control-flow-calls.mission")
                            .string();
    simrv::core::Machine machine;
    simrv::tui::Tui tui(machine);
    simrv::tui::TuiTestAccess::init_panes(tui, machine);
    simrv::tui::TuiTestAccess::configure_mission(tui, lesson, "/tmp/control-flow-calls.elf");
    simrv::tui::TuiTestAccess::set_cached_term_width(tui, 160);
    simrv::tui::TuiTestAccess::set_cached_term_height(tui, 32);
    if (tui.is_modal_active()) tui.close_modal();
    tui.set_workbench_slot_page(0, simrv::tui::TuiRegPage::GPR);
    tui.render(true);

    auto* pane = simrv::tui::TuiTestAccess::inspector(tui);
    // At term_height = 32, num_rows = 25 (>= 20), so Log (rows 17..22, term y 21..26) and
    // Mission (rows 23..24, term y 27..28) are both active on column 0.
    const auto active = pane->active_drawers(80, 25, false);
    expect(active.height() == 8, "both 6-row Log and 2-row Mission drawers are active at 25 rows");
    expect(pane->drawer_at_terminal_row(27, 80, 25, false)->id == simrv::tui::TuiDrawerId::Mission,
           "terminal row 27 maps to the Mission drawer");
    expect(pane->drawer_at_terminal_row(21, 80, 25, false)->id == simrv::tui::TuiDrawerId::Log,
           "terminal row 21 maps to the Log drawer header");

    // Clicking the Mission drawer opens the MissionObjective modal
    tui.handle_mouse(10, 27, 0);
    expect(
        tui.is_modal_active() && tui.get_active_modal() == simrv::tui::ModalType::MissionObjective,
        "clicking the Mission drawer opens the MissionObjective modal");
    tui.close_modal();

    // Toggling the Log drawer off removes Log drawer hit-capture and expands main content bounds
    pane->set_log_drawer_open(false);
    expect(!pane->find_drawer_span(simrv::tui::TuiDrawerId::Log, 80, 25, false).has_value(),
           "closing Log drawer removes its span from active_drawers");
    expect(pane->content_end_terminal_row(80, 25, false) == 26,
           "closing Log drawer extends main content end row down to above Mission drawer");
    pane->set_log_drawer_open(true);
}

void test_tui_running_state_synchronization() {
    simrv::core::MachineConfig config{};
    config.tui.enabled = true;
    simrv::core::Machine machine(config);
    expect(machine.initialize().has_value(), "machine initializes successfully with TUI enabled");
    auto tui = std::make_shared<simrv::tui::Tui>(machine);
    machine.set_telemetry_sink(tui);
    machine.set_console_sink(tui);
    simrv::tui::TuiTestAccess::set_workbench_slots(*tui, {{simrv::tui::TuiRegPage::GPR, 0},
                                                          {simrv::tui::TuiRegPage::CONSOLE, 0},
                                                          {simrv::tui::TuiRegPage::DISPLAY, 0}});
    const auto preferences_path =
        std::filesystem::temp_directory_path() / "simrv-live-overview-preferences.conf";
    simrv::tui::TuiTestAccess::set_preferences_path(*tui, preferences_path);
    simrv::tui::TuiTestAccess::set_focused_slot(*tui, 1);
    expect(machine.execution_state() == simrv::core::ExecutionState::Paused,
           "machine starts in Paused state when TUI is enabled");
    expect(machine.is_paused(), "machine.is_paused() returns true on startup with TUI");
    expect(tui->is_paused(), "tui.is_paused() is true on startup");
    tui->set_paused(false);
    expect(!tui->is_paused(), "tui.is_paused() is false after unpausing");
    expect(tui->get_workbench_slots()[0].page == simrv::tui::TuiRegPage::GPR,
           "running while Console is focused keeps the selected inspector page visible");
    expect(tui->get_workbench_slots()[1].page == simrv::tui::TuiRegPage::CONSOLE,
           "starting execution leaves the other workbench column unchanged");
    tui->save_preferences();
    simrv::tui::TuiPreferences saved_preferences;
    expect(saved_preferences.load(preferences_path) && saved_preferences.slots.size() == 3 &&
               saved_preferences.slots[0].page == simrv::tui::TuiRegPage::GPR &&
               saved_preferences.slots[1].page == simrv::tui::TuiRegPage::CONSOLE &&
               saved_preferences.slots[2].page == simrv::tui::TuiRegPage::DISPLAY,
           "saving while running preserves the selected workbench pages");
    expect(machine.execution_state() == simrv::core::ExecutionState::Running,
           "machine execution state is Running after unpausing TUI");
    expect(!machine.is_paused(), "machine.is_paused() is false after unpausing");

    tui->set_paused(true);
    expect(tui->is_paused(), "tui.is_paused() is true after pausing");
    expect(tui->get_workbench_slots()[0].page == simrv::tui::TuiRegPage::GPR,
           "pausing keeps the inspector page selected during execution");
    expect(tui->focused_slot() == 1,
           "running from a Console-focused state does not move keyboard focus");
    expect(machine.execution_state() == simrv::core::ExecutionState::Paused,
           "machine execution state is Paused after pausing TUI");
    expect(machine.is_paused(), "machine.is_paused() is true after pausing");

    simrv::tui::TuiTestAccess::set_focused_slot(*tui, 2);
    tui->set_paused(false);
    expect(tui->get_workbench_slots()[0].page == simrv::tui::TuiRegPage::GPR &&
               tui->get_workbench_slots()[2].page == simrv::tui::TuiRegPage::DISPLAY,
           "running while Display is focused preserves inspector and display pages");
    tui->set_paused(true);
    expect(tui->get_workbench_slots()[0].page == simrv::tui::TuiRegPage::GPR,
           "pausing after a Display-focused run keeps the inspector page");

    simrv::tui::TuiTestAccess::set_focused_slot(*tui, 0);
    tui->set_paused(false);
    tui->set_workbench_slot_page(0, simrv::tui::TuiRegPage::STACK);
    tui->set_paused(true);
    expect(tui->get_workbench_slots()[0].page == simrv::tui::TuiRegPage::STACK,
           "pausing preserves a manual inspector choice made while execution runs");

    machine.set_telemetry_sink(nullptr);
    machine.set_console_sink(nullptr);
    tui.reset();
    std::error_code ignored;
    std::filesystem::remove(preferences_path, ignored);
}

void test_machine_releases_tui_sinks_before_runtime() {
    auto machine = std::make_unique<simrv::core::Machine>();
    auto tui = std::make_shared<simrv::tui::Tui>(*machine);
    simrv::tui::TuiTestAccess::set_mission_breakpoint(*tui, 0x80000024);
    machine->set_telemetry_sink(tui);
    machine->set_console_sink(tui);
    tui.reset();
    machine.reset();
    expect(true, "machine releases the mission TUI before destroying its breakpoint manager");
}

void test_inspection_report() {
    simrv::core::MachineConfig config{};
    config.execution.num_harts = 2;
    config.tui.inspection_output = "/tmp/simrv-inspection-report-test.json";
    simrv::core::Machine machine(config);
    machine.add_hart_for_testing(std::make_unique<simrv::core::CPU>());
    machine.hart(1).state().pc = 0x80000100;
    machine.hart(1).state().regs.write(simrv::xlen::RegId::A0, 42);
    machine.hart(1).pipeline_context.op_id = simrv::isa::OperationId::ADDI;
    machine.hart(1).pipeline_context.ir_org = 0x02A00513;

    const std::vector<std::string> trace{"0x80000100: addi a0, zero, 42"};
    const std::string report = simrv::tui::make_inspection_report(machine, 1, trace);
    expect(report.contains("\"schema_version\": 1"), "inspection report is versioned");
    expect(report.contains("\"selected_hart\": 1"), "inspection report selects requested hart");
    const std::string expected_a0 = simrv::xlen::kIsXLen64 ? "0x000000000000002a" : "0x0000002a";
    expect(report.contains(expected_a0), "inspection report captures GPR state");
    expect(report.contains("addi a0, zero, 42"), "inspection report captures recent trace");
    expect(!report.contains("\"mission\""), "non-mission reports omit mission evidence");
    expect(!report.contains("student") && !report.contains("grade"),
           "inspection report contains no identity or grading fields");

    const simrv::tui::MissionEvidence evidence{
        .lesson_id = "lesson-demo",
        .completed = true,
        .step = 2,
        .step_count = 2,
        .checkpoint_count = 2,
        .reached_checkpoints = {"entry", "finish"},
        .checkpoint_details = {{"entry", "Start here"}, {"finish", "Result reached"}}};
    const std::string mission_report =
        simrv::tui::make_inspection_report(machine, 1, trace, &evidence);
    const auto entry_pos = mission_report.find("\"entry\"");
    const auto finish_pos = mission_report.find("\"finish\"");
    expect(mission_report.find("\"id\": \"lesson-demo\"") != std::string::npos &&
               mission_report.find("\"completed\": true") != std::string::npos &&
               mission_report.find("\"step\": 2") != std::string::npos &&
               mission_report.find("\"step_count\": 2") != std::string::npos &&
               mission_report.find("\"checkpoint_count\": 2") != std::string::npos &&
               mission_report.find("\"reached_checkpoints\"") != std::string::npos &&
               mission_report.find("\"message\": \"Start here\"") != std::string::npos &&
               mission_report.find("\"message\": \"Result reached\"") != std::string::npos &&
               entry_pos != std::string::npos && finish_pos > entry_pos,
           "inspection export includes lesson id, completion metadata, and ordered checkpoint "
           "evidence");
    expect(!mission_report.contains("student") && !mission_report.contains("grade"),
           "mission evidence adds no student identity or grading fields");

    const auto path = std::filesystem::temp_directory_path() / "simrv-inspection-report-test.json";
    std::error_code error;
    std::filesystem::remove(path, error);
    auto first = simrv::tui::write_inspection_report(path, report, false);
    expect(first.has_value() && *first == simrv::tui::InspectionReportWriteStatus::Written,
           "inspection report writes to an explicit unused path");
    auto guarded = simrv::tui::write_inspection_report(path, report, false);
    expect(
        guarded.has_value() && *guarded == simrv::tui::InspectionReportWriteStatus::WouldOverwrite,
        "inspection report refuses implicit overwrite");
    auto confirmed = simrv::tui::write_inspection_report(path, report, true);
    expect(confirmed.has_value() && *confirmed == simrv::tui::InspectionReportWriteStatus::Written,
           "inspection report replaces a file only after confirmation");
    std::filesystem::remove(path, error);
}

void test_help_uses_canonical_registry() {
    std::vector<std::string> rows;
    simrv::tui::modals::HelpModal::render(
        rows, [&rows](const std::string& row) { rows.push_back(row); }, 48, 78);
    std::string rendered;
    for (const auto& row : rows) rendered += row + '\n';
    for (const auto& binding : simrv::tui::Keybindings::all()) {
        expect(rendered.find(binding.key_display) != std::string::npos,
               "help renders every canonical action binding");
        expect(rendered.find(binding.help_label) != std::string::npos,
               "help renders every canonical action description");
    }
    std::vector<std::string> compact_rows;
    simrv::tui::modals::HelpModal::render(
        compact_rows, [&compact_rows](const std::string& row) { compact_rows.push_back(row); }, 24,
        78);
    for (std::size_t i = 2; i < compact_rows.size(); ++i) {
        expect(simrv::tui::get_display_width(compact_rows[i]) == 76,
               "each dual-column help row exactly fills the modal interior");
    }

    std::vector<std::string> narrow_rows;
    simrv::tui::modals::HelpModal::render(
        narrow_rows, [&narrow_rows](const std::string& row) { narrow_rows.push_back(row); }, 24,
        40);
    for (const auto& row : narrow_rows) {
        expect(simrv::tui::get_display_width(row) == 38,
               "narrow help rows wrap within the modal interior");
    }
}

void test_category_groups_and_glossary() {
    using simrv::tui::get_category_group;
    using simrv::tui::get_category_name;
    using simrv::tui::get_default_page_for_group;
    using simrv::tui::TuiCategoryGroup;
    using simrv::tui::TuiRegPage;

    expect(get_category_group(TuiRegPage::GPR) == TuiCategoryGroup::Regs, "GPR is in Regs group");
    expect(get_category_group(TuiRegPage::FPR) == TuiCategoryGroup::Regs, "FPR is in Regs group");
    expect(get_category_group(TuiRegPage::VEC) == TuiCategoryGroup::Regs, "VEC is in Regs group");

    expect(get_category_group(TuiRegPage::STACK) == TuiCategoryGroup::Memory,
           "STACK is in Memory group");
    expect(get_category_group(TuiRegPage::CACHE) == TuiCategoryGroup::Memory,
           "CACHE is in Memory group");
    expect(get_category_group(TuiRegPage::TLB) == TuiCategoryGroup::Memory,
           "TLB is in Memory group");
    expect(get_category_group(TuiRegPage::BUS) == TuiCategoryGroup::Memory,
           "BUS is in Memory group");

    expect(get_category_group(TuiRegPage::PIPELINE) == TuiCategoryGroup::Pipeline,
           "PIPELINE is in Pipeline group");
    expect(get_category_group(TuiRegPage::BPRED) == TuiCategoryGroup::Pipeline,
           "BPRED is in Pipeline group");
    expect(get_category_group(TuiRegPage::HAZARD) == TuiCategoryGroup::Pipeline,
           "HAZARD is in Pipeline group");

    expect(get_category_group(TuiRegPage::EXPLAIN) == TuiCategoryGroup::Tools,
           "EXPLAIN is in Tools group");
    expect(get_category_group(TuiRegPage::TRACE) == TuiCategoryGroup::Tools,
           "TRACE is in Tools group");

    expect(get_default_page_for_group(TuiCategoryGroup::Regs) == TuiRegPage::GPR,
           "Regs default is GPR");
    expect(get_default_page_for_group(TuiCategoryGroup::Memory) == TuiRegPage::STACK,
           "Memory default is STACK");
    expect(get_default_page_for_group(TuiCategoryGroup::Pipeline) == TuiRegPage::PIPELINE,
           "Pipeline default is PIPELINE");
    expect(get_default_page_for_group(TuiCategoryGroup::Tools) == TuiRegPage::EXPLAIN,
           "Tools default is EXPLAIN");

    // Test GlossaryModal rendering for all 6 topics
    for (int topic = 0; topic < 6; ++topic) {
        std::vector<std::string> rows;
        simrv::tui::modals::GlossaryModal::render(
            rows, [&rows](const std::string& row) { rows.push_back(row); }, topic, 0, 30, 78);
        expect(!rows.empty(), "glossary renders rows for topic " + std::to_string(topic));
        bool found_nav = false;
        for (const auto& r : rows) {
            expect(simrv::tui::get_display_width(r) <= 78,
                   "glossary row stays within the requested modal width");
            if (r.find("Previous") != std::string::npos && r.find("Next") != std::string::npos &&
                r.find("Close") != std::string::npos) {
                found_nav = true;
                break;
            }
        }
        expect(found_nav, "glossary includes consistent action-key navigation");
    }

    std::vector<std::string> pipeline_rows;
    simrv::tui::modals::GlossaryModal::render(
        pipeline_rows, [&pipeline_rows](const std::string& row) { pipeline_rows.push_back(row); },
        1, 0, 30, 78);
    std::string pipeline_text;
    for (auto const& row : pipeline_rows) pipeline_text += row + '\n';
    expect(pipeline_text.contains("IF ") && pipeline_text.contains("MEM") &&
               pipeline_text.contains(" · "),
           "pipeline glossary uses an aligned stage-and-description grammar");
    expect(pipeline_text.contains("◦") && pipeline_text.contains("Forwarding") &&
               pipeline_text.contains("Load-use interlock"),
           "pipeline glossary uses nested bullets for hazard mitigations");

    std::vector<std::string> cfu_pipeline_rows;
    simrv::tui::modals::GlossaryModal::render(
        cfu_pipeline_rows,
        [&cfu_pipeline_rows](const std::string& row) { cfu_pipeline_rows.push_back(row); }, 1, 15,
        30, 78);
    std::string cfu_pipeline_text;
    for (auto const& row : cfu_pipeline_rows) cfu_pipeline_text += row + '\n';
    expect(cfu_pipeline_text.contains("Custom function units") &&
               cfu_pipeline_text.contains("CUSTOM_0"),
           "pipeline glossary includes CFU accelerator concepts");

    std::vector<std::string> scrolled_pipeline_rows;
    simrv::tui::modals::GlossaryModal::render(
        scrolled_pipeline_rows,
        [&scrolled_pipeline_rows](const std::string& row) {
            scrolled_pipeline_rows.push_back(row);
        },
        1, 2, 16, 78);
    std::vector<std::string> unscrolled_pipeline_rows;
    simrv::tui::modals::GlossaryModal::render(
        unscrolled_pipeline_rows,
        [&unscrolled_pipeline_rows](const std::string& row) {
            unscrolled_pipeline_rows.push_back(row);
        },
        1, 0, 16, 78);
    expect(scrolled_pipeline_rows.size() == unscrolled_pipeline_rows.size(),
           "glossary scrolling preserves modal height");
    expect(scrolled_pipeline_rows != unscrolled_pipeline_rows,
           "glossary scrolling changes viewport content");
    std::string compact_pipeline_text;
    for (auto const& row : unscrolled_pipeline_rows) compact_pipeline_text += row + '\n';
    expect(compact_pipeline_text.contains("Up") && compact_pipeline_text.contains("Down"),
           "scrollable glossary exposes directional controls");

    std::vector<std::string> roomy_pipeline_rows;
    simrv::tui::modals::GlossaryModal::render(
        roomy_pipeline_rows,
        [&roomy_pipeline_rows](const std::string& row) { roomy_pipeline_rows.push_back(row); }, 1,
        0, 100, 78);
    std::string roomy_pipeline_text;
    for (auto const& row : roomy_pipeline_rows) roomy_pipeline_text += row + '\n';
    expect(!roomy_pipeline_text.contains("Up") && !roomy_pipeline_text.contains("Down"),
           "non-scrollable glossary omits directional controls");

    std::vector<std::string> narrow_glossary_rows;
    simrv::tui::modals::GlossaryModal::render(
        narrow_glossary_rows,
        [&narrow_glossary_rows](const std::string& row) { narrow_glossary_rows.push_back(row); }, 0,
        0, 24, 40);
    for (const auto& row : narrow_glossary_rows) {
        expect(simrv::tui::get_display_width(row) <= 40,
               "narrow glossary rows do not exceed the modal width");
    }

    int topic_idx = 0;
    int scroll_offset = 0;
    simrv::tui::modals::GlossaryModal::move_topic(topic_idx, scroll_offset, 1);
    expect(topic_idx == 1, "moving topic advances topic index");
    simrv::tui::modals::GlossaryModal::move_topic(topic_idx, scroll_offset, -1);
    expect(topic_idx == 0, "moving topic backward returns to topic 0");
    simrv::tui::modals::GlossaryModal::scroll_content(scroll_offset, 2, 20);
    expect(scroll_offset == 2, "scrolling advances scroll offset");
}

void test_input_routing() {
    using simrv::tui::InputContext;
    using simrv::tui::InputRoute;
    using simrv::tui::normalize_guest_terminal_byte;
    using simrv::tui::route_input;

    const InputContext focused{.modal_active = false, .paused = false};
    expect(route_input('\r', focused) == InputRoute::Guest,
           "Enter reaches the guest while its terminal is focused");
    expect(route_input('\n', focused) == InputRoute::Guest,
           "newline reaches the guest while its terminal is focused");
    expect(route_input(0x10, focused) == InputRoute::Pause, "Ctrl-P pauses a running guest");
    expect(route_input(0x12, focused) == InputRoute::Reboot,
           "Ctrl-R requests reboot instead of reaching a running guest");
    expect(route_input(0x11, focused) == InputRoute::Quit, "Ctrl-Q quits a running guest");
    expect(route_input(0x03, focused) == InputRoute::Quit, "Ctrl-C quits a running guest");
    expect(route_input(0x01, focused) == InputRoute::Guest,
           "Ctrl-A is passed through when the guest is running");

    const InputContext paused{.modal_active = false, .paused = true};
    expect(route_input('\r', paused) == InputRoute::Navigation,
           "Enter is a navigation action while paused");
    const InputContext modal{.modal_active = true, .paused = true};
    expect(route_input('\r', modal) == InputRoute::Modal, "Enter submits the active modal");
    expect(route_input(0x01, modal) == InputRoute::Modal, "Ctrl-A has no global binding");
    expect(route_input(0x11, modal) == InputRoute::Quit, "Ctrl-Q remains globally available");
    expect(route_input(0x03, modal) == InputRoute::Quit, "Ctrl-C remains globally available");
    expect(route_input(0x12, modal) == InputRoute::Reboot,
           "Ctrl-R remains globally available after shutdown notices and other modals");
    expect(route_input(0x1B, modal) == InputRoute::ControlSequence,
           "Escape is parsed before modal dispatch");
    expect(normalize_guest_terminal_byte('\r') == '\n',
           "host carriage return becomes the guest console line delimiter");
    expect(normalize_guest_terminal_byte('x') == 'x', "ordinary guest input is unchanged");
}

void test_responsive_layout() {
    using simrv::tui::calculate_frame_geometry;
    using simrv::tui::calculate_overlay_geometry;
    using simrv::tui::calculate_pane_widths;
    using simrv::tui::TuiLayout;

    const auto narrow = calculate_pane_widths(40, TuiLayout::Split);
    expect(narrow.left + narrow.right == 37, "narrow split geometry accounts for all borders");
    expect(narrow.left > 0 && narrow.right > 0, "narrow resize keeps both panes visible");

    const auto desktop = calculate_pane_widths(140, TuiLayout::Split);
    expect(desktop.left == 61 && desktop.right == 76,
           "desktop split keeps the intended default inspection pane");
    const auto constrained = calculate_pane_widths(80, TuiLayout::Split, 200);
    expect(constrained.right == 30, "manual resizing preserves a usable guest terminal");

    const auto full_right = calculate_pane_widths(80, TuiLayout::FullRight, 60);
    expect(full_right.left == 0 && full_right.right == 78,
           "full-right resize ignores stale split-pane width");

    struct FrameCase {
        int width;
        int height;
        TuiLayout layout;
    };
    constexpr FrameCase frames[] = {
        {40, 10, TuiLayout::Split},     {80, 24, TuiLayout::Split},
        {120, 32, TuiLayout::Split},    {160, 48, TuiLayout::Split},
        {120, 32, TuiLayout::FullLeft}, {120, 32, TuiLayout::FullRight},
    };
    for (const auto& frame_case : frames) {
        const auto frame =
            calculate_frame_geometry(frame_case.width, frame_case.height, frame_case.layout);
        expect(frame.renderable, "representative terminal geometry is renderable");
        expect(frame.frame_rows == frame_case.height,
               "header, content, divider, and footer consume the exact terminal height");
        const int separators = frame_case.layout == TuiLayout::Split ? 3 : 2;
        expect(frame.panes.left + frame.panes.right + separators == frame_case.width,
               "pane widths and vertical borders consume the exact terminal width");
    }
    expect(!calculate_frame_geometry(39, 24, TuiLayout::Split).renderable,
           "sub-minimum width is rejected by the shared geometry policy");
    expect(!calculate_frame_geometry(80, 9, TuiLayout::Split).renderable,
           "sub-minimum height is rejected by the shared geometry policy");

    const auto short_modal = calculate_overlay_geometry(40, 10, 78, 30);
    expect(short_modal.renderable && short_modal.width == 36 && short_modal.height == 10,
           "a tall modal is constrained to the minimum terminal frame");
    expect(short_modal.start_x == 2 && short_modal.start_y == 0 &&
               short_modal.visible_content_rows == 8,
           "constrained modal geometry remains centered with two visible borders");
    const auto desktop_modal = calculate_overlay_geometry(120, 32, 78, 12);
    expect(desktop_modal.width == 78 && desktop_modal.height == 14 && desktop_modal.start_x == 21 &&
               desktop_modal.start_y == 9,
           "desktop modals retain their intended centered dimensions");
}

void test_frame_composition() {
    using simrv::tui::calculate_frame_geometry;
    using simrv::tui::compose_frame_lines;
    using simrv::tui::format_to_width;
    using simrv::tui::TuiLayout;

    struct FrameCase {
        int width;
        int height;
        TuiLayout layout;
        std::uint64_t golden_hash;
    };
    constexpr FrameCase cases[] = {
        {40, 10, TuiLayout::Split, 1195331755914945392ULL},
        {80, 24, TuiLayout::Split, 2550224749323736092ULL},
        {120, 32, TuiLayout::Split, 8780889241500289282ULL},
        {160, 48, TuiLayout::Split, 11898988648789430246ULL},
        {120, 32, TuiLayout::FullLeft, 17252040885446061950ULL},
        {120, 32, TuiLayout::FullRight, 3818806389424029207ULL},
    };
    for (const auto& frame_case : cases) {
        const auto frame =
            calculate_frame_geometry(frame_case.width, frame_case.height, frame_case.layout);
        const std::string header = format_to_width("header-0", frame_case.width) + "\n" +
                                   format_to_width("header-1", frame_case.width) + "\n" +
                                   format_to_width("header-2", frame_case.width);
        const std::string footer = format_to_width("footer-0", frame_case.width) + "\n" +
                                   format_to_width("footer-1", frame_case.width) + "\n" +
                                   format_to_width("footer-2", frame_case.width);
        const auto lines = compose_frame_lines(
            frame, frame_case.width, frame_case.layout, header, footer,
            [](int row, int) { return "left-" + std::to_string(row); },
            [](int row, int) { return "right-" + std::to_string(row); });
        expect(static_cast<int>(lines.size()) == frame_case.height,
               "composed frame has exactly one row per terminal row");
        for (const auto& line : lines) {
            expect(simrv::tui::get_display_width(line) == frame_case.width,
                   "every composed frame row exactly matches terminal width");
        }
        const std::string body = strip_ansi(lines.at(3));
        expect(body.starts_with("║") && body.ends_with("║"),
               "composed body retains both outer borders");
        expect((body.find("│") != std::string::npos) == (frame_case.layout == TuiLayout::Split),
               "center divider appears only in split layout");
        std::string ansi_screen;
        for (const auto& line : lines) ansi_screen += line + '\n';
        const auto golden_hash = fnv1a64(ansi_screen);
        // Deliberate updates are surfaced by the failing ctest message with the new hash.
        expect(golden_hash == frame_case.golden_hash,
               "exact ANSI frame golden changed for " + std::to_string(frame_case.width) + "x" +
                   std::to_string(frame_case.height) + " layout " +
                   std::to_string(static_cast<int>(frame_case.layout)) + ": " +
                   std::to_string(golden_hash));
    }

    const auto junction_frame = calculate_frame_geometry(80, 24, TuiLayout::Split);
    const auto junction_lines = compose_frame_lines(
        junction_frame, 80, TuiLayout::Split, "header-0\nheader-1\nheader-2",
        "footer-0\nfooter-1\nfooter-2",
        [](int row, int width) {
            return row == 2 ? simrv::tui::make_repeated_string("─", width) : std::string{};
        },
        [](int, int) { return std::string{}; });
    const std::string split_junction = strip_ansi(junction_lines.at(5));
    expect(split_junction.starts_with("╟"), "left-pane section rules join the double outer border");
    expect(split_junction.find("┤") != std::string::npos,
           "left-pane section rules join the split-pane divider");
    expect(simrv::tui::get_display_width(junction_lines.at(5)) == 80,
           "section junctions preserve split-frame width");

    const auto full_left_frame = calculate_frame_geometry(80, 24, TuiLayout::FullLeft);
    const auto full_left_lines = compose_frame_lines(
        full_left_frame, 80, TuiLayout::FullLeft, "header-0\nheader-1\nheader-2",
        "footer-0\nfooter-1\nfooter-2",
        [](int row, int width) {
            return row == 2 ? simrv::tui::make_repeated_string("─", width) : std::string{};
        },
        [](int, int) { return std::string{}; });
    const std::string full_left_junction = strip_ansi(full_left_lines.at(5));
    expect(full_left_junction.starts_with("╟") && full_left_junction.ends_with("╢"),
           "full-left section rules join both double outer borders");
}

void test_themes_and_mouse_interactions() {
    using simrv::tui::cycle_theme_style;
    using simrv::tui::get_active_theme_style;
    using simrv::tui::get_display_width;
    using simrv::tui::get_theme_glyphs;
    using simrv::tui::set_theme_style;
    using simrv::tui::TuiThemeStyle;

    // 1. Validate Classic ANSI theme glyphs contain no multi-byte unicode or emojis
    const auto& ansi_glyphs = get_theme_glyphs(TuiThemeStyle::ClassicAnsi);
    expect(std::string(ansi_glyphs.top_left) == "+", "ANSI top left is +");
    expect(std::string(ansi_glyphs.horiz) == "-", "ANSI horiz is -");
    expect(std::string(ansi_glyphs.vert) == "|", "ANSI vert is |");
    expect(std::string(ansi_glyphs.icon_settings) == "CFG", "ANSI settings icon is plain text CFG");
    expect(std::string(ansi_glyphs.icon_theme) == "THM", "ANSI theme icon is plain text THM");
    expect(std::string(ansi_glyphs.icon_power) == "RST", "ANSI power icon is plain text RST");

    // Check ASCII purity for all ANSI glyph strings
    const char* const all_ansi_ptrs[] = {
        ansi_glyphs.top_left,      ansi_glyphs.top_right,  ansi_glyphs.bot_left,
        ansi_glyphs.bot_right,     ansi_glyphs.horiz,      ansi_glyphs.vert,
        ansi_glyphs.tee_left,      ansi_glyphs.tee_right,  ansi_glyphs.tee_top,
        ansi_glyphs.tee_bot,       ansi_glyphs.cross,      ansi_glyphs.double_horiz,
        ansi_glyphs.double_vert,   ansi_glyphs.bullet,     ansi_glyphs.arrow_up,
        ansi_glyphs.arrow_down,    ansi_glyphs.arrow_left, ansi_glyphs.arrow_right,
        ansi_glyphs.icon_settings, ansi_glyphs.icon_help,  ansi_glyphs.icon_theme,
        ansi_glyphs.icon_power,    ansi_glyphs.icon_warn,  ansi_glyphs.icon_error};
    for (const char* ptr : all_ansi_ptrs) {
        for (const char* c = ptr; *c != '\0'; ++c) {
            expect(static_cast<unsigned char>(*c) < 128,
                   "Classic ANSI theme contains strictly pure ASCII (no emojis)");
        }
    }

    // 2. Validate Modern Unicode theme glyphs
    const auto& modern_glyphs = get_theme_glyphs(TuiThemeStyle::ModernUnicode);
    expect(std::string(modern_glyphs.top_left) == "╭", "Modern top left is ╭");
    expect(std::string(modern_glyphs.horiz) == "─", "Modern horiz is ─");
    expect(std::string(modern_glyphs.vert) == "│", "Modern vert is │");

    // 3. Validate Theme Cycling
    set_theme_style(TuiThemeStyle::ModernUnicode);
    expect(get_active_theme_style() == TuiThemeStyle::ModernUnicode, "active style is modern");
    cycle_theme_style();
    expect(get_active_theme_style() == TuiThemeStyle::ClassicAnsi, "cycled to classic ansi");
    cycle_theme_style();
    expect(get_active_theme_style() == TuiThemeStyle::SakuraPastel, "cycled to sakura pastel");
    cycle_theme_style();
    expect(get_active_theme_style() == TuiThemeStyle::ModernUnicode,
           "cycled back to modern unicode");
}

void test_log_buffer_wrapping() {
    simrv::tui::LogBuffer buffer;
    buffer.push("Short log line");
    buffer.push(
        "\033[36mLoaded 105604 symbols from ELF image: "
        "linux-images/rv64/alpine-gui/opensbi-linux-payload.elf\033[0m");

    auto lines_w30 = buffer.get_wrapped_lines(30, 20);
    expect(lines_w30.size() >= 3, "Log wrapped across multiple lines when width=30");
    expect(strip_ansi(lines_w30[0]) == "Short log line", "First short line is unchanged");
    expect(strip_ansi(lines_w30[2]).starts_with("  "),
           "Wrapped continuation line starts with 2-space indent");

    auto lines_w100 = buffer.get_wrapped_lines(100, 20);
    expect(lines_w100.size() == 2, "Log reflows to 2 lines when width=100");
    expect(buffer.get_wrapped_lines(100, 20) == lines_w100,
           "Repeated log wrapping returns the cached layout");
    buffer.push("Cache invalidation line");
    auto updated = buffer.get_wrapped_lines(100, 20);
    expect(updated.size() == 3 && strip_ansi(updated.back()) == "Cache invalidation line",
           "Appending a log entry invalidates the wrapped layout cache");
}

void test_modal_components() {
    using namespace simrv::tui::modals;

    // Test dynamic metadata registry
    const auto meta_bp = get_modal_metadata(simrv::tui::ModalType::SetBreakpoint);
    expect(meta_bp.title == " SET BREAKPOINT ", "ModalMetadata title matches for SetBreakpoint");
    expect(!meta_bp.is_wide, "SetBreakpoint is not wide");

    const auto meta_settings = get_modal_metadata(simrv::tui::ModalType::Settings);
    expect(meta_settings.title == " SIMULATOR SETTINGS & CONFIGURATION ", "Settings title matches");
    expect(meta_settings.is_wide, "Settings is wide modal");

    const auto meta_notice = get_modal_metadata(simrv::tui::ModalType::Notice, true, "ALERT");
    expect(meta_notice.title.contains("❌ ALERT"), "Notice error title contains icon and name");

    // Test unified footer builder
    const auto footer = build_modal_footer({{"[Enter]", "Apply"}, {"[Esc]", "Cancel"}});
    expect(footer.contains(" Enter ") && footer.contains("Apply") && footer.contains(" Esc ") &&
               footer.contains("Cancel"),
           "Modal footer renders filled keycaps and action labels");
    const int footer_width = simrv::tui::get_display_width(footer);
    const auto centered_footer = align_modal_control_row(footer, 60);
    expect(strip_ansi(centered_footer)
               .starts_with(std::string(static_cast<std::size_t>((60 - footer_width) / 2), ' ')),
           "modal footer uses the shared control-row centering policy");
    const auto footer_layout = layout_modal_control_row(footer, 60);
    expect(footer_layout.spans.size() == 2 &&
               footer_layout.spans[0].contains(footer_layout.spans[0].start) &&
               footer_layout.spans[1].start > footer_layout.spans[0].start,
           "modal footer rendering exposes aligned click spans for every action");

    // Test tab bar builder
    static constexpr std::array<std::string_view, 3> tabs = {"Tab1", "Tab2", "Tab3"};
    const auto tab_bar = build_modal_tab_bar(tabs, 1);
    expect(tab_bar.contains(" 1 ") && tab_bar.contains("Tab1") && tab_bar.contains(" 2 ") &&
               tab_bar.contains("Tab2") && tab_bar.contains(" 3 ") && tab_bar.contains("Tab3"),
           "Modal tab bar renders all numbered tabs");
    const int tab_width = simrv::tui::get_display_width(tab_bar);
    const auto centered_tabs = align_modal_control_row(tab_bar, 48);
    expect(strip_ansi(centered_tabs)
               .starts_with(std::string(static_cast<std::size_t>((48 - tab_width) / 2), ' ')),
           "modal tabs use the shared control-row centering policy");

    // Test section divider and menu item row
    const auto div = build_section_divider("ISA Extensions");
    expect(div.contains("ISA Extensions") && div.contains("──"), "Section divider formatted");

    const auto row_sel = build_menu_item_row("Option A", "[ON]", true, 20);
    expect(row_sel.contains("\033[1;7m") && row_sel.contains("ON") && !row_sel.contains("[ON]"),
           "Selected menu row uses a filled label and an unboxed value");

    // Test text input helper
    std::vector<std::string> input_rows;
    build_text_input_rows(input_rows, "Enter Path:", "/tmp/test.bin", "hint example");
    expect(input_rows.size() == 3, "Input helper creates prompt, cursor, and hint rows");
    expect(input_rows[1].contains(" INPUT ") && input_rows[1].contains("/tmp/test.bin_"),
           "Active input row has a filled label and trailing cursor");
}

void test_instruction_explainer_is_side_effect_free() {
    simrv::core::Machine machine;
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram);
    expect(machine.ram_view().span().size() == ram.size(), "RamView span matches backing size");
    auto& cpu = machine.primary_hart();
    cpu.machine_ = &machine;
    cpu.reset();
    constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
    constexpr simrv::xlen::Instruction addi = 0x00100093U;
    std::memcpy(ram.data(), &addi, sizeof(addi));
    cpu.state().pc = pc;
    cpu.pipeline_context.ir = 0xDEADBEEFU;
    cpu.pipeline_context.cpc = simrv::xlen::VirtAddr{pc + 0x40};

    const auto before_pc = cpu.state().pc;
    const auto before_context_ir = cpu.pipeline_context.ir;
    const auto before_context_pc = cpu.pipeline_context.cpc;
    const auto before_icache_hits = cpu.icache.hit_count();
    const auto before_icache_misses = cpu.icache.miss_count();
    const auto before_tlb_lru = cpu.tlb.inst_r_lru;

    simrv::tui::InspectorPane pane(machine);
    pane.set_page(simrv::tui::TuiRegPage::EXPLAIN);
    pane.set_explain_pc(pc);
    pane.set_visible_rows(30);
    const auto rendered = pane.render_row(2, 80);

    expect(rendered.contains("Instruction Explainer"), "explainer renders inspected instruction");
    expect(cpu.state().pc == before_pc, "explainer preserves architectural PC");
    expect(cpu.pipeline_context.ir == before_context_ir &&
               cpu.pipeline_context.cpc == before_context_pc,
           "explainer preserves the live pipeline context");
    expect(cpu.icache.hit_count() == before_icache_hits &&
               cpu.icache.miss_count() == before_icache_misses,
           "explainer preserves instruction-cache counters");
    expect(cpu.tlb.inst_r_lru == before_tlb_lru, "explainer preserves TLB replacement state");

    // Microarchitectural claims are shown only when cycle telemetry is available.
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleObservable;
    pane.set_paused(true);
    pane.set_page(simrv::tui::TuiRegPage::EXPLAIN);
    pane.set_visible_rows(80);
    bool found_microarch = false;
    bool found_rd_bank = false;
    for (int r = 0; r < 60; ++r) {
        std::string line = pane.render_row(r, 80);
        if (line.contains("Microarchitectural & Hazard Profile")) found_microarch = true;
        if (line.contains("rd (dest)") && line.contains("Int RegFile")) found_rd_bank = true;
    }
    expect(found_microarch, "explainer renders Microarchitectural & Hazard Profile header");
    expect(found_rd_bank, "explainer renders destination register bank classification");
}

void test_instruction_explainer_and_csr_metadata() {
    // 1. CSR metadata queries
    auto satp_details = simrv::util::get_csr_details(0x180);
    expect(satp_details.has_value() && satp_details->name == "satp", "satp metadata is resolved");
    expect(satp_details->min_privilege == simrv::xlen::PrivilegeLevel::Supervisor,
           "satp requires Supervisor privilege");
    expect(!satp_details->read_only, "satp is read-write");

    auto vstart_details = simrv::util::get_csr_details(0x008);
    expect(vstart_details.has_value() && vstart_details->name == "vstart",
           "vstart metadata is resolved");
    expect(vstart_details->extension == "Vector", "vstart belongs to Vector extension");

    auto stimecmp_details = simrv::util::get_csr_details(0x14D);
    expect(stimecmp_details.has_value() && stimecmp_details->name == "stimecmp",
           "stimecmp metadata is resolved");
    expect(stimecmp_details->extension == "Sstc", "stimecmp belongs to Sstc extension");

    auto mseccfg_details = simrv::util::get_csr_details(0x747);
    expect(mseccfg_details.has_value() && mseccfg_details->name == "mseccfg",
           "mseccfg metadata is resolved");
    expect(mseccfg_details->extension == "Smepmp", "mseccfg belongs to Smepmp extension");

    auto pmpcfg0_details = simrv::util::get_csr_details(0x3A0);
    expect(pmpcfg0_details.has_value() && pmpcfg0_details->name == "pmpcfg0",
           "pmpcfg0 metadata is resolved");

    // 2. Vector and CFU operation details
    auto vmv_desc = simrv::util::get_operation_details(simrv::isa::OperationId::VMV1R_V);
    expect(vmv_desc.first == "VMV1R.V" && !vmv_desc.second.contains("UNKNOWN"),
           "VMV1R.V has dedicated pedagogical description");

    auto vadc_desc = simrv::util::get_operation_details(simrv::isa::OperationId::VADC_VVM);
    expect(vadc_desc.first == "VADC.VVM" && !vadc_desc.second.contains("UNKNOWN"),
           "VADC.VVM has dedicated pedagogical description");

    auto cfu_desc = simrv::util::get_operation_details(simrv::isa::OperationId::CFU);
    expect(cfu_desc.first == "CFU", "CFU mnemonic is resolved");

    // 3. Extension classifications
    expect(simrv::util::get_isa_extension_name(simrv::isa::OperationId::VCLMUL_VV) == "Zvbc",
           "VCLMUL.VV classifies as Zvbc");
    expect(simrv::util::get_isa_extension_name(simrv::isa::OperationId::VANDN_VV) == "Zvbb",
           "VANDN.VV classifies as Zvbb");
    expect(simrv::util::get_isa_extension_name(simrv::isa::OperationId::CFU) == "Custom",
           "CFU classifies as Custom");

    // 4. Trap cause naming updates (Privileged 1.13)
    expect(simrv::core::trap_cause_name(to_trap_cause(ExceptionCode::SoftwareCheck)) ==
               "Software check / pointer checking",
           "SoftwareCheck trap cause is named canonically");
    expect(simrv::core::trap_cause_name(to_trap_cause(ExceptionCode::HardwareError)) ==
               "Hardware error",
           "HardwareError trap cause is named canonically");

    // 5. CFU format, traits, and details
    expect(simrv::isa::get_instruction_format(simrv::isa::Opcode::Custom0) ==
               simrv::isa::InstFormat::R,
           "Custom0 opcode classifies as R-Type format");
    expect(simrv::isa::is_cfu_op(simrv::isa::OperationId::CFU),
           "CFU operation identifies as CFU op");

    auto cfu_details = simrv::util::get_cfu_details(0x05, 4);
    expect(cfu_details.name == "CFU" && cfu_details.funct3 == 4 && cfu_details.funct7 == 0x05,
           "CFU details structure carries correct metadata");
    expect(cfu_details.description.contains("custom function 4"),
           "CFU description includes function selector");
}

void test_inspector_panels_traits_and_scoreboard() {
    simrv::core::Machine machine;
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    auto& cpu = machine.primary_hart();
    cpu.machine_ = &machine;
    cpu.reset();

    // 1. Project authoritative pipeline slots into the sampled TUI scoreboard.
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
    cpu.ca_pipeline.execute->valid = true;
    cpu.ca_pipeline.execute->writes_int = true;
    cpu.ca_pipeline.execute->wb_dest = static_cast<simrv::xlen::RegId>(1);
    cpu.ca_pipeline.execute->remaining_latency = 2;
    cpu.ca_pipeline.memory->valid = true;
    cpu.ca_pipeline.memory->writes_int = true;
    cpu.ca_pipeline.memory->wb_dest = static_cast<simrv::xlen::RegId>(2);
    cpu.ca_pipeline.memory->wb_valid = true;
    machine.publish_tui_execution_snapshot_for_testing();

    simrv::tui::InspectorPane pane(machine);
    pane.refresh_execution_snapshot();
    pane.set_page(simrv::tui::TuiRegPage::GPR);
    pane.set_paused(true);
    pane.set_visible_rows(30);

    std::string reg1_row = pane.render_row(3, 80);  // content row for x1
    expect(reg1_row.contains("[EX 2c]"),
           "register view renders in-flight reservation badge [EX 2c]");

    std::string reg2_row = pane.render_row(4, 80);  // content row for x2
    expect(reg2_row.contains("[FWD]"), "register view renders forwarding badge [FWD]");

    // 2. Test Live Scoreboard table in Hazard pane (cycle mode active)
    pane.set_page(simrv::tui::TuiRegPage::HAZARD);
    std::string hazard_row_header = pane.render_row(12, 80);  // logical_row 10
    expect(hazard_row_header.contains("Live Multi-Bank Scoreboard"),
           "hazard pane renders Live Multi-Bank Scoreboard section header");
    std::string hazard_row_entry = pane.render_row(13, 80);  // logical_row 11
    expect(hazard_row_entry.contains("INT") && hazard_row_entry.contains("x1") &&
               hazard_row_entry.contains("EX"),
           "hazard pane renders active in-flight reservation item");

    machine.runtime_profile.engine = simrv::core::ExecutionEngine::InstructionFast;
    machine.publish_tui_execution_snapshot_for_testing();
    pane.refresh_execution_snapshot();
    pane.set_page(simrv::tui::TuiRegPage::GPR);
    expect(!pane.render_row(3, 80).contains("[EX 2c]"),
           "functional-mode snapshots contain no pipeline reservations");

    simrv::core::Machine smp_machine(simrv::core::MachineConfig{
        .execution = {.num_harts = 2}, .tui = {.enabled = true, .inspection_output = {}}});
    smp_machine.add_hart_for_testing(std::make_unique<simrv::core::CPU>());
    std::vector<simrv::xlen::Byte> smp_ram(1024 * 1024, simrv::xlen::Byte{0});
    smp_machine.set_ram_for_testing(smp_ram.data(), smp_ram.size());
    smp_machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
    auto& secondary = smp_machine.hart(1);
    secondary.ca_pipeline.writeback->valid = true;
    secondary.ca_pipeline.writeback->writes_fp = true;
    secondary.ca_pipeline.writeback->wb_dest = static_cast<simrv::xlen::RegId>(3);
    secondary.ca_pipeline.writeback->wb_valid = true;
    for (const auto pipeline_type :
         {simrv::pipeline::PipelineType::ThreeStage, simrv::pipeline::PipelineType::FiveStage}) {
        secondary.pipeline_sim.config.pipeline_type = pipeline_type;
        smp_machine.publish_tui_execution_snapshot_for_testing();
        const auto primary_snapshot = smp_machine.tui_execution_snapshot(0);
        const auto secondary_snapshot = smp_machine.tui_execution_snapshot(1);
        expect(!primary_snapshot.scoreboard.is_busy(simrv::pipeline::operation::RegBank::Float,
                                                    static_cast<simrv::xlen::RegId>(3)),
               "scoreboard projection remains isolated by hart");
        expect(secondary_snapshot.scoreboard.can_forward(simrv::pipeline::operation::RegBank::Float,
                                                         static_cast<simrv::xlen::RegId>(3)),
               "three- and five-stage snapshots project FP forwarding state");
    }

    simrv::tui::InspectorPane secondary_pane(smp_machine);
    secondary_pane.set_selected_hart(1);
    secondary_pane.refresh_execution_snapshot();
    secondary_pane.set_page(simrv::tui::TuiRegPage::FPR);
    secondary_pane.set_paused(true);
    secondary_pane.set_visible_rows(30);
    expect(secondary_pane.render_row(5, 80).contains("[FWD]"),
           "selected-hart register view consumes its sampled FP scoreboard");
}

void test_multicolumn_refinement() {
    simrv::core::Machine machine;
    simrv::tui::InspectorPane pane(machine);
    pane.set_paused(true);
    pane.set_visible_rows(35);
    pane.set_page(simrv::tui::TuiRegPage::PIPELINE);

    // 1. Check compact column header
    std::string h1 = pane.render_column_header(0, "GPR Registers", false, 40);
    std::string h2 = pane.render_column_header(1, "Pipeline Stages", true, 40);
    expect(h1.contains("[1: GPR Registers]"),
           "unfocused column header contains column number and name");
    expect(h2.contains("[2: Pipeline Stages]"),
           "focused column header contains column number and name");

    // 2. Primary render contains performance/debug/log
    bool primary_has_perf_or_log = false;
    for (int r = 0; r < 35; ++r) {
        std::string line = pane.render_row(r, 45);
        if (line.contains("Performance") || line.contains("Log")) {
            primary_has_perf_or_log = true;
            break;
        }
    }
    expect(primary_has_perf_or_log, "primary pane render contains performance or log");

    // 3. Secondary column render MUST NOT contain performance, debug, or log
    bool secondary_has_perf_or_log = false;
    for (int r = 0; r < 35; ++r) {
        std::string line =
            pane.render_column_row(r, 45, /*col_idx=*/1, /*total_cols=*/4, /*is_focused=*/false);
        if (line.contains("Performance") || line.contains("Debug - Live") || line.contains("Log")) {
            secondary_has_perf_or_log = true;
            break;
        }
    }
    expect(!secondary_has_perf_or_log,
           "secondary column in multi-column layout suppresses duplicate performance and log");

    // Row 0 of secondary column is the column header
    std::string sec_header = pane.render_column_row(0, 45, 1, 4, false);
    expect(sec_header.contains("[2: Pipeline Stages]"),
           "secondary column row 0 is the column header");

    // 4. Forced column header for column 0
    std::string col0_forced =
        pane.render_column_row(0, 45, 0, 2, false, /*force_column_header=*/true);
    expect(col0_forced.contains("[1: Pipeline Stages]"),
           "forced column 0 header renders column header");

    // A column header occupies one row.  Its scroll marker therefore belongs
    // on the final visible panel row, not one row above it.
    pane.set_page(simrv::tui::TuiRegPage::GPR);
    pane.set_visible_rows(10);
    (void)pane.render_column_row(0, 40, 0, 4, true);
    std::string const penultimate = pane.render_column_row(8, 40, 0, 4, true);
    std::string const last = pane.render_column_row(9, 40, 0, 4, true);
    expect(!strip_ansi(penultimate).contains("more lines below"),
           "multi-column scroll marker does not consume the penultimate row");
    expect(strip_ansi(last).contains("more lines below"),
           "multi-column scroll marker occupies the final content row");
    pane.scroll(1);
    expect(pane.get_scroll_offset() == 1,
           "multi-column primary pane scrolls after its one-row header");

    // 5. Layout presets in functional mode do not produce duplicate panels
    simrv::core::Machine fm_machine;
    simrv::tui::Tui fm_tui(fm_machine);
    simrv::tui::TuiTestAccess::set_cached_term_width(fm_tui, 192);
    fm_tui.apply_layout_preset(simrv::tui::LayoutPreset::MemoryInterconnect);
    const auto& f4_slots = fm_tui.get_workbench_slots();
    expect(f4_slots.size() >= 2, "preset 4 has at least 2 slots");
    expect(f4_slots[0].page != f4_slots[1].page,
           "preset 4 slots 0 and 1 have distinct pages in functional mode");

    // Resizing applies to the focused column and transfers space to its neighbor.
    simrv::core::Machine resize_machine;
    simrv::tui::Tui resize_tui(resize_machine);
    simrv::tui::TuiTestAccess::set_cached_term_width(resize_tui, 180);
    resize_tui.apply_layout_preset(simrv::tui::LayoutPreset::GeneralDebug);
    auto before_widths = simrv::tui::TuiTestAccess::column_widths(resize_tui, 180);
    expect(before_widths.count == 3, "wide preset selects the three-column layout");
    resize_tui.adjust_inspector_width(2);
    auto after_widths = simrv::tui::TuiTestAccess::column_widths(resize_tui, 180);
    expect(after_widths.widths[0] == before_widths.widths[0] + 2 &&
               after_widths.widths[1] == before_widths.widths[1] - 2 &&
               after_widths.widths[2] == before_widths.widths[2],
           "bracket resize transfers width from the next focused-panel neighbor");

    // A new column is inserted immediately to the right of the selected column.
    simrv::core::Machine insert_machine;
    simrv::tui::Tui insert_tui(insert_machine);
    simrv::tui::TuiTestAccess::set_cached_term_width(insert_tui, 160);
    auto const old_slots = simrv::tui::TuiTestAccess::workbench_slots(insert_tui);
    expect(old_slots.size() == 2, "default workbench starts with two columns");
    expect(insert_tui.add_workbench_column(), "adding a workbench column succeeds");
    auto const& inserted_slots = simrv::tui::TuiTestAccess::workbench_slots(insert_tui);
    expect(inserted_slots.size() == 3 && inserted_slots[0].page == simrv::tui::TuiRegPage::GPR &&
               inserted_slots[2].page == simrv::tui::TuiRegPage::CONSOLE,
           "new workbench column is inserted beside the selected column");

    simrv::core::Machine selected_insert_machine;
    simrv::tui::Tui selected_insert_tui(selected_insert_machine);
    simrv::tui::TuiTestAccess::set_cached_term_width(selected_insert_tui, 220);
    simrv::tui::TuiTestAccess::set_focused_slot(selected_insert_tui, 1);
    expect(selected_insert_tui.add_workbench_column(),
           "adding a workbench column beside a non-primary selection succeeds");
    auto const& selected_inserted_slots =
        simrv::tui::TuiTestAccess::workbench_slots(selected_insert_tui);
    expect(selected_inserted_slots.size() == 3 &&
               selected_inserted_slots[0].page == simrv::tui::TuiRegPage::GPR &&
               selected_inserted_slots[1].page == simrv::tui::TuiRegPage::CONSOLE &&
               selected_inserted_slots[2].page == simrv::tui::TuiRegPage::STACK,
           "new workbench column is inserted immediately right of the focused column");

    // A four-column header must use the same resolved widths as its body after a resize.
    simrv::core::Machine header_machine;
    simrv::tui::Tui header_tui(header_machine);
    simrv::tui::TuiTestAccess::init_panes(header_tui, header_machine);
    simrv::tui::TuiTestAccess::set_cached_term_width(header_tui, 240);
    simrv::tui::TuiTestAccess::set_cached_term_height(header_tui, 24);
    header_tui.apply_layout_preset(simrv::tui::LayoutPreset::GeneralDebug);
    auto const before_rule =
        strip_ansi(simrv::tui::TuiTestAccess::last_screen_lines(header_tui).at(2));
    auto const before_header =
        strip_ansi(simrv::tui::TuiTestAccess::last_screen_lines(header_tui).at(4));
    header_tui.adjust_inspector_width(2);
    auto const after_rule =
        strip_ansi(simrv::tui::TuiTestAccess::last_screen_lines(header_tui).at(2));
    auto const after_header =
        strip_ansi(simrv::tui::TuiTestAccess::last_screen_lines(header_tui).at(4));
    expect(before_rule != after_rule,
           "top header junctions are redrawn after focused-panel resize");
    expect(before_header != after_header,
           "four-column header is redrawn after focused-panel resize");
    expect(simrv::tui::get_display_width(after_header) == 240,
           "four-column header retains updated internal junctions");

    // A resize may consume more than one donor column, while every panel remains above its
    // minimum width.  This keeps [/] responsive even after an adjacent panel is exhausted.
    simrv::core::Machine cascade_machine;
    simrv::tui::Tui cascade_tui(cascade_machine);
    simrv::tui::TuiTestAccess::init_panes(cascade_tui, cascade_machine);
    simrv::tui::TuiTestAccess::set_cached_term_width(cascade_tui, 220);
    simrv::tui::TuiTestAccess::set_cached_term_height(cascade_tui, 50);
    cascade_tui.apply_layout_preset(simrv::tui::LayoutPreset::GeneralDebug);
    auto const cascade_before = simrv::tui::TuiTestAccess::column_widths(cascade_tui, 220);
    cascade_tui.adjust_inspector_width(6);
    auto const cascade_after = simrv::tui::TuiTestAccess::column_widths(cascade_tui, 220);
    expect(cascade_after.widths[0] == cascade_before.widths[0] + 6 &&
               cascade_after.widths[1] >= simrv::tui::framework::kMultiColumnUnitWidth &&
               cascade_after.widths[2] >= simrv::tui::framework::kMultiColumnUnitWidth,
           "four-column resize pushes through exhausted adjacent panel limits");
    cascade_tui.adjust_inspector_width(-6);
    auto const cascade_restored = simrv::tui::TuiTestAccess::column_widths(cascade_tui, 220);
    expect(cascade_restored.widths[0] == cascade_before.widths[0] &&
               cascade_restored.widths[1] == cascade_before.widths[1] &&
               cascade_restored.widths[2] == cascade_before.widths[2] &&
               cascade_restored.widths[3] == cascade_before.widths[3],
           "opposite resize restores the prior multi-panel distribution");

    // 6. Multi-column right border junction connects on horizontal rule
    const auto geom = simrv::tui::calculate_frame_geometry(120, 20, simrv::tui::TuiLayout::Split);
    simrv::tui::framework::ColumnWidths col_widths{.widths = {30, 30, 0, 0}, .count = 2};
    std::string header = simrv::tui::format_to_width("h0", 63) + "\n" +
                         simrv::tui::format_to_width("h1", 63) + "\n" +
                         simrv::tui::format_to_width("h2", 63);
    std::string footer = simrv::tui::format_to_width("f0", 63) + "\n" +
                         simrv::tui::format_to_width("f1", 63) + "\n" +
                         simrv::tui::format_to_width("f2", 63);
    const auto multi_lines = simrv::tui::compose_multi_frame_lines(
        geom, 63, col_widths, header, footer, [](size_t col_idx, int row, int) -> std::string {
            if (row == 0) return simrv::tui::make_repeated_string("─", 30);
            return "c" + std::to_string(col_idx);
        });
    std::string rule_row = strip_ansi(multi_lines.at(3));
    expect(rule_row.ends_with("╢"), "multi-column horizontal rule connects to right border with ╢");
}

void test_horizontal_scrolling() {
    simrv::core::Machine machine;
    simrv::tui::InspectorPane pane(machine);
    std::vector<std::string> trace = {
        "0000000080000000 addi x1, x0, 1 -- deliberately wide trace row with extended mnemonic and "
        "operands"};
    pane.set_trace_buffer(&trace);
    pane.set_visible_rows(30);

    // Inspector pages exceeding column width now consistently expose horizontal scrolling.
    pane.set_page(simrv::tui::TuiRegPage::PIPELINE);
    (void)pane.render_column_row(1, 40, 1, 3, true);
    expect(pane.supports_horizontal_scroll(), "PIPELINE on 40-col pane supports horizontal scroll");

    pane.set_page(simrv::tui::TuiRegPage::CACHE);
    (void)pane.render_column_row(1, 40, 1, 3, true);
    expect(pane.supports_horizontal_scroll(), "CACHE on 40-col pane supports horizontal scroll");

    pane.set_page(simrv::tui::TuiRegPage::STACK);
    (void)pane.render_column_row(1, 40, 1, 3, true);
    expect(pane.supports_horizontal_scroll(), "STACK on 40-col pane supports horizontal scroll");

    pane.set_page(simrv::tui::TuiRegPage::BUS);
    (void)pane.render_column_row(1, 40, 1, 3, true);
    expect(pane.supports_horizontal_scroll(), "BUS on 40-col pane supports horizontal scroll");

    pane.set_page(simrv::tui::TuiRegPage::TLB);
    (void)pane.render_column_row(1, 40, 1, 3, true);
    expect(pane.supports_horizontal_scroll(), "TLB on 40-col pane supports horizontal scroll");

    pane.set_page(simrv::tui::TuiRegPage::EXPLAIN);
    (void)pane.render_column_row(1, 40, 1, 3, true);
    expect(pane.supports_horizontal_scroll(), "EXPLAIN on 40-col pane supports horizontal scroll");

    pane.set_page(simrv::tui::TuiRegPage::TRACE);
    (void)pane.render_column_row(1, 40, 1, 3, true);
    expect(pane.supports_horizontal_scroll(), "wide TRACE rows support horizontal scroll");

    // Scroll offset starts at zero and is clamped by the trace viewport.
    expect(pane.get_horizontal_scroll_offset() == 0, "initial horizontal scroll is 0");
    pane.scroll_horizontal(8);
    expect(pane.get_horizontal_scroll_offset() == 8, "scroll_horizontal(8) advances offset");
    pane.scroll_horizontal(-8);
    expect(pane.get_horizontal_scroll_offset() == 0, "scroll_horizontal(-8) returns to start");

    // Directional scroll indicators appear on overflowing content
    pane.set_page(simrv::tui::TuiRegPage::PIPELINE);
    auto const row_unscrolled = pane.render_row(2, 40);
    expect(row_unscrolled.find("►") != std::string::npos ||
               row_unscrolled.find("▶") != std::string::npos,
           "overflowing row renders right directional indicator");

    pane.scroll_horizontal(8);
    auto const row_scrolled = pane.render_row(2, 40);
    expect(
        row_scrolled.find("◄") != std::string::npos || row_scrolled.find("◀") != std::string::npos,
        "scrolled row renders left directional indicator");

    // Switching page resets horizontal state
    pane.set_page(simrv::tui::TuiRegPage::GPR);
    expect(pane.get_horizontal_scroll_offset() == 0,
           "switching page resets horizontal scroll state");

    pane.set_page(simrv::tui::TuiRegPage::TRACE);
    pane.reset_horizontal_scroll();
    expect(pane.get_horizontal_scroll_offset() == 0, "reset_horizontal_scroll clears offset");

    // The log is a separate reusable viewport and remains independent from page scrolling.
    pane.set_log_lines({"log-00", "log-01", "log-02", "log-03", "log-04", "log-05", "log-06",
                        "log-07", "log-08", "log-09"});
    pane.set_page(simrv::tui::TuiRegPage::GPR);
    pane.set_paused(true);
    pane.set_visible_rows(20);
    expect(pane.get_log_scroll_offset() == 0, "log viewport starts at the newest entries");
    auto const newest_log_header = strip_ansi(pane.render_row(14, 80));
    expect(newest_log_header.find("▲ 5 above") != std::string::npos,
           "newest log view reports older entries above");
    pane.scroll_log(2);
    expect(pane.get_log_scroll_offset() == 2, "log viewport scrolls independently");
    auto const scrolled_log_header = strip_ansi(pane.render_row(14, 80));
    expect(scrolled_log_header.find("▲ 3 above") != std::string::npos &&
               scrolled_log_header.find("▼ 2 below") != std::string::npos,
           "scrolled log header reports visual directions correctly");
    pane.set_page(simrv::tui::TuiRegPage::PIPELINE);
    expect(pane.get_scroll_offset() == 0,
           "switching inspector pages does not inherit the log scroll position");
    pane.scroll_log(100);
    expect(pane.get_log_scroll_offset() == 5, "log viewport clamps to its visible capacity");
    pane.reset_log_scroll();
    expect(pane.get_log_scroll_offset() == 0, "reset_log_scroll returns to newest entries");

    // Standard pane widths (80 cols) fit content inside without horizontal scrolling
    pane.set_page(simrv::tui::TuiRegPage::PIPELINE);
    (void)pane.render_column_row(1, 80, 1, 3, true);
    expect(!pane.supports_horizontal_scroll(),
           "PIPELINE on 80-col pane fits inside without scroll");

    pane.set_page(simrv::tui::TuiRegPage::CACHE);
    (void)pane.render_column_row(1, 80, 1, 3, true);
    expect(!pane.supports_horizontal_scroll(), "CACHE on 80-col pane fits inside without scroll");

    pane.set_page(simrv::tui::TuiRegPage::STACK);
    (void)pane.render_column_row(1, 80, 1, 3, true);
    expect(!pane.supports_horizontal_scroll(), "STACK on 80-col pane fits inside without scroll");

    pane.set_page(simrv::tui::TuiRegPage::GPR);
    (void)pane.render_column_row(1, 80, 1, 3, true);
    expect(!pane.supports_horizontal_scroll(), "GPR on 80-col pane fits inside without scroll");
}

void test_stack_vertical_scrolling() {
    simrv::core::Machine machine;
    std::array<simrv::xlen::Byte, 4096> backing{};
    machine.set_ram_for_testing(backing.data(), backing.size());
    machine.memory().initialize_mmu();

    auto& cpu = machine.primary_hart();
    cpu.state().regs.write(simrv::xlen::RegId::Sp, 0x80000400);

    simrv::tui::InspectorPane pane(machine);
    pane.set_page(simrv::tui::TuiRegPage::STACK);
    pane.set_visible_rows(25);

    auto addr_unscrolled = pane.get_stack_addr_at_row(7);
    expect(addr_unscrolled.has_value() && *addr_unscrolled == 0x80000400,
           "unscrolled stack center row maps to base sp");

    pane.scroll(2);
    expect(pane.get_scroll_offset() == 2, "vertical scroll advances scroll offset");

    auto addr_scrolled = pane.get_stack_addr_at_row(7);
    expect(addr_scrolled.has_value() && *addr_scrolled == 0x80000400,
           "get_stack_addr_at_row with logical row 7 returns base sp without double offset");
}

void test_flight_recorder_merge_and_wraparound() {
    simrv::core::Machine machine;
    simrv::tui::Tui tui(machine);
    tui.record_flight_instruction(0x1000, simrv::isa::Opcode::OpImm, simrv::isa::OperationId::ADDI,
                                  1);
    tui.record_flight_instruction(0x1004, simrv::isa::Opcode::OpImm, simrv::isa::OperationId::ADDI,
                                  0);
    simrv::tui::TuiTestAccess::drain_trace(tui);
    const auto& rows = simrv::tui::TuiTestAccess::trace_rows(tui);
    expect(rows.size() == 2 && rows[0].contains("[H1]") && rows[1].contains("[H0]"),
           "flight recorder merges per-hart records by global retirement order");

    for (size_t i = 0; i < simrv::tui::Tui::kTraceBufferSize + 4; ++i) {
        tui.record_flight_instruction(0x2000 + i * 4, simrv::isa::Opcode::OpImm,
                                      simrv::isa::OperationId::ADDI, 1);
    }
    simrv::tui::TuiTestAccess::drain_trace(tui);
    expect(simrv::tui::TuiTestAccess::trace_rows(tui).size() == simrv::tui::Tui::kTraceBufferSize,
           "flight recorder retains its bounded most-recent history");
}

void test_bus_inspector_and_tilelink_channels() {
    using namespace simrv::memory;
    simrv::core::Machine machine;

    auto& bus = machine.memory().system_bus();
    bus.record_transaction(TileLinkChannel::A, "AcquireBlock", 1, 0, 0x80001000, "NtoT");
    bus.record_transaction(TileLinkChannel::B, "ProbeBlock", 1, 0, 0x80001000, "Hart 1 ToN");
    bus.record_transaction(TileLinkChannel::C, "ProbeAckData", 1, 0, 0x80001000, "TtoN");
    bus.record_transaction(TileLinkChannel::D, "GrantData", 1, 5, 0x80001000, "ToT");
    bus.record_transaction(TileLinkChannel::E, "GrantAck", 0, 5, 0, "");

    expect(bus.transaction_history().size() == 5, "bus recorded 5 transactions");

    simrv::tui::InspectorPane pane(machine);
    pane.set_page(simrv::tui::TuiRegPage::BUS);
    pane.set_visible_rows(45);

    const int width = 88;
    // The bus view has two tab rows followed by its logical rows. DMA contention now
    // occupies logical rows 18-19, before the transaction history.
    const std::string r16 = strip_ansi(pane.render_row(18, width));
    expect(r16.find("TileLink-C Channels") != std::string::npos,
           "row 18 contains TileLink-C channel section header");

    const std::string r17 = strip_ansi(pane.render_row(19, width));
    expect(r17.find("A(Req)") != std::string::npos && r17.find("B(Probe)") != std::string::npos,
           "row 19 contains channel status indicators");

    const std::string r18 = strip_ansi(pane.render_row(22, width));
    expect(r18.find("Recent Bus Transactions") != std::string::npos,
           "row 20 contains Recent Bus Transactions header");

    const std::string r19 = strip_ansi(pane.render_row(23, width));
    expect(r19.find("[E]") != std::string::npos && r19.find("GrantAck") != std::string::npos,
           "row 21 contains latest recorded transaction (E GrantAck)");

    // Test narrow column width formatting
    const int narrow_width = 45;
    const std::string narrow_r17 = strip_ansi(pane.render_row(19, narrow_width));
    expect(narrow_r17.find("A:") != std::string::npos && narrow_r17.find("B:") != std::string::npos,
           "narrow row 19 formats compact channel status");
}

void test_inspector_vector_csr_rows() {
    simrv::core::Machine machine;
    simrv::tui::InspectorPane pane(machine);
    pane.set_page(simrv::tui::TuiRegPage::VEC);
    pane.set_visible_rows(30);

    auto& cpu = machine.hart(0);
    cpu.state().vl = 16;
    cpu.state().vtype = 0;  // SEW=8, LMUL=1
    cpu.state().vstart = 2;
    cpu.state().vxrm = 1;
    cpu.state().vxsat = 0;

    const int width = 80;
    const std::string r16 = strip_ansi(pane.render_row(18, width));
    expect(r16.find("Vector Control & Status") != std::string::npos,
           "row 16 contains Vector Control & Status header");

    const std::string r17 = strip_ansi(pane.render_row(19, width));
    expect(r17.find("vl") != std::string::npos && r17.find("vtype") != std::string::npos,
           "row 17 displays vl and vtype");

    const std::string r18 = strip_ansi(pane.render_row(20, width));
    expect(r18.find("vstart") != std::string::npos && r18.find("vlenb") != std::string::npos,
           "row 18 displays vstart and vlenb");

    const std::string r19 = strip_ansi(pane.render_row(21, width));
    expect(r19.find("vxrm") != std::string::npos && r19.find("vxsat") != std::string::npos,
           "row 19 displays vxrm and vxsat");

    const std::string r20 = strip_ansi(pane.render_row(22, width));
    expect(r20.find("vta/vma") != std::string::npos && r20.find("vill") != std::string::npos,
           "row 20 displays vta/vma and vill");
}

void test_memory_inspector_and_custom_address() {
    simrv::core::Machine machine;
    std::array<simrv::xlen::Byte, 4096> backing{};
    machine.set_ram_for_testing(backing.data(), backing.size());
    machine.memory().initialize_mmu();

    const char text[] = "SimRV2026!";
    std::memcpy(backing.data(), text, sizeof(text));

    simrv::tui::InspectorPane pane(machine);
    pane.set_page(simrv::tui::TuiRegPage::STACK);
    pane.set_visible_rows(25);

    const int width = 80;

    // 1. Default: inspect_addr_ is 0, pane is in Stack Watch mode
    expect(!pane.is_custom_memory_inspect(), "default inspect mode is not custom memory");
    const std::string stack_title = strip_ansi(pane.render_row(2, width));
    expect(stack_title.find("Stack") != std::string::npos, "default title contains Stack");

    // 2. Set custom address
    const simrv::xlen::Address target_addr = 0x80000000;
    pane.set_inspect_addr(target_addr);
    expect(pane.is_custom_memory_inspect(), "custom memory inspect is active");
    const std::string mem_title = strip_ansi(pane.render_row(2, width));
    expect(mem_title.find("Memory Watch") != std::string::npos,
           "custom title contains Memory Watch");

    // Render center row (row_idx 9 corresponds to logical_row 7, word_offset 0)
    const std::string center_row = strip_ansi(pane.render_row(9, width));
    expect(center_row.find("target") != std::string::npos, "center row shows target marker");
    expect(center_row.find("SimR") != std::string::npos,
           "center row displays ASCII memory preview");

    // 3. Test AddressModal reset input
    std::string reset_cmd = "sp";
    std::string status_msg;
    auto cb = [&](const std::string& msg) { status_msg = msg; };
    bool const ok = simrv::tui::modals::AddressModal::submit(reset_cmd, machine, &pane, cb);
    expect(ok && !pane.is_custom_memory_inspect(), "submitting 'sp' resets custom memory inspect");
}

void test_responsive_labels_and_header() {
    simrv::core::Machine machine;
    simrv::tui::StatusBar bar(machine);
    bar.set_paused(true);

    // 1. On standard 80-column terminal, header line fits within inner_w (78)
    std::string const header_80 = strip_ansi(bar.render_row(0, 80));
    expect(header_80.find("STEP") != std::string::npos,
           "speed badge displays STEP when paused in compact 80-col mode");
    expect(header_80.find("PAUSED  [PAUSED]") == std::string::npos,
           "no duplicate PAUSED badges in header");

    // Check that each rendered line in header_80 is exactly 80 columns wide
    size_t line_start = 0;
    while (line_start < header_80.size()) {
        size_t next_newline = header_80.find('\n', line_start);
        if (next_newline == std::string::npos) next_newline = header_80.size();
        std::string line = header_80.substr(line_start, next_newline - line_start);
        if (!line.empty()) {
            expect(simrv::tui::get_display_width(line) == 80,
                   "header line exactly matches 80 columns without truncation");
        }
        line_start = next_newline + 1;
    }

    // 2. On wide 130-column terminal, full metrics and MAX speed format
    std::string const header_130 = strip_ansi(bar.render_row(0, 130));
    expect(header_130.find("MAX") != std::string::npos,
           "wide header displays MAX speed badge when paused");
    expect(header_130.find("Instructions") != std::string::npos,
           "wide header displays full Instructions label");

    // 3. Cache Set Occupancy Map dynamic wrapping
    simrv::tui::InspectorPane pane(machine);
    pane.set_page(simrv::tui::TuiRegPage::CACHE);
    pane.set_visible_rows(25);
    std::string const cache_row_narrow = strip_ansi(pane.render_row(14, 50));
    expect(simrv::tui::get_display_width(cache_row_narrow) == 50,
           "narrow cache occupancy row fits inside 50-col width");

    std::string const cache_row_wide = strip_ansi(pane.render_row(14, 88));
    expect(simrv::tui::get_display_width(cache_row_wide) == 88,
           "wide cache occupancy row fits inside 88-col width");

    // 4. Mode badge click area must be strictly on row 2 (not border row 1 or divider row 3)
    simrv::tui::Tui tui(machine);
    simrv::tui::TuiTestAccess::init_panes(tui, machine);
    const bool was_cycle = machine.runtime_profile.is_cycle_mode();
    expect(bar.is_pos_on_mode_badge(11, 80), "col 11 is on mode badge at 80 cols");

    // Row 1 (top border) click should not toggle execution mode
    tui.handle_mouse(11, 1, 0);
    expect(machine.runtime_profile.is_cycle_mode() == was_cycle,
           "clicking row 1 border does not toggle execution mode");

    // Row 3 (divider) click should not toggle execution mode
    tui.handle_mouse(11, 3, 0);
    expect(machine.runtime_profile.is_cycle_mode() == was_cycle,
           "clicking row 3 divider does not toggle execution mode");

    // Row 2 (actual header text) click must toggle execution mode
    tui.handle_mouse(11, 2, 0);
    expect(machine.runtime_profile.is_cycle_mode() != was_cycle,
           "clicking row 2 mode badge toggles execution mode");

    // 5. Tier 1 / Tier 2 Tab Labels and Responsive Pipeline Layout
    pane.set_page(simrv::tui::TuiRegPage::PIPELINE);
    std::string const tier1 = strip_ansi(pane.render_row(0, 60));
    expect(tier1.find("Registers") != std::string::npos, "tier 1 displays Registers");
    expect(tier1.find("Register Files") == std::string::npos,
           "tier 1 replaces verbose Register Files");

    std::string const tier2 = strip_ansi(pane.render_row(1, 60));
    expect(tier2.find("Stages") != std::string::npos,
           "tier 2 displays Stages subtab under Pipeline");

    // Dynamic column balancing & computation line on standard 56-col pane
    auto& ctx = machine.primary_hart().pipeline_context;
    ctx.opcode = simrv::isa::Opcode::Store;
    ctx.op_id = simrv::isa::OperationId::SD;
    ctx.rs1 = static_cast<simrv::xlen::RegId>(5);  // t0
    if constexpr (sizeof(simrv::xlen::Register) > 4) {
        ctx.rrs1 = 0xffffffffd60ef000ULL;
        ctx.rrs2 = 0xffffffd60ff00000ULL;
        ctx.mem_addr = 0xffffffffd60ef000ULL;
        ctx.mem_wdata = 0xffffffd60ff00000ULL;
    } else {
        ctx.rrs1 = 0xd60ef000U;
        ctx.rrs2 = 0x0ff00000U;
        ctx.mem_addr = 0xd60ef000U;
        ctx.mem_wdata = 0x0ff00000U;
    }
    ctx.imm = 0;

    std::string const comp_row = strip_ansi(pane.render_row(12, 56));
    expect(comp_row.find('+') == std::string::npos,
           "computation row in 56-col pane does not truncate with +");

    std::string const mem_row = strip_ansi(pane.render_row(15, 56));
    expect(mem_row.find("Data: Data:") == std::string::npos,
           "MEM stage row does not duplicate Data label");

    // 6. Pipeline horizontal scrolling on typical 56-col pane
    (void)pane.render_row(2, 56);
    expect(pane.supports_horizontal_scroll(),
           "PIPELINE on 56-col pane supports horizontal scrolling");

    // 7. Live scrolling updates immediately without waiting for a click
    tui.scroll(3);
    expect(tui.get_scroll_offset() == 3, "live scrolling updates scroll offset immediately");
    tui.reset_scroll();
    expect(tui.get_scroll_offset() == 0, "live reset_scroll clears offset immediately");
}

void test_tui_differential_rendering_and_throttling() {
    using namespace simrv::tui;
    using namespace simrv::core;

    Machine machine(MachineConfig{.execution = {.appmode = true}, .tui = {.enabled = true}});
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.primary_hart().reset();

    constexpr simrv::xlen::Instruction addi_inst = 0x00100093;  // addi x1, x0, 1
    std::memcpy(ram.data(), &addi_inst, sizeof(addi_inst));
    machine.primary_hart().state().pc = simrv::memory::kDramBaseAddress;

    Tui tui(machine);
    TuiTestAccess::init_panes(tui, machine);
    TuiTestAccess::set_cached_term_width(tui, 120);
    TuiTestAccess::set_cached_term_height(tui, 30);

    // Initial render must perform a full redraw to establish baseline screen geometry
    tui.render(true);
    const auto& stats = tui.render_stats();
    expect(stats.full_redraws == 1, "initial render performs exactly 1 full redraw");
    expect(stats.lines_drawn > 0, "initial render draws screen lines");
    expect(stats.differential_redraws == 0, "initial render has 0 differential redraws");

    const uint64_t initial_drawn = stats.lines_drawn;

    // Single step instruction
    machine.step_sync();
    TuiTestAccess::update_cache(tui);
    tui.render(true);

    // Differential rendering should now engage: only changed lines are drawn, unchanged lines are
    // skipped
    expect(stats.differential_redraws == 1,
           "stepping triggers a differential redraw rather than full redraw");
    expect(stats.full_redraws == 1, "no additional full redraw occurred during single step");
    expect(stats.lines_skipped > 0, "differential render skips unchanged screen lines");
    expect(stats.lines_drawn - initial_drawn < initial_drawn,
           "differential render draws fewer lines than a full screen");

    // Identical frame render without state change should be suppressed
    const uint64_t drawn_before_suppress = stats.lines_drawn;
    tui.render(false);
    expect(stats.suppressed_frames > 0, "identical frame with no changes is suppressed");
    expect(stats.lines_drawn == drawn_before_suppress, "suppressed frame draws 0 lines");

    // High-speed execution burst throttling: when UI thread is running, rapid renders (<
    // min_interval) throttle
    TuiTestAccess::set_ui_running(tui, true);
    TuiTestAccess::set_last_draw_time(tui, std::chrono::steady_clock::now());
    // Immediate render attempt within interval should be throttled
    tui.render(true);
    expect(stats.throttled_frames == 1,
           "render within frame interval throttles to minimize ANSI generation");

    // When interval elapses, next render should proceed
    TuiTestAccess::set_last_draw_time(
        tui, std::chrono::steady_clock::now() - std::chrono::milliseconds(50));
    tui.render(true);
    expect(stats.throttled_frames == 1, "render after interval expiration is not throttled");

    TuiTestAccess::set_ui_running(tui, false);

    // Sub-view visibility throttling during continuous execution
    // Default slots: slot 0 = GPR, slot 1 = CONSOLE
    expect(!tui.is_page_visible(TuiRegPage::TRACE),
           "TRACE is not visible in default workbench layout");
    expect(!tui.is_trace_active(), "trace capture is inactive when TRACE tab is not visible");
    expect(!tui.is_page_visible(TuiRegPage::PIPELINE),
           "PIPELINE is not visible in default workbench layout");

    // Setting slot 1 to TRACE activates trace tracking
    tui.set_workbench_slot_page(1, TuiRegPage::TRACE);
    expect(tui.is_page_visible(TuiRegPage::TRACE), "TRACE is visible after slot page assignment");
    expect(tui.is_trace_active(), "trace capture becomes active when TRACE tab is visible");

    // Setting slot 1 to PIPELINE updates detail visibility
    tui.set_workbench_slot_page(1, TuiRegPage::PIPELINE);
    expect(tui.is_page_visible(TuiRegPage::PIPELINE),
           "PIPELINE is visible after slot page assignment");
    expect(!tui.is_trace_active(), "trace capture becomes inactive when TRACE is replaced");
}

void test_multi_column_workbench_tools_and_swapping() {
    // 1. ToolPickerModal catalog and accelerator lookup
    const auto& tools = simrv::tui::modals::ToolPickerModal::all_tools();
    expect(tools.size() == 15, "tool picker provides 15 selectable workbench tools");
    expect(simrv::tui::modals::ToolPickerModal::find_by_accelerator('o') ==
               simrv::tui::TuiRegPage::LOG,
           "accelerator 'o' maps to LOG");
    expect(simrv::tui::modals::ToolPickerModal::find_by_accelerator('g') ==
               simrv::tui::TuiRegPage::GPR,
           "accelerator 'g' maps to GPR");
    expect(simrv::tui::modals::ToolPickerModal::find_by_accelerator('f') ==
               simrv::tui::TuiRegPage::FPR,
           "accelerator 'f' maps to FPR");
    expect(simrv::tui::modals::ToolPickerModal::find_by_accelerator('v') ==
               simrv::tui::TuiRegPage::VEC,
           "accelerator 'v' maps to VEC");
    expect(simrv::tui::modals::ToolPickerModal::find_by_accelerator('s') ==
               simrv::tui::TuiRegPage::STACK,
           "accelerator 's' maps to STACK");
    expect(simrv::tui::modals::ToolPickerModal::find_by_accelerator('c') ==
               simrv::tui::TuiRegPage::CACHE,
           "accelerator 'c' maps to CACHE");
    expect(simrv::tui::modals::ToolPickerModal::find_by_accelerator('m') ==
               simrv::tui::TuiRegPage::TLB,
           "accelerator 'm' maps to TLB");
    expect(simrv::tui::modals::ToolPickerModal::find_by_accelerator('b') ==
               simrv::tui::TuiRegPage::BUS,
           "accelerator 'b' maps to BUS");
    expect(simrv::tui::modals::ToolPickerModal::find_by_accelerator('p') ==
               simrv::tui::TuiRegPage::PIPELINE,
           "accelerator 'p' maps to PIPELINE");
    expect(simrv::tui::modals::ToolPickerModal::find_by_accelerator('d') ==
               simrv::tui::TuiRegPage::BPRED,
           "accelerator 'd' maps to BPRED");
    expect(simrv::tui::modals::ToolPickerModal::find_by_accelerator('z') ==
               simrv::tui::TuiRegPage::HAZARD,
           "accelerator 'z' maps to HAZARD");
    expect(simrv::tui::modals::ToolPickerModal::find_by_accelerator('x') ==
               simrv::tui::TuiRegPage::TRACE,
           "accelerator 'x' maps to TRACE");
    expect(simrv::tui::modals::ToolPickerModal::find_by_accelerator('e') ==
               simrv::tui::TuiRegPage::EXPLAIN,
           "accelerator 'e' maps to EXPLAIN");
    expect(simrv::tui::modals::ToolPickerModal::find_by_accelerator('t') ==
               simrv::tui::TuiRegPage::CONSOLE,
           "accelerator 't' maps to CONSOLE");
    expect(simrv::tui::modals::ToolPickerModal::find_by_accelerator('y') ==
               simrv::tui::TuiRegPage::DISPLAY,
           "accelerator 'y' maps to DISPLAY");
    expect(!simrv::tui::modals::ToolPickerModal::find_by_accelerator('l').has_value(),
           "accelerator 'l' is no longer reserved for Live Overview");
    expect(!simrv::tui::modals::ToolPickerModal::find_by_accelerator('q').has_value(),
           "accelerator 'q' is not bound to a tool (reserved for exit/close)");

    // 2. Tui workbench slot swapping and movement
    simrv::core::Machine machine;
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleObservable;
    simrv::tui::Tui tui(machine);
    simrv::tui::TuiTestAccess::init_panes(tui, machine);
    simrv::tui::TuiTestAccess::set_cached_term_width(tui, 120);
    simrv::tui::TuiTestAccess::set_cached_term_height(tui, 30);
    tui.apply_layout_preset(simrv::tui::LayoutPreset::GeneralDebug);

    const auto& slots = tui.get_workbench_slots();
    expect(slots.size() >= 2, "workbench has at least 2 slots in debug preset");
    expect(slots[0].page == simrv::tui::TuiRegPage::GPR, "slot 0 is GPR");
    expect(slots[1].page == simrv::tui::TuiRegPage::CONSOLE, "slot 1 is CONSOLE");

    // Swap slots: slot 0 becomes CONSOLE, slot 1 becomes GPR
    tui.swap_workbench_slots(0, 1);
    expect(slots[0].page == simrv::tui::TuiRegPage::CONSOLE, "slot 0 is now CONSOLE after swap");
    expect(slots[1].page == simrv::tui::TuiRegPage::GPR, "slot 1 is now GPR after swap");

    // Move focused column right/left
    expect(tui.focused_slot() == 0, "focus starts at slot 0");
    tui.move_focused_column_right();
    expect(tui.focused_slot() == 1, "focused slot moves to 1");
    expect(slots[0].page == simrv::tui::TuiRegPage::GPR, "GPR is back at slot 0");
    expect(slots[1].page == simrv::tui::TuiRegPage::CONSOLE, "CONSOLE is back at slot 1");

    // Boundary check: cannot move right past the end
    tui.move_focused_column_right();
    expect(tui.focused_slot() == 1, "focused slot stays at 1 at right boundary");

    // Move left back to 0
    tui.move_focused_column_left();
    expect(tui.focused_slot() == 0, "focused slot moved back to 0");
    expect(slots[0].page == simrv::tui::TuiRegPage::CONSOLE, "CONSOLE moved back to slot 0");

    // 3. Tool picker modal invocation and submission
    tui.open_tool_picker(0);
    expect(tui.is_modal_active(), "tool picker modal is active");
    expect(tui.get_active_modal() == simrv::tui::ModalType::ToolPicker,
           "active modal is ToolPicker");

    auto& modal = simrv::tui::TuiTestAccess::modal(tui);
    expect(modal.get_tool_picker_slot() == 0, "modal targets slot 0");
    modal.set_tool_picker_cursor(4);  // index 4 is CACHE
    expect(modal.get_selected_tool_page() == simrv::tui::TuiRegPage::CACHE,
           "cursor at index 4 selects CACHE");
    tui.submit_modal();
    expect(!tui.is_modal_active(), "modal is closed after submit");
    expect(slots[0].page == simrv::tui::TuiRegPage::CACHE, "slot 0 page updated to CACHE");

    // The display accelerator must assign the selected column and close the picker in one path.
    tui.open_tool_picker(1);
    expect(simrv::tui::TuiTestAccess::handle_modal_key(tui, static_cast<uint8_t>('y'),
                                                       static_cast<simrv::tui::TuiKey>('y')),
           "display accelerator is consumed by tool picker");
    expect(!tui.is_modal_active(), "display accelerator closes tool picker");
    expect(slots[1].page == simrv::tui::TuiRegPage::DISPLAY,
           "display accelerator assigns DISPLAY to target column");

    // 4. ToolPickerModal row mapping and clickable selection
    expect(simrv::tui::modals::ToolPickerModal::tool_index_at_row(3) == 0,
           "row 3 maps to tool 0 (GPR)");
    expect(simrv::tui::modals::ToolPickerModal::tool_index_at_row(7) == 3,
           "row 7 maps to tool 3 (STACK)");
    expect(simrv::tui::modals::ToolPickerModal::tool_index_at_row(12) == 7,
           "row 12 maps to tool 7 (PIPELINE)");
    expect(simrv::tui::modals::ToolPickerModal::tool_index_at_row(16) == 10,
           "row 16 maps to tool 10 (TRACE)");
    expect(simrv::tui::modals::ToolPickerModal::tool_index_at_row(18) == 12,
           "row 18 maps to tool 12 (CONSOLE)");
    expect(simrv::tui::modals::ToolPickerModal::tool_index_at_row(19) == 13,
           "row 19 maps to tool 13 (DISPLAY)");
    expect(simrv::tui::modals::ToolPickerModal::tool_index_at_row(20) == 14,
           "row 20 maps to tool 14 (LOG)");
    expect(!simrv::tui::modals::ToolPickerModal::tool_index_at_row(2).has_value(),
           "row 2 is category banner, maps to no tool");
    expect(simrv::tui::modals::ToolPickerModal::row_for_tool_index(0) == 3,
           "tool 0 (GPR) is at row 3");
    expect(simrv::tui::modals::ToolPickerModal::row_for_tool_index(7) == 12,
           "tool 7 (PIPELINE) is at row 12");
    expect(simrv::tui::modals::ToolPickerModal::row_for_tool_index(10) == 16,
           "tool 10 (TRACE) is at row 16");

    // Interactive clicking inside ToolPickerModal
    tui.open_tool_picker(0);
    std::vector<std::string> overlay_lines(30, std::string(120, ' '));
    modal.render_overlay(overlay_lines, 120, 30);
    bool found_modal_control = false;
    for (int y = 1; y <= 30; ++y) {
        for (int x = 1; x <= 120; ++x) {
            found_modal_control = found_modal_control || modal.is_control_at(x, y);
        }
    }
    expect(found_modal_control, "modal control hitboxes are detectable for pointer styling");
    // TRACE (tool index 10) is at content row 16; terminal Y: start_y (3) + 1 (border) + 16
    // (content) + 1 (1-indexed) = 20.
    auto click_res = modal.handle_click(60, 20, 120, 30);
    expect(click_res == simrv::tui::TuiModal::ModalClickResult::Submit,
           "clicking tool row submits selection");
    expect(modal.get_tool_picker_cursor() == 10, "cursor updated to TRACE (index 10)");
    tui.submit_modal();
    expect(slots[0].page == simrv::tui::TuiRegPage::TRACE, "slot 0 assigned to TRACE via click");

    // 5. Column header click policy: first click focuses, second click opens menu
    // Slot 0 is TRACE, slot 1 is CONSOLE. Focus is currently at slot 0.
    expect(tui.focused_slot() == 0, "focus starts at slot 0");

    // First click on column 1 header (x=80, y=4):
    tui.handle_mouse(80, 4, 0);
    expect(tui.focused_slot() == 1, "first click on column 1 header focuses column 1");
    expect(!tui.is_modal_active(), "first click on unfocused column does not open tool picker");

    // Second click on column 1 header (which is now focused):
    tui.handle_mouse(80, 4, 0);
    expect(tui.is_modal_active(), "second click on already focused column opens tool picker");
    expect(modal.get_tool_picker_slot() == 1, "modal targets column 1");
    tui.close_modal();

    // 6. Pipeline menu available in functional/IA mode
    tui.set_workbench_slot_page(0, simrv::tui::TuiRegPage::PIPELINE);
    tui.handle_mouse(10, 6, 0);  // click in body of slot 0 to focus it
    expect(tui.focused_slot() == 0, "slot 0 is focused");

    auto* inspector = simrv::tui::TuiTestAccess::inspector(tui);
    expect(inspector != nullptr, "inspector pane is initialized");
    expect(inspector->has_tool_menu(simrv::tui::TuiRegPage::PIPELINE),
           "pipeline panel in functional mode has tool menu");

    std::string hdr = inspector->render_column_header(
        0, "Pipeline", true, 46, "", inspector->has_tool_menu(simrv::tui::TuiRegPage::PIPELINE));
    expect(hdr.find("▼") != std::string::npos,
           "dropdown affordance is present when has_menu is true");
    expect(hdr.find("[Ctrl-W]") != std::string::npos, "tool hint is present when has_menu is true");

    // Click on focused pipeline header opens tool picker:
    tui.handle_mouse(10, 4, 0);
    expect(tui.is_modal_active(), "clicking header of pipeline panel opens tool menu");
    tui.close_modal();

    // 7. Pipeline panel width check (fits within 46 columns without false horizontal scroll
    // overflow)
    std::string pipe_row = inspector->render_column_row(1, 46, 0, 3, true, true);
    expect(pipe_row.find("►") == std::string::npos,
           "pipeline row fits in 46 columns without horizontal scroll overflow marker");
}

void test_multi_column_panel_management_and_modal_usability() {
    simrv::core::Machine machine;
    simrv::tui::Tui tui(machine);
    simrv::tui::TuiTestAccess::init_panes(tui, machine);
    tui.close_modal();

    // 1. Top status bar rendering: verify PANE 1/2 badge is removed
    auto* status_bar = simrv::tui::TuiTestAccess::status_bar(tui);
    expect(status_bar != nullptr, "status bar is initialized");
    std::string header_row = status_bar->render_row(0, 120);
    expect(header_row.find("PANE 1/") == std::string::npos,
           "status bar row 0 does not contain PANE 1/ badge");
    expect(header_row.find("PANE 2/") == std::string::npos,
           "status bar row 0 does not contain PANE 2/ badge");

    // Click on SimRV title should map to LoadBinary
    auto act = status_bar->get_header_action_at_col(2, 120);
    expect(act.action == simrv::tui::HeaderAction::LoadBinary,
           "clicking SimRV title triggers LoadBinary action");

    // Status bar footer reorganization
    std::string footer_screen = status_bar->render_row(1, 120);  // row_idx 1 is Footer block
    expect(strip_ansi(footer_screen).find("Ctrl-W Tool") != std::string::npos,
           "footer contains Ctrl-W Tool");
    expect(strip_ansi(footer_screen).find("Tab") != std::string::npos, "footer contains Tab");
    expect(strip_ansi(footer_screen).find("Ctrl-N Add Panel") != std::string::npos,
           "footer contains Ctrl-N Add Panel");
    expect(strip_ansi(footer_screen).find("Ctrl-X Close Panel") != std::string::npos,
           "footer contains Ctrl-X Close Panel");
    expect(strip_ansi(footer_screen).find("Move L") != std::string::npos, "footer contains Move L");
    expect(strip_ansi(footer_screen).find("Move R") != std::string::npos, "footer contains Move R");
    expect(strip_ansi(footer_screen).find("Ctrl-L") == std::string::npos,
           "footer does not contain Ctrl-L");

    // 2. Uniform 2-column header and close button [×]
    simrv::tui::TuiTestAccess::set_cached_term_width(tui, 120);
    simrv::tui::TuiTestAccess::set_cached_term_height(tui, 30);
    tui.render(true);

    auto const& slots = simrv::tui::TuiTestAccess::workbench_slots(tui);
    expect(slots.size() == 2, "initial layout has 2 slots");
    auto* inspector = simrv::tui::TuiTestAccess::inspector(tui);
    std::string col0_hdr = inspector->render_column_header(0, "GPR", true, 58, "", false, true);
    expect(strip_ansi(col0_hdr).find("[×]") != std::string::npos ||
               strip_ansi(col0_hdr).find("[x]") != std::string::npos,
           "column 0 header has [×] close button when can_close is true");
    std::string col1_hdr =
        inspector->render_column_header(1, "Console", false, 58, "", false, true);
    expect(strip_ansi(col1_hdr).find("[×]") != std::string::npos ||
               strip_ansi(col1_hdr).find("[x]") != std::string::npos,
           "column 1 header has [×] close button when can_close is true");

    // 3. Adding columns with add_workbench_column() and size warning toast
    // Attempting to add 3rd column when width is narrow (100 < 144)
    simrv::tui::TuiTestAccess::set_cached_term_width(tui, 100);
    bool added = tui.add_workbench_column();
    expect(!added, "cannot add 3rd column when width is less than 144");
    expect(simrv::tui::TuiTestAccess::status_override(tui).find("Terminal too narrow") !=
               std::string::npos,
           "status override reports terminal too narrow for 3 columns");

    // Now expand terminal to 160 cols (>= 144) and add column
    simrv::tui::TuiTestAccess::set_cached_term_width(tui, 160);
    added = tui.add_workbench_column();
    expect(added, "successfully added 3rd column at 160 width");
    expect(slots.size() == 3, "slot count is now 3");

    // Attempting to add 4th column when width is narrow (180 < 192)
    simrv::tui::TuiTestAccess::set_cached_term_width(tui, 180);
    added = tui.add_workbench_column();
    expect(!added, "cannot add 4th column when width is less than 192");
    expect(simrv::tui::TuiTestAccess::status_override(tui).find("Terminal too narrow") !=
               std::string::npos,
           "status override reports terminal too narrow for 4 columns");

    // Expand terminal to 220 cols (>= 192) and add 4th column
    simrv::tui::TuiTestAccess::set_cached_term_width(tui, 220);
    added = tui.add_workbench_column();
    expect(added, "successfully added 4th column at 220 width");
    expect(slots.size() == 4, "slot count is now 4");

    // Attempting to add 5th column exceeds maximum
    added = tui.add_workbench_column();
    expect(!added, "cannot add 5th column beyond maximum 4 columns");
    expect(simrv::tui::TuiTestAccess::status_override(tui).find("Maximum columns reached") !=
               std::string::npos,
           "status override reports maximum columns reached");

    // 4. Closing columns: close_focused_column() and [×] click
    // Close 4th column
    bool closed = tui.close_focused_column();
    expect(closed, "successfully closed column 4");
    expect(slots.size() == 3, "slot count is now 3");

    // Close column 2 via close_column
    closed = tui.close_column(2);
    expect(closed, "successfully closed column 2");
    expect(slots.size() == 2, "slot count is now 2");

    // Close column 1 via close_column
    closed = tui.close_column(1);
    expect(closed, "successfully closed column 1");
    expect(slots.size() == 1, "slot count is now 1");

    // Attempting to close the last surviving column must fail with warning
    closed = tui.close_column(0);
    expect(!closed, "cannot close the last remaining column");
    expect(simrv::tui::TuiTestAccess::status_override(tui).find("Cannot close the last") !=
               std::string::npos,
           "status override reports cannot close last remaining column");

    // Restore to 2 columns for mouse testing
    simrv::tui::TuiTestAccess::set_cached_term_width(tui, 120);
    tui.add_workbench_column();
    // 5. Two-panel click areas: row 4 column header, row 5 content, selection start
    expect(slots.size() == 2, "restored to 2 columns");
    tui.set_workbench_slot_page(0, simrv::tui::TuiRegPage::GPR);
    tui.set_workbench_slot_page(1, simrv::tui::TuiRegPage::CONSOLE);
    simrv::tui::TuiTestAccess::set_focused_slot(tui, 1);
    expect(tui.focused_slot() == 1, "column 1 is initially focused");

    // The separator belongs to the preceding panel; the next panel begins after it.
    simrv::tui::TuiTestAccess::consume_control_seq(tui, "\033[<0;54;4M");
    expect(tui.focused_slot() == 1, "divider click does not activate either workbench column");
    simrv::tui::TuiTestAccess::set_focused_slot(tui, 0);
    simrv::tui::TuiTestAccess::consume_control_seq(tui, "\033[<0;55;4M");
    expect(tui.focused_slot() == 1, "first cell after divider focuses the second column");
    simrv::tui::TuiTestAccess::set_focused_slot(tui, 1);

    // Click on column 0 row 4:
    // First click should focus column 0 without opening modal or cycling tabs
    tui.handle_mouse(10, 4, 0);
    expect(tui.focused_slot() == 0, "click on column 0 row 4 focuses column 0");
    expect(!tui.is_modal_active(), "first click on column 0 does not open modal");
    expect(slots[0].page == simrv::tui::TuiRegPage::GPR, "slot 0 remains GPR (not cycled as tab)");

    // Second click on column 0 row 4 (already focused): should open tool picker
    tui.handle_mouse(10, 4, 0);
    expect(tui.is_modal_active(), "second click on column 0 row 4 opens tool picker");
    expect(simrv::tui::TuiTestAccess::modal(tui).get_type() == simrv::tui::ModalType::ToolPicker,
           "opened tool picker modal");
    tui.close_modal();

    // Click on column 0 row 5 (content row 0): start selection drag
    simrv::tui::TuiTestAccess::set_cached_term_height(tui, 24);
    simrv::tui::TuiTestAccess::consume_control_seq(tui, "\033[<0;10;5M");
    const auto& sel = simrv::tui::TuiTestAccess::selection(tui);
    expect(sel.content_start_y == 5, "selection content_start_y is 5 in 2-panel mode");
    expect(sel.content_end_y == 14,
           "main inspector selection stops before the separate Log subpanel");
    simrv::tui::TuiTestAccess::consume_control_seq(tui, "\033[<32;200;30M");
    expect(sel.end_y == sel.content_end_y && sel.end_x == sel.pane_width - 1,
           "selection drag clamps to the originating subpanel bounds");
    expect(slots[0].page == simrv::tui::TuiRegPage::GPR, "row 5 click does not alter GPR page");
    tui.clear_selection();
    if (tui.is_modal_active()) tui.close_modal();
    simrv::tui::TuiTestAccess::consume_control_seq(tui, "\033[<0;10;18M");
    const auto& log_sel = simrv::tui::TuiTestAccess::selection(tui);
    expect(log_sel.content_start_y == 15 && log_sel.content_end_y == 20,
           "Log selection is constrained to the Log subpanel");
    tui.clear_selection();

    // Click on column 0 row 5 content: verify row 5 does not alter GPR page
    tui.handle_mouse(10, 5, 0);
    expect(slots[0].page == simrv::tui::TuiRegPage::GPR,
           "handle_mouse row 5 content preserves page");

    // Register-pane wheel events must advance the page viewport independently of the log
    // viewport.  Render first so the pane has the same visible-row geometry as the live TUI.
    if (tui.is_modal_active()) tui.close_modal();
    tui.render(true);
    auto* register_pane = simrv::tui::TuiTestAccess::inspector(tui);
    const int register_offset_before = register_pane->get_scroll_offset();
    tui.handle_mouse(10, 6, 65);  // wheel down over the GPR body, above the Log subpanel
    expect(register_pane->get_scroll_offset() > register_offset_before,
           "mouse wheel scrolls the register subpanel");
    expect(register_pane->get_log_scroll_offset() == 0,
           "register wheel does not move the independent Log subpanel");
    const auto& scrolled_screen = simrv::tui::TuiTestAccess::last_screen_lines(tui);
    size_t log_header_row = scrolled_screen.size();
    for (size_t i = 0; i < scrolled_screen.size(); ++i) {
        if (strip_ansi(scrolled_screen.at(i)).find("Log · ") != std::string::npos) {
            log_header_row = i;
            break;
        }
    }
    expect(log_header_row < scrolled_screen.size(),
           "rendered register pane contains a Log boundary");
    bool marker_in_log = false;
    for (size_t i = log_header_row; i < scrolled_screen.size(); ++i) {
        if (strip_ansi(scrolled_screen.at(i)).find("more lines below - scroll down") !=
            std::string::npos) {
            marker_in_log = true;
            break;
        }
    }
    expect(!marker_in_log, "register scroll indicators stay above the Log subpanel");

    // In a four-column layout the first register pane can be narrow while the last inspector
    // pane is wide.  The wheel must still use the focused pane's geometry, not the last-rendered
    // pane's width (which would incorrectly clamp this scroll to zero).
    simrv::core::Machine geometry_machine;
    simrv::tui::Tui geometry_tui(geometry_machine);
    simrv::tui::TuiTestAccess::init_panes(geometry_tui, geometry_machine);
    simrv::tui::TuiTestAccess::set_cached_term_width(geometry_tui, 220);
    simrv::tui::TuiTestAccess::set_cached_term_height(geometry_tui, 45);
    geometry_tui.apply_layout_preset(simrv::tui::LayoutPreset::GeneralDebug);
    geometry_tui.set_workbench_slot_page(3, simrv::tui::TuiRegPage::STACK);
    geometry_tui.render(true);
    auto* geometry_pane = simrv::tui::TuiTestAccess::inspector(geometry_tui);
    simrv::tui::TuiTestAccess::set_focused_slot(geometry_tui, 0);
    geometry_tui.handle_mouse(10, 6, 65);
    geometry_pane->set_page(simrv::tui::TuiRegPage::GPR);
    expect(geometry_pane->get_scroll_offset() > 0,
           "register wheel uses the focused pane width in a heterogeneous layout");

    // Click [×] on column 1 header (x ~ 118, y = 4)
    tui.handle_mouse(118, 4, 0);
    expect(slots.size() == 1, "clicking [×] button on column 1 closed column 1");

    // 5. Modal mouse wheel isolation and HelpModal scrolling
    tui.open_modal(simrv::tui::ModalType::Help);
    expect(tui.is_modal_active(), "help modal is active");
    auto& modal = simrv::tui::TuiTestAccess::modal(tui);

    // Initial help render at 80x24: verify bottom border shows overflow indicator
    std::vector<std::string> overlay_lines(24, std::string(80, ' '));
    modal.render_overlay(overlay_lines, 80, 24);
    std::string rendered_overlay;
    for (const auto& l : overlay_lines) rendered_overlay += l + '\n';
    expect(rendered_overlay.find("▼") != std::string::npos,
           "bottom border displays overflow indicator when help exceeds viewport");
    const auto below_badge = rendered_overlay.find("more below");
    const auto below_row_end = rendered_overlay.find('\n', below_badge);
    const auto below_bg = rendered_overlay.find(simrv::tui::g_theme_modal_bg, below_badge);
    expect(below_badge != std::string::npos && below_bg != std::string::npos &&
               below_bg < below_row_end,
           "modal background is restored after the bottom overflow indicator");

    // Test wheel down on modal
    modal.handle_wheel(1);
    std::vector<std::string> scrolled_lines(24, std::string(80, ' '));
    modal.render_overlay(scrolled_lines, 80, 24);
    std::string scrolled_overlay;
    for (const auto& l : scrolled_lines) scrolled_overlay += l + '\n';
    expect(scrolled_overlay.find("▲ more") != std::string::npos,
           "top border displays ▲ more indicator after scrolling down");
    const auto above_badge = scrolled_overlay.find("▲ more");
    const auto above_row_end = scrolled_overlay.find('\n', above_badge);
    const auto above_bg = scrolled_overlay.find(simrv::tui::g_theme_modal_bg, above_badge);
    expect(above_badge != std::string::npos && above_bg != std::string::npos &&
               above_bg < above_row_end,
           "modal background is restored after the top overflow indicator");

    // Test keyboard scrolling in help modal: j / k
    simrv::tui::TuiTestAccess::handle_modal_key(tui, 'j', simrv::tui::TuiKey::j);
    simrv::tui::TuiTestAccess::handle_modal_key(tui, 'k', simrv::tui::TuiKey::k);

    // Test arrow key scrolling
    simrv::tui::TuiTestAccess::arrow(tui, "\033[B");   // Down
    simrv::tui::TuiTestAccess::arrow(tui, "\033[A");   // Up
    simrv::tui::TuiTestAccess::arrow(tui, "\033[6~");  // PgDn
    simrv::tui::TuiTestAccess::arrow(tui, "\033[5~");  // PgUp

    // Close help modal with 'q'
    simrv::tui::TuiTestAccess::handle_modal_key(tui, 'q', simrv::tui::TuiKey::q);
    expect(!tui.is_modal_active(), "help modal closed with 'q'");

    // 6. Shift-Tab and Ctrl-Left/Right directional column navigation
    if (tui.is_modal_active()) tui.close_modal();
    tui.add_workbench_column();
    expect(tui.get_workbench_slots().size() == 2, "workbench has 2 columns");
    simrv::tui::TuiTestAccess::set_focused_slot(tui, 0);
    expect(tui.focused_slot() == 0, "focus starts at slot 0");

    // Ctrl-Right (\033[1;5C): focus next slot
    simrv::tui::TuiTestAccess::consume_control_seq(tui, "\033[1;5C");
    expect(tui.focused_slot() == 1, "Ctrl-Right moved focus to slot 1");

    // Shift-Tab (\033[Z): focus prev slot (reverse direction)
    simrv::tui::TuiTestAccess::consume_control_seq(tui, "\033[Z");
    expect(tui.focused_slot() == 0, "Shift-Tab moved focus back to slot 0");

    // Ctrl-Left (\033[1;5D): focus prev slot with wraparound
    simrv::tui::TuiTestAccess::consume_control_seq(tui, "\033[1;5D");
    expect(tui.focused_slot() == 1, "Ctrl-Left wrapped focus to slot 1");

    // Reverse cycling: r vs R (registers), l vs L (tools)
    tui.set_workbench_slot_page(1, simrv::tui::TuiRegPage::GPR);
    tui.cycle_reg_page(true);
    expect(tui.get_workbench_slots()[1].page != simrv::tui::TuiRegPage::GPR,
           "reverse cycle_reg_page moved away from GPR");
    tui.cycle_reg_page(false);
    expect(tui.get_workbench_slots()[1].page == simrv::tui::TuiRegPage::GPR,
           "forward cycle_reg_page moved back to GPR");

    tui.cycle_tool_page(true);
    expect(tui.get_workbench_slots()[1].page == simrv::tui::TuiRegPage::CONSOLE ||
               tui.get_workbench_slots()[1].page == simrv::tui::TuiRegPage::EXPLAIN,
           "reverse cycle_tool_page moved to Tools group");

    // Shift-Tab inside ToolPickerModal
    tui.open_tool_picker(0);
    expect(tui.is_modal_active(), "tool picker modal is active");
    expect(modal.get_tool_picker_slot() == 0, "tool picker slot is 0");
    simrv::tui::TuiTestAccess::consume_control_seq(tui, "\033[Z");
    expect(modal.get_tool_picker_slot() == 1,
           "Shift-Tab in ToolPicker cycled slot in reverse to 1");
    simrv::tui::TuiTestAccess::handle_modal_key(tui, '\t', simrv::tui::TuiKey::Tab);
    expect(modal.get_tool_picker_slot() == 0, "Tab in ToolPicker cycled slot forward to 0");
    tui.close_modal();

    // Shift-Tab inside SettingsModal
    tui.open_modal(simrv::tui::ModalType::Settings);
    expect(tui.is_modal_active(), "settings modal is active");
    expect(modal.get_settings_draft().active_tab == 0, "settings starts at tab 0");
    simrv::tui::TuiTestAccess::consume_control_seq(tui, "\033[Z");
    expect(modal.get_settings_draft().active_tab == 2,
           "Shift-Tab in Settings cycled tab in reverse to 2");
    simrv::tui::TuiTestAccess::handle_modal_key(tui, '\t', simrv::tui::TuiKey::Tab);
    expect(modal.get_settings_draft().active_tab == 0, "Tab in Settings cycled tab forward to 0");
    tui.close_modal();

    // Verify Ctrl-L is removed from the key registry.
    const auto layout_before = simrv::tui::TuiTestAccess::layout(tui);
    expect(!simrv::tui::Keybindings::match(simrv::tui::TuiKey::CtrlL).has_value(),
           "Ctrl-L is not registered as a navigation command");
    expect(simrv::tui::TuiTestAccess::layout(tui) == layout_before, "Ctrl-L did not cycle layout");
}

void test_sixel_display_modal_clearing() {
    using namespace simrv::tui;
    using namespace simrv::core;

    Machine machine(MachineConfig{.execution = {.appmode = true}, .tui = {.enabled = true}});
    std::vector<simrv::xlen::Byte> ram(1024 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.primary_hart().reset();

    Tui tui(machine);
    TuiTestAccess::init_panes(tui, machine);
    TuiTestAccess::set_cached_term_width(tui, 120);
    TuiTestAccess::set_cached_term_height(tui, 30);
    TuiTestAccess::set_sixel_supported(tui, true);
    tui.apply_layout_preset(LayoutPreset::GeneralDebug);

    // Initial render establishing clean baseline
    tui.render(true);

    // 1. Switching to Sixel Display from ToolPickerModal
    tui.open_tool_picker(1);
    tui.render(true);
    expect(tui.is_modal_active(), "tool picker modal is active");

    bool has_tool_picker_text = false;
    for (const auto& line : TuiTestAccess::last_screen_lines(tui)) {
        if (line.find("Assign a tool to Column 2") != std::string::npos ||
            line.find("Sixel Display") != std::string::npos) {
            has_tool_picker_text = true;
            break;
        }
    }
    expect(has_tool_picker_text, "rendered lines contain tool picker modal text while active");

    // Submit Sixel Display via accelerator 'y'
    expect(TuiTestAccess::handle_modal_key(tui, static_cast<uint8_t>('y'), TuiKey::y),
           "display accelerator handled");
    expect(!tui.is_modal_active(), "modal closed after selecting Sixel Display");
    expect(tui.get_workbench_slots()[1].page == TuiRegPage::DISPLAY, "slot 1 is now DISPLAY");

    // Render frame after modal close
    tui.render(true);

    // Verify all modal text is cleared from the screen lines
    for (const auto& line : TuiTestAccess::last_screen_lines(tui)) {
        expect(line.find("Assign a tool to Column 2") == std::string::npos,
               "tool picker header cleared when switching to sixel display");
        expect(line.find("ANALYSIS & SYSTEM") == std::string::npos,
               "tool picker category header cleared when switching to sixel display");
    }

    // 2. Opening and closing a modal while Sixel Display is already active
    tui.open_modal(ModalType::Help);
    tui.render(true);
    expect(tui.is_modal_active(), "help modal is active on top of sixel display");

    bool has_help_text = false;
    for (const auto& line : TuiTestAccess::last_screen_lines(tui)) {
        if (line.find("KEYBOARD SHORTCUTS") != std::string::npos ||
            line.find("SIMULATOR CONTROL") != std::string::npos) {
            has_help_text = true;
            break;
        }
    }
    expect(has_help_text, "rendered lines contain help modal text while active");

    // Close the modal
    tui.close_modal();
    expect(!tui.is_modal_active(), "help modal is closed");

    // Render frame after closing
    tui.render(true);

    for (const auto& line : TuiTestAccess::last_screen_lines(tui)) {
        expect(line.find("KEYBOARD SHORTCUTS") == std::string::npos,
               "help modal content cleared when closing modal on sixel display");
        expect(line.find("SIMULATOR CONTROL") == std::string::npos,
               "help modal header cleared when closing modal on sixel display");
    }

    // 3. Verify clean background generation before first sixel image
    TuiTestAccess::set_sixel_rendered(tui, false);
    std::string first_sixel_cmds;
    TuiTestAccess::render_draw_sixel(tui, 60, 50, 20, first_sixel_cmds);
    expect(first_sixel_cmds.find("\033[0m\033[5;60H") != std::string::npos,
           "first sixel image forces clean background spaces in panel body");
    expect(first_sixel_cmds.find(std::string(50, ' ')) != std::string::npos,
           "clean background covers entire panel width with spaces");

    // Subsequent sixel image when already rendered does not re-emit blank spaces
    TuiTestAccess::set_sixel_rendered(tui, true);
    std::string subsequent_sixel_cmds;
    TuiTestAccess::render_draw_sixel(tui, 60, 50, 20, subsequent_sixel_cmds);
    expect(subsequent_sixel_cmds.find("\033[0m\033[5;60H") == std::string::npos,
           "subsequent sixel image does not prepend blank rows, preserving fluid updates");
}

void test_sixel_fluid_framebuffer_updates() {
    using namespace simrv::tui;
    using namespace simrv::core;

    constexpr size_t test_dram_size = 128 * 1024 * 1024;
    Machine machine(MachineConfig{.memory = {.dram_size = test_dram_size},
                                  .execution = {.appmode = true},
                                  .tui = {.enabled = true}});
    std::vector<simrv::xlen::Byte> ram(test_dram_size, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.primary_hart().reset();

    const auto fb = machine.framebuffer_view();
    expect(fb.data() != nullptr && fb.size() == 800 * 600 * 4,
           "valid 800x600 framebuffer mapped for testing");

    Tui tui(machine);
    TuiTestAccess::init_panes(tui, machine);
    TuiTestAccess::set_cached_term_width(tui, 120);
    TuiTestAccess::set_cached_term_height(tui, 30);
    TuiTestAccess::set_sixel_supported(tui, true);
    tui.apply_layout_preset(LayoutPreset::GeneralDebug);

    // Assign Sixel Display to Column 2
    tui.open_tool_picker(1);
    expect(TuiTestAccess::handle_modal_key(tui, static_cast<uint8_t>('y'), TuiKey::y),
           "display accelerator selects DISPLAY");
    tui.render(true);

    const auto sig0 = TuiTestAccess::sixel_framebuffer_signature(tui);
    expect(sig0 != 0, "initial sixel framebuffer signature is non-zero");

    // Modify a single pixel at an unaligned byte offset (offset 13, was skipped by 64-byte stride)
    auto* fb_bytes = const_cast<simrv::xlen::Byte*>(fb.data());
    fb_bytes[13] = simrv::xlen::Byte{0x42};
    tui.render(true);

    const auto sig1 = TuiTestAccess::sixel_framebuffer_signature(tui);
    expect(sig1 != sig0,
           "unaligned pixel write immediately updates sixel signature without missing frames");

    // Verify re-rendering an unchanged buffer keeps signature stable
    tui.render(true);
    expect(TuiTestAccess::sixel_framebuffer_signature(tui) == sig1,
           "unchanged framebuffer preserves signature");
}

void test_virtio_input_and_display_mouse() {
    using namespace simrv::tui;
    simrv::core::Machine machine;
    Tui tui(machine);

    // Verify coordinate mapping in display panel
    const auto out_pos = TuiTestAccess::display_coords_to_fb(tui, 0, 0, 0, 80, 24);
    expect(!out_pos.has_value(), "out-of-bounds coordinates return nullopt");

    // In-bounds cell mapping
    const auto in_pos = TuiTestAccess::display_coords_to_fb(tui, 20, 14, 0, 80, 24);
    expect(in_pos.has_value(), "valid display coordinates map to framebuffer pixel");
    expect(in_pos->first >= 0 && in_pos->first < 800, "fb_x within 800 width");
    expect(in_pos->second >= 0 && in_pos->second < 600, "fb_y within 600 height");

    // SGR mouse coordinates stay in terminal cells. Map the center cell of the
    // sixel image to its corresponding framebuffer point.
    constexpr int mouse_term_width = 400;
    constexpr int mouse_term_height = 300;
    constexpr int cell_width = 8;
    constexpr int cell_height = 16;
    constexpr int body_rows = mouse_term_height - 7 - 1;
    const auto widths = TuiTestAccess::column_widths(tui, mouse_term_width);
    const int panel_width = widths.widths[0];
    const int source_width = static_cast<int>(machine.framebuffer_width());
    const int source_height = static_cast<int>(machine.framebuffer_height());
    const auto [image_width, image_height] = TuiTestAccess::compute_sixel_dimensions(
        panel_width, body_rows, cell_width, cell_height, source_width, source_height);
    const int image_cols = std::max(1, (image_width + cell_width - 1) / cell_width);
    const int image_rows = std::max(1, (image_height + cell_height - 1) / cell_height);
    const int display_x = 2 + std::max(0, (panel_width - image_cols) / 2);
    const int display_y = 5 + std::max(0, (body_rows - image_rows) / 2);
    const int x = display_x + image_cols / 2;
    const int y = display_y + image_rows / 2;
    const int rel_col = x - display_x;
    const int rel_row = y - display_y;
    const int px = std::clamp(rel_col * cell_width + cell_width / 2, 0, image_width - 1);
    const int py = std::clamp(rel_row * cell_height + cell_height / 2, 0, image_height - 1);
    const auto mapped =
        TuiTestAccess::display_coords_to_fb(tui, x, y, 0, mouse_term_width, mouse_term_height);
    expect(mapped.has_value(), "cell coordinates inside the sixel image map");
    if (mapped.has_value()) {
        expect(mapped->first == std::clamp(px * source_width / image_width, 0, source_width - 1),
               "cell mouse x maps through the rendered sixel scale");
        expect(
            mapped->second == std::clamp(py * source_height / image_height, 0, source_height - 1),
            "cell mouse y maps through the rendered sixel scale");
    }

    // Check distant points too: a cell-size mismatch must not grow into a cursor drift
    // proportional to distance from the image's upper-left corner.
    for (const auto [column_fraction, row_fraction] :
         std::array<std::pair<int, int>, 3>{{{1, 4}, {3, 4}, {9, 10}}}) {
        const int sample_x = display_x + (image_cols - 1) * column_fraction / row_fraction;
        const int sample_y = display_y + (image_rows - 1) * column_fraction / row_fraction;
        const auto sample = TuiTestAccess::display_coords_to_fb(
            tui, sample_x, sample_y, 0, mouse_term_width, mouse_term_height);
        expect(sample.has_value(), "distant cell coordinates inside the sixel image map");
        if (sample.has_value()) {
            const int sample_col = sample_x - display_x;
            const int sample_row = sample_y - display_y;
            const int sample_px =
                std::clamp(sample_col * cell_width + cell_width / 2, 0, image_width - 1);
            const int sample_py =
                std::clamp(sample_row * cell_height + cell_height / 2, 0, image_height - 1);
            expect(sample->first ==
                       std::clamp(sample_px * source_width / image_width, 0, source_width - 1),
                   "distant mouse x remains aligned with the sixel raster");
            expect(sample->second ==
                       std::clamp(sample_py * source_height / image_height, 0, source_height - 1),
                   "distant mouse y remains aligned with the sixel raster");
        }
    }

    // Pixel reports should map directly into the sixel raster, avoiding cell-scale drift.
    const int raster_col = 2 + std::max(0, (panel_width - image_cols) / 2);
    const int raster_row = 5 + std::max(0, (body_rows - image_rows) / 2);
    for (const auto [fraction_num, fraction_den] :
         std::array<std::pair<int, int>, 3>{{{1, 4}, {1, 2}, {9, 10}}}) {
        const int raster_px = (image_width - 1) * fraction_num / fraction_den;
        const int raster_py = (image_height - 1) * fraction_num / fraction_den;
        const int pixel_x = (raster_col - 1) * cell_width + raster_px + 1;
        const int pixel_y = (raster_row - 1) * cell_height + raster_py + 1;
        const auto pixel_mapped = TuiTestAccess::display_pixels_to_fb(
            tui, pixel_x, pixel_y, 0, mouse_term_width, mouse_term_height,
            mouse_term_width * cell_width, mouse_term_height * cell_height);
        expect(pixel_mapped.has_value(), "pixel coordinates inside the sixel raster map");
        if (pixel_mapped.has_value()) {
            expect(pixel_mapped->first ==
                       std::clamp(raster_px * source_width / image_width, 0, source_width - 1),
                   "pixel mouse x maps without cumulative sixel scale drift");
            expect(pixel_mapped->second ==
                       std::clamp(raster_py * source_height / image_height, 0, source_height - 1),
                   "pixel mouse y maps without cumulative sixel scale drift");
        }
    }

    // Terminal cell metrics can differ from the queried pixel size used to choose the
    // sixel raster dimensions. The raster origin anchors from the terminal's physical geometry,
    // and distance inside the raster maps 1:1 without cumulative scale drift.
    constexpr int actual_cell_width = cell_width + 1;
    constexpr int actual_cell_height = cell_height + 2;
    const int actual_pixel_x = (raster_col - 1) * actual_cell_width + (image_width - 1);
    const int actual_pixel_y = (raster_row - 1) * actual_cell_height + (image_height - 1);
    const auto drift_mapped = TuiTestAccess::display_pixels_to_fb(
        tui, actual_pixel_x + 1, actual_pixel_y + 1, 0, mouse_term_width, mouse_term_height,
        mouse_term_width * actual_cell_width, mouse_term_height * actual_cell_height);
    expect(drift_mapped.has_value(), "distant pixel coordinate maps with mismatched cell metrics");
    if (drift_mapped.has_value()) {
        expect(drift_mapped->first == (image_width - 1) * source_width / image_width &&
                   drift_mapped->second == (image_height - 1) * source_height / image_height,
               "terminal pixel geometry keeps the far raster corner aligned");
    }

    // A terminal may report a few pixels of window padding beyond its cell grid. Spreading
    // those pixels across every cell causes the mapped cursor to drift across the sixel image.
    constexpr int padded_pixel_width = mouse_term_width * cell_width + 4;
    constexpr int padded_pixel_height = mouse_term_height * cell_height + 4;
    const int padded_pixel_x = 2 + (raster_col - 1) * cell_width + image_width - 1;
    const int padded_pixel_y = 2 + (raster_row - 1) * cell_height + image_height - 1;
    const auto padded_mapped = TuiTestAccess::display_pixels_to_fb(
        tui, padded_pixel_x + 1, padded_pixel_y + 1, 0, mouse_term_width, mouse_term_height,
        padded_pixel_width, padded_pixel_height);
    expect(padded_mapped.has_value(), "far sixel point maps with terminal window padding");
    if (padded_mapped.has_value()) {
        expect(padded_mapped->first == (image_width - 1) * source_width / image_width &&
                   padded_mapped->second == (image_height - 1) * source_height / image_height,
               "terminal window padding does not create distance-dependent cursor drift");
    }

    // Fractional display scaling can produce a non-integral average cell pitch. Rounding
    // that pitch makes the error accumulate with distance from the sixel image origin.
    constexpr double fractional_cell_width = 8.5;
    constexpr double fractional_cell_height = 17.25;
    const int fractional_pixel_x =
        static_cast<int>(
            std::lround((raster_col - 1) * fractional_cell_width + (image_width - 1))) +
        1;
    const int fractional_pixel_y =
        static_cast<int>(
            std::lround((raster_row - 1) * fractional_cell_height + (image_height - 1))) +
        1;
    const auto fractional_mapped = TuiTestAccess::display_pixels_to_fb(
        tui, fractional_pixel_x, fractional_pixel_y, 0, mouse_term_width, mouse_term_height,
        static_cast<int>(mouse_term_width * fractional_cell_width),
        static_cast<int>(mouse_term_height * fractional_cell_height));
    expect(fractional_mapped.has_value(), "far sixel point maps with fractional cell pitch");
    if (fractional_mapped.has_value()) {
        expect(fractional_mapped->first == (image_width - 1) * source_width / image_width &&
                   fractional_mapped->second == (image_height - 1) * source_height / image_height,
               "fractional cell pitch does not accumulate pointer drift across the raster");
    }

    // Configure workbench with DISPLAY slot
    tui.set_workbench_slot_page(0, TuiRegPage::DISPLAY);
    expect(tui.is_page_visible(TuiRegPage::DISPLAY), "DISPLAY is visible in workbench");

    // Test machine input forwarding APIs
    machine.send_input_key(30 /* KEY_A */, true);
    machine.send_input_key(30, false);
    machine.send_input_mouse_motion(320, 240);
    machine.send_input_mouse_button(0x110 /* BTN_LEFT */, true);
    machine.send_input_mouse_button(0x110, false);
    machine.send_input_mouse_wheel(1);

    // Test mouse click and wheel handling on DISPLAY
    TuiTestAccess::handle_display_mouse(tui, 20, 14, 0 /* BTN_LEFT */, 0, 80, 24);
    TuiTestAccess::handle_display_mouse(tui, 10, 10, 64, 0, 80, 24);
    TuiTestAccess::handle_display_mouse(tui, 10, 10, 65, 0, 80, 24);

    // Test dynamic framebuffer adaptation (e.g. legacy 640x480 DTB image)
    machine.set_framebuffer_resolution_for_testing(640, 480);
    expect(machine.framebuffer_width() == 640, "adapted width is 640");
    expect(machine.framebuffer_height() == 480, "adapted height is 480");
    const auto adapted_pos = TuiTestAccess::display_coords_to_fb(tui, 20, 14, 0, 80, 24);
    expect(adapted_pos.has_value(), "adapted display coordinates map to framebuffer pixel");
    expect(adapted_pos->first >= 0 && adapted_pos->first < 640, "fb_x within adapted 640 width");
    expect(adapted_pos->second >= 0 && adapted_pos->second < 480, "fb_y within adapted 480 height");
}

void test_concurrent_terminal_and_sixel_display() {
    using namespace simrv::tui;
    simrv::core::Machine machine;
    Tui tui(machine);
    TuiTestAccess::init_panes(tui, machine);
    TuiTestAccess::set_cached_term_width(tui, 120);
    TuiTestAccess::set_cached_term_height(tui, 30);

    // Configure workbench with both CONSOLE and DISPLAY side-by-side
    TuiTestAccess::set_workbench_slots(tui, {{TuiRegPage::CONSOLE, 0}, {TuiRegPage::DISPLAY, 0}});
    expect(tui.is_page_visible(TuiRegPage::CONSOLE), "CONSOLE is visible in workbench");
    expect(tui.is_page_visible(TuiRegPage::DISPLAY), "DISPLAY is visible in workbench");

    // Render frame
    tui.render(true);
    const auto& lines = TuiTestAccess::last_screen_lines(tui);
    expect(!lines.empty(), "rendered lines is not empty");

    // Check that line 3 (header line) contains both Console and Display headers
    bool found_console_hdr = false;
    bool found_display_hdr = false;
    for (const auto& line : lines) {
        if (line.find("Console") != std::string::npos) found_console_hdr = true;
        if (line.find("Display") != std::string::npos) found_display_hdr = true;
    }
    expect(found_console_hdr, "header contains Console column header");
    expect(found_display_hdr, "header contains Display column header");

    // Push UART output and verify terminal content updates without error
    tui.handle_char_write('H');
    tui.handle_char_write('i');
    tui.render(true);

    // Test scrolling with CONSOLE focused
    TuiTestAccess::set_focused_slot(tui, 0);
    tui.scroll(5);
    tui.reset_scroll();
}

void test_terminal_cursor_tracking_and_scrollback() {
    using namespace simrv::tui;
    simrv::core::Machine machine;
    Tui tui(machine);
    TuiTestAccess::init_panes(tui, machine);
    TuiTestAccess::set_cached_term_width(tui, 80);
    TuiTestAccess::set_cached_term_height(tui, 24);

    auto& vt = TuiTestAccess::vt(tui);

    // Initial state: empty terminal (default 24 rows)
    expect(TuiTestAccess::get_terminal_pane_start_line(tui, 24) == 0,
           "start line is 0 for empty terminal");

    // Write a few lines without scrollback
    for (int i = 0; i < 5; ++i) {
        vt.write_string("A\r\n");
    }
    expect(vt.get_scrollback_size() == 0, "no scrollback after 5 lines");
    expect(vt.get_cursor_y() == 5, "cursor_y is 5");
    expect(TuiTestAccess::get_terminal_pane_start_line(tui, 24) == 0,
           "start line is 0 when all lines fit in view");

    // Fill screen and create scrollback (write 30 more lines to push past 24 rows)
    for (int i = 0; i < 30; ++i) {
        vt.write_string("B\r\n");
    }
    expect(vt.get_scrollback_size() > 0, "scrollback generated after filling screen");
    const int scrollback_count = vt.get_scrollback_size();
    expect(vt.get_lines_count() == scrollback_count + 24, "lines count equals scrollback + screen");

    // At scroll_offset = 0, start line should be exactly scrollback_count
    // so live viewport maps 1:1 with screen cells and cursor line offset equals cursor_y
    expect(TuiTestAccess::get_terminal_pane_start_line(tui, 24) == scrollback_count,
           "start line matches scrollback count so live viewport maps to screen cells");

    const int cursor_abs_line = vt.get_scrollback_size() + vt.get_cursor_y();
    const int start_line = TuiTestAccess::get_terminal_pane_start_line(tui, 24);
    const int line_offset = cursor_abs_line - start_line;
    expect(line_offset == vt.get_cursor_y(),
           "cursor line offset in terminal pane exactly matches active cursor_y");

    // When scrolled back by 4 lines, start_line moves back by 4
    TuiTestAccess::set_scroll_offset(tui, 4);
    expect(TuiTestAccess::get_terminal_pane_start_line(tui, 24) == scrollback_count - 4,
           "start line shifts back proportionally when scrolling history");
    TuiTestAccess::set_scroll_offset(tui, 0);
}

void test_sixel_display_resolution_adaptation_and_size_warning() {
    using namespace simrv::tui;
    using namespace simrv::core;

    constexpr size_t test_dram_size = 128 * 1024 * 1024;
    Machine machine(MachineConfig{.memory = {.dram_size = test_dram_size},
                                  .execution = {.appmode = true},
                                  .tui = {.enabled = true}});
    std::vector<simrv::xlen::Byte> ram(test_dram_size, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.primary_hart().reset();

    Tui tui(machine);
    TuiTestAccess::init_panes(tui, machine);
    tui.close_modal();

    // 1. Small width check (< kMinDisplayWidth = 24 cols)
    TuiTestAccess::set_user_column_widths(tui, framework::kNoColumnWidthOverrides);
    TuiTestAccess::set_layout(tui, TuiLayout::Split);
    TuiTestAccess::set_cached_term_width(tui, 40);
    TuiTestAccess::set_cached_term_height(tui, 24);
    TuiTestAccess::set_workbench_slots(tui, {{TuiRegPage::DISPLAY, 0}, {TuiRegPage::CONSOLE, 0}});
    TuiTestAccess::set_sixel_supported(tui, true);

    tui.render(true);
    const auto& small_w_lines = TuiTestAccess::last_screen_lines(tui);
    bool found_too_small = false;
    for (const auto& line : small_w_lines) {
        if (line.find("Too small") != std::string::npos ||
            line.find("Display space too small") != std::string::npos) {
            found_too_small = true;
            break;
        }
    }
    expect(found_too_small, "displays warning when width < 24");

    // Sixel drawing must be suppressed when width is under minimum
    std::string small_sixel_cmds;
    TuiTestAccess::render_draw_sixel(tui, 2, 20, 15, small_sixel_cmds);
    expect(small_sixel_cmds.empty(), "sixel drawing suppressed when width < kMinDisplayWidth");

    // 2. Small height check (< kMinDisplayBodyRows = 8 body rows)
    TuiTestAccess::set_cached_term_width(tui, 80);
    TuiTestAccess::set_cached_term_height(tui, 14);  // content_rows = 7, body_rows <= 7 < 8
    tui.render(true);
    const auto& small_h_lines = TuiTestAccess::last_screen_lines(tui);
    bool found_too_small_h = false;
    for (const auto& line : small_h_lines) {
        if (line.find("Display space too small") != std::string::npos) {
            found_too_small_h = true;
            break;
        }
    }
    expect(found_too_small_h, "displays warning when body rows < 8");

    // Sixel drawing must be suppressed when body rows is under minimum
    small_sixel_cmds.clear();
    TuiTestAccess::render_draw_sixel(tui, 2, 80, 5, small_sixel_cmds);
    expect(small_sixel_cmds.empty(),
           "sixel drawing suppressed when body rows < kMinDisplayBodyRows");

    // 3. Sixel unsupported check when panel size is sufficient
    TuiTestAccess::set_cached_term_width(tui, 80);
    TuiTestAccess::set_cached_term_height(tui, 24);
    TuiTestAccess::set_sixel_supported(tui, false);
    tui.render(true);
    const auto& unsupp_lines = TuiTestAccess::last_screen_lines(tui);
    bool found_unsupported = false;
    for (const auto& line : unsupp_lines) {
        if (line.find("Sixel graphics unsupported") != std::string::npos) {
            found_unsupported = true;
            break;
        }
    }
    expect(found_unsupported, "displays unsupported warning when sixel not supported");

    // 4. Normal sixel display: resolution adapts to terminal without 640px clamp
    TuiTestAccess::set_sixel_supported(tui, true);
    TuiTestAccess::set_sixel_rendered(tui, false);
    tui.render(true);
    const auto& normal_lines = TuiTestAccess::last_screen_lines(tui);
    bool found_warning_in_normal = false;
    for (const auto& line : normal_lines) {
        if (line.find("Display space too small") != std::string::npos ||
            line.find("Sixel graphics unsupported") != std::string::npos) {
            found_warning_in_normal = true;
            break;
        }
    }
    expect(!found_warning_in_normal,
           "no warnings displayed when space is sufficient and sixel supported");

    std::string normal_sixel_cmds;
    TuiTestAccess::render_draw_sixel(tui, 2, 80, 20, normal_sixel_cmds);
    expect(!normal_sixel_cmds.empty(), "sixel commands generated for valid display dimensions");

    // 5. Verify strictly integer scaling calculations
    // Exact 1x native scale when terminal has sufficient space for 1x but not 2x
    const auto dims_1x = TuiTestAccess::compute_sixel_dimensions(100, 40, 10, 20, 800, 600);
    expect(dims_1x.first == 800 && dims_1x.second == 600,
           "1000x800 space uses exact 1x scale (800x600)");

    // Fractional space must NOT scale fractionally; must remain 1x integer scale
    const auto dims_frac = TuiTestAccess::compute_sixel_dimensions(120, 45, 10, 20, 800, 600);
    expect(dims_frac.first == 800 && dims_frac.second == 600,
           "1200x900 space uses exact 1x scale rather than fractional 1.5x");

    // Exact 2x scale when space is >= 1600x1200
    const auto dims_2x = TuiTestAccess::compute_sixel_dimensions(200, 60, 10, 20, 800, 600);
    expect(dims_2x.first == 1600 && dims_2x.second == 1200,
           "2000x1200 space uses exact 2x integer scale (1600x1200)");

    // Exact 3x scale when space is >= 2400x1800
    const auto dims_3x = TuiTestAccess::compute_sixel_dimensions(300, 100, 10, 20, 800, 600);
    expect(dims_3x.first == 2400 && dims_3x.second == 1800,
           "3000x2000 space uses exact 3x integer scale (2400x1800)");

    // Integer downscaling (1/2x) when space is < 800x600
    const auto dims_half = TuiTestAccess::compute_sixel_dimensions(64, 25, 10, 20, 800, 600);
    expect(dims_half.first == 400 && dims_half.second == 300,
           "640x500 space uses exact 1/2x integer scale (400x300)");

    // Integer downscaling (1/4x) for very small panels
    const auto dims_quarter = TuiTestAccess::compute_sixel_dimensions(25, 10, 10, 20, 800, 600);
    expect(dims_quarter.first == 200 && dims_quarter.second == 150,
           "250x200 space uses exact 1/4x integer scale (200x150)");

    // Legacy 640x480 guest framebuffer integer scaling
    const auto dims_legacy_1x = TuiTestAccess::compute_sixel_dimensions(80, 30, 10, 20, 640, 480);
    expect(dims_legacy_1x.first == 640 && dims_legacy_1x.second == 480,
           "800x600 space for 640x480 guest uses exact 1x scale (640x480)");
    const auto dims_legacy_2x = TuiTestAccess::compute_sixel_dimensions(150, 50, 10, 20, 640, 480);
    expect(dims_legacy_2x.first == 1280 && dims_legacy_2x.second == 960,
           "1500x1000 space for 640x480 guest uses exact 2x scale (1280x960)");
}

void test_memory_modal_and_hart_modal() {
    using namespace simrv::tui::modals;

    simrv::core::Machine machine;
    std::vector<simrv::xlen::Byte> ram(64 * 1024, simrv::xlen::Byte{0});
    machine.set_ram_for_testing(ram);
    machine.memory().initialize_mmu();

    // Populate some known test bytes in RAM
    ram[0x1000] = simrv::xlen::Byte{0xAB};
    ram[0x1001] = simrv::xlen::Byte{0xCD};
    ram[0x1002] = simrv::xlen::Byte{0xEF};
    ram[0x1003] = simrv::xlen::Byte{0x12};

    // 1. Test MemoryModal navigation and inspection
    const simrv::xlen::Address start_addr = simrv::memory::kDramBaseAddress + 0x1000;
    MemoryModal::open(start_addr, machine, nullptr);
    expect(MemoryModal::current_address() == start_addr, "MemoryModal opens at target address");
    expect(MemoryModal::input_mode() == MemoryModal::InputMode::Normal,
           "MemoryModal starts in Normal mode");

    // Move cursor within row
    MemoryModal::move_cursor(1, 0, machine);
    expect(MemoryModal::current_address() == start_addr + 1, "move_cursor moves right 1 byte");

    // Move cursor across rows
    MemoryModal::move_cursor(0, 2, machine);
    expect(MemoryModal::current_address() == start_addr + 1 + 32,
           "move_cursor down 2 rows advances 32 bytes");

    // Page scrolling
    MemoryModal::scroll_page(1, machine);
    expect(MemoryModal::current_address() > start_addr + 32, "scroll_page advances memory window");

    // Toggle addressing mode
    MemoryModal::toggle_addressing_mode();
    MemoryModal::toggle_addressing_mode();

    // Goto Address
    MemoryModal::set_input_mode(MemoryModal::InputMode::GotoAddress);
    expect(MemoryModal::input_mode() == MemoryModal::InputMode::GotoAddress,
           "GotoAddress mode set");
    std::string status_msg;
    auto set_status = [&](const std::string& msg) { status_msg = msg; };
    const bool goto_ok = MemoryModal::submit_input("0x80001000", machine, nullptr, set_status);
    expect(goto_ok, "submit_input with valid hex address succeeds");
    expect(MemoryModal::current_address() == static_cast<simrv::xlen::Address>(0x80001000ULL),
           "MemoryModal navigated to 0x80001000");
    expect(MemoryModal::input_mode() == MemoryModal::InputMode::Normal,
           "Input mode returns to Normal after goto");

    // In-place byte editing
    MemoryModal::set_input_mode(MemoryModal::InputMode::EditByte);
    const bool edit_ok = MemoryModal::submit_input("7F", machine, nullptr, set_status);
    expect(edit_ok, "submit_input with valid hex byte succeeds");
    expect(static_cast<uint8_t>(ram[0x1000]) == 0x7FU, "Byte in RAM updated in-place");
    expect(MemoryModal::input_mode() == MemoryModal::InputMode::Normal,
           "Input mode returns to Normal after edit");

    // MemoryModal rendering test
    std::vector<std::string> mem_rows;
    auto add_mem_row = [&](const std::string& r) { mem_rows.push_back(r); };
    MemoryModal::render(mem_rows, add_mem_row, "", machine, nullptr, 30, 80);
    expect(!mem_rows.empty(), "MemoryModal generates render rows");
    bool found_hex_header = false;
    for (const auto& row : mem_rows) {
        if (row.contains("Focus:") || row.contains("Word32:")) {
            found_hex_header = true;
            break;
        }
    }
    expect(found_hex_header, "MemoryModal renders Focus / Data Interpretations sections");

    // 2. Test HartModal navigation and switching
    HartModal::open(0, machine);
    HartModal::move_cursor(1, machine);
    HartModal::move_cursor(-1, machine);
    size_t selected_hart = 999;
    const bool hart_sub_ok = HartModal::submit(machine, selected_hart);
    expect(hart_sub_ok, "HartModal::submit succeeds");
    expect(selected_hart == 0, "HartModal selects primary hart on single-hart machine");

    std::vector<std::string> hart_rows;
    auto add_hart_row = [&](const std::string& r) { hart_rows.push_back(r); };
    HartModal::render(hart_rows, add_hart_row, machine);
    expect(!hart_rows.empty(), "HartModal generates render rows");
    bool found_hart_table = false;
    for (const auto& row : hart_rows) {
        if (row.contains("Hart") && row.contains("Status")) {
            found_hart_table = true;
            break;
        }
    }
    expect(found_hart_table, "HartModal renders hart table header");

    // 3. Test MemoryModal interactive hex and ascii cell clicking
    MemoryModal::open(0x80000000ULL, machine, nullptr);
    constexpr unsigned int xlen = 64;
    // Row 2 byte 0 at rel_x = 16
    expect(MemoryModal::handle_cell_click(2, 16, xlen), "Clicking byte 0 at row 2 succeeds");
    expect(MemoryModal::current_address() == (0x80000000ULL + 2 * 16 + 0),
           "Current address matches clicked row 2 byte 0");

    // Row 3 byte 8 at rel_x = 16 + 8 * 3 + 1 = 41
    expect(MemoryModal::handle_cell_click(3, 41, xlen), "Clicking byte 8 at row 3 succeeds");
    expect(MemoryModal::current_address() == (0x80000000ULL + 3 * 16 + 8),
           "Current address matches clicked row 3 byte 8");

    // Row 4 ASCII byte 5 at rel_x = 16 + 51 + 5 = 72
    expect(MemoryModal::handle_cell_click(4, 72, xlen), "Clicking ASCII byte 5 at row 4 succeeds");
    expect(MemoryModal::current_address() == (0x80000000ULL + 4 * 16 + 5),
           "Current address matches clicked row 4 ASCII byte 5");
}

void test_rc15_features() {
    using namespace simrv::tui;
    using namespace simrv::tui::modals;

    // 1. LogModal functionality
    LogBuffer log_buffer;
    log_buffer.push(simrv::log::Level::Info, "Standard informational log message");
    log_buffer.push(simrv::log::Level::Warn, "A warning to pay attention to");
    log_buffer.push(simrv::log::Level::Error, "A critical error condition");

    expect(log_buffer.warn_count() == 1, "LogBuffer correctly tallies warnings");
    expect(log_buffer.error_count() == 1, "LogBuffer correctly tallies errors");

    LogModal modal;
    modal.open(log_buffer);
    expect(modal.is_following(), "LogModal opens with follow tail enabled");
    modal.toggle_follow();
    expect(!modal.is_following(), "toggle_follow turns follow off");

    expect(!modal.filter_level().has_value(), "LogModal starts with no filter (ALL)");
    modal.cycle_filter_level();
    expect(modal.filter_level() == simrv::log::Level::Info, "Filter cycled to Info");
    modal.cycle_filter_level();
    expect(modal.filter_level() == simrv::log::Level::Warn, "Filter cycled to Warn");
    modal.cycle_filter_level();
    expect(modal.filter_level() == simrv::log::Level::Error, "Filter cycled to Error");
    modal.cycle_filter_level();
    expect(!modal.filter_level().has_value(), "Filter cycled back to ALL");

    modal.set_search_query("critical");
    expect(modal.search_query() == "critical", "Search query set");
    std::string copied = modal.copy_text(log_buffer);
    expect(copied.contains("critical error"), "copy_text filters and returns matching content");

    std::vector<std::string> rows;
    auto add_row = [&](const std::string& r) { rows.push_back(r); };
    modal.render(rows, add_row, log_buffer, 30, 80);
    expect(!rows.empty(), "LogModal renders rows");

    // 2. KeyAction verification for [d] and [L]
    const auto& dump_binding = Keybindings::get(KeyAction::DumpMemory);
    expect(dump_binding.primary_char == 'd' && dump_binding.alt_char == 'D',
           "DumpMemory is bound to 'd'/'D'");
    const auto& log_binding = Keybindings::get(KeyAction::OpenLogModal);
    expect(log_binding.primary_char == 'L', "OpenLogModal is bound to 'L'");
}

void test_formalized_registry_and_presets() {
    using namespace simrv::tui;
    using namespace simrv::tui::modals;

    // 1. Unified kTuiPages invariants & ToolPickerModal synchronization
    expect(std::size(kTuiPages) == ToolPickerModal::total_tool_count(),
           "kTuiPages count matches ToolPickerModal tool count");
    expect(std::string(get_page_name(TuiRegPage::LOG)) == "Simulator Log",
           "LOG page name is Simulator Log");
    expect(std::string(get_page_info(TuiRegPage::LOG)->picker_label) == "Simulator Log",
           "LOG picker label is Simulator Log");
    for (const auto& info : kTuiPages) {
        expect(!info.name.empty() && !info.tab_name.empty() && !info.picker_label.empty() &&
                   !info.description.empty() && info.accelerator != '\0',
               "every TuiPageInfo entry has complete display and picker metadata");
        expect(ToolPickerModal::find_by_accelerator(info.accelerator) == info.page,
               "every TuiPageInfo accelerator resolves in ToolPickerModal");
        if (!info.mission_slug.empty()) {
            expect(parse_tui_page_slug(info.mission_slug) == info.page,
                   "every mission_slug round-trips through parse_tui_page_slug");
        }
    }
    for (size_t i = 0; i < ToolPickerModal::total_tool_count(); ++i) {
        const int row = ToolPickerModal::row_for_tool_index(i);
        expect(ToolPickerModal::tool_index_at_row(row) == i,
               "ToolPickerModal row_for_tool_index and tool_index_at_row round-trip");
    }

    // 2. Category group cycling and layout_for_column_count policy
    expect(cycle_category_group(TuiCategoryGroup::Regs, false) == TuiCategoryGroup::Memory &&
               cycle_category_group(TuiCategoryGroup::Regs, true) == TuiCategoryGroup::Tools,
           "cycle_category_group wraps forward and reverse across kTuiCategoryGroups");
    expect(layout_for_column_count(1, false) == TuiLayout::FullLeft &&
               layout_for_column_count(1, true) == TuiLayout::FullRight &&
               layout_for_column_count(2) == TuiLayout::Split &&
               layout_for_column_count(3) == TuiLayout::ThreeColumn &&
               layout_for_column_count(4) == TuiLayout::FourColumn,
           "layout_for_column_count maps 1-4 columns to canonical TuiLayout values");

    // 3. Table-driven kLayoutPresets across column counts and IA/CA modes
    for (const auto& preset : kLayoutPresets) {
        expect(!preset.name.empty() && !preset.shortcut_label.empty() &&
                   !preset.modal_summary.empty() && !preset.status_notice.empty(),
               "every LayoutPresetInfo entry has complete labels and status notice");
        for (uint8_t cols = 1; cols <= 4; ++cols) {
            const auto [ia_pages, ia_count] = preset.slots_for(cols, false);
            const auto [ca_pages, ca_count] = preset.slots_for(cols, true);
            expect(ia_count == cols && ca_count == cols,
                   "LayoutPresetInfo::slots_for returns requested column count");
            for (size_t c = 0; c < ia_count; ++c) {
                const auto* pinfo = get_page_info(ia_pages[c]);
                expect(pinfo != nullptr && !pinfo->cycle_accurate_only,
                       "instruction-accurate preset slots never assign cycle-accurate-only pages");
            }
        }
    }

    // 4. ToolPickerModal mode-aligned filtering (unavailable tools hidden)
    ToolFilter ia_filter{.cycle_accurate = false, .has_fp = false, .has_vector = false};
    const auto ia_tools = ToolPickerModal::all_tools(ia_filter);
    expect(ia_tools.size() == 10, "IA mode without FP/Vector provides exactly 10 tools");
    expect(!ToolPickerModal::find_by_accelerator('c', ia_filter).has_value(),
           "CACHE unavailable in IA");
    expect(!ToolPickerModal::find_by_accelerator('d', ia_filter).has_value(),
           "BPRED unavailable in IA");
    expect(!ToolPickerModal::find_by_accelerator('z', ia_filter).has_value(),
           "HAZARD unavailable in IA");
    expect(!ToolPickerModal::find_by_accelerator('f', ia_filter).has_value(),
           "FPR unavailable without FP");
    expect(!ToolPickerModal::find_by_accelerator('v', ia_filter).has_value(),
           "VEC unavailable without Vector");
    expect(ToolPickerModal::find_by_accelerator('o', ia_filter) == TuiRegPage::LOG,
           "LOG available in IA");
    expect(ToolPickerModal::content_row_count(ia_filter) == 18,
           "IA content row count adapts to 18 rows");

    for (size_t i = 0; i < ia_tools.size(); ++i) {
        const int row = ToolPickerModal::row_for_tool_index(i, ia_filter);
        expect(ToolPickerModal::tool_index_at_row(row, ia_filter) == i,
               "filtered ToolPickerModal row mapping round-trips cleanly");
    }
}

void test_formalized_scroll_framework() {
    using namespace simrv::tui::framework;

    // 1. ScrollBounds & ScrollView in Top-down mode
    ScrollView top_sv(ScrollIndicatorMode::EmbeddedRows, ScrollOrigin::Top);
    top_sv.set_geometry(100, 20, 80, 40);
    expect(!top_sv.can_scroll_up(), "top-pinned scrollview at offset 0 cannot scroll up");
    expect(top_sv.can_scroll_down(), "top-pinned scrollview at offset 0 can scroll down");
    expect(top_sv.remaining_above() == 0, "remaining above is 0 initially");
    expect(top_sv.remaining_below() == 80, "remaining below is 80 initially");

    top_sv.scroll(ScrollDirection::Down, ScrollGranularity::Step, 5);
    expect(top_sv.offset_y() == 5, "scroll Down advances offset_y by 5");
    expect(top_sv.can_scroll_up(), "after scroll down, can scroll up");
    expect(top_sv.remaining_above() == 5, "remaining above is 5");
    expect(top_sv.remaining_below() == 75, "remaining below is 75");

    top_sv.scroll(ScrollDirection::Right, ScrollGranularity::Step, 10);
    expect(top_sv.offset_x() == 10, "scroll Right advances offset_x by 10");
    top_sv.scroll(ScrollDirection::Left, ScrollGranularity::Edge);
    expect(top_sv.offset_x() == 0, "scroll Left with Edge resets offset_x to 0");

    top_sv.scroll_to_bottom();
    expect(top_sv.offset_y() == 80, "scroll_to_bottom jumps to max_offset_y (80)");
    expect(!top_sv.can_scroll_down(), "at bottom, cannot scroll down");
    top_sv.scroll_to_top();
    expect(top_sv.offset_y() == 0, "scroll_to_top jumps back to 0");

    // Indicator rendering
    top_sv.scroll_y(10);
    const auto top_ind = strip_ansi(top_sv.render_top_indicator(60));
    const auto bot_ind = strip_ansi(top_sv.render_bottom_indicator(60));
    expect(top_ind.contains("more lines above - scroll up"), "top indicator contains upward hint");
    expect(bot_ind.contains("more lines below - scroll down"),
           "bottom indicator contains downward hint");

    // 2. ScrollBounds & ScrollView in Bottom-pinned mode (Log/Terminal scrollback)
    ScrollView bot_sv(ScrollIndicatorMode::HeaderSummary, ScrollOrigin::Bottom);
    bot_sv.set_geometry(50, 10);
    expect(bot_sv.offset_y() == 0, "bottom-pinned view starts at offset 0 (live tail)");
    expect(bot_sv.can_scroll_up(), "bottom-pinned view can scroll up into history");
    expect(!bot_sv.can_scroll_down(), "bottom-pinned view cannot scroll below live tail");
    expect(bot_sv.remaining_above() == 40, "bottom-pinned view has 40 older lines above");
    expect(bot_sv.remaining_below() == 0, "bottom-pinned view has 0 lines below at tail");

    bot_sv.scroll(ScrollDirection::Up, ScrollGranularity::Step, 4);
    expect(bot_sv.offset_y() == 4, "scrolling Up into history advances offset_y to 4");
    expect(bot_sv.remaining_above() == 36, "remaining above decrements to 36");
    expect(bot_sv.remaining_below() == 4, "remaining below increments to 4");
    const auto log_summary = bot_sv.header_summary("Log");
    expect(log_summary.contains("▲ 36 above") && log_summary.contains("▼ 4 below"),
           "bottom-pinned header summary displays visual above and below correctly");

    // 3. Navigation Alt keys (Alt-w, Alt-s, Alt-u, Alt-d)
    simrv::core::Machine machine;
    simrv::tui::Tui tui(machine);
    simrv::tui::TuiTestAccess::init_panes(tui, machine);
    tui.set_reg_page(simrv::tui::TuiRegPage::GPR);

    expect(simrv::tui::TuiTestAccess::handle_alt_key(tui, 'w', 0),
           "Alt-w handled for inspector scroll up");
    expect(simrv::tui::TuiTestAccess::handle_alt_key(tui, 's', 0),
           "Alt-s handled for inspector scroll down");
    expect(simrv::tui::TuiTestAccess::handle_alt_key(tui, 'u', 0),
           "Alt-u handled for terminal scroll up");
    expect(simrv::tui::TuiTestAccess::handle_alt_key(tui, 'd', 0),
           "Alt-d handled for terminal scroll down");

    // 4. Home and End sequences
    expect(simrv::tui::TuiTestAccess::consume_control_seq(tui, "\033[H"),
           "Home key sequence accepted");
    expect(simrv::tui::TuiTestAccess::consume_control_seq(tui, "\033[F"),
           "End key sequence accepted");
    expect(simrv::tui::TuiTestAccess::consume_control_seq(tui, "\033[4~"),
           "Alternate End key accepted");

    // 5. Workbench slot scroll offset tracking
    tui.scroll_inspector(6);
    const auto& slots = simrv::tui::TuiTestAccess::workbench_slots(tui);
    expect(!slots.empty() && slots.front().scroll_offset == 6,
           "scrolling inspector updates focused workbench slot scroll_offset");
    tui.reset_scroll_inspector();
    expect(slots.front().scroll_offset == 0,
           "resetting inspector scroll updates workbench slot scroll_offset to 0");

    // 6. Multi-column drawer row alignment
    simrv::tui::InspectorPane test_pane(machine);
    test_pane.set_log_lines({"l0", "l1", "l2", "l3", "l4", "l5", "l6", "l7", "l8", "l9"});
    test_pane.set_page(simrv::tui::TuiRegPage::GPR);
    test_pane.set_paused(true);
    test_pane.set_visible_rows(39);
    // In a 39-row multi-column frame:
    // row 0 is column header
    // rows 1..32 are content rows (row 32 is bottom indicator)
    // rows 33..38 are drawer rows (height 6, with row 33 header and row 38 bottom log line)
    const auto drawer_header = strip_ansi(test_pane.render_column_row(33, 40, 0, 3, true));
    const auto last_drawer_row = strip_ansi(test_pane.render_column_row(38, 40, 0, 3, true));
    expect(drawer_header.contains("Log (▲ 5 above)"),
           "multi-column drawer header starts immediately after content rows");
    expect(last_drawer_row.contains("l9"),
           "multi-column drawer bottom row aligns with the last frame row without leakage");
}

void test_formalized_framework_primitives() {
    using namespace simrv::tui::framework;

    // 1. Generic DrawerStack
    DrawerStack<uint8_t> drawers;
    drawers.add({.id = 1, .name = "DrawerA", .height = 4, .render_row = [](int r, int) {
                     return "rowA" + std::to_string(r);
                 }});
    drawers.add({.id = 2, .name = "DrawerB", .height = 2, .render_row = [](int r, int) {
                     return "rowB" + std::to_string(r);
                 }});
    expect(drawers.height() == 6, "drawer stack total height is 6");
    expect(drawers.size() == 2, "drawer stack has 2 drawers");
    expect(drawers.top_drawer_row(20) == 14, "top drawer starts at row 14 of 20");

    auto spanA = drawers.find_span(1, 20);
    expect(spanA.has_value() && spanA->start_row == 14 && spanA->height == 4,
           "spanA starts at 14 with height 4");
    expect(spanA->is_header_row(14) && !spanA->is_header_row(15), "row 14 is header for spanA");
    expect(spanA->contains(14) && spanA->contains(17) && !spanA->contains(18),
           "spanA bounds check");
    expect(spanA->start_terminal_row(4) == 18, "spanA maps to terminal row 18 with offset 4");

    auto spanB = drawers.find_span(2, 20);
    expect(spanB.has_value() && spanB->start_row == 18 && spanB->height == 2,
           "spanB starts at 18 with height 2");

    expect(drawers.render_row(14, 20, 40) == "rowA0", "drawer row 14 renders rowA0");
    expect(drawers.render_row(17, 20, 40) == "rowA3", "drawer row 17 renders rowA3");
    expect(drawers.render_row(18, 20, 40) == "rowB0", "drawer row 18 renders rowB0");
    expect(drawers.render_row(19, 20, 40) == "rowB1", "drawer row 19 renders rowB1");
    expect(!drawers.render_row(13, 20, 40).has_value(), "row outside drawers returns nullopt");
    expect(!drawers.render_row(20, 20, 40).has_value(), "row >= total returns nullopt");

    // 2. TabBar component
    TabBar tabs;
    tabs.add_item({.id = 10, .label = "General", .shortcut = '1'});
    tabs.add_item({.id = 20, .label = "Extensions", .shortcut = '2'});
    tabs.add_item({.id = 30, .label = "Advanced", .shortcut = '3'});
    expect(tabs.size() == 3, "tab bar has 3 items");
    tabs.set_active_index(0);

    const auto rendered_tabs = tabs.render(60, Density::Comfortable, HorizontalAlign::Center);
    expect(tabs.spans().size() == 3, "rendering generated 3 tab spans");
    const auto& s0 = tabs.spans()[0];
    const auto& s1 = tabs.spans()[1];
    const auto& s2 = tabs.spans()[2];
    expect(s0.start_col < s1.start_col && s1.start_col < s2.start_col,
           "tabs ordered left to right");

    expect(tabs.hit_test(s0.start_col) == 0, "hit-testing first column of tab 0 returns index 0");
    expect(tabs.hit_test(s0.start_col + s0.width - 1) == 0,
           "hit-testing last column of tab 0 returns 0");
    expect(tabs.hit_test_id(s1.start_col) == 20, "hit-test by id returns id 20 for tab 1");
    expect(tabs.hit_test(s2.start_col) == 2, "hit-test tab 2 returns index 2");
    if (s0.start_col > 0) {
        expect(!tabs.hit_test(s0.start_col - 1).has_value(), "column before tabs returns nullopt");
    }

    // 3. ActionFooter component
    ActionFooter footer;
    footer.add_action({.key_hint = "[Enter]", .label = "Apply", .action_id = 101});
    footer.add_action({.key_hint = "[Esc]", .label = "Cancel", .action_id = 102});
    expect(footer.size() == 2, "footer has 2 actions");

    const auto rendered_footer = footer.render(60, Density::Comfortable, HorizontalAlign::Center);
    expect(footer.spans().size() == 2, "rendering generated 2 action spans");
    const auto& a0 = footer.spans()[0];
    const auto& a1 = footer.spans()[1];
    expect(footer.hit_test(a0.start_col) == 101, "clicking first action returns action_id 101");
    expect(footer.hit_test(a1.start_col) == 102, "clicking second action returns action_id 102");
    expect(!footer.hit_test(0).has_value(), "clicking leading space returns nullopt");

    // 4. Workbench component
    struct TestSlot {
        int id = 0;
        int scroll = 0;
    };
    Workbench<TestSlot, 4> wb;
    expect(wb.size() == 1, "workbench starts with 1 slot");
    expect(wb.capacity() == 4, "workbench capacity is 4");
    expect(wb.focused_index() == 0, "focused index starts at 0");
    expect(!wb.can_close(), "cannot close single slot");
    expect(wb.can_add(), "can add when size < 4");

    expect(wb.add({.id = 10, .scroll = 0}), "adding slot 1 succeeds");
    expect(wb.size() == 2, "size is now 2");
    expect(wb.focused_index() == 1, "focus moved to added slot");
    expect(wb.can_close(), "can close when size > 1");

    expect(wb.add({.id = 20, .scroll = 0}), "adding slot 2 succeeds");
    expect(wb.add({.id = 30, .scroll = 0}), "adding slot 3 succeeds");
    expect(wb.size() == 4, "workbench has 4 slots");
    expect(!wb.can_add(), "cannot add past capacity 4");
    expect(!wb.add({.id = 40, .scroll = 0}), "adding past capacity returns false");

    // Focus navigation
    expect(wb.focused_index() == 3, "focus is at 3");
    wb.focus_next();
    expect(wb.focused_index() == 0, "focus wraps to 0");
    wb.focus_prev();
    expect(wb.focused_index() == 3, "focus wraps back to 3");

    // Swap and move
    wb.focus(1);
    expect(wb[1].id == 10, "slot 1 has id 10");
    expect(wb[2].id == 20, "slot 2 has id 20");
    expect(wb.move_right(1), "move_right(1) succeeds");
    expect(wb[1].id == 20 && wb[2].id == 10, "slots 1 and 2 swapped");
    expect(wb.focused_index() == 2, "focused index tracked moved slot to 2");

    // Close
    expect(wb.close(0), "close slot 0 succeeds");
    expect(wb.size() == 3, "size is now 3");
    expect(wb.focused_index() == 1, "focused index adjusted down after close");

    // 5. PointerRouter component
    PointerRouter router;
    int background_clicks = 0;
    int modal_clicks = 0;

    router.add_target(ScreenRect{.origin = {.x = 0, .y = 0}, .size = {.width = 80, .height = 24}},
                      0, "background", [&](const PointerEvent&) {
                          ++background_clicks;
                          return true;
                      });

    router.add_target(ScreenRect{.origin = {.x = 20, .y = 5}, .size = {.width = 40, .height = 10}},
                      10, "modal", [&](const PointerEvent&) {
                          ++modal_clicks;
                          return true;
                      });

    expect(router.target_count() == 2, "router has 2 targets");

    // Click outside modal (at 5, 2)
    expect(router.dispatch(PointerEvent{.pos = {.x = 5, .y = 2}}),
           "event dispatched outside modal");
    expect(background_clicks == 1 && modal_clicks == 0, "only background received click");

    // Click inside modal (at 25, 7)
    expect(router.dispatch(PointerEvent{.pos = {.x = 25, .y = 7}}),
           "event dispatched inside modal");
    expect(background_clicks == 1 && modal_clicks == 1,
           "modal with higher z-order intercepted click");

    // Click completely out of screen bounds
    expect(!router.dispatch(PointerEvent{.pos = {.x = 90, .y = 30}}),
           "out-of-bounds event rejected");
}

class TestModal : public simrv::tui::framework::IModal {
   public:
    [[nodiscard]] auto title() const -> std::string override { return "Test Custom Modal"; }
    void render_content(std::vector<std::string>& content_rows, int /*width*/,
                        int /*height*/) override {
        content_rows.push_back("Custom Modal Body Content");
        content_rows.push_back("[Enter] Submit  [Esc] Close");
    }
    auto handle_key(uint8_t byte, simrv::tui::TuiKey key)
        -> simrv::tui::framework::ModalAction override {
        if (byte == 'q' || key == simrv::tui::TuiKey::Esc)
            return simrv::tui::framework::ModalAction::Close;
        if (key == simrv::tui::TuiKey::Enter) return simrv::tui::framework::ModalAction::Submit;
        return simrv::tui::framework::ModalAction::Continue;
    }
    auto handle_click(simrv::tui::framework::ScreenCoord local_pt)
        -> simrv::tui::framework::ModalAction override {
        if (local_pt.y == 2) return simrv::tui::framework::ModalAction::Submit;
        return simrv::tui::framework::ModalAction::Continue;
    }
};

void test_modal_host_and_custom_dialog() {
    using namespace simrv::tui;
    using namespace simrv::tui::framework;

    // 1. Pure ModalHost testing
    ModalHost host;
    expect(!host.is_active(), "host is initially inactive");
    expect(host.modal() == nullptr, "host has no modal");

    host.open(std::make_unique<TestModal>());
    expect(host.is_active(), "host is active after open");
    expect(host.modal() != nullptr, "host has modal");
    expect(host.modal()->title() == "Test Custom Modal", "title matches");

    // Key handling: Continue
    auto act = host.handle_key('a', TuiKey::None);
    expect(act == ModalAction::Continue, "regular key continues");
    expect(host.is_active(), "host remains active");

    // Click handling: Submit
    auto click_act = host.handle_click(ScreenCoord{.x = 5, .y = 2});
    expect(click_act == ModalAction::Submit, "clicking row 2 submits");
    expect(host.is_active(), "host remains active on submit");

    // Key handling: Close
    act = host.handle_key('q', TuiKey::None);
    expect(act == ModalAction::Close, "q closes modal");
    expect(!host.is_active(), "host is closed after Close action");

    // 2. Integration with TuiModal
    simrv::core::Machine machine;
    TuiModal modal(machine);
    expect(!modal.is_active(), "TuiModal starts inactive");

    modal.open_custom_modal(std::make_unique<TestModal>());
    expect(modal.is_active(), "TuiModal is active with custom modal");
    expect(modal.active_modal_impl() != nullptr, "active_modal_impl is non-null");

    std::vector<std::string> lines(30, std::string(80, ' '));
    modal.render_overlay(lines, 80, 24);

    bool found_title = false;
    bool found_body = false;
    for (const auto& line : lines) {
        if (line.find("Test Custom Modal") != std::string::npos) found_title = true;
        if (line.find("Custom Modal Body Content") != std::string::npos) found_body = true;
    }
    expect(found_title, "custom modal title was rendered in overlay");
    expect(found_body, "custom modal body content was rendered in overlay");

    // Click inside modal box (y=12 is inside the centered box)
    auto click_res = modal.handle_click(40, 12, 80, 24);
    expect(click_res == TuiModal::ModalClickResult::Handled,
           "click inside custom modal was handled");

    // Click outside box closes modal
    click_res = modal.handle_click(1, 1, 80, 24);
    expect(click_res == TuiModal::ModalClickResult::Closed, "click outside custom modal closed it");
    expect(!modal.is_active(), "modal is inactive after outside click");
}

}  // namespace

int main() {
    (void)setenv("SIMRV_TUI_PREFERENCES", "off", 1);
    test_uart_search_ctrl_f_and_escape_cancel();
    test_tui_preferences_round_trip();
    test_density_resolution();
    test_terminal_controls();
    test_terminal_scrollback_and_selection();
    test_utf8_and_theme_helpers();
    test_key_registry();
    test_cleanup_input_boundaries();
    test_mirrored_modal_arrows();
    test_page_guidance();
    test_classroom_cli_defaults();
    test_load_modal_path_completion();
    test_control_flow_calls_mission();
    test_drawer_stack_layout();
    test_tui_running_state_synchronization();
    test_machine_releases_tui_sinks_before_runtime();
    test_inspection_report();
    test_help_uses_canonical_registry();
    test_category_groups_and_glossary();
    test_sysconfig_modal_modes();
    test_cpu_config_reload_confirmation_and_slow_mode();
    test_input_routing();
    test_responsive_layout();
    test_frame_composition();
    test_themes_and_mouse_interactions();
    test_log_buffer_wrapping();
    test_modal_components();
    test_instruction_explainer_is_side_effect_free();
    test_instruction_explainer_and_csr_metadata();
    test_inspector_panels_traits_and_scoreboard();
    test_multicolumn_refinement();
    test_horizontal_scrolling();
    test_stack_vertical_scrolling();
    test_flight_recorder_merge_and_wraparound();
    test_bus_inspector_and_tilelink_channels();
    test_inspector_vector_csr_rows();
    test_memory_inspector_and_custom_address();
    test_responsive_labels_and_header();
    test_tui_differential_rendering_and_throttling();
    test_multi_column_workbench_tools_and_swapping();
    test_multi_column_panel_management_and_modal_usability();
    test_sixel_display_modal_clearing();
    test_sixel_fluid_framebuffer_updates();
    test_virtio_input_and_display_mouse();
    test_concurrent_terminal_and_sixel_display();
    test_terminal_cursor_tracking_and_scrollback();
    test_sixel_display_resolution_adaptation_and_size_warning();
    test_memory_modal_and_hart_modal();
    test_rc15_features();
    test_formalized_registry_and_presets();
    test_formalized_scroll_framework();
    test_formalized_framework_primitives();
    test_modal_host_and_custom_dialog();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "TUI framework tests passed\n";
    return EXIT_SUCCESS;
}
