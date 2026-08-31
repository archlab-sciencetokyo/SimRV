/**
 * @file BreakpointModal.cpp
 * @brief Implementation of Breakpoint, Watchpoint, and ManageBreakpoints modal dialog handling.
 */
#include "simrv/tui/modals/BreakpointModal.hpp"

#include <algorithm>
#include <charconv>
#include <format>

#include "simrv/core/Machine.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/modals/ModalComponents.hpp"
#include "simrv/tui/panels/InspectorPane.hpp"

namespace simrv::tui::modals {

void BreakpointModal::open(ModalType type, std::string& input, simrv::core::Machine& machine,
                           InspectorPane* inspector_pane) {
    input.clear();
    if (type == ModalType::SetBreakpoint) {
        input = std::format("0x{:08x}", machine.primary_hart().state().pc);
    } else if (type == ModalType::SetWatchpoint) {
        input = std::format("0x{:08x}", inspector_pane ? inspector_pane->get_inspect_addr() : 0);
    }
}

auto BreakpointModal::submit(ModalType type, const std::string& input,
                             simrv::core::Machine& machine,
                             const std::function<void(const std::string&)>& set_status_override_cb)
    -> bool {
    if (input.empty()) return false;

    if (type == ModalType::SetBreakpoint) {
        Address addr = 0;
        bool ok = false;
        if (input.starts_with("0x") || input.starts_with("0X")) {
            auto result = std::from_chars(input.data() + 2, input.data() + input.size(), addr, 16);
            ok = (result.ec == std::errc{});
        } else if (std::ranges::all_of(input, ::isxdigit)) {
            auto result = std::from_chars(input.data(), input.data() + input.size(), addr, 16);
            ok = (result.ec == std::errc{});
        } else {
            auto sym_opt = machine.symbol_table().lookup_name(input);
            if (sym_opt.has_value()) {
                addr = *sym_opt;
                ok = true;
            }
        }

        if (!ok) {
            set_status_override_cb(std::format("Symbol/address not found: {}", input));
            return false;
        }

        machine.breakpoint_manager().add_pc_breakpoint(addr);
        set_status_override_cb(std::format("Breakpoint set at 0x{:08x}", addr));
        return true;

    } else if (type == ModalType::SetWatchpoint) {
        auto parsed_reg = simrv::debug::parse_register_name(input);
        if (parsed_reg.has_value()) {
            machine.breakpoint_manager().add_reg_watchpoint(parsed_reg->type, parsed_reg->index,
                                                            parsed_reg->canonical_name);
            set_status_override_cb(
                std::format("Register Watchpoint set on {}", parsed_reg->canonical_name));
            return true;
        }

        Address addr = 0;
        bool ok = false;
        if (input.starts_with("0x") || input.starts_with("0X")) {
            auto result = std::from_chars(input.data() + 2, input.data() + input.size(), addr, 16);
            ok = (result.ec == std::errc{});
        } else if (std::ranges::all_of(input, ::isxdigit)) {
            auto result = std::from_chars(input.data(), input.data() + input.size(), addr, 16);
            ok = (result.ec == std::errc{});
        } else {
            auto sym_opt = machine.symbol_table().lookup_name(input);
            if (sym_opt.has_value()) {
                addr = *sym_opt;
                ok = true;
            }
        }

        if (!ok) {
            set_status_override_cb(std::format("Register, symbol or address not found: {}", input));
            return false;
        }

        machine.breakpoint_manager().add_watchpoint(addr, 4, simrv::debug::WatchType::Write, "");
        set_status_override_cb(std::format("Memory Write Watchpoint set at 0x{:08x}", addr));
        return true;
    }
    return false;
}

void BreakpointModal::move_cursor(int& cursor, int delta, const simrv::core::Machine& machine) {
    const auto& pc_bps = machine.breakpoint_manager().get_pc_breakpoints();
    const auto& wps = machine.breakpoint_manager().get_watchpoints();
    int count = static_cast<int>(pc_bps.size() + wps.size());
    if (count == 0) {
        cursor = 0;
        return;
    }
    cursor = (cursor + delta + count) % count;
}

auto BreakpointModal::remove_at_cursor(
    int& cursor, simrv::core::Machine& machine,
    const std::function<void(const std::string&)>& set_status_override_cb) -> bool {
    const auto& pc_bps = machine.breakpoint_manager().get_pc_breakpoints();
    const auto& wps = machine.breakpoint_manager().get_watchpoints();
    int total = static_cast<int>(pc_bps.size() + wps.size());
    if (total == 0 || cursor < 0 || cursor >= total) return false;

    if (cursor < static_cast<int>(pc_bps.size())) {
        auto it = std::next(pc_bps.begin(), cursor);
        Address pc = *it;
        machine.breakpoint_manager().remove_pc_breakpoint(pc);
        if (set_status_override_cb) {
            set_status_override_cb(std::format("Removed breakpoint at 0x{:08x}", pc));
        }
    } else {
        std::size_t wp_idx = static_cast<std::size_t>(cursor - static_cast<int>(pc_bps.size()));
        const auto& wp = wps[wp_idx];
        std::string name = (wp.target == simrv::debug::WatchTarget::Memory)
                               ? std::format("0x{:08x}", wp.addr)
                               : wp.reg_name;
        machine.breakpoint_manager().remove_watchpoint(wp_idx);
        if (set_status_override_cb) {
            set_status_override_cb(std::format("Removed watchpoint on {}", name));
        }
    }
    const auto& new_pc_bps = machine.breakpoint_manager().get_pc_breakpoints();
    const auto& new_wps = machine.breakpoint_manager().get_watchpoints();
    int new_total = static_cast<int>(new_pc_bps.size() + new_wps.size());
    if (new_total == 0) {
        cursor = 0;
    } else if (cursor >= new_total) {
        cursor = new_total - 1;
    }
    return true;
}

void BreakpointModal::render(ModalType type, std::vector<std::string>& content_rows,
                             const std::string& input, const simrv::core::Machine* machine,
                             int bp_cursor) {
    if (type == ModalType::SetBreakpoint) {
        build_text_input_rows(content_rows, "Target PC Address (hex) or Symbol:", input);
        content_rows.push_back("");
        content_rows.push_back(
            build_modal_footer({{"[Enter]", "Set Breakpoint"}, {"[Esc]", "Cancel"}}));
    } else if (type == ModalType::SetWatchpoint) {
        build_text_input_rows(content_rows, "Target Register, Address, or Symbol:", input,
                              "Pauses simulation on memory write or register state change.");
        content_rows.push_back("");
        content_rows.push_back(
            build_modal_footer({{"[Enter]", "Set Watchpoint"}, {"[Esc]", "Cancel"}}));
    } else if (type == ModalType::ManageBreakpoints) {
        content_rows.push_back(
            std::format("{}Active Breakpoints & Watchpoints:\033[0m", kThemeText));
        content_rows.push_back("");

        if (!machine) return;
        const auto& pc_bps = machine->breakpoint_manager().get_pc_breakpoints();
        const auto& wps = machine->breakpoint_manager().get_watchpoints();

        int item_idx = 0;
        if (pc_bps.empty() && wps.empty()) {
            content_rows.push_back(
                std::format("  {}No active breakpoints or watchpoints.\033[0m", kThemeMuted));
        } else {
            for (auto pc : pc_bps) {
                bool is_sel = (item_idx == bp_cursor);
                std::string prefix = is_sel ? std::format("{}>\033[0m ", kThemeMint) : "  ";
                std::string sym = machine->symbol_table().lookup(pc);
                std::string sym_str = sym.empty() ? "" : std::format(" ({})", sym);
                content_rows.push_back(std::format("{}PC Breakpoint at \033[1;33m0x{:08x}\033[0m{}",
                                                   prefix, pc, sym_str));
                item_idx++;
                if (item_idx >= 9) break;
            }
            for (const auto& wp : wps) {
                if (item_idx >= 9) break;
                bool is_sel = (item_idx == bp_cursor);
                std::string prefix = is_sel ? std::format("{}>\033[0m ", kThemeMint) : "  ";
                if (wp.target == simrv::debug::WatchTarget::Memory) {
                    content_rows.push_back(std::format(
                        "{}Memory Watchpoint at \033[1;33m0x{:08x}\033[0m", prefix, wp.addr));
                } else {
                    content_rows.push_back(std::format(
                        "{}Register Watchpoint on \033[1;35m{}\033[0m", prefix, wp.reg_name));
                }
                item_idx++;
            }
        }

        content_rows.push_back("");
        content_rows.push_back(build_modal_footer({{"[↑]", "Up"},
                                                   {"[↓]", "Down"},
                                                   {"[Backspace/d]", "Delete"},
                                                   {"[c]", "Clear"},
                                                   {"[Esc]", "Close"}}));
        content_rows.push_back(
            build_modal_footer({{":", "Add Breakpoint"}, {"[w]", "Add Watchpoint"}}));
    }
}

}  // namespace simrv::tui::modals
