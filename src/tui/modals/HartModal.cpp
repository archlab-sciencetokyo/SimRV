/**
 * @file HartModal.cpp
 * @brief Multi-Hart SMP Visual Hart Switcher Modal implementation.
 */
#include "simrv/tui/modals/HartModal.hpp"

#include <algorithm>
#include <format>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/debug/SymbolTable.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/modals/ModalComponents.hpp"
#include "simrv/util/FormatUtil.hpp"

namespace simrv::tui::modals {

void HartModal::open(size_t current_hart, const simrv::core::Machine& machine) {
    const size_t total = machine.num_harts();
    cursor_ = (total > 0) ? static_cast<int>(current_hart % total) : 0;
}

void HartModal::move_cursor(int delta, const simrv::core::Machine& machine) {
    const int count = static_cast<int>(machine.num_harts());
    if (count > 0) {
        cursor_ = (cursor_ + (delta % count) + count) % count;
    }
}

auto HartModal::submit(simrv::core::Machine& machine, size_t& selected_hart_out) -> bool {
    const size_t total = machine.num_harts();
    if (total == 0) return false;
    selected_hart_out = static_cast<size_t>(std::clamp(cursor_, 0, static_cast<int>(total - 1)));
    return true;
}

void HartModal::render(std::vector<std::string>& /*content_rows*/,
                       const std::function<void(const std::string&)>& add_row,
                       const simrv::core::Machine& machine) {
    const size_t total_harts = machine.num_harts();
    add_row(std::format(" Configured SMP Topology: \033[1m{} Hart(s)\033[0m", total_harts));
    add_row("");
    add_row(
        "   \033[1;37mHart    Priv  Status          PC                  Symbol                "
        "Retired\033[0m");

    for (size_t i = 0; i < total_harts; ++i) {
        const auto& hart = machine.hart(i);
        const auto& st = hart.state();
        const bool is_selected = (static_cast<int>(i) == cursor_);

        std::string priv_str = (st.priv == simrv::xlen::PrivilegeLevel::Machine)      ? "M"
                               : (st.priv == simrv::xlen::PrivilegeLevel::Supervisor) ? "S"
                                                                                      : "U";

        const auto h_status = hart.hart_status.load(std::memory_order_relaxed);
        std::string status_str;
        if (h_status == simrv::core::HartStatus::Stopped ||
            h_status == simrv::core::HartStatus::StopPending) {
            status_str = std::format("\033[1;31mSTOPPED\033[0m");
        } else if (hart.is_wfi_quiescent()) {
            status_str = std::format("\033[36mWFI (Parked)\033[0m");
        } else if (machine.is_paused()) {
            status_str = std::format("\033[33mPAUSED\033[0m");
        } else {
            status_str = std::format("\033[1;32mRUNNING\033[0m");
        }

        std::string pc_str = (st.regs.xlen == 64)
                                 ? std::format("0x{:016x}", st.pc)
                                 : std::format("0x{:08x}        ", static_cast<uint32_t>(st.pc));

        std::string sym = machine.symbol_table().lookup(st.pc);
        if (sym.empty()) sym = "-";
        if (sym.size() > 18) sym = sym.substr(0, 15) + "...";

        const auto snap = machine.tui_execution_snapshot(i);
        std::string ret_str = simrv::util::format_scaled(snap.instruction_count);

        std::string line;
        if (is_selected) {
            line = std::format(
                " \033[1;7m▶ #{:<5} {:<5} {:<15} {:<19} {:<21} {:>8} \033[0m", i, priv_str,
                hart.is_wfi_quiescent() ? "WFI" : (machine.is_paused() ? "PAUSED" : "RUNNING"),
                (st.regs.xlen == 64) ? std::format("0x{:016x}", st.pc)
                                     : std::format("0x{:08x}", static_cast<uint32_t>(st.pc)),
                sym, ret_str);
        } else {
            line = std::format("   #{:<5} {:<5} {:<24} {:<19} {:<21} {:>8}", i, priv_str,
                               status_str, pc_str, sym, ret_str);
        }
        add_row(line);
    }

    add_row("");
    add_row(build_modal_footer({{.key = "[Enter]", .action = "Select Hart"},
                                {.key = "[Up/Down]", .action = "Navigate"},
                                {.key = "[Esc/q]", .action = "Cancel"}}));
}

}  // namespace simrv::tui::modals
