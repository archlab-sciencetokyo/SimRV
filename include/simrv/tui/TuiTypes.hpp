/**
 * @file TuiTypes.hpp
 * @brief Enums and common types for SimRV TUI dashboard.
 */
#pragma once

#include <cstdint>

#include "simrv/tui/framework/Layout.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::tui {

using TuiLayout = framework::Layout;
using HartSelection = HartId;

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
    STACK,
    DISASM
};

struct WorkbenchSlot {
    TuiRegPage page = TuiRegPage::GPR;
    int scroll_offset = 0;
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
        case TuiRegPage::DISASM:
        default:
            return TuiCategoryGroup::Tools;
    }
}

[[nodiscard]] constexpr auto get_page_name(TuiRegPage page) -> const char* {
    switch (page) {
        case TuiRegPage::GPR:
            return "GPR Registers";
        case TuiRegPage::FPR:
            return "FPR Float Regs";
        case TuiRegPage::VEC:
            return "Vector Registers";
        case TuiRegPage::PIPELINE:
            return "Pipeline Stages";
        case TuiRegPage::CACHE:
            return "Cache Hierarchy";
        case TuiRegPage::TLB:
            return "TLB & Page Table";
        case TuiRegPage::BPRED:
            return "Branch Predictor";
        case TuiRegPage::HAZARD:
            return "Hazards & Stalls";
        case TuiRegPage::BUS:
            return "Bus & Coherence";
        case TuiRegPage::TRACE:
            return "Execution Trace";
        case TuiRegPage::EXPLAIN:
            return "Instruction Explainer";
        case TuiRegPage::STACK:
            return "Stack & Memory";
        case TuiRegPage::DISASM:
            return "Disassembly";
    }
    return "Tool View";
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
            return "Register Files";
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
    ToggleTheme,
    ToggleDebug
};

}  // namespace simrv::tui
