/**
 * @file BreakpointModal.cpp
 * @brief Implementation of Breakpoint and Watchpoint modal dialog handling.
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

    Address addr = 0;
    bool ok = false;
    if (input.starts_with("0x") || input.starts_with("0X")) {
        auto result =
            std::from_chars(input.data() + 2, input.data() + input.size(), addr, 16);
        ok = (result.ec == std::errc{});
    } else if (std::all_of(input.begin(), input.end(), ::isxdigit)) {
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

    if (type == ModalType::SetBreakpoint) {
        machine.breakpoints.add_pc_breakpoint(addr);
        set_status_override_cb(std::format("Breakpoint set at 0x{:08x}", addr));
    } else if (type == ModalType::SetWatchpoint) {
        machine.breakpoints.add_watchpoint(addr, 4, simrv::debug::WatchType::Write, "");
        set_status_override_cb(std::format("Write Watchpoint set at 0x{:08x}", addr));
    }
    return true;
}

void BreakpointModal::render(ModalType type, std::vector<std::string>& content_rows,
                              const std::string& input) {
    if (type == ModalType::SetBreakpoint) {
        content_rows.push_back(std::format("{}Enter PC Address (hex) or Symbol:\033[0m", kThemeText));
        content_rows.push_back(std::format("  \033[1m>\033[0m {}{}_\033[0m", kThemeMint, input));
    } else if (type == ModalType::SetWatchpoint) {
        content_rows.push_back(std::format("{}Enter Target Watch Address (hex) or Symbol:\033[0m", kThemeText));
        content_rows.push_back(std::format("  \033[1m>\033[0m {}{}_\033[0m", kThemeMint, input));
        content_rows.push_back(std::format("{}Triggers simulation pause on write access\033[0m", kThemeMuted));
    }
}

}  // namespace simrv::tui::modals
