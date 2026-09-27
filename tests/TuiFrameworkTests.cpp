#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

#include "simrv/core/Machine.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/pipeline/OperationInfo.hpp"
#include "simrv/pipeline/Scoreboard.hpp"
#include "simrv/tui/InspectionReport.hpp"
#include "simrv/tui/LogBuffer.hpp"
#include "simrv/tui/TuiFrameRenderer.hpp"
#include "simrv/tui/TuiGuidance.hpp"
#include "simrv/tui/TuiInputRouter.hpp"
#include "simrv/tui/TuiKeybindings.hpp"
#include "simrv/tui/TuiLayoutPolicy.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/VirtualTerminal.hpp"
#include "simrv/tui/framework/Layout.hpp"
#include "simrv/tui/modals/AddressModal.hpp"
#include "simrv/tui/modals/GlossaryModal.hpp"
#include "simrv/tui/modals/HelpModal.hpp"
#include "simrv/tui/modals/ModalComponents.hpp"
#include "simrv/tui/modals/SettingsModal.hpp"
#include "simrv/tui/modals/SystemConfigModal.hpp"
#include "simrv/tui/modals/ToolPickerModal.hpp"
#include "simrv/tui/panels/InspectorPane.hpp"
#include "simrv/tui/panels/StatusBar.hpp"
#include "simrv/tui/panels/TerminalPane.hpp"
#include "simrv/util/CliParser.hpp"

namespace simrv::tui {
struct TuiTestAccess {
    static auto arrow(Tui& tui, std::string_view sequence) -> bool {
        tui.esc_buf_ = sequence;
        return tui.handle_arrow_key_sequence();
    }
    static auto modal(Tui& tui) -> TuiModal& { return tui.modal_; }
    static void set_cached_term_width(Tui& tui, int w) { tui.cached_term_width_ = w; }
    static void set_cached_term_height(Tui& tui, int h) { tui.cached_term_height_ = h; }
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
        return tui.workbench_slots_;
    }
    static auto layout(const Tui& tui) -> TuiLayout { return tui.layout_; }
    static auto status_bar(const Tui& tui) -> StatusBar* { return tui.status_bar_.get(); }
    static auto handle_modal_key(Tui& tui, uint8_t byte, TuiKey key) -> bool {
        return tui.handle_modal_keyboard_input(byte, key);
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
    static auto selection(const Tui& tui) -> const SelectionState& { return tui.selection_; }
    static void set_focused_slot(Tui& tui, size_t slot) { tui.focused_slot_index_ = slot; }
    static auto handle_nav_key(Tui& tui, uint8_t byte, TuiKey key) -> bool {
        return tui.handle_navigation_keyboard_input(byte, key);
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
    expect(bindings.size() == 36, "all key actions have registry entries");
    std::set<simrv::tui::KeyAction> actions;
    std::set<char> claimed_chars;
    for (const auto& binding : bindings) {
        actions.insert(binding.action);
        expect(!binding.key_display.empty(), "key display is not empty");
        expect(!binding.help_label.empty(), "help description is not empty");
        expect(&simrv::tui::Keybindings::get(binding.action) == &binding,
               "action lookup returns the canonical registry entry");
        if (binding.primary_char != '\0') {
            expect(claimed_chars.insert(binding.primary_char).second,
                   "canonical primary bindings do not collide");
        }
        if (binding.alt_char != '\0' && binding.alt_char != binding.primary_char) {
            expect(claimed_chars.insert(binding.alt_char).second,
                   "canonical alternate bindings do not collide");
        }
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
        simrv::tui::TuiFooterAction::ToggleStudentGuide,
        simrv::tui::TuiFooterAction::TogglePanel,
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
    ActionContext paused{.paused = true, .image_loaded = true, .cycle_accurate = true};
    expect(simrv::tui::Keybindings::is_available(KeyAction::Step, paused),
           "step is available for a paused loaded image");
    auto running = paused;
    running.paused = false;
    expect(!simrv::tui::Keybindings::is_available(KeyAction::InspectAddress, running),
           "inspection requires a paused machine");
    expect(!simrv::tui::Keybindings::is_available(KeyAction::ToggleStudentGuide, running),
           "the Student Guide can only be changed while navigating the paused TUI");
    const auto& guide_binding = simrv::tui::Keybindings::get(KeyAction::ToggleStudentGuide);
    expect(guide_binding.primary_char == 'g' && guide_binding.footer_label == "[g] Guide" &&
               guide_binding.help_label.contains("Student Guide"),
           "the Student Guide has one mnemonic canonical binding and name");
    expect(simrv::tui::Keybindings::get(KeyAction::ActivateStudentGuide).key_display == "[Enter]",
           "performing the guide suggestion has a canonical Enter binding");
    expect(!simrv::tui::Keybindings::is_available(KeyAction::ActivateStudentGuide, paused),
           "Enter retains its normal meaning while the Student Guide is hidden");
    paused.student_guide_enabled = true;
    expect(simrv::tui::Keybindings::is_available(KeyAction::ActivateStudentGuide, paused),
           "the guide action becomes available when the paused guide is visible");
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

    // Test TUI FPS & SMP configuration in SettingsModal
    SettingsDraft settings_draft;
    settings_draft.tui_fps = 30;
    settings_draft.num_harts = 1;
    settings_draft.smp_quantum = 1000;
    settings_draft.smp_multithreaded = false;
    int s_cursor = 0;

    SettingsModal::move_cursor(s_cursor, 3);
    expect(s_cursor == 3, "Settings modal advances cursor to TUI Target Refresh Rate");

    SettingsModal::adjust_setting(settings_draft, 3, 1);
    expect(settings_draft.tui_fps == 60, "Settings modal cycles TUI FPS to 60");

    SettingsModal::adjust_setting(settings_draft, 4, 3);
    expect(settings_draft.num_harts == 4, "Settings modal adjusts SMP active core count");

    SettingsModal::adjust_setting(settings_draft, 5, 1);
    expect(settings_draft.smp_multithreaded == true, "Settings modal toggles SMP worker threads");

    SettingsModal::adjust_setting(settings_draft, 6, 1);
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
    mode_draft.cycle_accurate = true;
    mode_draft.sys_config.cycle_accurate = true;
    expect(SettingsModal::submit(mode_draft, live_mode_machine, [](simrv::tui::TuiRegPage) {}),
           "Settings modal accepts live switch to Cycle-Accurate mode");
    expect(!live_mode_machine.reboot_requested,
           "Switching to cycle-accurate mode in settings does NOT reboot the machine");
    expect(live_mode_machine.runtime_profile.is_cycle_mode(),
           "Machine runtime profile successfully updated to cycle-accurate mode");

    mode_draft.cycle_accurate = false;
    mode_draft.sys_config.cycle_accurate = false;
    expect(SettingsModal::submit(mode_draft, live_mode_machine, [](simrv::tui::TuiRegPage) {}),
           "Settings modal accepts live switch to Instruction-Accurate mode");
    expect(!live_mode_machine.reboot_requested,
           "Switching to instruction-accurate mode in settings does NOT reboot the machine");
    expect(live_mode_machine.runtime_profile.is_instruction_mode(),
           "Machine runtime profile successfully updated to instruction mode");

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

void test_page_guidance() {
    using simrv::tui::TuiRegPage;
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
    expect(!simrv::tui::should_show_guidance(true, false, 24),
           "guidance is hidden by default until the Student Guide is enabled");
    expect(simrv::tui::should_show_guidance(true, true, 24),
           "the Student Guide is visible while paused when space permits");
    expect(!simrv::tui::should_show_guidance(false, true, 24),
           "guidance does not replace guest-terminal content while running");
    expect(!simrv::tui::should_show_guidance(true, true, 15),
           "architectural data wins over guidance in short terminals");

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
    std::array<std::string, 7> storage = {"SimRV",       "--tui", "--class", "--inspection-output",
                                          "report.json", "-m",    "demo.elf"};
    std::array<char*, 7> arguments{};
    for (size_t index = 0; index < storage.size(); ++index) {
        arguments[index] = storage[index].data();
    }
    auto parsed = simrv::util::parse_command_line(arguments);
    expect(parsed.has_value(), "classroom CLI options parse together");
    if (!parsed) return;
    auto config = parsed->options.to_machine_config();
    expect(config.tui.enabled && config.tui.class_mode,
           "classroom CLI selects the guided TUI profile");
    expect(config.tui.inspection_output == "report.json",
           "inspection output path reaches typed machine configuration");
    simrv::core::Machine machine(config);
    simrv::tui::Tui tui(machine);
    expect(tui.is_student_guide_enabled(),
           "classroom mode starts the interactive Student Guide enabled");

    simrv::core::MachineConfig empty_config{};
    empty_config.tui.enabled = true;
    empty_config.tui.class_mode = true;
    simrv::core::Machine empty_machine(empty_config);
    simrv::tui::Tui empty_tui(empty_machine);
    empty_tui.activate_student_guide_suggestion();
    expect(empty_tui.get_active_modal() == simrv::tui::ModalType::LoadBinary,
           "the Student Guide can execute its context-sensitive suggestion");

    const auto mission_path = (std::filesystem::path(__FILE__).parent_path().parent_path() /
                               "examples/isa/lessons/control-flow-calls.mission")
                                  .string();
    std::array<std::string, 7> mission_storage = {
        "SimRV", "--tui", "--class", "--mission", mission_path, "-m", "control-flow-calls.elf"};
    std::array<char*, 7> mission_args{};
    for (size_t index = 0; index < mission_storage.size(); ++index) {
        mission_args[index] = mission_storage[index].data();
    }
    const auto mission_parsed = simrv::util::parse_command_line(mission_args);
    expect(mission_parsed.has_value() &&
               mission_parsed->options.to_machine_config().tui.mission == mission_path,
           "mission CLI selects an external classroom mission");

    std::array<std::string, 6> invalid_storage = {"SimRV",      "--class", "--mission",
                                                  mission_path, "-m",      "demo.elf"};
    std::array<char*, 6> invalid_args{};
    for (size_t index = 0; index < invalid_storage.size(); ++index) {
        invalid_args[index] = invalid_storage[index].data();
    }
    expect(!simrv::util::parse_command_line(invalid_args).has_value(),
           "mission rejects a non-TUI classroom invocation");
}

void test_control_flow_calls_mission() {
    simrv::tui::MissionProgress mission;
    const auto lesson = (std::filesystem::path(__FILE__).parent_path().parent_path() /
                         "examples/isa/lessons/control-flow-calls.mission")
                            .string();
    mission.configure(lesson, "/tmp/control-flow-calls.elf");
    expect(mission.enabled() && mission.step() == 0, "mission starts for its bundled ELF");
    expect(mission.guidance()->title.contains("Conditional branch"),
           "mission starts at branch objective");
    mission.observe_symbol("mission_loop");
    expect(mission.step() == 0, "out-of-order checkpoints do not advance a mission");
    for (const auto* checkpoint : {"mission_branch", "mission_loop", "mission_before_call",
                                   "mission_in_callee", "mission_after_call", "mission_done"}) {
        mission.observe_symbol(checkpoint);
    }
    expect(mission.completed() && mission.step() == 6, "ordered checkpoints complete the mission");
    expect(mission.guidance()->title.contains("complete"),
           "completed mission provides review guidance");
    mission.restart();
    expect(mission.enabled() && mission.step() == 0, "mission restart resets local progress");
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
}

void test_tui_running_state_synchronization() {
    simrv::core::MachineConfig config{};
    config.tui.enabled = true;
    simrv::core::Machine machine(config);
    expect(machine.initialize().has_value(), "machine initializes successfully with TUI enabled");
    auto tui = std::make_shared<simrv::tui::Tui>(machine);
    machine.set_telemetry_sink(tui);
    machine.set_console_sink(tui);
    expect(machine.execution_state() == simrv::core::ExecutionState::Paused,
           "machine starts in Paused state when TUI is enabled");
    expect(machine.is_paused(), "machine.is_paused() returns true on startup with TUI");
    expect(tui->is_paused(), "tui.is_paused() is true on startup");
    tui->set_paused(false);
    expect(!tui->is_paused(), "tui.is_paused() is false after unpausing");
    expect(machine.execution_state() == simrv::core::ExecutionState::Running,
           "machine execution state is Running after unpausing TUI");
    expect(!machine.is_paused(), "machine.is_paused() is false after unpausing");

    tui->set_paused(true);
    expect(tui->is_paused(), "tui.is_paused() is true after pausing");
    expect(machine.execution_state() == simrv::core::ExecutionState::Paused,
           "machine execution state is Paused after pausing TUI");
    expect(machine.is_paused(), "machine.is_paused() is true after pausing");
    machine.set_telemetry_sink(nullptr);
    machine.set_console_sink(nullptr);
}

void test_inspection_report() {
    simrv::core::MachineConfig config{};
    config.execution.num_harts = 2;
    config.tui.inspection_output = "/tmp/simrv-inspection-report-test.json";
    simrv::core::Machine machine(config);
    machine.add_hart_for_testing(std::make_unique<simrv::core::CPU>());
    machine.hart(1).state().pc = 0x80000100;
    machine.hart(1).state().regs.write(RegId::A0, 42);
    machine.hart(1).pipeline_context.op_id = simrv::isa::OperationId::ADDI;
    machine.hart(1).pipeline_context.ir_org = 0x02A00513;

    const std::vector<std::string> trace{"0x80000100: addi a0, zero, 42"};
    const std::string report = simrv::tui::make_inspection_report(machine, 1, trace);
    expect(report.contains("\"schema_version\": 1"), "inspection report is versioned");
    expect(report.contains("\"selected_hart\": 1"), "inspection report selects requested hart");
    const std::string expected_a0 = simrv::xlen::kIsXLen64 ? "0x000000000000002a" : "0x0000002a";
    expect(report.contains(expected_a0), "inspection report captures GPR state");
    expect(report.contains("addi a0, zero, 42"), "inspection report captures recent trace");
    expect(!report.contains("student") && !report.contains("grade"),
           "inspection report contains no identity or grading fields");

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
    expect(!simrv::tui::should_show_guidance(true, true, 24, 55),
           "guided text stays hidden when the inspector is too narrow to read it");
    expect(simrv::tui::should_show_guidance(true, true, 24, 56),
           "guided text appears at the readable inspector width threshold");

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
        "linux-images/rv64/fw_payload.elf\033[0m");

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
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    machine.set_ram_for_testing(ram);
    expect(machine.ram_view().span().size() == ram.size(), "RamView span matches backing size");
    auto& cpu = machine.primary_hart();
    cpu.machine_ = &machine;
    cpu.reset();
    constexpr Address pc = simrv::memory::kDramBaseAddress;
    constexpr Instruction addi = 0x00100093U;
    std::memcpy(ram.data(), &addi, sizeof(addi));
    cpu.state().pc = pc;
    cpu.pipeline_context.ir = 0xDEADBEEFU;
    cpu.pipeline_context.cpc = VirtAddr{pc + 0x40};

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

void test_inspector_panels_traits_and_scoreboard() {
    simrv::core::Machine machine;
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    auto& cpu = machine.primary_hart();
    cpu.machine_ = &machine;
    cpu.reset();

    // 1. Project authoritative pipeline slots into the sampled TUI scoreboard.
    machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
    cpu.ca_pipeline.execute->valid = true;
    cpu.ca_pipeline.execute->writes_int = true;
    cpu.ca_pipeline.execute->wb_dest = static_cast<RegId>(1);
    cpu.ca_pipeline.execute->remaining_latency = 2;
    cpu.ca_pipeline.memory->valid = true;
    cpu.ca_pipeline.memory->writes_int = true;
    cpu.ca_pipeline.memory->wb_dest = static_cast<RegId>(2);
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
    std::vector<Byte> smp_ram(1024 * 1024, Byte{0});
    smp_machine.set_ram_for_testing(smp_ram.data(), smp_ram.size());
    smp_machine.runtime_profile.engine = simrv::core::ExecutionEngine::CycleFast;
    auto& secondary = smp_machine.hart(1);
    secondary.ca_pipeline.writeback->valid = true;
    secondary.ca_pipeline.writeback->writes_fp = true;
    secondary.ca_pipeline.writeback->wb_dest = static_cast<RegId>(3);
    secondary.ca_pipeline.writeback->wb_valid = true;
    for (const auto pipeline_type :
         {simrv::pipeline::PipelineType::ThreeStage, simrv::pipeline::PipelineType::FiveStage}) {
        secondary.pipeline_sim.config.pipeline_type = pipeline_type;
        smp_machine.publish_tui_execution_snapshot_for_testing();
        const auto primary_snapshot = smp_machine.tui_execution_snapshot(0);
        const auto secondary_snapshot = smp_machine.tui_execution_snapshot(1);
        expect(!primary_snapshot.scoreboard.is_busy(simrv::pipeline::operation::RegBank::Float,
                                                    static_cast<RegId>(3)),
               "scoreboard projection remains isolated by hart");
        expect(secondary_snapshot.scoreboard.can_forward(simrv::pipeline::operation::RegBank::Float,
                                                         static_cast<RegId>(3)),
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
    std::array<Byte, 4096> backing{};
    machine.set_ram_for_testing(backing.data(), backing.size());
    machine.memory().initialize_mmu();

    auto& cpu = machine.primary_hart();
    cpu.state().regs.write(simrv::RegId::Sp, 0x80000400);

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
    pane.set_student_guide_enabled(false);

    const int width = 88;
    // Row 0 and 1 are tier 1 and tier 2 tab bars; logical_row 16 is row_idx 18
    const std::string r16 = strip_ansi(pane.render_row(18, width));
    expect(r16.find("TileLink-C Channels") != std::string::npos,
           "row 18 contains TileLink-C channel section header");

    const std::string r17 = strip_ansi(pane.render_row(19, width));
    expect(r17.find("A(Req)") != std::string::npos && r17.find("B(Probe)") != std::string::npos,
           "row 19 contains channel status indicators");

    const std::string r18 = strip_ansi(pane.render_row(20, width));
    expect(r18.find("Recent Bus Transactions") != std::string::npos,
           "row 20 contains Recent Bus Transactions header");

    const std::string r19 = strip_ansi(pane.render_row(21, width));
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
    std::array<Byte, 4096> backing{};
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
    const Address target_addr = 0x80000000;
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
    ctx.rs1 = static_cast<RegId>(5);  // t0
    if constexpr (sizeof(Register) > 4) {
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
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.primary_hart().reset();

    constexpr Instruction addi_inst = 0x00100093;  // addi x1, x0, 1
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
    tui.set_target_fps(30);  // 33ms interval

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
    expect(tools.size() == 13, "tool picker provides 13 selectable workbench tools");
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
    expect(!simrv::tui::modals::ToolPickerModal::find_by_accelerator('q').has_value(),
           "accelerator 'q' is not bound to a tool (reserved for exit/close)");

    // 2. Tui workbench slot swapping and movement
    simrv::core::Machine machine;
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
    // TRACE (tool index 10) is at content row 16; terminal Y: start_y (3) + 1 (border) + 16
    // (content) + 1 (1-indexed) = 21
    auto click_res = modal.handle_click(60, 21, 120, 30);
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

    // 6. Pipeline menu suppression in functional mode
    tui.set_workbench_slot_page(0, simrv::tui::TuiRegPage::PIPELINE);
    tui.handle_mouse(10, 6, 0);  // click in body of slot 0 to focus it
    expect(tui.focused_slot() == 0, "slot 0 is focused");

    auto* inspector = simrv::tui::TuiTestAccess::inspector(tui);
    expect(inspector != nullptr, "inspector pane is initialized");
    expect(!inspector->has_tool_menu(simrv::tui::TuiRegPage::PIPELINE),
           "pipeline panel in functional mode has no tool menu");

    std::string hdr = inspector->render_column_header(0, "Pipeline", true, 46, "", false);
    expect(hdr.find("▼") == std::string::npos,
           "dropdown affordance is omitted when has_menu is false");
    expect(hdr.find("[Ctrl-W]") == std::string::npos,
           "tool hint is omitted when has_menu is false");

    // Click on focused pipeline header when only stages tool is available:
    tui.handle_mouse(10, 4, 0);
    expect(!tui.is_modal_active(),
           "clicking header of pipeline panel in functional mode does not open menu");

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
    simrv::tui::TuiTestAccess::consume_control_seq(tui, "\033[<0;10;5M");
    const auto& sel = simrv::tui::TuiTestAccess::selection(tui);
    expect(sel.content_start_y == 5, "selection content_start_y is 5 in 2-panel mode");
    expect(slots[0].page == simrv::tui::TuiRegPage::GPR, "row 5 click does not alter GPR page");
    tui.clear_selection();

    // Click on column 0 row 5 content: verify row 5 does not alter GPR page
    tui.handle_mouse(10, 5, 0);
    expect(slots[0].page == simrv::tui::TuiRegPage::GPR,
           "handle_mouse row 5 content preserves page");

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

    // Test wheel down on modal
    modal.handle_wheel(1);
    std::vector<std::string> scrolled_lines(24, std::string(80, ' '));
    modal.render_overlay(scrolled_lines, 80, 24);
    std::string scrolled_overlay;
    for (const auto& l : scrolled_lines) scrolled_overlay += l + '\n';
    expect(scrolled_overlay.find("▲ more") != std::string::npos,
           "top border displays ▲ more indicator after scrolling down");

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
    tui.add_workbench_column();
    expect(slots.size() == 2, "workbench has 2 columns");
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
    expect(slots[1].page != simrv::tui::TuiRegPage::GPR,
           "reverse cycle_reg_page moved away from GPR");
    tui.cycle_reg_page(false);
    expect(slots[1].page == simrv::tui::TuiRegPage::GPR,
           "forward cycle_reg_page moved back to GPR");

    tui.cycle_tool_page(true);
    expect(slots[1].page == simrv::tui::TuiRegPage::CONSOLE ||
               slots[1].page == simrv::tui::TuiRegPage::EXPLAIN,
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

    // Verify Ctrl-L is removed from navigation handling
    const auto layout_before = simrv::tui::TuiTestAccess::layout(tui);
    const bool handled_ctrl_l =
        simrv::tui::TuiTestAccess::handle_nav_key(tui, 12, simrv::tui::TuiKey::CtrlL);
    expect(!handled_ctrl_l, "Ctrl-L is not handled as a navigation command");
    expect(simrv::tui::TuiTestAccess::layout(tui) == layout_before, "Ctrl-L did not cycle layout");
}

}  // namespace

int main() {
    test_terminal_controls();
    test_terminal_scrollback_and_selection();
    test_utf8_and_theme_helpers();
    test_key_registry();
    test_cleanup_input_boundaries();
    test_mirrored_modal_arrows();
    test_page_guidance();
    test_classroom_cli_defaults();
    test_control_flow_calls_mission();
    test_tui_running_state_synchronization();
    test_inspection_report();
    test_help_uses_canonical_registry();
    test_category_groups_and_glossary();
    test_sysconfig_modal_modes();
    test_input_routing();
    test_responsive_layout();
    test_frame_composition();
    test_themes_and_mouse_interactions();
    test_log_buffer_wrapping();
    test_modal_components();
    test_instruction_explainer_is_side_effect_free();
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
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "TUI framework tests passed\n";
    return EXIT_SUCCESS;
}
