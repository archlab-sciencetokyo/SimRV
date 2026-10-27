/**
 * @file BreakpointModal.cpp
 * @brief Implementation of Breakpoint, Watchpoint, and ManageBreakpoints modal dialog handling.
 */
#include "simrv/tui/modals/BreakpointModal.hpp"

#include <algorithm>
#include <charconv>
#include <format>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/debug/BreakpointManager.hpp"
#include "simrv/debug/SymbolTable.hpp"
#include "simrv/tui/TuiModal.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/modals/ModalComponents.hpp"
#include "simrv/tui/panels/InspectorPane.hpp"

namespace simrv::tui::modals {

auto BreakpointModal::title() const -> std::string {
    if (type_ == ModalType::SetBreakpoint) return "Set PC Breakpoint";
    if (type_ == ModalType::SetWatchpoint) return "Set Memory Watchpoint";
    return "Manage Breakpoints & Watchpoints";
    if (type_ == ModalType::SetBreakpoint) return " SET BREAKPOINT ";
    if (type_ == ModalType::SetWatchpoint) return " SET WATCHPOINT ";
    return " MANAGE BREAKPOINTS & WATCHPOINTS ";
}

void BreakpointModal::render_content(std::vector<std::string>& content_rows, int /*width*/,
                                     int /*height*/) {
    render(type_, content_rows, input_, machine_, cursor_);
}

auto BreakpointModal::cursor_row() const -> int {
    return type_ == ModalType::ManageBreakpoints ? 2 + cursor_ : 0;
}

void BreakpointModal::scroll_to_top() {
    if (type_ == ModalType::ManageBreakpoints) cursor_ = 0;
}

void BreakpointModal::scroll_to_bottom() {
    if (type_ == ModalType::ManageBreakpoints && machine_) {
        move_cursor(cursor_, 999999, *machine_);
    }
}

void BreakpointModal::handle_wheel(int delta) {
    if (type_ == ModalType::ManageBreakpoints && machine_) {
        move_cursor(cursor_, delta, *machine_);
    }
}

auto BreakpointModal::handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction {
    if (type_ == ModalType::ManageBreakpoints) {
        if (byte == 27 || key == simrv::tui::TuiKey::Esc || byte == 'q' || byte == 'Q') {
            return framework::ModalAction::Close;
        }
        if (key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline || byte == ' ' ||
            byte == 8 || byte == 127 || key == simrv::tui::TuiKey::Backspace) {
            if (host_) host_->remove_bp_at_cursor({});
            return framework::ModalAction::Continue;
        }
        if (byte == 'a' || byte == 'A') {
            if (host_) host_->open(ModalType::SetBreakpoint, nullptr, 0);
            return framework::ModalAction::Continue;
        }
        if (byte == 'w' || byte == 'W') {
            if (host_) host_->open(ModalType::SetWatchpoint, nullptr, 0);
            return framework::ModalAction::Continue;
        }
        if (byte == 'c' || byte == 'C') {
            if (machine_) {
                machine_->breakpoint_manager().clear_pc_breakpoints();
                machine_->breakpoint_manager().clear_watchpoints();
            }
            cursor_ = 0;
            return framework::ModalAction::Continue;
        }
        if (byte == 'j' || byte == 'J') {
            handle_wheel(1);
            return framework::ModalAction::Continue;
        }
        if (byte == 'k' || byte == 'K') {
            handle_wheel(-1);
            return framework::ModalAction::Continue;
        }
        return framework::ModalAction::Continue;
    }

    if (byte == 27 || key == simrv::tui::TuiKey::Esc) {
        return framework::ModalAction::Close;
    }
    if (key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline) {
        return framework::ModalAction::Submit;
    }
    if (byte == 8 || byte == 127 || key == simrv::tui::TuiKey::Backspace) {
        if (!input_.empty()) input_.pop_back();
        return framework::ModalAction::Continue;
    }
    if (byte >= 32 && byte <= 126) {
        input_.push_back(static_cast<char>(byte));
        return framework::ModalAction::Continue;
    }
    return framework::ModalAction::Continue;
}

auto BreakpointModal::handle_control_click(int content_row, size_t action_index)
    -> framework::ModalAction {
    if (type_ == ModalType::ManageBreakpoints) {
        if (content_row >= 8) {
            if (action_index == 0) {
                if (host_) host_->open(ModalType::SetBreakpoint, nullptr, 0);
                return framework::ModalAction::Continue;
            }
            if (action_index == 1) {
                if (host_) host_->open(ModalType::SetWatchpoint, nullptr, 0);
                return framework::ModalAction::Continue;
            }
        }
        if (action_index == 0) {
            handle_wheel(-1);
            return framework::ModalAction::Continue;
        }
        if (action_index == 1) {
            handle_wheel(1);
            return framework::ModalAction::Continue;
        }
        if (action_index == 2) {
            if (host_) host_->remove_bp_at_cursor({});
            return framework::ModalAction::Continue;
        }
        if (action_index == 3) {
            if (machine_) {
                machine_->breakpoint_manager().clear_pc_breakpoints();
                machine_->breakpoint_manager().clear_watchpoints();
            }
            cursor_ = 0;
            return framework::ModalAction::Continue;
        }
        return framework::ModalAction::Close;
    }
    return action_index == 0 ? framework::ModalAction::Submit : framework::ModalAction::Close;
}

auto BreakpointModal::handle_click(framework::ScreenCoord local_pt) -> framework::ModalAction {
    (void)local_pt;
    return framework::ModalAction::Continue;
}

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
        simrv::xlen::Address addr = 0;
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

        simrv::xlen::Address addr = 0;
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

struct ActiveTriggerInfo {
    size_t index{};
    bool is_execute{};
    bool is_load{};
    bool is_store{};
    simrv::xlen::Address addr{};
};

auto get_active_triggers(const simrv::core::Machine& machine) -> std::vector<ActiveTriggerInfo> {
    std::vector<ActiveTriggerInfo> trigs;
    const auto& state = machine.primary_hart().state();
    if (!state.has_active_triggers) return trigs;
    const unsigned xlen = state.regs.xlen;
    for (size_t i = 0; i < simrv::core::ArchState::kNumTriggers; ++i) {
        const auto t1 = state.tdata1[i];
        if (t1 == 0) continue;
        const auto type = (static_cast<uint64_t>(t1) >> (xlen == 64 ? 60U : 28U)) & 0xFU;
        if (type != 2) continue;
        ActiveTriggerInfo info;
        info.index = i;
        info.is_execute = (t1 & (1U << 2U)) != 0;
        info.is_store = (t1 & (1U << 1U)) != 0;
        info.is_load = (t1 & (1U << 0U)) != 0;
        info.addr = state.tdata2[i];
        trigs.push_back(info);
    }
    return trigs;
}

void BreakpointModal::move_cursor(int& cursor, int delta, const simrv::core::Machine& machine) {
    const auto& pc_bps = machine.breakpoint_manager().get_pc_breakpoints();
    const auto& wps = machine.breakpoint_manager().get_watchpoints();
    const auto trigs = get_active_triggers(machine);
    int count = static_cast<int>(pc_bps.size() + wps.size() + trigs.size());
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
    const auto trigs = get_active_triggers(machine);
    int total = static_cast<int>(pc_bps.size() + wps.size() + trigs.size());
    if (total == 0 || cursor < 0 || cursor >= total) return false;

    if (cursor < static_cast<int>(pc_bps.size())) {
        auto it = std::next(pc_bps.begin(), cursor);
        simrv::xlen::Address pc = *it;
        const auto& records = machine.breakpoint_manager().get_pc_breakpoint_records();
        const bool has_tui = std::ranges::any_of(records, [pc](const auto& record) {
            return record.addr == pc && record.owner == simrv::debug::BreakpointOwner::Tui;
        });
        const bool has_mission = std::ranges::any_of(records, [pc](const auto& record) {
            return record.addr == pc && record.owner == simrv::debug::BreakpointOwner::Mission;
        });
        if (!has_tui && has_mission) {
            if (set_status_override_cb)
                set_status_override_cb(
                    "Lesson checkpoint is managed by the mission; dismiss it with z to remove it.");
            return true;
        }
        machine.breakpoint_manager().remove_pc_breakpoint(pc);
        if (set_status_override_cb) {
            set_status_override_cb(
                has_mission
                    ? std::format("Removed user breakpoint at 0x{:08x}; lesson checkpoint remains",
                                  pc)
                    : std::format("Removed breakpoint at 0x{:08x}", pc));
        }
    } else if (cursor < static_cast<int>(pc_bps.size() + wps.size())) {
        auto wp_idx = static_cast<std::size_t>(cursor - static_cast<int>(pc_bps.size()));
        const auto& wp = wps[wp_idx];
        std::string name = (wp.target == simrv::debug::WatchTarget::Memory)
                               ? std::format("0x{:08x}", wp.addr)
                               : wp.reg_name;
        machine.breakpoint_manager().remove_watchpoint_by_index(wp_idx);
        if (set_status_override_cb) {
            set_status_override_cb(std::format("Removed watchpoint on {}", name));
        }
    } else {
        auto trig_idx =
            static_cast<std::size_t>(cursor - static_cast<int>(pc_bps.size() + wps.size()));
        const auto& trig = trigs[trig_idx];
        machine.primary_hart().state().tdata1[trig.index] = 0;
        machine.primary_hart().state().refresh_trigger_status();
        if (set_status_override_cb) {
            set_status_override_cb(std::format("Disabled hardware trigger #{}", trig.index));
        }
    }
    const auto& new_pc_bps = machine.breakpoint_manager().get_pc_breakpoints();
    const auto& new_wps = machine.breakpoint_manager().get_watchpoints();
    const auto new_trigs = get_active_triggers(machine);
    int new_total = static_cast<int>(new_pc_bps.size() + new_wps.size() + new_trigs.size());
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
        build_text_input_rows(
            content_rows, "Target PC Address (hex) or Symbol:", input,
            "Enforces exact single-instruction step evaluation for 100% trap precision.");
        content_rows.push_back("");
        content_rows.push_back(build_modal_footer({{.key = "[Enter]", .action = "Set Breakpoint"},
                                                   {.key = "[Esc]", .action = "Cancel"}}));
    } else if (type == ModalType::SetWatchpoint) {
        build_text_input_rows(
            content_rows, "Target Register, Address, or Symbol:", input,
            "Pauses on memory write or register delta (disengages fast-batching).");
        content_rows.push_back("");
        content_rows.push_back(build_modal_footer({{.key = "[Enter]", .action = "Set Watchpoint"},
                                                   {.key = "[Esc]", .action = "Cancel"}}));
    } else if (type == ModalType::ManageBreakpoints) {
        content_rows.push_back(
            std::format("{}Active Breakpoints, Watchpoints & Triggers:\033[0m", kThemeText));
        content_rows.push_back(std::format(
            "  {}Select a row to inspect or remove it. Mission checkpoints are lesson-owned.\033[0m",
            kThemeMuted));
        content_rows.push_back("");

        if (!machine) return;
        const auto& pc_bps = machine->breakpoint_manager().get_pc_breakpoints();
        const auto& wps = machine->breakpoint_manager().get_watchpoints();
        const auto trigs = get_active_triggers(*machine);

        int item_idx = 0;
        if (pc_bps.empty() && wps.empty() && trigs.empty()) {
            content_rows.push_back(
                std::format("  {}No active breakpoints, watchpoints, or hardware triggers.\033[0m",
                            kThemeMuted));
        } else {
            for (auto pc : pc_bps) {
                bool is_sel = (item_idx == bp_cursor);
                std::string prefix = is_sel ? std::format("{}>\033[0m ", kThemeMint) : "  ";
                std::string sym = machine->symbol_table().lookup(pc);
                std::string sym_str = sym.empty() ? "" : std::format(" ({})", sym);
                const auto& records = machine->breakpoint_manager().get_pc_breakpoint_records();
                const bool mission_owned = std::ranges::any_of(records, [pc](const auto& record) {
                    return record.addr == pc &&
                           record.owner == simrv::debug::BreakpointOwner::Mission;
                });
                const bool user_owned = std::ranges::any_of(records, [pc](const auto& record) {
                    return record.addr == pc && record.owner == simrv::debug::BreakpointOwner::Tui;
                });
                const auto kind = mission_owned && !user_owned ? "Mission checkpoint at"
                                  : mission_owned              ? "Breakpoint + checkpoint at"
                                                               : "PC Breakpoint at";
                content_rows.push_back(
                    std::format("{}{} \033[1;33m0x{:08x}\033[0m{}", prefix, kind, pc, sym_str));
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
            for (const auto& trig : trigs) {
                if (item_idx >= 9) break;
                bool is_sel = (item_idx == bp_cursor);
                std::string prefix = is_sel ? std::format("{}>\033[0m ", kThemeMint) : "  ";
                std::string kind = trig.is_execute ? "PC Match"
                                   : (trig.is_load && trig.is_store)
                                       ? "RW Watch"
                                       : (trig.is_store ? "Write Watch" : "Read Watch");
                content_rows.push_back(std::format("{}Sdtrig #{} [{}]: \033[1;36m0x{:08x}\033[0m",
                                                   prefix, trig.index, kind, trig.addr));
                item_idx++;
            }
        }

        content_rows.push_back("");
        content_rows.push_back(build_modal_footer({{.key = "[↑ / ↓]", .action = "Select"},
                                                   {.key = "[Enter / Backspace]",
                                                    .action = "Remove selected"},
                                                   {.key = "[Esc]", .action = "Close"}}));
        content_rows.push_back(build_modal_footer({{.key = "[A]", .action = "Add PC breakpoint"},
                                                   {.key = "[W]", .action = "Add watchpoint"},
                                                   {.key = "[C]",
                                                    .action = "Clear breakpoints / watchpoints"}}));
    }
}

}  // namespace simrv::tui::modals
