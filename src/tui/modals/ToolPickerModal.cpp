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
        {TuiRegPage::GPR, 'g', "GPR Registers", "Integer Registers (x0-x31 / ABI)",
         TuiCategoryGroup::Regs},
        {TuiRegPage::FPR, 'f', "FPR Float Regs", "Floating-Point Registers (f0-f31)",
         TuiCategoryGroup::Regs},
        {TuiRegPage::VEC, 'v', "Vector Registers", "Vector Registers (v0-v31 / VTYPE)",
         TuiCategoryGroup::Regs},
        // Memory
        {TuiRegPage::STACK, 's', "Stack & Memory", "Call Stack & Virtual Memory Inspector",
         TuiCategoryGroup::Memory},
        {TuiRegPage::CACHE, 'c', "Cache Hierarchy", "L1I / L1D / L2 Caches & Way Inspection",
         TuiCategoryGroup::Memory},
        {TuiRegPage::TLB, 'm', "TLB & Page Table", "TLB Translations & Hardware Page Table Walk",
         TuiCategoryGroup::Memory},
        {TuiRegPage::BUS, 'b', "Bus & Coherence", "TileLink Bus Topology & Directory Hub",
         TuiCategoryGroup::Memory},
        // Pipeline
        {TuiRegPage::PIPELINE, 'p', "Pipeline Stages", "Execution Stage Visualizer & Micro-Ops",
         TuiCategoryGroup::Pipeline},
        {TuiRegPage::BPRED, 'd', "Branch Predictor", "Branch Predictor (BTB, BHT, RAS)",
         TuiCategoryGroup::Pipeline},
        {TuiRegPage::HAZARD, 'z', "Hazards & Stalls", "Scoreboard Dependency Hazards & Stalls",
         TuiCategoryGroup::Pipeline},
        // Tools & System
        {TuiRegPage::TRACE, 'x', "Execution Trace", "Instruction Trace History Ring Buffer",
         TuiCategoryGroup::Tools},
        {TuiRegPage::EXPLAIN, 'e', "Instruction Explainer",
         "Instruction Semantic Explainer & Hints", TuiCategoryGroup::Tools},
        {TuiRegPage::CONSOLE, 't', "Guest Console", "Guest Serial Terminal Console (PTY)",
         TuiCategoryGroup::Tools},
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
                             int /*box_w*/) {
    add_row_cb(std::format("Assign a tool or view to Column {} (currently: {}):", slot_idx + 1,
                           get_page_name(current_page)));
    add_row_cb("");

    const auto& tools = tools_registry();
    TuiCategoryGroup current_grp = static_cast<TuiCategoryGroup>(99);

    for (size_t i = 0; i < tools.size(); ++i) {
        const auto& tool = tools[i];
        if (tool.group != current_grp) {
            current_grp = tool.group;
            if (i > 0) add_row_cb("");
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

    add_row_cb("");
    add_row_cb(build_modal_footer({{"[Enter / a-z]", "Assign Tool"},
                                   {"[Up/Down]", "Navigate"},
                                   {"[Tab]", "Target Next Col"},
                                   {"[Esc / q]", "Cancel"}}));
}

}  // namespace simrv::tui::modals
