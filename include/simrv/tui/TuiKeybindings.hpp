/**
 * @file TuiKeybindings.hpp
 * @brief Centralized TUI Keybindings Registry.
 */
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "simrv/tui/TuiKey.hpp"

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
    ToggleExplain,
    ToggleTrace
};

struct KeyBindingInfo {
    KeyAction action;
    std::string key_display;   // e.g. "[m]"
    char primary_char;         // e.g. 'm'
    char alt_char;             // e.g. 'M'
    std::string footer_label;  // e.g. "[m] ManageBP"
    std::string help_label;    // e.g. "Manage Break/Watchpoints"
};

class Keybindings {
   public:
    static auto get(KeyAction action) -> const KeyBindingInfo&;
    static auto get_footer_text(KeyAction action) -> std::string;
    static auto get_help_key(KeyAction action) -> std::string;
    static auto get_help_desc(KeyAction action) -> std::string;
};

}  // namespace simrv::tui
