/**
 * @file TuiTypes.hpp
 * @brief Enums and common types for SimRV TUI dashboard.
 */
#pragma once

#include <cstdint>

namespace simrv::tui {

enum class TuiLayout : uint8_t { Split, FullRight, FullLeft };

enum class TuiRegPage : uint8_t {
    GPR,
    FPR,
    VEC,
    PIPELINE,
    CACHE,
    TLB,
    BPRED,
    HAZARD,
    BUS,
    TRACE,
    EXPLAIN,
    STACK
};

enum class TuiRightPanelMode : uint8_t { Terminal, Display };

enum class TuiFooterAction : uint8_t {
    Step,
    StepBack,
    CycleRegs,
    CycleTools,
    SetBreakpoint,
    SetWatchpoint,
    TogglePcBreakpoint,
    SetSpeed,
    InspectMem,
    LoadBinary,
    ToggleHelp,
    RunPause,
    Quit,
    CycleLayout,
    TogglePanel,
    ToggleTrace,
    OpenSettings,
    ConfigureMisa,
    ConfigureSystem,
    ManageBreakpoints
};

}  // namespace simrv::tui
