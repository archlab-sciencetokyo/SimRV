/**
 * @file TuiTypes.hpp
 * @brief Enums and common types for SimRV TUI dashboard.
 */
#pragma once

#include <cstdint>

namespace simrv::tui {

enum class TuiLayout : uint8_t { Split, FullRight, FullLeft };

enum class TuiCategoryGroup : uint8_t { Regs, Memory, Pipeline, Tools };

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

[[nodiscard]] constexpr auto get_category_group(TuiRegPage page) -> TuiCategoryGroup {
    switch (page) {
        case TuiRegPage::GPR:
        case TuiRegPage::FPR:
        case TuiRegPage::VEC:
            return TuiCategoryGroup::Regs;
        case TuiRegPage::STACK:
        case TuiRegPage::CACHE:
        case TuiRegPage::TLB:
        case TuiRegPage::BUS:
            return TuiCategoryGroup::Memory;
        case TuiRegPage::PIPELINE:
        case TuiRegPage::BPRED:
        case TuiRegPage::HAZARD:
            return TuiCategoryGroup::Pipeline;
        case TuiRegPage::TRACE:
        case TuiRegPage::EXPLAIN:
        default:
            return TuiCategoryGroup::Tools;
    }
}

[[nodiscard]] constexpr auto get_default_page_for_group(TuiCategoryGroup group,
                                                        bool /*cycle_accurate*/ = false)
    -> TuiRegPage {
    switch (group) {
        case TuiCategoryGroup::Regs:
            return TuiRegPage::GPR;
        case TuiCategoryGroup::Memory:
            return TuiRegPage::STACK;
        case TuiCategoryGroup::Pipeline:
            return TuiRegPage::PIPELINE;
        case TuiCategoryGroup::Tools:
        default:
            return TuiRegPage::EXPLAIN;
    }
}

[[nodiscard]] constexpr auto get_category_name(TuiCategoryGroup group) -> const char* {
    switch (group) {
        case TuiCategoryGroup::Regs:
            return "Regs";
        case TuiCategoryGroup::Memory:
            return "Memory";
        case TuiCategoryGroup::Pipeline:
            return "Pipeline";
        case TuiCategoryGroup::Tools:
        default:
            return "Tools";
    }
}

enum class TuiRightPanelMode : uint8_t { Terminal, Display };

enum class TuiFooterAction : uint8_t {
    Step,
    StepBack,
    RunPause,
    SetSpeed,
    SetBreakpoint,
    SetWatchpoint,
    TogglePcBreakpoint,
    ManageBreakpoints,
    InspectMem,
    CycleLayout,
    ToggleLearn,
    LoadBinary,
    Quit,
    CycleRegs,
    CycleTools,
    ToggleHelp,
    TogglePanel,
    ToggleTrace,
    OpenSettings,
    ConfigureMisa,
    ConfigureSystem,
    Reboot,
    SwitchHart,
    ToggleTheme
};

}  // namespace simrv::tui
