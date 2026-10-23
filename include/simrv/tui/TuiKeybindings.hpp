/**
 * @file TuiKeybindings.hpp
 * @brief Centralized TUI Keybindings Registry.
 */
#pragma once

#include <cstdint>
#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/tui/TuiKey.hpp"
#include "simrv/tui/TuiTypes.hpp"

namespace simrv::tui {

enum class KeyAction : uint8_t {
    Step,
    RunPause,
    Reset,
    SetBreakpoint,
    SetWatchpoint,
    ManageBreakpoints,
    TogglePcBreakpoint,
    InspectAddress,
    SetSpeed,
    LoadBinary,
    Settings,
    Help,
    Quit,
    FocusNextPane,
    FocusPrevPane,
    CycleRegPage,
    CycleToolPage,
    ToggleExplain,
    ExportInspection,
    SwitchHart,
    OpenGlossary,
    ToggleTheme,
    SelectPreset1,
    SelectPreset2,
    SelectPreset3,
    SelectPreset4,
    OpenLayoutPresets,
    ToggleExecutionMode,
    OpenToolPicker,
    MoveColumnLeft,
    MoveColumnRight,
    AddColumn,
    CloseColumn,
    DumpMemory,
    OpenLogModal,
    CheckMissionObjective,
    RevealMissionWhy,
    AdvanceMissionStep,
    OpenTracePage,
    SelectPage1,
    SelectPage2,
    SelectPage3,
    SelectPage4,
    NarrowFocusedColumn,
    WidenFocusedColumn
};

enum class ActionCategory : uint8_t { Execution, Inspect, Navigate, Configure, Help };

struct ActionCategoryInfo {
    ActionCategory category;
    std::string_view title;
};

struct ActionContext {
    bool paused = true;
    bool modal_active = false;
    bool shutdown = false;
    bool image_loaded = false;
    bool cycle_accurate = false;
    bool mission_active = false;
};

struct KeyBindingInfo {
    KeyAction action;
    std::string key_display;   // e.g. "[m]"
    char primary_char;         // e.g. 'm'
    char alt_char;             // e.g. 'M'
    char alt_sequence = '\0';  // character after Escape, when applicable
    char alt_sequence_alt = '\0';
    std::string footer_label;  // e.g. "[m] ManageBP"
    std::string help_label;    // e.g. "Manage Break/Watchpoints"
    ActionCategory category = ActionCategory::Navigate;
    bool allowed_running = false;
    bool allowed_in_modal = false;
    bool requires_image = false;
    bool requires_cycle_accurate = false;
    std::array<TuiKey, 4> special_keys{};
};

class Keybindings {
   public:
    static auto get(KeyAction action) -> const KeyBindingInfo&;
    static auto get_footer_text(KeyAction action) -> std::string;
    static auto get_help_key(KeyAction action) -> std::string;
    static auto get_help_desc(KeyAction action) -> std::string;
    [[nodiscard]] static auto all() -> std::span<const KeyBindingInfo>;
    [[nodiscard]] static auto match(TuiKey key) -> std::optional<KeyAction>;
    [[nodiscard]] static auto match_alt(char key) -> std::optional<KeyAction>;
    [[nodiscard]] static auto categories() -> std::span<const ActionCategoryInfo>;
    [[nodiscard]] static auto is_available(KeyAction action, const ActionContext& context) -> bool;
    [[nodiscard]] static auto unavailable_reason(KeyAction action, const ActionContext& context)
        -> std::string_view;
    [[nodiscard]] static auto available(const ActionContext& context)
        -> std::vector<const KeyBindingInfo*>;
};

/// Map clickable footer actions back to their canonical key/action descriptor.
[[nodiscard]] auto key_action_for_footer(TuiFooterAction action) -> KeyAction;

}  // namespace simrv::tui
