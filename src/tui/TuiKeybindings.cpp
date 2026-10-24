/**
 * @file TuiKeybindings.cpp
 * @brief Centralized TUI Keybindings Registry implementation.
 */
#include "simrv/tui/TuiKeybindings.hpp"

#include <algorithm>
#include <array>
#include <stdexcept>

namespace simrv::tui {

static const auto kKeyBindings =
    std::to_array<KeyBindingInfo>({{.action = KeyAction::Step,
                                    .key_display = "[F6] / [s] / [Space]",
                                    .primary_char = 's',
                                    .alt_char = 'S',
                                    .footer_label = "[F6] Step",
                                    .help_label = "Step 1 instruction",
                                    .category = ActionCategory::Execution,
                                    .special_keys = {TuiKey::F6, TuiKey::Space}},
                                   {.action = KeyAction::RunPause,
                                    .key_display = "[F5] / [c] / [Ctrl-P]",
                                    .primary_char = 'c',
                                    .alt_char = 'C',
                                    .footer_label = "[F5] Run",
                                    .help_label = "Run / Pause simulation",
                                    .category = ActionCategory::Execution,
                                    .special_keys = {TuiKey::F5, TuiKey::CtrlP}},
                                   {.action = KeyAction::FocusNextPane,
                                    .key_display = "[Tab] / [Ctrl-Right]",
                                    .primary_char = '\0',
                                    .alt_char = '\0',
                                    .footer_label = "[Tab] Pane",
                                    .help_label = "Focus Next Column",
                                    .category = ActionCategory::Navigate,
                                    .allowed_running = true,
                                    .allowed_in_modal = false,
                                    .special_keys = {TuiKey::Tab}},
                                   {.action = KeyAction::FocusPrevPane,
                                    .key_display = "[Shift-Tab] / [Ctrl-Left]",
                                    .primary_char = '\0',
                                    .alt_char = '\0',
                                    .footer_label = "[S-Tab] Pane",
                                    .help_label = "Focus Prev Column",
                                    .category = ActionCategory::Navigate,
                                    .allowed_running = true,
                                    .allowed_in_modal = false,
                                    .special_keys = {TuiKey::BackTab}},
                                   {.action = KeyAction::Reset,
                                    .key_display = "[Ctrl-R]",
                                    .primary_char = '\0',
                                    .alt_char = '\0',
                                    .footer_label = "[Ctrl-R] Reboot",
                                    .help_label = "Reboot CPU / System",
                                    .category = ActionCategory::Execution,
                                    .special_keys = {TuiKey::CtrlR}},
                                   {.action = KeyAction::SetBreakpoint,
                                    .key_display = "[:]",
                                    .primary_char = ':',
                                    .alt_char = '\0',
                                    .footer_label = "[:] Breakpoint",
                                    .help_label = "Set PC Breakpoint",
                                    .category = ActionCategory::Inspect},
                                   {.action = KeyAction::SetWatchpoint,
                                    .key_display = "[w]",
                                    .primary_char = 'w',
                                    .alt_char = 'W',
                                    .footer_label = "[w] Watchpoint",
                                    .help_label = "Set Watchpoint",
                                    .category = ActionCategory::Inspect},
                                   {.action = KeyAction::ManageBreakpoints,
                                    .key_display = "[m]",
                                    .primary_char = 'm',
                                    .alt_char = 'M',
                                    .footer_label = "[m] ManageBP",
                                    .help_label = "Manage Break/Watchpoints",
                                    .category = ActionCategory::Inspect},
                                   {.action = KeyAction::TogglePcBreakpoint,
                                    .key_display = "[F8] / [k]",
                                    .primary_char = 'k',
                                    .alt_char = 'K',
                                    .footer_label = "[F8] Toggle BP",
                                    .help_label = "Toggle PC Breakpoint",
                                    .category = ActionCategory::Inspect,
                                    .special_keys = {TuiKey::F8}},
                                   {.action = KeyAction::InspectAddress,
                                    .key_display = "[i]",
                                    .primary_char = 'i',
                                    .alt_char = 'I',
                                    .footer_label = "[i] Inspect",
                                    .help_label = "Inspect Memory Address",
                                    .category = ActionCategory::Inspect},
                                   {.action = KeyAction::SetSpeed,
                                    .key_display = "[f]",
                                    .primary_char = 'f',
                                    .alt_char = 'F',
                                    .footer_label = "[f] Speed",
                                    .help_label = "Set Frequency (Hz)",
                                    .category = ActionCategory::Execution},
                                   {.action = KeyAction::LoadBinary,
                                    .key_display = "[o]",
                                    .primary_char = 'o',
                                    .alt_char = 'O',
                                    .alt_sequence = 'o',
                                    .footer_label = "[o] Load",
                                    .help_label = "Load Binary / Disk",
                                    .category = ActionCategory::Configure},
                                   {.action = KeyAction::Settings,
                                    .key_display = "[F2] / [,]",
                                    .primary_char = ',',
                                    .alt_char = ',',
                                    .alt_sequence = 's',
                                    .footer_label = "[F2] Settings",
                                    .help_label = "Simulator Settings",
                                    .category = ActionCategory::Configure,
                                    .special_keys = {TuiKey::F2}},
                                   {.action = KeyAction::Help,
                                    .key_display = "[F1] / [h]",
                                    .primary_char = 'h',
                                    .alt_char = 'H',
                                    .footer_label = "[F1] Help",
                                    .help_label = "Show Help modal",
                                    .category = ActionCategory::Help,
                                    .special_keys = {TuiKey::F1}},
                                   {.action = KeyAction::ToggleTheme,
                                    .key_display = "[t]",
                                    .primary_char = 't',
                                    .alt_char = 'T',
                                    .alt_sequence = 't',
                                    .footer_label = "[t] Theme",
                                    .help_label = "Cycle Theme / Color Scheme",
                                    .category = ActionCategory::Configure,
                                    .allowed_running = true,
                                    .allowed_in_modal = false},
                                   {.action = KeyAction::Quit,
                                    .key_display = "[F10] / [q] / [Ctrl-Q]",
                                    .primary_char = 'q',
                                    .alt_char = 'Q',
                                    .footer_label = "[F10] Quit",
                                    .help_label = "Quit Simulator",
                                    .category = ActionCategory::Execution,
                                    .special_keys = {TuiKey::F10, TuiKey::CtrlQ}},
                                   {.action = KeyAction::CycleRegPage,
                                    .key_display = "[F3]",
                                    .primary_char = '\0',
                                    .alt_char = '\0',
                                    .footer_label = "[F3] Regs",
                                    .help_label = "Cycle Register Page",
                                    .category = ActionCategory::Navigate,
                                    .special_keys = {TuiKey::F3}},
                                   {.action = KeyAction::CycleToolPage,
                                    .key_display = "[F11]",
                                    .primary_char = '\0',
                                    .alt_char = '\0',
                                    .footer_label = "[F11] Tools",
                                    .help_label = "Cycle Tool Tabs",
                                    .category = ActionCategory::Navigate,
                                    .special_keys = {TuiKey::F11}},
                                   {.action = KeyAction::ToggleExplain,
                                    .key_display = "[e]",
                                    .primary_char = 'e',
                                    .alt_char = 'E',
                                    .alt_sequence = 'e',
                                    .footer_label = "[e] Explain",
                                    .help_label = "Instruction Explainer",
                                    .category = ActionCategory::Inspect},
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
                                    .help_label = "Switch active Hart telemetry",
                                    .category = ActionCategory::Inspect},
                                   {.action = KeyAction::OpenGlossary,
                                    .key_display = "[F9] / [?]",
                                    .primary_char = '?',
                                    .alt_char = '?',
                                    .footer_label = "[F9] Glossary",
                                    .help_label = "Architecture Glossary & Concepts",
                                    .category = ActionCategory::Help,
                                    .allowed_running = true,
                                    .allowed_in_modal = false,
                                    .special_keys = {TuiKey::F9}},
                                   {.action = KeyAction::SelectPreset1,
                                    .key_display = "[Alt-1]",
                                    .primary_char = '\0',
                                    .alt_char = '\0',
                                    .alt_sequence = '1',
                                    .footer_label = "[Alt-1] Debug",
                                    .help_label = "Layout Preset: General Debug",
                                    .category = ActionCategory::Navigate,
                                    .allowed_running = true,
                                    .allowed_in_modal = false},
                                   {.action = KeyAction::SelectPreset2,
                                    .key_display = "[Alt-2]",
                                    .primary_char = '\0',
                                    .alt_char = '\0',
                                    .alt_sequence = '2',
                                    .footer_label = "[Alt-2] uArch",
                                    .help_label = "Layout Preset: Microarchitecture",
                                    .category = ActionCategory::Navigate,
                                    .allowed_running = true,
                                    .allowed_in_modal = false},
                                   {.action = KeyAction::SelectPreset3,
                                    .key_display = "[Alt-3]",
                                    .primary_char = '\0',
                                    .alt_char = '\0',
                                    .alt_sequence = '3',
                                    .footer_label = "[Alt-3] Trace",
                                    .help_label = "Layout Preset: Trace & Execution",
                                    .category = ActionCategory::Navigate,
                                    .allowed_running = true,
                                    .allowed_in_modal = false},
                                   {.action = KeyAction::SelectPreset4,
                                    .key_display = "[Alt-4]",
                                    .primary_char = '\0',
                                    .alt_char = '\0',
                                    .alt_sequence = '4',
                                    .footer_label = "[Alt-4] Mem",
                                    .help_label = "Layout Preset: Memory & Interconnect",
                                    .category = ActionCategory::Navigate,
                                    .allowed_running = true,
                                    .allowed_in_modal = false},
                                   {.action = KeyAction::OpenLayoutPresets,
                                    .key_display = "[F4]",
                                    .primary_char = '\0',
                                    .alt_char = '\0',
                                    .footer_label = "[F4] Presets",
                                    .help_label = "Open Layout Presets Modal",
                                    .category = ActionCategory::Navigate,
                                    .allowed_running = true,
                                    .allowed_in_modal = false,
                                    .special_keys = {TuiKey::F4}},
                                   {.action = KeyAction::ToggleExecutionMode,
                                    .key_display = "[v]",
                                    .primary_char = 'v',
                                    .alt_char = 'V',
                                    .footer_label = "[v] Mode",
                                    .help_label = "Toggle Precision (IA / CA)",
                                    .category = ActionCategory::Configure,
                                    .allowed_running = true,
                                    .allowed_in_modal = false},
                                   {.action = KeyAction::OpenToolPicker,
                                    .key_display = "[Ctrl-W] / click header",
                                    .primary_char = '\0',
                                    .alt_char = '\0',
                                    .footer_label = "[Ctrl-W] Tool",
                                    .help_label = "Select Column Tool View",
                                    .category = ActionCategory::Navigate,
                                    .allowed_running = false,
                                    .allowed_in_modal = false,
                                    .special_keys = {TuiKey::CtrlW}},
                                   {.action = KeyAction::MoveColumnLeft,
                                    .key_display = "[<] / [Alt-Left]",
                                    .primary_char = '<',
                                    .alt_char = '<',
                                    .alt_sequence = '<',
                                    .footer_label = "[<] Move L",
                                    .help_label = "Move Focused Column Left",
                                    .category = ActionCategory::Navigate,
                                    .allowed_running = false,
                                    .allowed_in_modal = false},
                                   {.action = KeyAction::MoveColumnRight,
                                    .key_display = "[>] / [Alt-Right]",
                                    .primary_char = '>',
                                    .alt_char = '>',
                                    .alt_sequence = '>',
                                    .footer_label = "[>] Move R",
                                    .help_label = "Move Focused Column Right",
                                    .category = ActionCategory::Navigate,
                                    .allowed_in_modal = false},
                                   {.action = KeyAction::AddColumn,
                                    .key_display = "[Ctrl-N] / [Alt-N]",
                                    .primary_char = '\0',
                                    .alt_char = '\0',
                                    .alt_sequence = 'n',
                                    .alt_sequence_alt = 'a',
                                    .footer_label = "[Ctrl-N] Add Panel",
                                    .help_label = "Add Panel to Workbench",
                                    .category = ActionCategory::Navigate,
                                    .allowed_running = false,
                                    .allowed_in_modal = false,
                                    .special_keys = {TuiKey::CtrlA, TuiKey::CtrlN}},
                                   {.action = KeyAction::CloseColumn,
                                    .key_display = "[Ctrl-X] / [Alt-X]",
                                    .primary_char = '\0',
                                    .alt_char = '\0',
                                    .alt_sequence = 'x',
                                    .footer_label = "[Ctrl-X] Close Panel",
                                    .help_label = "Close Focused Panel",
                                    .category = ActionCategory::Navigate,
                                    .allowed_running = false,
                                    .allowed_in_modal = false,
                                    .special_keys = {TuiKey::CtrlX}},
                                   {.action = KeyAction::DumpMemory,
                                    .key_display = "[d]",
                                    .primary_char = 'd',
                                    .alt_char = 'D',
                                    .footer_label = "[d] HexDump",
                                    .help_label = "Interactive Hex Memory Dump",
                                    .category = ActionCategory::Inspect,
                                    .allowed_running = false,
                                    .allowed_in_modal = false},
                                   {.action = KeyAction::OpenLogModal,
                                    .key_display = "[L]",
                                    .primary_char = 'L',
                                    .alt_char = 'L',
                                    .footer_label = "[L] Logs",
                                    .help_label = "Interactive Log Viewer Modal",
                                    .category = ActionCategory::Inspect,
                                    .allowed_running = true,
                                    .allowed_in_modal = false},
                                   {.action = KeyAction::CheckMissionObjective,
                                    .key_display = "[F12]",
                                    .primary_char = '\0',
                                    .alt_char = '\0',
                                    .footer_label = "[F12] Objective",
                                    .help_label = "Open or Reopen Mission Instruction",
                                    .category = ActionCategory::Help,
                                    .allowed_running = true,
                                    .allowed_in_modal = true,
                                    .special_keys = {TuiKey::F12}},
                                   {.action = KeyAction::OpenTracePage,
                                    .key_display = "[F7]",
                                    .primary_char = '\0',
                                    .alt_char = '\0',
                                    .footer_label = "[F7] Trace",
                                    .help_label = "Open Execution Trace",
                                    .category = ActionCategory::Navigate,
                                    .special_keys = {TuiKey::F7}},
                                   {.action = KeyAction::SelectPage1,
                                    .key_display = "[1] GPR",
                                    .primary_char = '1',
                                    .alt_char = '\0',
                                    .footer_label = "[1] GPR",
                                    .help_label = "Show GPR Registers",
                                    .category = ActionCategory::Navigate},
                                   {.action = KeyAction::SelectPage2,
                                    .key_display = "[2] Stack",
                                    .primary_char = '2',
                                    .alt_char = '\0',
                                    .footer_label = "[2] Stack",
                                    .help_label = "Show Stack and Memory",
                                    .category = ActionCategory::Navigate},
                                   {.action = KeyAction::SelectPage3,
                                    .key_display = "[3] Pipeline",
                                    .primary_char = '3',
                                    .alt_char = '\0',
                                    .footer_label = "[3] Pipeline",
                                    .help_label = "Show Pipeline Stages",
                                    .category = ActionCategory::Navigate},
                                   {.action = KeyAction::SelectPage4,
                                    .key_display = "[4] Explain",
                                    .primary_char = '4',
                                    .alt_char = '\0',
                                    .footer_label = "[4] Explain",
                                    .help_label = "Show Instruction Explainer",
                                    .category = ActionCategory::Navigate},
                                   {.action = KeyAction::NarrowFocusedColumn,
                                    .key_display = "[Left-Bracket]",
                                    .primary_char = '\0',
                                    .alt_char = '\0',
                                    .footer_label = "[[] Narrow",
                                    .help_label = "Narrow Focused Column",
                                    .category = ActionCategory::Navigate,
                                    .special_keys = {TuiKey::LeftBracket}},
                                   {.action = KeyAction::WidenFocusedColumn,
                                    .key_display = "[Right-Bracket]",
                                    .primary_char = '\0',
                                    .alt_char = '\0',
                                    .footer_label = "[]] Widen",
                                    .help_label = "Widen Focused Column",
                                    .category = ActionCategory::Navigate,
                                    .special_keys = {TuiKey::RightBracket}}});

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

auto Keybindings::match(TuiKey key) -> std::optional<KeyAction> {
    for (const auto& binding : kKeyBindings) {
        if (key != TuiKey::None &&
            (key == static_cast<TuiKey>(binding.primary_char) ||
             key == static_cast<TuiKey>(binding.alt_char) ||
             std::ranges::find(binding.special_keys, key) != binding.special_keys.end())) {
            return binding.action;
        }
    }
    return std::nullopt;
}

auto Keybindings::match_alt(char key) -> std::optional<KeyAction> {
    for (const auto& binding : kKeyBindings) {
        if ((binding.alt_sequence != '\0' && binding.alt_sequence == key) ||
            (binding.alt_sequence_alt != '\0' && binding.alt_sequence_alt == key)) {
            return binding.action;
        }
    }
    return std::nullopt;
}

auto Keybindings::categories() -> std::span<const ActionCategoryInfo> {
    static constexpr std::array<ActionCategoryInfo, 5> kCategories = {{
        {.category = ActionCategory::Execution, .title = "Execution"},
        {.category = ActionCategory::Inspect, .title = "Inspect & Debug"},
        {.category = ActionCategory::Navigate, .title = "Navigation & Views"},
        {.category = ActionCategory::Configure, .title = "Configuration"},
        {.category = ActionCategory::Help, .title = "Help & Learning"},
    }};
    return kCategories;
}

auto Keybindings::unavailable_reason(KeyAction action, const ActionContext& context)
    -> std::string_view {
    if (context.modal_active && action != KeyAction::Quit && action != KeyAction::Help &&
        action != KeyAction::CheckMissionObjective) {
        return "Close the active dialog first";
    }
    if (context.shutdown && action != KeyAction::Reset && action != KeyAction::LoadBinary &&
        action != KeyAction::Quit && action != KeyAction::Help) {
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
    if (action == KeyAction::CheckMissionObjective && !context.mission_active) {
        return "Start a classroom mission first";
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
        case TuiFooterAction::OpenSettings:
            return KeyAction::Settings;
        case TuiFooterAction::ManageBreakpoints:
            return KeyAction::ManageBreakpoints;
        case TuiFooterAction::Reboot:
            return KeyAction::Reset;
        case TuiFooterAction::SwitchHart:
            return KeyAction::SwitchHart;
        case TuiFooterAction::ToggleTheme:
            return KeyAction::ToggleTheme;
        case TuiFooterAction::ToggleExecutionMode:
            return KeyAction::ToggleExecutionMode;
        case TuiFooterAction::OpenToolPicker:
            return KeyAction::OpenToolPicker;
        case TuiFooterAction::MoveColumnLeft:
            return KeyAction::MoveColumnLeft;
        case TuiFooterAction::MoveColumnRight:
            return KeyAction::MoveColumnRight;
        case TuiFooterAction::AddColumn:
            return KeyAction::AddColumn;
        case TuiFooterAction::CloseColumn:
            return KeyAction::CloseColumn;
        case TuiFooterAction::FocusNextPane:
            return KeyAction::FocusNextPane;
        case TuiFooterAction::FocusPrevPane:
            return KeyAction::FocusPrevPane;
    }
    throw std::out_of_range("unknown TUI footer action");
}

}  // namespace simrv::tui
