/**
 * @file TuiTypes.hpp
 * @brief Enums and common types for SimRV TUI dashboard.
 */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <utility>

#include "simrv/tui/framework/Layout.hpp"
#include "simrv/xlen/IdentifierTypes.hpp"

namespace simrv::tui {

using TuiLayout = framework::Layout;
using HartSelection = simrv::xlen::HartId;

enum class TuiCategoryGroup : uint8_t { Regs, Memory, Pipeline, Tools };
enum class TuiFastModeContent : uint8_t { Stable, SampledMetrics, FastChangingDetail };

enum class ModalType : uint8_t {
    None,
    SetBreakpoint,
    SetWatchpoint,
    SetSpeed,
    InspectAddress,
    LoadBinary,
    LoadDiskImage,
    Help,
    Glossary,
    Settings,
    ManageBreakpoints,
    Notice,
    PlatformChangeConfirm,
    ConfigurationChangeConfirm,
    LayoutPresets,
    ToolPicker,
    SaveCpuConfig,
    LoadCpuConfig,
    LoadFilePicker,
    MemoryDump,
    SwitchHart,
    LogViewer,
    MissionObjective
};

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
    std::string_view picker_label;
    std::string_view description;
    std::string_view mission_slug = {};
    char accelerator = '\0';
    uint8_t picker_order = 0;
    bool cycle_accurate_only = false;
    bool floating_point_only = false;
    bool vector_only = false;
    bool default_for_group = false;
    bool cycle_page = true;
    bool reg_hotkey_cycle = true;
    bool visible_tab = true;
    bool captures_execution_detail = false;
    TuiFastModeContent fast_mode_content = TuiFastModeContent::Stable;
};

inline constexpr TuiPageInfo kTuiPages[] = {
    {.page = TuiRegPage::GPR,
     .group = TuiCategoryGroup::Regs,
     .name = "GPR Registers",
     .tab_name = "GPR",
     .picker_label = "GPR Registers",
     .description = "Integer Registers (x0-x31 / ABI)",
     .mission_slug = "registers",
     .accelerator = 'g',
     .picker_order = 0,
     .default_for_group = true,
     .fast_mode_content = TuiFastModeContent::FastChangingDetail},
    {.page = TuiRegPage::FPR,
     .group = TuiCategoryGroup::Regs,
     .name = "FPR Float Regs",
     .tab_name = "FPR",
     .picker_label = "FPR Float Regs",
     .description = "Floating-Point Registers (f0-f31)",
     .accelerator = 'f',
     .picker_order = 1,
     .floating_point_only = true,
     .fast_mode_content = TuiFastModeContent::FastChangingDetail},
    {.page = TuiRegPage::VEC,
     .group = TuiCategoryGroup::Regs,
     .name = "Vector Registers",
     .tab_name = "VEC",
     .picker_label = "Vector Registers",
     .description = "Vector Registers (v0-v31 / VTYPE)",
     .accelerator = 'v',
     .picker_order = 2,
     .vector_only = true,
     .fast_mode_content = TuiFastModeContent::FastChangingDetail},
    {.page = TuiRegPage::STACK,
     .group = TuiCategoryGroup::Memory,
     .name = "Stack & Memory",
     .tab_name = "Stack",
     .picker_label = "Stack & Memory",
     .description = "Call Stack & Virtual Memory Inspector",
     .mission_slug = "stack",
     .accelerator = 's',
     .picker_order = 3,
     .default_for_group = true,
     .fast_mode_content = TuiFastModeContent::FastChangingDetail},
    {.page = TuiRegPage::CACHE,
     .group = TuiCategoryGroup::Memory,
     .name = "Cache Hierarchy",
     .tab_name = "Cache",
     .picker_label = "Cache Hierarchy",
     .description = "L1I / L1D / L2 Caches & Way Inspection",
     .mission_slug = "cache",
     .accelerator = 'c',
     .picker_order = 4,
     .cycle_accurate_only = true,
     .fast_mode_content = TuiFastModeContent::FastChangingDetail},
    {.page = TuiRegPage::TLB,
     .group = TuiCategoryGroup::Memory,
     .name = "TLB & Page Table",
     .tab_name = "TLB",
     .picker_label = "TLB & Page Table",
     .description = "TLB Translations & Hardware Page Table Walk",
     .mission_slug = "tlb",
     .accelerator = 'm',
     .picker_order = 5,
     .fast_mode_content = TuiFastModeContent::FastChangingDetail},
    {.page = TuiRegPage::BUS,
     .group = TuiCategoryGroup::Memory,
     .name = "Bus & Coherence",
     .tab_name = "Bus",
     .picker_label = "Bus & Coherence",
     .description = "TileLink Bus Topology & Directory Hub",
     .mission_slug = "bus",
     .accelerator = 'b',
     .picker_order = 6,
     .fast_mode_content = TuiFastModeContent::FastChangingDetail},
    {.page = TuiRegPage::PIPELINE,
     .group = TuiCategoryGroup::Pipeline,
     .name = "Pipeline Stages",
     .tab_name = "Stages",
     .picker_label = "Pipeline Stages",
     .description = "Execution Stage Visualizer & Micro-Ops",
     .mission_slug = "pipeline",
     .accelerator = 'p',
     .picker_order = 7,
     .default_for_group = true,
     .captures_execution_detail = true,
     .fast_mode_content = TuiFastModeContent::FastChangingDetail},
    {.page = TuiRegPage::BPRED,
     .group = TuiCategoryGroup::Pipeline,
     .name = "Branch Predictor",
     .tab_name = "BPred",
     .picker_label = "Branch Predictor",
     .description = "Branch Predictor (BTB, BHT, RAS)",
     .mission_slug = "branch-predictor",
     .accelerator = 'd',
     .picker_order = 8,
     .cycle_accurate_only = true,
     .captures_execution_detail = true,
     .fast_mode_content = TuiFastModeContent::FastChangingDetail},
    {.page = TuiRegPage::HAZARD,
     .group = TuiCategoryGroup::Pipeline,
     .name = "Hazards & Stalls",
     .tab_name = "Hazard",
     .picker_label = "Hazards & Stalls",
     .description = "Scoreboard Dependency Hazards & Stalls",
     .mission_slug = "hazard",
     .accelerator = 'z',
     .picker_order = 9,
     .cycle_accurate_only = true,
     .captures_execution_detail = true,
     .fast_mode_content = TuiFastModeContent::FastChangingDetail},
    {.page = TuiRegPage::EXPLAIN,
     .group = TuiCategoryGroup::Tools,
     .name = "Instruction Explainer",
     .tab_name = "Explain",
     .picker_label = "Instruction Explainer",
     .description = "Instruction Semantic Explainer & Hints",
     .mission_slug = "explain",
     .accelerator = 'e',
     .picker_order = 11,
     .default_for_group = true,
     .captures_execution_detail = true,
     .fast_mode_content = TuiFastModeContent::FastChangingDetail},
    {.page = TuiRegPage::TRACE,
     .group = TuiCategoryGroup::Tools,
     .name = "Execution Trace",
     .tab_name = "Trace",
     .picker_label = "Execution Trace",
     .description = "Instruction Trace History Ring Buffer",
     .mission_slug = "trace",
     .accelerator = 'x',
     .picker_order = 10,
     .captures_execution_detail = true},
    {.page = TuiRegPage::LOG,
     .group = TuiCategoryGroup::Tools,
     .name = "Simulator Log",
     .tab_name = "Log",
     .picker_label = "Simulator Log",
     .description = "Structured simulation and subsystem diagnostic logs",
     .accelerator = 'o',
     .picker_order = 14,
     .cycle_page = false,
     .reg_hotkey_cycle = false},
    {.page = TuiRegPage::CONSOLE,
     .group = TuiCategoryGroup::Tools,
     .name = "Console",
     .tab_name = "Console",
     .picker_label = "Guest Console",
     .description = "Guest Serial Terminal Console (PTY)",
     .mission_slug = "console",
     .accelerator = 't',
     .picker_order = 12,
     .cycle_page = false,
     .reg_hotkey_cycle = true,
     .visible_tab = false},
    {.page = TuiRegPage::DISPLAY,
     .group = TuiCategoryGroup::Tools,
     .name = "Display",
     .tab_name = "Display",
     .picker_label = "Sixel Display",
     .description = "Linux framebuffer rendered through sixel",
     .mission_slug = "display",
     .accelerator = 'y',
     .picker_order = 13,
     .cycle_page = false,
     .reg_hotkey_cycle = false,
     .visible_tab = false},
};

struct TuiCategoryInfo {
    TuiCategoryGroup group;
    std::string_view name;
    std::string_view compact_name;
    std::string_view picker_header;
};

inline constexpr TuiCategoryInfo kTuiCategories[] = {
    {TuiCategoryGroup::Regs, "Registers", "Regs", "REGISTERS"},
    {TuiCategoryGroup::Memory, "Memory", "Memory", "MEMORY & STORAGE"},
    {TuiCategoryGroup::Pipeline, "Pipeline", "Pipe", "PIPELINE & CORE"},
    {TuiCategoryGroup::Tools, "Tools", "Tools", "ANALYSIS & SYSTEM"},
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

[[nodiscard]] constexpr auto get_category_info(TuiCategoryGroup group) -> const TuiCategoryInfo* {
    for (const auto& info : kTuiCategories) {
        if (info.group == group) return &info;
    }
    return nullptr;
}

[[nodiscard]] constexpr auto parse_tui_page_slug(std::string_view slug)
    -> std::optional<TuiRegPage> {
    if (slug.empty()) return std::nullopt;
    for (const auto& info : kTuiPages) {
        if (!info.mission_slug.empty() && info.mission_slug == slug) {
            return info.page;
        }
    }
    return std::nullopt;
}

enum class LayoutPreset : uint8_t {
    GeneralDebug,
    Microarchitecture,
    TraceExecution,
    MemoryInterconnect
};

struct LayoutPresetVariant {
    std::array<TuiRegPage, 4> cols_4;
    std::array<TuiRegPage, 3> cols_3;
    std::array<TuiRegPage, 2> cols_2;
    TuiRegPage cols_1;
};

struct LayoutPresetInfo {
    LayoutPreset preset;
    std::string_view name;
    std::string_view shortcut_label;
    std::string_view modal_summary;
    std::string_view status_notice;
    LayoutPresetVariant cycle_accurate;
    LayoutPresetVariant instruction_accurate;

    [[nodiscard]] constexpr auto slots_for(uint8_t max_cols, bool is_cycle) const noexcept
        -> std::pair<std::array<TuiRegPage, 4>, size_t> {
        const auto& v = is_cycle ? cycle_accurate : instruction_accurate;
        if (max_cols >= 4) return {v.cols_4, 4};
        if (max_cols == 3) {
            return {{v.cols_3[0], v.cols_3[1], v.cols_3[2], TuiRegPage::GPR}, 3};
        }
        if (max_cols == 2) {
            return {{v.cols_2[0], v.cols_2[1], TuiRegPage::GPR, TuiRegPage::GPR}, 2};
        }
        return {{v.cols_1, TuiRegPage::GPR, TuiRegPage::GPR, TuiRegPage::GPR}, 1};
    }
};

inline constexpr std::array<LayoutPresetInfo, 4> kLayoutPresets = {{
    {.preset = LayoutPreset::GeneralDebug,
     .name = "General Debug",
     .shortcut_label = "1 / Alt-1",
     .modal_summary = "General Debug        [GPR, Stack, Trace, Console]",
     .status_notice = "Layout Preset: General Debug [Alt-1]",
     .cycle_accurate = {.cols_4 = {TuiRegPage::GPR, TuiRegPage::STACK, TuiRegPage::TRACE,
                                   TuiRegPage::CONSOLE},
                        .cols_3 = {TuiRegPage::GPR, TuiRegPage::STACK, TuiRegPage::CONSOLE},
                        .cols_2 = {TuiRegPage::GPR, TuiRegPage::CONSOLE},
                        .cols_1 = TuiRegPage::GPR},
     .instruction_accurate = {.cols_4 = {TuiRegPage::GPR, TuiRegPage::STACK, TuiRegPage::TRACE,
                                         TuiRegPage::CONSOLE},
                              .cols_3 = {TuiRegPage::GPR, TuiRegPage::STACK, TuiRegPage::CONSOLE},
                              .cols_2 = {TuiRegPage::GPR, TuiRegPage::CONSOLE},
                              .cols_1 = TuiRegPage::GPR}},
    {.preset = LayoutPreset::Microarchitecture,
     .name = "Microarchitecture",
     .shortcut_label = "2 / Alt-2",
     .modal_summary = "Microarchitecture    [Pipeline, Cache, Hazard, Console]",
     .status_notice = "Layout Preset: Microarchitecture [Alt-2]",
     .cycle_accurate = {.cols_4 = {TuiRegPage::PIPELINE, TuiRegPage::CACHE, TuiRegPage::HAZARD,
                                   TuiRegPage::CONSOLE},
                        .cols_3 = {TuiRegPage::PIPELINE, TuiRegPage::CACHE, TuiRegPage::CONSOLE},
                        .cols_2 = {TuiRegPage::PIPELINE, TuiRegPage::CONSOLE},
                        .cols_1 = TuiRegPage::PIPELINE},
     .instruction_accurate = {.cols_4 = {TuiRegPage::PIPELINE, TuiRegPage::STACK, TuiRegPage::TRACE,
                                         TuiRegPage::CONSOLE},
                              .cols_3 = {TuiRegPage::PIPELINE, TuiRegPage::STACK,
                                         TuiRegPage::CONSOLE},
                              .cols_2 = {TuiRegPage::PIPELINE, TuiRegPage::CONSOLE},
                              .cols_1 = TuiRegPage::PIPELINE}},
    {.preset = LayoutPreset::TraceExecution,
     .name = "Trace & Execution",
     .shortcut_label = "3 / Alt-3",
     .modal_summary = "Trace & Execution    [GPR, Trace, Explain, Console]",
     .status_notice = "Layout Preset: Trace & Execution [Alt-3]",
     .cycle_accurate = {.cols_4 = {TuiRegPage::GPR, TuiRegPage::TRACE, TuiRegPage::EXPLAIN,
                                   TuiRegPage::CONSOLE},
                        .cols_3 = {TuiRegPage::GPR, TuiRegPage::TRACE, TuiRegPage::CONSOLE},
                        .cols_2 = {TuiRegPage::TRACE, TuiRegPage::CONSOLE},
                        .cols_1 = TuiRegPage::TRACE},
     .instruction_accurate = {.cols_4 = {TuiRegPage::GPR, TuiRegPage::TRACE, TuiRegPage::EXPLAIN,
                                         TuiRegPage::CONSOLE},
                              .cols_3 = {TuiRegPage::GPR, TuiRegPage::TRACE, TuiRegPage::CONSOLE},
                              .cols_2 = {TuiRegPage::TRACE, TuiRegPage::CONSOLE},
                              .cols_1 = TuiRegPage::TRACE}},
    {.preset = LayoutPreset::MemoryInterconnect,
     .name = "Memory & Interconnect",
     .shortcut_label = "4 / Alt-4",
     .modal_summary = "Memory & Interconnect[Stack, TLB/Cache, Bus]",
     .status_notice = "Layout Preset: Memory & Interconnect [Alt-4]",
     .cycle_accurate = {.cols_4 = {TuiRegPage::STACK, TuiRegPage::CACHE, TuiRegPage::TLB,
                                   TuiRegPage::BUS},
                        .cols_3 = {TuiRegPage::STACK, TuiRegPage::CACHE, TuiRegPage::BUS},
                        .cols_2 = {TuiRegPage::STACK, TuiRegPage::BUS},
                        .cols_1 = TuiRegPage::STACK},
     .instruction_accurate = {.cols_4 = {TuiRegPage::STACK, TuiRegPage::TLB, TuiRegPage::BUS,
                                         TuiRegPage::CONSOLE},
                              .cols_3 = {TuiRegPage::STACK, TuiRegPage::TLB, TuiRegPage::BUS},
                              .cols_2 = {TuiRegPage::STACK, TuiRegPage::BUS},
                              .cols_1 = TuiRegPage::STACK}},
}};

[[nodiscard]] constexpr auto get_layout_preset_info(LayoutPreset preset) noexcept
    -> const LayoutPresetInfo& {
    for (const auto& info : kLayoutPresets) {
        if (info.preset == preset) return info;
    }
    return kLayoutPresets[0];
}

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

[[nodiscard]] constexpr auto get_category_picker_header(TuiCategoryGroup group)
    -> std::string_view {
    const auto* info = get_category_info(group);
    return info != nullptr ? info->picker_header : "ANALYSIS & SYSTEM";
}

[[nodiscard]] constexpr auto cycle_category_group(TuiCategoryGroup current,
                                                  bool reverse = false) noexcept
    -> TuiCategoryGroup {
    constexpr size_t kCount = std::size(kTuiCategoryGroups);
    for (size_t i = 0; i < kCount; ++i) {
        if (kTuiCategoryGroups[i] == current) {
            return reverse ? kTuiCategoryGroups[(i + kCount - 1) % kCount]
                           : kTuiCategoryGroups[(i + 1) % kCount];
        }
    }
    return TuiCategoryGroup::Regs;
}

[[nodiscard]] constexpr auto cycle_page_in_group(TuiRegPage current, bool cycle_accurate,
                                                 bool has_fp, bool has_vector, bool reverse = false,
                                                 bool use_reg_hotkey_cycle = false) noexcept
    -> TuiRegPage {
    const TuiCategoryGroup grp = get_category_group(current);
    std::array<TuiRegPage, 8> candidates{};
    size_t count = 0;
    for (const auto& info : kTuiPages) {
        const bool in_cycle = use_reg_hotkey_cycle ? info.reg_hotkey_cycle : info.cycle_page;
        if (info.group != grp || !in_cycle || (info.cycle_accurate_only && !cycle_accurate) ||
            (info.floating_point_only && !has_fp) || (info.vector_only && !has_vector)) {
            continue;
        }
        candidates[count++] = info.page;
    }
    if (count == 0) return current;
    for (size_t i = 0; i < count; ++i) {
        if (candidates[i] == current) {
            return reverse ? candidates[(i + count - 1) % count] : candidates[(i + 1) % count];
        }
    }
    return reverse ? candidates[count > 1 ? count - 2 : 0] : candidates[0];
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
