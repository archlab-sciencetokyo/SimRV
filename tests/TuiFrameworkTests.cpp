#include <cstdlib>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>

#include "simrv/tui/TuiGuidance.hpp"
#include "simrv/tui/TuiInputRouter.hpp"
#include "simrv/tui/TuiKeybindings.hpp"
#include "simrv/tui/TuiLayoutPolicy.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/VirtualTerminal.hpp"

namespace {

auto failures = 0;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

auto plain_line(const simrv::tui::VirtualTerminal& terminal, int row, int width) -> std::string {
    const std::string rendered = terminal.get_line_as_string(row, width);
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
    expect(simrv::tui::make_repeated_string("═", 3) == "═══", "border repetition is exact");

    simrv::tui::set_tui_theme(simrv::tui::TuiTheme::HighContrast);
    expect(simrv::tui::is_high_contrast(), "high-contrast theme updates shared state");
    simrv::tui::set_tui_theme(simrv::tui::TuiTheme::Adaptive);
    expect(!simrv::tui::is_high_contrast(), "adaptive theme clears high contrast");
}

void test_key_registry() {
    const auto bindings = simrv::tui::Keybindings::all();
    expect(bindings.size() == 22, "all key actions have registry entries");
    std::set<simrv::tui::KeyAction> actions;
    for (const auto& binding : bindings) {
        actions.insert(binding.action);
        expect(!binding.key_display.empty(), "key display is not empty");
        expect(!binding.help_label.empty(), "help description is not empty");
        expect(&simrv::tui::Keybindings::get(binding.action) == &binding,
               "action lookup returns the canonical registry entry");
    }
    expect(actions.size() == bindings.size(), "key actions are unique");

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
    expect(simrv::tui::Keybindings::unavailable_reason(KeyAction::InspectAddress, running) ==
               "Pause the simulator first",
           "disabled actions explain how to become available");
    auto functional = paused;
    functional.cycle_accurate = false;
    expect(!simrv::tui::Keybindings::is_available(KeyAction::ConfigureSystem, functional),
           "cycle configuration is unavailable in functional mode");
    auto modal = paused;
    modal.modal_active = true;
    expect(simrv::tui::Keybindings::is_available(KeyAction::Quit, modal),
           "quit remains available over a modal");
    expect(!simrv::tui::Keybindings::is_available(KeyAction::Step, modal),
           "modal input does not leak into simulation controls");
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
    expect(route_input(0x1B, modal) == InputRoute::ControlSequence,
           "Escape is parsed before modal dispatch");
    expect(normalize_guest_terminal_byte('\r') == '\n',
           "host carriage return becomes the guest console line delimiter");
    expect(normalize_guest_terminal_byte('x') == 'x', "ordinary guest input is unchanged");
}

void test_responsive_layout() {
    using simrv::tui::calculate_pane_widths;
    using simrv::tui::TuiLayout;

    const auto narrow = calculate_pane_widths(40, TuiLayout::Split);
    expect(narrow.left + narrow.right == 37, "narrow split geometry accounts for all borders");
    expect(narrow.left > 0 && narrow.right > 0, "narrow resize keeps both panes visible");

    const auto desktop = calculate_pane_widths(140, TuiLayout::Split);
    expect(desktop.left == 56 && desktop.right == 81,
           "desktop split keeps the intended forty-percent inspection pane");
    const auto constrained = calculate_pane_widths(80, TuiLayout::Split, 200);
    expect(constrained.right == 20, "manual resizing preserves a usable guest terminal");

    const auto full_right = calculate_pane_widths(80, TuiLayout::FullRight, 60);
    expect(full_right.left == 0 && full_right.right == 78,
           "full-right resize ignores stale split-pane width");
}

}  // namespace

int main() {
    test_terminal_controls();
    test_terminal_scrollback_and_selection();
    test_utf8_and_theme_helpers();
    test_key_registry();
    test_page_guidance();
    test_input_routing();
    test_responsive_layout();
    if (failures != 0) return EXIT_FAILURE;
    std::cout << "TUI framework tests passed\n";
    return EXIT_SUCCESS;
}
