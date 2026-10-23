/**
 * @file TuiTypes.hpp
 * @brief Enums and common types for SimRV TUI dashboard.
 */
#pragma once

#include <cstdint>
#include <string_view>

#include "simrv/tui/framework/Layout.hpp"
#include "simrv/xlen/IdentifierTypes.hpp"

namespace simrv::tui {

using TuiLayout = framework::Layout;
using HartSelection = simrv::xlen::HartId;

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
    CONSOLE,
    DISPLAY,
    LIVE,
    LOG,
    DISASM = CONSOLE
};

struct TuiPageInfo {
    TuiRegPage page;
    TuiCategoryGroup group;
    std::string_view name;
    std::string_view tab_name;
    bool cycle_accurate_only = false;
    bool floating_point_only = false;
    bool vector_only = false;
    bool default_for_group = false;
    bool cycle_page = true;
    bool visible_tab = true;
};

inline constexpr TuiPageInfo kTuiPages[] = {
    {TuiRegPage::GPR, TuiCategoryGroup::Regs, "GPR Registers", "GPR", false, false, false, true},
    {TuiRegPage::FPR, TuiCategoryGroup::Regs, "FPR Float Regs", "FPR", false, true},
    {TuiRegPage::VEC, TuiCategoryGroup::Regs, "Vector Registers", "VEC", false, false, true},
    {TuiRegPage::STACK, TuiCategoryGroup::Memory, "Stack & Memory", "Stack", false, false, false, true},
    {TuiRegPage::CACHE, TuiCategoryGroup::Memory, "Cache Hierarchy", "Cache", true},
    {TuiRegPage::TLB, TuiCategoryGroup::Memory, "TLB & Page Table", "TLB"},
    {TuiRegPage::BUS, TuiCategoryGroup::Memory, "Bus & Coherence", "Bus"},
    {TuiRegPage::PIPELINE, TuiCategoryGroup::Pipeline, "Pipeline Stages", "Stages", false, false, false, true},
    {TuiRegPage::BPRED, TuiCategoryGroup::Pipeline, "Branch Predictor", "BPred", true},
    {TuiRegPage::HAZARD, TuiCategoryGroup::Pipeline, "Hazards & Stalls", "Hazard", true},
    {TuiRegPage::LIVE, TuiCategoryGroup::Tools, "Live Overview", "Live", false, false, false, false, true},
    {TuiRegPage::EXPLAIN, TuiCategoryGroup::Tools, "Instruction Explainer", "Explain", false, false, false, true},
    {TuiRegPage::TRACE, TuiCategoryGroup::Tools, "Execution Trace", "Trace"},
    {TuiRegPage::LOG, TuiCategoryGroup::Tools, "Execution Log", "Log", false, false, false, false, false, true},
    {TuiRegPage::CONSOLE, TuiCategoryGroup::Tools, "Console", "Console", false, false, false, false, false, false},
    {TuiRegPage::DISPLAY, TuiCategoryGroup::Tools, "Display", "Display", false, false, false, false, false, false},
};

struct TuiCategoryInfo {
    TuiCategoryGroup group;
    std::string_view name;
    std::string_view compact_name;
};

inline constexpr TuiCategoryInfo kTuiCategories[] = {
    {TuiCategoryGroup::Regs, "Registers", "Regs"},
    {TuiCategoryGroup::Memory, "Memory", "Memory"},
    {TuiCategoryGroup::Pipeline, "Pipeline", "Pipe"},
    {TuiCategoryGroup::Tools, "Tools", "Tools"},
};

inline constexpr TuiCategoryGroup kTuiCategoryGroups[] = {
    TuiCategoryGroup::Regs, TuiCategoryGroup::Memory, TuiCategoryGroup::Pipeline,
    TuiCategoryGroup::Tools};

[[nodiscard]] constexpr auto get_page_info(TuiRegPage page) -> const TuiPageInfo* {
    for (const auto& info : kTuiPages) {
        if (info.page == page) return &info;
    }
    return nullptr;
}

enum class LayoutPreset : uint8_t {
    GeneralDebug,
    Microarchitecture,
    TraceExecution,
    MemoryInterconnect
};

struct WorkbenchSlot {
    TuiRegPage page = TuiRegPage::GPR;
    int scroll_offset = 0;
};

[[nodiscard]] constexpr auto get_category_group(TuiRegPage page) -> TuiCategoryGroup {
    const auto* info = get_page_info(page);
    return info != nullptr ? info->group : TuiCategoryGroup::Tools;
}

[[nodiscard]] constexpr auto get_page_name(TuiRegPage page) -> const char* {
    const auto* info = get_page_info(page);
    return info != nullptr ? info->name.data() : "Tool View";
}

[[nodiscard]] constexpr auto get_default_page_for_group(TuiCategoryGroup group,
                                                        bool /*cycle_accurate*/ = false)
    -> TuiRegPage {
    for (const auto& info : kTuiPages) {
        if (info.group == group && info.default_for_group) return info.page;
    }
    return TuiRegPage::EXPLAIN;
}

[[nodiscard]] constexpr auto get_category_name(TuiCategoryGroup group) -> const char* {
    for (const auto& info : kTuiCategories) {
        if (info.group == group) return info.name.data();
    }
    return "Tools";
}

enum class TuiRightPanelMode : uint8_t { Terminal, Display };

enum class HeaderAction : uint8_t {
    None,
    RunPause,
    SetSpeed,
    SelectHart,
    TogglePanelMode,
    ToggleAttached,
    OpenSettings,
    OpenGlossary,
    ToggleTheme,
    Reboot,
    ToggleMode,
    LoadBinary
};

struct HeaderHitResult {
    HeaderAction action = HeaderAction::None;
    size_t hart_index = 0;
};

enum class TuiFooterAction : uint8_t {
    Step,
    RunPause,
    SetSpeed,
    SetBreakpoint,
    SetWatchpoint,
    TogglePcBreakpoint,
    ManageBreakpoints,
    InspectMem,
    LoadBinary,
    Quit,
    CycleRegs,
    CycleTools,
    ToggleHelp,
    OpenSettings,
    Reboot,
    SwitchHart,
    ToggleTheme,
    ToggleExecutionMode,
    OpenToolPicker,
    MoveColumnLeft,
    MoveColumnRight,
    AddColumn,
    CloseColumn,
    FocusNextPane,
    FocusPrevPane
};

}  // namespace simrv::tui
