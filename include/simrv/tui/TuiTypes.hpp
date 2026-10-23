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
enum class TuiFastModeContent : uint8_t { Stable, SampledMetrics, FastChangingDetail };

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
    LIVE,  // Retired page id, reserved so saved layout ordinals remain stable.
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
    bool captures_execution_detail = false;
    TuiFastModeContent fast_mode_content = TuiFastModeContent::Stable;
};

inline constexpr TuiPageInfo kTuiPages[] = {
    {.page = TuiRegPage::GPR,
     .group = TuiCategoryGroup::Regs,
     .name = "GPR Registers",
     .tab_name = "GPR",
     .default_for_group = true,
     .fast_mode_content = TuiFastModeContent::FastChangingDetail},
    {.page = TuiRegPage::FPR,
     .group = TuiCategoryGroup::Regs,
     .name = "FPR Float Regs",
     .tab_name = "FPR",
     .floating_point_only = true,
     .fast_mode_content = TuiFastModeContent::FastChangingDetail},
    {.page = TuiRegPage::VEC,
     .group = TuiCategoryGroup::Regs,
     .name = "Vector Registers",
     .tab_name = "VEC",
     .vector_only = true,
     .fast_mode_content = TuiFastModeContent::FastChangingDetail},
    {.page = TuiRegPage::STACK,
     .group = TuiCategoryGroup::Memory,
     .name = "Stack & Memory",
     .tab_name = "Stack",
     .default_for_group = true,
     .fast_mode_content = TuiFastModeContent::FastChangingDetail},
    {.page = TuiRegPage::CACHE,
     .group = TuiCategoryGroup::Memory,
     .name = "Cache Hierarchy",
     .tab_name = "Cache",
     .cycle_accurate_only = true,
     .fast_mode_content = TuiFastModeContent::FastChangingDetail},
    {.page = TuiRegPage::TLB,
     .group = TuiCategoryGroup::Memory,
     .name = "TLB & Page Table",
     .tab_name = "TLB",
     .fast_mode_content = TuiFastModeContent::FastChangingDetail},
    {.page = TuiRegPage::BUS,
     .group = TuiCategoryGroup::Memory,
     .name = "Bus & Coherence",
     .tab_name = "Bus",
     .fast_mode_content = TuiFastModeContent::FastChangingDetail},
    {.page = TuiRegPage::PIPELINE,
     .group = TuiCategoryGroup::Pipeline,
     .name = "Pipeline Stages",
     .tab_name = "Stages",
     .default_for_group = true,
     .captures_execution_detail = true,
     .fast_mode_content = TuiFastModeContent::FastChangingDetail},
    {.page = TuiRegPage::BPRED,
     .group = TuiCategoryGroup::Pipeline,
     .name = "Branch Predictor",
     .tab_name = "BPred",
     .cycle_accurate_only = true,
     .captures_execution_detail = true,
     .fast_mode_content = TuiFastModeContent::FastChangingDetail},
    {.page = TuiRegPage::HAZARD,
     .group = TuiCategoryGroup::Pipeline,
     .name = "Hazards & Stalls",
     .tab_name = "Hazard",
     .cycle_accurate_only = true,
     .captures_execution_detail = true,
     .fast_mode_content = TuiFastModeContent::FastChangingDetail},
    {.page = TuiRegPage::EXPLAIN,
     .group = TuiCategoryGroup::Tools,
     .name = "Instruction Explainer",
     .tab_name = "Explain",
     .default_for_group = true,
     .captures_execution_detail = true,
     .fast_mode_content = TuiFastModeContent::FastChangingDetail},
    {.page = TuiRegPage::TRACE,
     .group = TuiCategoryGroup::Tools,
     .name = "Execution Trace",
     .tab_name = "Trace",
     .captures_execution_detail = true},
    {.page = TuiRegPage::LOG,
     .group = TuiCategoryGroup::Tools,
     .name = "Execution Log",
     .tab_name = "Log",
     .cycle_page = false},
    {.page = TuiRegPage::CONSOLE,
     .group = TuiCategoryGroup::Tools,
     .name = "Console",
     .tab_name = "Console",
     .cycle_page = false,
     .visible_tab = false},
    {.page = TuiRegPage::DISPLAY,
     .group = TuiCategoryGroup::Tools,
     .name = "Display",
     .tab_name = "Display",
     .cycle_page = false,
     .visible_tab = false},
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
