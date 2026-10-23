/**
 * @file ToolPickerModal.cpp
 * @brief Categorized keyboard-driven tool picker modal for multi-column workbench slots.
 */
#include "simrv/tui/modals/ToolPickerModal.hpp"

#include <cctype>
#include <format>
#include <span>

#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/modals/ModalComponents.hpp"

namespace simrv::tui::modals {

namespace {

auto tools_registry() -> const std::vector<ToolEntry>& {
    static const std::vector<ToolEntry> kTools = {
        // Registers
        {.page = TuiRegPage::GPR,
         .accelerator = 'g',
         .label = "GPR Registers",
         .description = "Integer Registers (x0-x31 / ABI)",
         .group = TuiCategoryGroup::Regs},
        {.page = TuiRegPage::FPR,
         .accelerator = 'f',
         .label = "FPR Float Regs",
         .description = "Floating-Point Registers (f0-f31)",
         .group = TuiCategoryGroup::Regs},
        {.page = TuiRegPage::VEC,
         .accelerator = 'v',
         .label = "Vector Registers",
         .description = "Vector Registers (v0-v31 / VTYPE)",
         .group = TuiCategoryGroup::Regs},
        // Memory
        {.page = TuiRegPage::STACK,
         .accelerator = 's',
         .label = "Stack & Memory",
         .description = "Call Stack & Virtual Memory Inspector",
         .group = TuiCategoryGroup::Memory},
        {.page = TuiRegPage::CACHE,
         .accelerator = 'c',
         .label = "Cache Hierarchy",
         .description = "L1I / L1D / L2 Caches & Way Inspection",
         .group = TuiCategoryGroup::Memory},
        {.page = TuiRegPage::TLB,
         .accelerator = 'm',
         .label = "TLB & Page Table",
         .description = "TLB Translations & Hardware Page Table Walk",
         .group = TuiCategoryGroup::Memory},
        {.page = TuiRegPage::BUS,
         .accelerator = 'b',
         .label = "Bus & Coherence",
         .description = "TileLink Bus Topology & Directory Hub",
         .group = TuiCategoryGroup::Memory},
        // Pipeline
        {.page = TuiRegPage::PIPELINE,
         .accelerator = 'p',
         .label = "Pipeline Stages",
         .description = "Execution Stage Visualizer & Micro-Ops",
         .group = TuiCategoryGroup::Pipeline},
        {.page = TuiRegPage::BPRED,
         .accelerator = 'd',
         .label = "Branch Predictor",
         .description = "Branch Predictor (BTB, BHT, RAS)",
         .group = TuiCategoryGroup::Pipeline},
        {.page = TuiRegPage::HAZARD,
         .accelerator = 'z',
         .label = "Hazards & Stalls",
         .description = "Scoreboard Dependency Hazards & Stalls",
         .group = TuiCategoryGroup::Pipeline},
        // Tools & System
        {.page = TuiRegPage::TRACE,
         .accelerator = 'x',
         .label = "Execution Trace",
         .description = "Instruction Trace History Ring Buffer",
         .group = TuiCategoryGroup::Tools},
        {.page = TuiRegPage::EXPLAIN,
         .accelerator = 'e',
         .label = "Instruction Explainer",
         .description = "Instruction Semantic Explainer & Hints",
         .group = TuiCategoryGroup::Tools},
        {.page = TuiRegPage::CONSOLE,
         .accelerator = 't',
         .label = "Guest Console",
         .description = "Guest Serial Terminal Console (PTY)",
         .group = TuiCategoryGroup::Tools},
        {.page = TuiRegPage::DISPLAY,
         .accelerator = 'y',
         .label = "Sixel Display",
         .description = "Linux framebuffer rendered through sixel",
         .group = TuiCategoryGroup::Tools},
        {.page = TuiRegPage::LOG,
         .accelerator = 'o',
         .label = "Execution Log",
         .description = "Structured simulation and subsystem diagnostic logs",
         .group = TuiCategoryGroup::Tools},
    };
    return kTools;
}

}  // namespace

auto ToolPickerModal::all_tools() -> const std::vector<ToolEntry>& { return tools_registry(); }

auto ToolPickerModal::total_tool_count() -> size_t { return tools_registry().size(); }

auto ToolPickerModal::tool_at_index(size_t index) -> std::optional<TuiRegPage> {
    const auto& list = tools_registry();
    if (index < list.size()) {
        return list[index].page;
    }
    return std::nullopt;
}

auto ToolPickerModal::tool_index_at_row(int content_row) -> std::optional<size_t> {
    switch (content_row) {
        case 3:
            return 0;  // GPR
        case 4:
            return 1;  // FPR
        case 5:
            return 2;  // VEC
        case 7:
            return 3;  // STACK
        case 8:
            return 4;  // CACHE
        case 9:
            return 5;  // TLB
        case 10:
            return 6;  // BUS
        case 12:
            return 7;  // PIPELINE
        case 13:
            return 8;  // BPRED
        case 14:
            return 9;  // HAZARD
        case 16:
            return 10;  // TRACE
        case 17:
            return 11;  // EXPLAIN
        case 18:
            return 12;  // CONSOLE
        case 19:
            return 13;  // DISPLAY
        case 20:
            return 14;  // LOG
        default:
            return std::nullopt;
    }
}

auto ToolPickerModal::row_for_tool_index(size_t index) -> int {
    static constexpr std::array<int, 15> kRows = {
        3,  4,  5,              // GPR, FPR, VEC
        7,  8,  9,  10,         // STACK, CACHE, TLB, BUS
        12, 13, 14,             // PIPELINE, BPRED, HAZARD
        16, 17, 18, 19, 20  // TRACE, EXPLAIN, CONSOLE, DISPLAY, LOG
    };
    if (index < kRows.size()) {
        return kRows[index];
    }
    return 3;
}

auto ToolPickerModal::find_by_accelerator(char c) -> std::optional<TuiRegPage> {
    const char lower = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    for (const auto& item : tools_registry()) {
        if (item.accelerator == lower) {
            return item.page;
        }
    }
    return std::nullopt;
}

void ToolPickerModal::render(std::vector<std::string>& /*content_rows*/,
                             const std::function<void(const std::string&)>& add_row_cb,
                             int slot_idx, int cursor, TuiRegPage current_page, int /*term_height*/,
                             int /*box_w*/, int num_slots) {
    // Row 0: Column selection tab bar
    static constexpr std::array<std::string_view, 4> kTabLabels = {"Column 1", "Column 2",
                                                                   "Column 3", "Column 4"};
    int const effective_slots = std::clamp(num_slots, 1, 4);
    std::vector<std::string_view> tabs;
    tabs.reserve(static_cast<size_t>(effective_slots));
    for (int s = 0; s < effective_slots; ++s) {
        tabs.push_back(kTabLabels.at(static_cast<size_t>(s)));
    }
    add_row_cb(build_modal_tab_bar(tabs, static_cast<size_t>(slot_idx)));

    // Row 1: Target slot description
    add_row_cb(std::format(" Assign a tool to Column {} (currently: {}):", slot_idx + 1,
                           get_page_name(current_page)));

    const auto& tools = tools_registry();
    auto current_grp = static_cast<TuiCategoryGroup>(99);

    for (size_t i = 0; i < tools.size(); ++i) {
        const auto& tool = tools[i];
        if (tool.group != current_grp) {
            current_grp = tool.group;
            std::string group_title;
            switch (current_grp) {
                case TuiCategoryGroup::Regs:
                    group_title = "REGISTERS";
                    break;
                case TuiCategoryGroup::Memory:
                    group_title = "MEMORY & STORAGE";
                    break;
                case TuiCategoryGroup::Pipeline:
                    group_title = "PIPELINE & CORE";
                    break;
                case TuiCategoryGroup::Tools:
                default:
                    group_title = "ANALYSIS & SYSTEM";
                    break;
            }
            add_row_cb(std::format(" \033[1;36m── {} ──\033[0m", group_title));
        }

        const bool is_cursor = (static_cast<int>(i) == cursor);
        const bool is_active = (tool.page == current_page);

        std::string badge = std::format("[{}]", tool.accelerator);
        std::string active_tag = is_active ? " \033[32m[Active]\033[0m" : "";

        if (is_cursor) {
            add_row_cb(std::format(" \033[1;7m {:<4} {:<22} \033[0m {}{:<40}\033[0m{}", badge,
                                   tool.label, kThemeMuted, tool.description, active_tag));
        } else {
            add_row_cb(std::format("  \033[1;33m{:<4}\033[0m {:<22} {}{:<40}\033[0m{}", badge,
                                   tool.label, kThemeMuted, tool.description, active_tag));
        }
    }

    // Row 19: Blank spacing row
    add_row_cb("");
    // Row 20: Clickable footer action row
    add_row_cb(build_modal_footer({{.key = "[Enter / Click]", .action = "Assign Tool"},
                                   {.key = "[Up/Down]", .action = "Navigate"},
                                   {.key = "[Tab / Click]", .action = "Target Col"},
                                   {.key = "[Esc / q]", .action = "Cancel"}}));
}

}  // namespace simrv::tui::modals
