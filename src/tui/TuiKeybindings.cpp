/**
 * @file TuiKeybindings.cpp
 * @brief Centralized TUI Keybindings Registry implementation.
 */
#include "simrv/tui/TuiKeybindings.hpp"

#include <array>
#include <stdexcept>

namespace simrv::tui {

static const std::array<KeyBindingInfo, 30> kKeyBindings = {
    {{.action = KeyAction::Step,
      .key_display = "[s] / [Space]",
      .primary_char = 's',
      .alt_char = 'S',
      .footer_label = "[s] Step",
      .help_label = "Step 1 instruction"},
     {.action = KeyAction::RunPause,
      .key_display = "[c] / [Space] / [Ctrl-P]",
      .primary_char = 'c',
      .alt_char = 'C',
      .footer_label = "[c] Run",
      .help_label = "Run / Pause simulation"},
     {.action = KeyAction::FocusNextPane,
      .key_display = "[Tab]",
      .primary_char = '\0',
      .alt_char = '\0',
      .footer_label = "[Tab] Pane",
      .help_label = "Focus Next Column",
      .category = ActionCategory::Navigate,
      .allowed_running = true,
      .allowed_in_modal = false},
     {.action = KeyAction::FocusPrevPane,
      .key_display = "[Shift-Tab]",
      .primary_char = '\0',
      .alt_char = '\0',
      .footer_label = "[S-Tab] Pane",
      .help_label = "Focus Prev Column",
      .category = ActionCategory::Navigate,
      .allowed_running = true,
      .allowed_in_modal = false},
     {.action = KeyAction::Reset,
      .key_display = "[Ctrl-R]",
      .primary_char = '\0',
      .alt_char = '\0',
      .footer_label = "[Ctrl-R] Reboot",
      .help_label = "Reboot CPU / System"},
     {.action = KeyAction::SetBreakpoint,
      .key_display = "[:]",
      .primary_char = ':',
      .alt_char = '\0',
      .footer_label = "[:] Breakpoint",
      .help_label = "Set PC Breakpoint"},
     {.action = KeyAction::SetWatchpoint,
      .key_display = "[w]",
      .primary_char = 'w',
      .alt_char = 'W',
      .footer_label = "[w] Watchpoint",
      .help_label = "Set Watchpoint"},
     {.action = KeyAction::ManageBreakpoints,
      .key_display = "[m]",
      .primary_char = 'm',
      .alt_char = 'M',
      .footer_label = "[m] ManageBP",
      .help_label = "Manage Break/Watchpoints"},
     {.action = KeyAction::TogglePcBreakpoint,
      .key_display = "[k]",
      .primary_char = 'k',
      .alt_char = 'K',
      .footer_label = "[k] Toggle BP",
      .help_label = "Toggle PC Breakpoint"},
     {.action = KeyAction::InspectAddress,
      .key_display = "[i]",
      .primary_char = 'i',
      .alt_char = 'I',
      .footer_label = "[i] Inspect",
      .help_label = "Inspect Memory Address"},
     {.action = KeyAction::SetSpeed,
      .key_display = "[f]",
      .primary_char = 'f',
      .alt_char = 'F',
      .footer_label = "[f] Speed",
      .help_label = "Set Frequency (Hz)"},
     {.action = KeyAction::LoadBinary,
      .key_display = "[o]",
      .primary_char = 'o',
      .alt_char = 'O',
      .footer_label = "[o] Load",
      .help_label = "Load Binary / Disk"},
     {.action = KeyAction::Settings,
      .key_display = "[,]",
      .primary_char = ',',
      .alt_char = ',',
      .footer_label = "[,] Settings",
      .help_label = "Simulator Settings"},
     {.action = KeyAction::ConfigureSystem,
      .key_display = "[y]",
      .primary_char = 'y',
      .alt_char = 'Y',
      .footer_label = "[y] SysConfig",
      .help_label = "CA System Config"},
     {.action = KeyAction::ConfigureMisa,
      .key_display = "[Alt-m]",
      .primary_char = '\0',
      .alt_char = '\0',
      .footer_label = "[Alt-m] MISA",
      .help_label = "Configure MISA"},
     {.action = KeyAction::Help,
      .key_display = "[F1] / [h]",
      .primary_char = 'h',
      .alt_char = 'H',
      .footer_label = "[?] Help",
      .help_label = "Show Help modal"},
     {.action = KeyAction::ToggleTheme,
      .key_display = "[t]",
      .primary_char = 't',
      .alt_char = 'T',
      .footer_label = "[t] Theme",
      .help_label = "Cycle Theme / Color Scheme",
      .category = ActionCategory::Configure,
      .allowed_running = true,
      .allowed_in_modal = false},
     {.action = KeyAction::Quit,
      .key_display = "[q] / [Ctrl-Q]",
      .primary_char = 'q',
      .alt_char = 'Q',
      .footer_label = "[q] Quit",
      .help_label = "Quit Simulator"},
     {.action = KeyAction::CycleLayout,
      .key_display = "[Ctrl-L]",
      .primary_char = '\0',
      .alt_char = '\0',
      .footer_label = "[Ctrl-L] Layout",
      .help_label = "Cycle Workbench Layout"},
     {.action = KeyAction::CycleRegPage,
      .key_display = "[r]",
      .primary_char = 'r',
      .alt_char = 'R',
      .footer_label = "[r] Regs",
      .help_label = "Cycle Register Page"},
     {.action = KeyAction::CycleToolPage,
      .key_display = "[l]",
      .primary_char = 'l',
      .alt_char = 'L',
      .footer_label = "[l] Tools",
      .help_label = "Cycle Tool Tabs"},
     {.action = KeyAction::CycleRightPanel,
      .key_display = "[p]",
      .primary_char = 'p',
      .alt_char = 'P',
      .footer_label = "[p] Panel",
      .help_label = "Cycle Right Pane"},
     {.action = KeyAction::ToggleStudentGuide,
      .key_display = "[g]",
      .primary_char = 'g',
      .alt_char = 'G',
      .footer_label = "[g] Guide",
      .help_label = "Toggle Interactive Student Guide",
      .category = ActionCategory::Help},
     {.action = KeyAction::ActivateStudentGuide,
      .key_display = "[Enter]",
      .primary_char = '\0',
      .alt_char = '\0',
      .footer_label = "[Enter] Try Next",
      .help_label = "Perform Student Guide Suggestion",
      .category = ActionCategory::Help},
     {.action = KeyAction::ToggleExplain,
      .key_display = "[e]",
      .primary_char = 'e',
      .alt_char = 'E',
      .footer_label = "[e] Explain",
      .help_label = "Instruction Explainer"},
     {.action = KeyAction::ToggleTrace,
      .key_display = "[v]",
      .primary_char = 'v',
      .alt_char = 'V',
      .footer_label = "[v] Trace",
      .help_label = "Toggle Trace Logging"},
     {.action = KeyAction::ExportInspection,
      .key_display = "[x]",
      .primary_char = 'x',
      .alt_char = 'X',
      .footer_label = "[x] Export",
      .help_label = "Export Paused Inspection Report",
      .category = ActionCategory::Inspect,
      .requires_image = true},
     {.action = KeyAction::SwitchHart,
      .key_display = "[n]",
      .primary_char = 'n',
      .alt_char = 'N',
      .footer_label = "[n] Hart",
      .help_label = "Switch active Hart telemetry"},
     {.action = KeyAction::OpenGlossary,
      .key_display = "[?]",
      .primary_char = '?',
      .alt_char = '?',
      .footer_label = "[?] Glossary",
      .help_label = "Architecture Glossary & Concepts",
      .category = ActionCategory::Help,
      .allowed_running = true,
      .allowed_in_modal = false},
     {.action = KeyAction::ToggleDebug,
      .key_display = "[Ctrl-D] / [d]",
      .primary_char = 'd',
      .alt_char = 'D',
      .footer_label = "[d] Debug",
      .help_label = "Toggle Debug Mode & Diagnostics",
      .category = ActionCategory::Inspect,
      .allowed_running = true,
      .allowed_in_modal = false}}};

auto Keybindings::get(KeyAction action) -> const KeyBindingInfo& {
    for (const auto& binding : kKeyBindings) {
        if (binding.action == action) {
            return binding;
        }
    }
    throw std::out_of_range("unknown TUI key action");
}

auto Keybindings::get_footer_text(KeyAction action) -> std::string {
    return get(action).footer_label;
}

auto Keybindings::get_help_key(KeyAction action) -> std::string { return get(action).key_display; }

auto Keybindings::get_help_desc(KeyAction action) -> std::string { return get(action).help_label; }

auto Keybindings::all() -> std::span<const KeyBindingInfo> { return kKeyBindings; }

auto Keybindings::unavailable_reason(KeyAction action, const ActionContext& context)
    -> std::string_view {
    if (context.modal_active && action != KeyAction::Quit && action != KeyAction::Help) {
        return "Close the active dialog first";
    }
    if (context.shutdown && action != KeyAction::Reset && action != KeyAction::LoadBinary &&
        action != KeyAction::Quit && action != KeyAction::Help &&
        action != KeyAction::ToggleStudentGuide && action != KeyAction::ActivateStudentGuide) {
        return "The target is shut down";
    }
    switch (action) {
        case KeyAction::Step:
        case KeyAction::SetBreakpoint:
        case KeyAction::SetWatchpoint:
        case KeyAction::ManageBreakpoints:
        case KeyAction::TogglePcBreakpoint:
        case KeyAction::InspectAddress:
        case KeyAction::Settings:
        case KeyAction::ConfigureSystem:
        case KeyAction::ConfigureMisa:
        case KeyAction::ToggleStudentGuide:
        case KeyAction::ActivateStudentGuide:
        case KeyAction::ExportInspection:
            if (!context.paused) return "Pause the simulator first";
            break;
        default:
            break;
    }
    if ((action == KeyAction::Step || action == KeyAction::RunPause ||
         action == KeyAction::ToggleExplain || action == KeyAction::ExportInspection) &&
        !context.image_loaded) {
        return "Load a program image first";
    }
    if (action == KeyAction::ActivateStudentGuide && !context.student_guide_enabled) {
        return "Enable the Student Guide first";
    }
    if ((action == KeyAction::SetBreakpoint || action == KeyAction::SetWatchpoint ||
         action == KeyAction::ManageBreakpoints || action == KeyAction::TogglePcBreakpoint) &&
        !context.debug_mode) {
        return "Enable debug mode first";
    }
    return {};
}

auto Keybindings::is_available(KeyAction action, const ActionContext& context) -> bool {
    return unavailable_reason(action, context).empty();
}

auto Keybindings::available(const ActionContext& context) -> std::vector<const KeyBindingInfo*> {
    std::vector<const KeyBindingInfo*> result;
    for (const auto& binding : kKeyBindings) {
        if (is_available(binding.action, context)) result.push_back(&binding);
    }
    return result;
}

auto key_action_for_footer(TuiFooterAction action) -> KeyAction {
    switch (action) {
        case TuiFooterAction::Step:
            return KeyAction::Step;
        case TuiFooterAction::CycleRegs:
            return KeyAction::CycleRegPage;
        case TuiFooterAction::CycleTools:
            return KeyAction::CycleToolPage;
        case TuiFooterAction::SetBreakpoint:
            return KeyAction::SetBreakpoint;
        case TuiFooterAction::SetWatchpoint:
            return KeyAction::SetWatchpoint;
        case TuiFooterAction::TogglePcBreakpoint:
            return KeyAction::TogglePcBreakpoint;
        case TuiFooterAction::SetSpeed:
            return KeyAction::SetSpeed;
        case TuiFooterAction::InspectMem:
            return KeyAction::InspectAddress;
        case TuiFooterAction::LoadBinary:
            return KeyAction::LoadBinary;
        case TuiFooterAction::ToggleHelp:
            return KeyAction::Help;
        case TuiFooterAction::RunPause:
            return KeyAction::RunPause;
        case TuiFooterAction::Quit:
            return KeyAction::Quit;
        case TuiFooterAction::CycleLayout:
            return KeyAction::CycleLayout;
        case TuiFooterAction::ToggleStudentGuide:
            return KeyAction::ToggleStudentGuide;
        case TuiFooterAction::TogglePanel:
            return KeyAction::CycleRightPanel;
        case TuiFooterAction::ToggleTrace:
            return KeyAction::ToggleTrace;
        case TuiFooterAction::OpenSettings:
            return KeyAction::Settings;
        case TuiFooterAction::ConfigureMisa:
            return KeyAction::ConfigureMisa;
        case TuiFooterAction::ConfigureSystem:
            return KeyAction::ConfigureSystem;
        case TuiFooterAction::ManageBreakpoints:
            return KeyAction::ManageBreakpoints;
        case TuiFooterAction::Reboot:
            return KeyAction::Reset;
        case TuiFooterAction::SwitchHart:
            return KeyAction::SwitchHart;
        case TuiFooterAction::ToggleTheme:
            return KeyAction::ToggleTheme;
        case TuiFooterAction::ToggleDebug:
            return KeyAction::ToggleDebug;
    }
    throw std::out_of_range("unknown TUI footer action");
}

}  // namespace simrv::tui
