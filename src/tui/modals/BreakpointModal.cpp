/**
 * @file BreakpointModal.cpp
 * @brief Implementation of Breakpoint, Watchpoint, and ManageBreakpoints modal dialog handling.
 */
#include "simrv/tui/modals/BreakpointModal.hpp"

#include <algorithm>
#include <charconv>
#include <format>
#include "simrv/core/Machine.hpp"
#include "simrv/tui/LeftPane.hpp"
#include "simrv/tui/TuiTheme.hpp"

namespace simrv::tui::modals {

void BreakpointModal::open(ModalType type, std::string& input, simrv::core::Machine& machine,
                            LeftPane* left_pane) {
    input.clear();
    if (type == ModalType::SetBreakpoint) {
        input = std::format("0x{:08x}", machine.cpu.state().pc);
    } else if (type == ModalType::SetWatchpoint) {
        input = std::format("0x{:08x}", left_pane ? left_pane->get_inspect_addr() : 0);
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
            auto result =
                std::from_chars(input.data() + 2, input.data() + input.size(), addr, 16);
            ok = (result.ec == std::errc{});
        } else if (std::ranges::all_of(input, ::isxdigit)) {
            auto result = std::from_chars(input.data(), input.data() + input.size(), addr, 16);
            ok = (result.ec == std::errc{});
        } else {
            auto sym_opt = machine.symbols.lookup_name(input);
            if (sym_opt.has_value()) {
                addr = *sym_opt;
                ok = true;
            }
        }

        if (!ok) {
            set_status_override_cb(std::format("Symbol/address not found: {}", input));
            return false;
        }

        machine.breakpoints.add_pc_breakpoint(addr);
        set_status_override_cb(std::format("Breakpoint set at 0x{:08x}", addr));
        return true;

    } else if (type == ModalType::SetWatchpoint) {
        auto parsed_reg = simrv::debug::parse_register_name(input);
        if (parsed_reg.has_value()) {
            machine.breakpoints.add_reg_watchpoint(parsed_reg->type, parsed_reg->index, parsed_reg->canonical_name);
            set_status_override_cb(std::format("Register Watchpoint set on {}", parsed_reg->canonical_name));
            return true;
        }

        Address addr = 0;
        bool ok = false;
        if (input.starts_with("0x") || input.starts_with("0X")) {
            auto result =
                std::from_chars(input.data() + 2, input.data() + input.size(), addr, 16);
            ok = (result.ec == std::errc{});
        } else if (std::ranges::all_of(input, ::isxdigit)) {
            auto result = std::from_chars(input.data(), input.data() + input.size(), addr, 16);
            ok = (result.ec == std::errc{});
        } else {
            auto sym_opt = machine.symbols.lookup_name(input);
            if (sym_opt.has_value()) {
                addr = *sym_opt;
                ok = true;
            }
        }

        if (!ok) {
            set_status_override_cb(std::format("Register, symbol or address not found: {}", input));
            return false;
        }

        machine.breakpoints.add_watchpoint(addr, 4, simrv::debug::WatchType::Write, "");
        set_status_override_cb(std::format("Memory Write Watchpoint set at 0x{:08x}", addr));
        return true;
    }
    return false;
}

void BreakpointModal::render(ModalType type, std::vector<std::string>& content_rows,
                              const std::string& input, const simrv::core::Machine* machine) {
    if (type == ModalType::SetBreakpoint) {
        content_rows.push_back(std::format("{}Enter PC Address (hex) or Symbol:\033[0m", kThemeText));
        content_rows.push_back(std::format("  \033[1m>\033[0m {}{}_\033[0m", kThemeMint, input));
    } else if (type == ModalType::SetWatchpoint) {
        content_rows.push_back(std::format("{}Enter Target Address, Register, or Symbol:\033[0m", kThemeText));
        content_rows.push_back(std::format("  \033[1m>\033[0m {}{}_\033[0m", kThemeMint, input));
        content_rows.push_back(std::format("{}Triggers simulation pause on memory write or register change\033[0m", kThemeMuted));
    } else if (type == ModalType::ManageBreakpoints) {
        content_rows.push_back(std::format("{}Active Breakpoints & Watchpoints:\033[0m", kThemeText));
        content_rows.push_back("");

        if (!machine) return;
        const auto& pc_bps = machine->breakpoints.get_pc_breakpoints();
        const auto& wps = machine->breakpoints.get_watchpoints();

        size_t idx = 1;
        if (pc_bps.empty() && wps.empty()) {
            content_rows.push_back(std::format("  {}No active breakpoints or watchpoints.\033[0m", kThemeMuted));
        } else {
            for (auto pc : pc_bps) {
                std::string sym = machine->symbols.lookup(pc);
                std::string sym_str = sym.empty() ? "" : std::format(" ({})", sym);
                content_rows.push_back(std::format("  \033[1;36m[{}]\033[0m  PC Breakpoint at \033[1;33m0x{:08x}\033[0m{}", idx++, pc, sym_str));
                if (idx > 9) break;
            }
            for (const auto& wp : wps) {
                if (idx > 9) break;
                if (wp.target == simrv::debug::WatchTarget::Memory) {
                    content_rows.push_back(std::format("  \033[1;36m[{}]\033[0m  Memory Write Watchpoint at \033[1;33m0x{:08x}\033[0m", idx++, wp.addr));
                } else {
                    content_rows.push_back(std::format("  \033[1;36m[{}]\033[0m  Register Watchpoint on \033[1;35m{}\033[0m", idx++, wp.reg_name));
                }
            }
        }

        content_rows.push_back("");
        content_rows.push_back(std::format("\033[90mShortcuts: [1-9] Remove  |  [c] Clear All  |  [:] Add BP  |  [w] Add WP  |  [Esc] Close\033[0m"));
    }
}

}  // namespace simrv::tui::modals
