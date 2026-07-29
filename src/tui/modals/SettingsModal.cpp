/**
 * @file SettingsModal.cpp
 * @brief Implementation of Settings modal configuration dialog handling.
 */
#include "simrv/tui/modals/SettingsModal.hpp"

#include <array>
#include <format>
#include "simrv/core/Machine.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/tui/TuiTheme.hpp"

namespace simrv::tui::modals {

namespace {

struct SettingItem {
    const char* key;
    const char* name;
    std::string val;
};

}  // namespace

void SettingsModal::open(SettingsDraft& draft, int& cursor, const simrv::core::Machine& machine) {
    cursor = 0;
    draft.cycle_accurate = machine.s_cycle_accurate;
    draft.debug_mode = machine.s_debug_mode;
    draft.rollback_enabled = machine.s_rollback_enabled;
    draft.high_contrast = machine.s_high_contrast;
    draft.use_mix = machine.s_use_mix;
    draft.bp_trace = machine.s_bp_trace;
    draft.traplog_mode = machine.s_traplog_mode;
    draft.dlog_mode = machine.s_dlog_mode;
    draft.high_performance = machine.s_high_performance;
    draft.lockstep_mode = machine.s_lockstep_mode;
    draft.gdb_mode = machine.s_gdb_mode;
}

void SettingsModal::move_cursor(int& cursor, int delta) {
    constexpr int kNumSettings = 11;
    cursor = (cursor + delta + kNumSettings) % kNumSettings;
}

void SettingsModal::toggle_setting(SettingsDraft& draft, int index,
                                    const simrv::core::Machine& machine) {
    switch (index) {
        case 0:
            draft.cycle_accurate = !draft.cycle_accurate;
            if (draft.cycle_accurate) {
                draft.high_performance = false;
            } else {
                draft.high_performance = true;
                draft.use_mix = false;
            }
            break;
        case 1:
            draft.debug_mode = !draft.debug_mode;
            if (draft.debug_mode) {
                draft.use_mix = draft.cycle_accurate;
                draft.rollback_enabled = true;
            } else {
                draft.use_mix = false;
                draft.rollback_enabled = false;
            }
            break;
        case 2:
            if (draft.high_performance) break;
            draft.rollback_enabled = !draft.rollback_enabled;
            break;
        case 3:
            draft.high_contrast = !draft.high_contrast;
            break;
        case 4:
            if (!draft.cycle_accurate) break;
            draft.use_mix = !draft.use_mix;
            break;
        case 5:
            draft.high_performance = !draft.high_performance;
            if (draft.high_performance) {
                draft.bp_trace = false;
                draft.rollback_enabled = false;
            }
            break;
        case 6:
            if (machine.s_spike_bin.empty()) break;
            draft.lockstep_mode = !draft.lockstep_mode;
            break;
        case 7:
            draft.gdb_mode = !draft.gdb_mode;
            break;
        case 8:
            if (!draft.cycle_accurate || draft.high_performance) break;
            draft.bp_trace = !draft.bp_trace;
            break;
        case 9:
            draft.traplog_mode = !draft.traplog_mode;
            break;
        case 10:
            if (machine.s_appmode) break;
            draft.dlog_mode = !draft.dlog_mode;
            break;
        default:
            break;
    }
}

auto SettingsModal::submit(const SettingsDraft& draft, simrv::core::Machine& machine,
                           const std::function<void(TuiRegPage)>& set_reg_page_cb) -> bool {
    machine.s_cycle_accurate = draft.cycle_accurate;
    machine.s_debug_mode = draft.debug_mode;
    machine.s_rollback_enabled = draft.rollback_enabled;
    if (!machine.s_rollback_enabled) {
        machine.cpu.undo_stack.clear();
    }
    if (draft.high_contrast != machine.s_high_contrast) {
        set_high_contrast(draft.high_contrast);
        machine.s_high_contrast = draft.high_contrast;
    }

    machine.s_use_mix = draft.use_mix;
    machine.s_bp_trace = draft.bp_trace;
    machine.s_traplog_mode = draft.traplog_mode;
    machine.s_dlog_mode = draft.dlog_mode;
    machine.s_high_performance = draft.high_performance;
    machine.s_lockstep_mode = draft.lockstep_mode;
    machine.s_gdb_mode = draft.gdb_mode;

    if (machine.s_use_mix) {
        set_reg_page_cb(TuiRegPage::GPR);
    }
    return true;
}

void SettingsModal::render(std::vector<std::string>& content_rows,
                           const std::function<void(const std::string&)>& add_row_cb,
                           const SettingsDraft& draft, int cursor,
                           const simrv::core::Machine& machine) {
    (void)content_rows;
    add_row_cb(
        std::format("{}Use \033[1m[↑/↓/←/→]\033[0m or key \033[1m[1-8,9,a,b]\033[0m to "
                    "toggle, \033[1m[Enter]\033[0m to apply:\033[0m",
                    kThemeMuted));
    add_row_cb("");

    const bool bp_disabled = !draft.cycle_accurate || draft.high_performance;
    const bool mix_disabled = !draft.cycle_accurate;
    const bool rollback_disabled = draft.high_performance;
    const bool dlog_disabled = machine.s_appmode;
    const bool lockstep_disabled = machine.s_spike_bin.empty();

    const auto settings = std::to_array<SettingItem>({
        {" 1", "Simulation Mode",
         draft.cycle_accurate ? "\033[1;36m[CA (Cycle-Accurate)]\033[0m"
                              : "\033[1;33m[IA (Instruction-Accurate)]\033[0m"},
        {" 2", "TUI Diagnostics View",
         draft.debug_mode ? "\033[1;32m[Debug Mode (Diagnostics ON)]\033[0m"
                          : "\033[90m[Normal Mode]\033[0m"},
        {" 3", "Step Rollback History",
         rollback_disabled ? "\033[90m[Disabled (High-Perf Mode)]\033[0m"
                           : (draft.rollback_enabled ? "\033[1;32m[ON]\033[0m"
                                                     : "\033[90m[OFF]\033[0m")},
        {" 4", "High Contrast Theme",
         draft.high_contrast ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
        {" 5", "Instruction Mix Stats",
         mix_disabled ? "\033[90m[Disabled (N/A in IA Mode)]\033[0m"
                      : (draft.use_mix ? "\033[1;32m[ON]\033[0m"
                                       : "\033[90m[OFF]\033[0m")},
        {" 6", "High-Performance Engine",
         draft.high_performance ? "\033[1;32m[ON]\033[0m"
                                : "\033[90m[OFF]\033[0m"},
        {" 7", "Co-Sim Spike Lockstep",
         lockstep_disabled ? "\033[90m[Disabled (No Spike Bin)]\033[0m"
                           : (draft.lockstep_mode ? "\033[1;32m[ON]\033[0m"
                                                 : "\033[90m[OFF]\033[0m")},
        {" 8", "GDB Server Stub (1234)",
         draft.gdb_mode ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
        {" 9", "Branch Prediction Trace",
         bp_disabled ? "\033[90m[Disabled (N/A in IA Mode)]\033[0m"
                     : (draft.bp_trace ? "\033[1;32m[ON (creates bptrace.txt)]\033[0m"
                                       : "\033[90m[OFF (creates text file)]\033[0m")},
        {" a", "Exception & Trap Log",
         draft.traplog_mode ? "\033[1;32m[ON (creates traplog.txt)]\033[0m"
                            : "\033[90m[OFF (creates text file)]\033[0m"},
        {" b", "Device MMIO Access Log",
         dlog_disabled ? "\033[90m[Disabled in Baremetal Mode]\033[0m"
                       : (draft.dlog_mode ? "\033[1;32m[ON (creates devicelog.txt)]\033[0m"
                                          : "\033[90m[OFF (creates text file)]\033[0m")},
    });

    for (std::size_t i = 0; i < settings.size(); ++i) {
        if (i == 0) {
            add_row_cb(std::format("{}\033[1;35m── Core Engine & Diagnostics ──\033[0m", kThemeText));
        } else if (i == 6) {
            add_row_cb("");
            add_row_cb(std::format("{}\033[1;35m── External Integrations & Debug Stubs ──\033[0m", kThemeText));
        } else if (i == 8) {
            add_row_cb("");
            add_row_cb(std::format("{}\033[1;35m── Traces & Logging (Creates Text Files) ──\033[0m", kThemeText));
        }
        bool is_sel = (static_cast<int>(i) == cursor);
        std::string prefix = is_sel ? std::format("{}>\033[0m ", kThemeMint) : "  ";
        std::string num_key =
            std::format("{}[{}]\033[0m", is_sel ? kThemeMint : kThemeSky, settings[i].key);
        std::string name_str = std::format(
            "{}{:<27}\033[0m", is_sel ? "\033[1;37m" : kThemeText, settings[i].name);
        add_row_cb(std::format("{}{} {} : {}", prefix, num_key, name_str, settings[i].val));
    }
    add_row_cb("");
    add_row_cb(
        std::format("{}Press \033[1m[Enter]\033[0m to apply settings, \033[1m[Esc]\033[0m "
                    "or \033[1m[q]\033[0m to cancel\033[0m",
                    kThemeMuted));
}

}  // namespace simrv::tui::modals
