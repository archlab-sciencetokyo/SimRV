/**
 * @file TuiKeybindings.cpp
 * @brief Centralized TUI Keybindings Registry implementation.
 */
#include "simrv/tui/TuiKeybindings.hpp"
#include <array>
#include <stdexcept>

namespace simrv::tui {

static const std::array<KeyBindingInfo, 23> kKeyBindings = {{
    {.action = KeyAction::Step,               .key_display = "[n]",           .primary_char = 'n', .alt_char = 'N', .footer_label = "[n] Step",         .help_label = "Step N insts"},
    {.action = KeyAction::Backstep,           .key_display = "[b]",           .primary_char = 'b', .alt_char = 'B', .footer_label = "[b] Back",         .help_label = "Undo step / Backstep"},
    {.action = KeyAction::RunPause,           .key_display = "[c] / [Space]", .primary_char = 'c', .alt_char = 'C', .footer_label = "[Space] Run/Pause",.help_label = "Run / Pause simulation"},
    {.action = KeyAction::Reset,              .key_display = "[Ctrl-R]",      .primary_char = '\0',.alt_char = '\0',.footer_label = "[Ctrl-R] Reset",   .help_label = "Reset CPU / System"},
    {.action = KeyAction::SetBreakpoint,      .key_display = "[:]",           .primary_char = ':', .alt_char = 'k', .footer_label = "[:] SetBP",        .help_label = "Set PC Breakpoint"},
    {.action = KeyAction::SetWatchpoint,      .key_display = "[w]",           .primary_char = 'w', .alt_char = 'W', .footer_label = "[w] SetWP",        .help_label = "Set Watchpoint"},
    {.action = KeyAction::ManageBreakpoints,  .key_display = "[m]",           .primary_char = 'm', .alt_char = 'M', .footer_label = "[m] ManageBP",     .help_label = "Manage Break/Watchpoints"},
    {.action = KeyAction::TogglePcBreakpoint, .key_display = "[k]",           .primary_char = 'k', .alt_char = 'K', .footer_label = "[k] TogBP",        .help_label = "Toggle PC Breakpoint"},
    {.action = KeyAction::InspectAddress,     .key_display = "[i]",           .primary_char = 'i', .alt_char = 'I', .footer_label = "[i] Mem",          .help_label = "Inspect Memory Address"},
    {.action = KeyAction::SetStepSize,        .key_display = "[g]",           .primary_char = 'g', .alt_char = 'G', .footer_label = "[g] StepSize",     .help_label = "Set N Step Size"},
    {.action = KeyAction::SetSpeed,           .key_display = "[f]",           .primary_char = 'f', .alt_char = 'F', .footer_label = "[f] Speed",        .help_label = "Set Frequency (Hz)"},
    {.action = KeyAction::LoadBinary,         .key_display = "[o]",           .primary_char = 'o', .alt_char = 'O', .footer_label = "[o] Load",         .help_label = "Load Binary / Disk"},
    {.action = KeyAction::Settings,           .key_display = "[,]",           .primary_char = ',', .alt_char = ',', .footer_label = "[,] Settings",     .help_label = "Simulator Settings"},
    {.action = KeyAction::ConfigureSystem,    .key_display = "[y]",           .primary_char = 'y', .alt_char = 'Y', .footer_label = "[y] SysConfig",    .help_label = "CA System Config"},
    {.action = KeyAction::ConfigureMisa,      .key_display = "[Alt-m]",       .primary_char = '\0',.alt_char = '\0',.footer_label = "[Alt-m] MISA",      .help_label = "Configure MISA"},
    {.action = KeyAction::Help,               .key_display = "[F1] / [h]",    .primary_char = 'h', .alt_char = 'H', .footer_label = "[F1/?] Help",      .help_label = "Show Help modal"},
    {.action = KeyAction::Quit,               .key_display = "[q]",           .primary_char = 'q', .alt_char = 'Q', .footer_label = "[q] Quit",         .help_label = "Quit Simulator"},
    {.action = KeyAction::CycleLayout,        .key_display = "[Tab]",         .primary_char = '\0',.alt_char = '\0',.footer_label = "[Tab] Layout",     .help_label = "Cycle UI Layout"},
    {.action = KeyAction::CycleRegPage,       .key_display = "[r]",           .primary_char = 'r', .alt_char = 'R', .footer_label = "[r] Regs",         .help_label = "Cycle Register Page"},
    {.action = KeyAction::CycleToolPage,      .key_display = "[l]",           .primary_char = 'l', .alt_char = 'L', .footer_label = "[l] Tools",        .help_label = "Cycle Tool Tabs"},
    {.action = KeyAction::CycleRightPanel,    .key_display = "[p]",           .primary_char = 'p', .alt_char = 'P', .footer_label = "[p] RightPane",    .help_label = "Cycle Right Pane"},
    {.action = KeyAction::ToggleExplain,      .key_display = "[e]",           .primary_char = 'e', .alt_char = 'E', .footer_label = "[e] Explain",      .help_label = "Instruction Explainer"},
    {.action = KeyAction::ToggleTrace,        .key_display = "[v]",           .primary_char = 'v', .alt_char = 'V', .footer_label = "[v] Trace",        .help_label = "Toggle Trace Logging"}
}};

auto Keybindings::get(KeyAction action) -> const KeyBindingInfo& {
    for (const auto& binding : kKeyBindings) {
        if (binding.action == action) {
            return binding;
        }
    }
    return kKeyBindings[0];
}

auto Keybindings::get_footer_text(KeyAction action) -> std::string {
    return get(action).footer_label;
}

auto Keybindings::get_help_key(KeyAction action) -> std::string {
    return get(action).key_display;
}

auto Keybindings::get_help_desc(KeyAction action) -> std::string {
    return get(action).help_label;
}

} // namespace simrv::tui
