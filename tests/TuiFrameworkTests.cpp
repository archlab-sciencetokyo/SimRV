#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>

#include "simrv/core/Machine.hpp"
#include "simrv/tui/LogBuffer.hpp"
#include "simrv/tui/TuiFrameRenderer.hpp"
#include "simrv/tui/TuiGuidance.hpp"
#include "simrv/tui/TuiInputRouter.hpp"
#include "simrv/tui/TuiKeybindings.hpp"
#include "simrv/tui/TuiLayoutPolicy.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/VirtualTerminal.hpp"
#include "simrv/tui/modals/GlossaryModal.hpp"
#include "simrv/tui/modals/HelpModal.hpp"
#include "simrv/tui/modals/ModalComponents.hpp"
#include "simrv/tui/modals/SettingsModal.hpp"
#include "simrv/tui/modals/SystemConfigModal.hpp"

namespace simrv::core {
void Machine::request_reboot() {}
}  // namespace simrv::core

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
    terminal.write_string("abc\rZ\n12\tX");
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

    terminal.resize(6, 3);
    expect(terminal.get_cursor_x() < 6 && terminal.get_cursor_y() < 3,
           "resize keeps the cursor in bounds");
    terminal.write_string("\033c");
    expect(terminal.get_cursor_x() == 0 && terminal.get_cursor_y() == 0,
           "terminal reset restores the cursor origin");
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

    simrv::tui::set_tui_theme(simrv::tui::TuiTheme::HighContrast);
    expect(simrv::tui::is_high_contrast(), "high-contrast theme updates shared state");
    simrv::tui::set_tui_theme(simrv::tui::TuiTheme::Adaptive);
    expect(!simrv::tui::is_high_contrast(), "adaptive theme clears high contrast");
}

void test_key_registry() {
    const auto bindings = simrv::tui::Keybindings::all();
    expect(bindings.size() == 27, "all key actions have registry entries");
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

    constexpr simrv::tui::TuiFooterAction footer_actions[] = {
        simrv::tui::TuiFooterAction::Step,
        simrv::tui::TuiFooterAction::StepBack,
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
        simrv::tui::TuiFooterAction::CycleLayout,
        simrv::tui::TuiFooterAction::ToggleLearn,
        simrv::tui::TuiFooterAction::TogglePanel,
        simrv::tui::TuiFooterAction::ToggleTrace,
        simrv::tui::TuiFooterAction::OpenSettings,
        simrv::tui::TuiFooterAction::ConfigureMisa,
        simrv::tui::TuiFooterAction::ConfigureSystem,
        simrv::tui::TuiFooterAction::ManageBreakpoints,
        simrv::tui::TuiFooterAction::Reboot,
        simrv::tui::TuiFooterAction::SwitchHart,
        simrv::tui::TuiFooterAction::ToggleTheme,
        simrv::tui::TuiFooterAction::ToggleDebug,
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
    ActionContext paused{.paused = true,
                         .image_loaded = true,
                         .debug_mode = true,
                         .cycle_accurate = true,
                         .rollback_enabled = true};
    expect(simrv::tui::Keybindings::is_available(KeyAction::Step, paused),
           "step is available for a paused loaded image");
    auto running = paused;
    running.paused = false;
    expect(!simrv::tui::Keybindings::is_available(KeyAction::InspectAddress, running),
           "inspection requires a paused machine");
    expect(!simrv::tui::Keybindings::is_available(KeyAction::ToggleLearn, running),
           "guided inspection can only be changed while navigating the paused TUI");
    expect(simrv::tui::Keybindings::get(KeyAction::ToggleLearn).primary_char == 'g',
           "guided inspection uses the canonical g binding");
    expect(simrv::tui::Keybindings::unavailable_reason(KeyAction::InspectAddress, running) ==
               "Pause the simulator first",
           "disabled actions explain how to become available");
    auto functional = paused;
    functional.cycle_accurate = false;
    expect(simrv::tui::Keybindings::is_available(KeyAction::ConfigureSystem, functional),
           "system configuration is available in functional mode");
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

    SettingsModal::move_cursor(s_cursor, 5);
    expect(s_cursor == 5, "Settings modal advances cursor to TUI Target Refresh Rate");

    SettingsModal::adjust_setting(settings_draft, 5, 1);
    expect(settings_draft.tui_fps == 60, "Settings modal cycles TUI FPS to 60");

    SettingsModal::adjust_setting(settings_draft, 6, 3);
    expect(settings_draft.num_harts == 4, "Settings modal adjusts SMP active core count");

    SettingsModal::adjust_setting(settings_draft, 7, 1);
    expect(settings_draft.smp_multithreaded == true, "Settings modal toggles SMP worker threads");

    SettingsModal::adjust_setting(settings_draft, 8, 1);
    expect(settings_draft.smp_quantum == 2500, "Settings modal advances SMP quantum level");

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
                                    TuiRegPage::TRACE,    TuiRegPage::EXPLAIN, TuiRegPage::STACK};
    for (auto page : pages) {
        auto const guidance = simrv::tui::guidance_for_page(page, true);
        expect(!guidance.title.empty(), "each inspection page has a guidance title");
        expect(!guidance.meaning.empty(), "each inspection page explains its meaning");
        expect(!guidance.relationship.empty(), "each inspection page links state to execution");
        expect(!simrv::tui::Keybindings::get(guidance.next_action).key_display.empty(),
               "each inspection page links to a canonical action");
    }
    expect(!simrv::tui::should_show_guidance(true, false, 24),
           "guidance is hidden by default until learn mode is enabled");
    expect(simrv::tui::should_show_guidance(true, true, 24),
           "learn mode shows guidance while paused when space permits");
    expect(!simrv::tui::should_show_guidance(false, true, 24),
           "guidance does not replace guest-terminal content while running");
    expect(!simrv::tui::should_show_guidance(true, true, 15),
           "architectural data wins over guidance in short terminals");
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
    expect(route_input(0x01, focused) == InputRoute::Guest,
           "Ctrl-A is passed through when the guest is running");

    const InputContext paused{.modal_active = false, .paused = true};
    expect(route_input('\r', paused) == InputRoute::Navigation,
           "Enter is a navigation action while paused");
    const InputContext modal{.modal_active = true, .paused = true};
    expect(route_input('\r', modal) == InputRoute::Modal, "Enter submits the active modal");
    expect(route_input(0x01, modal) == InputRoute::Modal, "Ctrl-A has no global binding");
    expect(route_input(0x11, modal) == InputRoute::Quit, "Ctrl-Q remains globally available");
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
    expect(desktop.left == 63 && desktop.right == 74,
           "desktop split keeps the intended default inspection pane");
    const auto constrained = calculate_pane_widths(80, TuiLayout::Split, 200);
    expect(constrained.right == 20, "manual resizing preserves a usable guest terminal");

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
        {80, 24, TuiLayout::Split, 12464046866688151874ULL},
        {120, 32, TuiLayout::Split, 12126785168693464750ULL},
        {160, 48, TuiLayout::Split, 3975935784378917810ULL},
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

}  // namespace

int main() {
    test_terminal_controls();
    test_terminal_scrollback_and_selection();
    test_utf8_and_theme_helpers();
    test_key_registry();
    test_page_guidance();
    test_help_uses_canonical_registry();
    test_category_groups_and_glossary();
    test_sysconfig_modal_modes();
    test_input_routing();
    test_responsive_layout();
    test_frame_composition();
    test_themes_and_mouse_interactions();
    test_log_buffer_wrapping();
    test_modal_components();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "TUI framework tests passed\n";
    return EXIT_SUCCESS;
}
