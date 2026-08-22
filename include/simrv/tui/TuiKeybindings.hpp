/**
 * @file TuiKeybindings.hpp
 * @brief Centralized TUI Keybindings Registry.
 */
#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/tui/TuiKey.hpp"
#include "simrv/tui/TuiTypes.hpp"

namespace simrv::tui {

enum class KeyAction : uint8_t {
    Step,
    Backstep,
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
    ConfigureSystem,
    ConfigureMisa,
    Help,
    Quit,
    CycleLayout,
    CycleRegPage,
    CycleToolPage,
    CycleRightPanel,
    ToggleLearn,
    ToggleExplain,
    ToggleTrace,
    SwitchHart,
    OpenGlossary,
    ToggleTheme
};

enum class ActionCategory : uint8_t { Execution, Inspect, Navigate, Configure, Help };

struct ActionContext {
    bool paused = true;
    bool modal_active = false;
    bool shutdown = false;
    bool image_loaded = false;
    bool debug_mode = false;
    bool cycle_accurate = false;
    bool rollback_enabled = false;
};

struct KeyBindingInfo {
    KeyAction action;
    std::string key_display;   // e.g. "[m]"
    char primary_char;         // e.g. 'm'
    char alt_char;             // e.g. 'M'
    std::string footer_label;  // e.g. "[m] ManageBP"
    std::string help_label;    // e.g. "Manage Break/Watchpoints"
    ActionCategory category = ActionCategory::Navigate;
    bool allowed_running = false;
    bool allowed_in_modal = false;
    bool requires_image = false;
    bool requires_debug = false;
    bool requires_cycle_accurate = false;
    bool requires_rollback = false;
};

class Keybindings {
   public:
    static auto get(KeyAction action) -> const KeyBindingInfo&;
    static auto get_footer_text(KeyAction action) -> std::string;
    static auto get_help_key(KeyAction action) -> std::string;
    static auto get_help_desc(KeyAction action) -> std::string;
    [[nodiscard]] static auto all() -> std::span<const KeyBindingInfo>;
    [[nodiscard]] static auto is_available(KeyAction action, const ActionContext& context) -> bool;
    [[nodiscard]] static auto unavailable_reason(KeyAction action, const ActionContext& context)
        -> std::string_view;
    [[nodiscard]] static auto available(const ActionContext& context)
        -> std::vector<const KeyBindingInfo*>;
};

/// Map clickable footer actions back to their canonical key/action descriptor.
[[nodiscard]] auto key_action_for_footer(TuiFooterAction action) -> KeyAction;

}  // namespace simrv::tui
