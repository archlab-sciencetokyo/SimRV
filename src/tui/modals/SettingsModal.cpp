/**
 * @file SettingsModal.cpp
 * @brief Implementation of multi-tab Settings modal configuration dialog handling.
 */
#include "simrv/tui/modals/SettingsModal.hpp"

#include <algorithm>
#include <array>
#include <format>

#include "simrv/core/Machine.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/modals/MisaModal.hpp"
#include "simrv/tui/modals/SystemConfigModal.hpp"

namespace simrv::tui::modals {

void SettingsModal::open(SettingsDraft& draft, const simrv::core::Machine& machine) {
    draft.active_tab = 0;
    draft.tab_cursor[0] = 0;
    draft.tab_cursor[1] = 0;
    draft.tab_cursor[2] = 0;

    // Tab 0: General
    draft.cycle_accurate = machine.runtime_profile.is_cycle_mode();
    draft.debug_mode = machine.s_debug_mode;
    draft.rollback_enabled = machine.s_rollback_enabled;
    draft.high_contrast = machine.s_high_contrast;
    draft.class_mode = machine.s_class_mode;
    draft.use_mix = machine.s_use_mix;
    draft.bp_trace = machine.s_bp_trace;
    draft.traplog_mode = machine.s_traplog_mode;
    draft.dlog_mode = machine.s_dlog_mode;
    draft.lockstep_mode = machine.s_lockstep_mode;
    draft.gdb_mode = machine.s_gdb_mode;
    draft.num_harts = static_cast<uint32_t>(machine.num_harts());
    draft.smp_quantum = machine.s_smp_quantum;
    draft.smp_multithreaded = machine.s_smp_multithreaded;
    draft.platform_profile = static_cast<uint8_t>(machine.s_platform_profile);
    draft.net_mode = machine.s_net_mode;

    // Tab 1: MISA
    MisaModal::open(draft.misa, draft.tab_cursor[1], machine);

    // Tab 2: System Config
    SystemConfigModal::open(draft.sys_config, draft.tab_cursor[2], machine);
}

void SettingsModal::cycle_tab(SettingsDraft& draft, int delta) {
    draft.active_tab = static_cast<uint8_t>((draft.active_tab + delta + 3) % 3);
}

void SettingsModal::set_tab(SettingsDraft& draft, uint8_t tab) { draft.active_tab = tab % 3; }

void SettingsModal::move_cursor(SettingsDraft& draft, int delta) {
    switch (draft.active_tab) {
        case 0: {
            constexpr int kNumSettings = 16;
            draft.tab_cursor[0] = (draft.tab_cursor[0] + delta + kNumSettings) % kNumSettings;
            break;
        }
        case 1:
            MisaModal::move_cursor(draft.tab_cursor[1], delta);
            break;
        case 2:
            SystemConfigModal::move_cursor(draft.sys_config, draft.tab_cursor[2], delta);
            break;
        default:
            break;
    }
}

void SettingsModal::move_cursor(int& cursor, int delta) {
    constexpr int kNumSettings = 16;
    cursor = (cursor + delta + kNumSettings) % kNumSettings;
}

void SettingsModal::adjust_setting(SettingsDraft& draft, int index, int dir,
                                   const simrv::core::Machine* machine) {
    int const old_tab = draft.active_tab;
    int const old_cur = draft.tab_cursor[0];
    draft.active_tab = 0;
    draft.tab_cursor[0] = index;
    adjust_setting(draft, dir, machine);
    draft.active_tab = static_cast<uint8_t>(old_tab);
    draft.tab_cursor[0] = old_cur;
}

void SettingsModal::toggle_setting(SettingsDraft& draft, int index,
                                   const simrv::core::Machine* machine) {
    adjust_setting(draft, index, 1, machine);
}

void SettingsModal::adjust_setting(SettingsDraft& draft, int dir,
                                   const simrv::core::Machine* machine) {
    switch (draft.active_tab) {
        case 0: {
            int const index = draft.tab_cursor[0];
            switch (index) {
                case 0:
                    draft.cycle_accurate = !draft.cycle_accurate;
                    draft.sys_config.cycle_accurate = draft.cycle_accurate;
                    break;
                case 1:
                    draft.debug_mode = !draft.debug_mode;
                    if (draft.debug_mode) {
                        draft.use_mix = true;
                        draft.rollback_enabled = true;
                    } else {
                        draft.use_mix = false;
                        draft.rollback_enabled = false;
                    }
                    break;
                case 2:
                    draft.rollback_enabled = !draft.rollback_enabled;
                    break;
                case 3:
                    draft.high_contrast = !draft.high_contrast;
                    break;
                case 4:
                    draft.class_mode = !draft.class_mode;
                    break;
                case 5: {  // SMP Core Count
                    int v = static_cast<int>(draft.num_harts) + dir;
                    draft.num_harts = static_cast<uint32_t>(std::clamp(v, 1, 16));
                    break;
                }
                case 6: {  // SMP Quantum
                    int v = static_cast<int>(draft.smp_quantum) + dir * 100;
                    draft.smp_quantum = static_cast<uint32_t>(std::clamp(v, 10, 1000000));
                    break;
                }
                case 7:  // SMP Multithreaded
                    draft.smp_multithreaded = !draft.smp_multithreaded;
                    break;
                case 8:  // Platform Profile (0: PCIe, 1: MMIO, 2: Hybrid)
                    draft.platform_profile =
                        static_cast<uint8_t>((draft.platform_profile + (dir > 0 ? 1 : 2)) % 3);
                    break;
                case 9:  // VirtIO Network Backend
                    if (draft.net_mode == "user")
                        draft.net_mode = (dir > 0) ? "tap" : "none";
                    else if (draft.net_mode == "tap")
                        draft.net_mode = (dir > 0) ? "socket" : "user";
                    else if (draft.net_mode == "socket")
                        draft.net_mode = (dir > 0) ? "none" : "tap";
                    else
                        draft.net_mode = (dir > 0) ? "user" : "socket";
                    break;
                case 10:
                    if (machine && machine->s_spike_bin.empty()) break;
                    draft.lockstep_mode = !draft.lockstep_mode;
                    break;
                case 11:
                    draft.gdb_mode = !draft.gdb_mode;
                    break;
                case 12:
                    if (!draft.cycle_accurate) break;
                    draft.bp_trace = !draft.bp_trace;
                    break;
                case 13:
                    draft.use_mix = !draft.use_mix;
                    break;
                case 14:
                    draft.traplog_mode = !draft.traplog_mode;
                    break;
                case 15:
                    if (machine && machine->s_appmode) break;
                    draft.dlog_mode = !draft.dlog_mode;
                    break;
                default:
                    break;
            }
            break;
        }
        case 1:
            MisaModal::toggle_item(draft.misa, draft.tab_cursor[1]);
            break;
        case 2:
            SystemConfigModal::adjust_setting(draft.sys_config, draft.tab_cursor[2], dir);
            break;
        default:
            break;
    }
}

void SettingsModal::toggle_setting(SettingsDraft& draft, const simrv::core::Machine* machine) {
    adjust_setting(draft, 1, machine);
}

void SettingsModal::push_digit(SettingsDraft& draft, std::string& input, char c) {
    if (draft.active_tab == 2) {
        SystemConfigModal::push_digit(draft.sys_config, draft.tab_cursor[2], input, c);
    }
}

void SettingsModal::pop_digit(SettingsDraft& draft, std::string& input) {
    if (draft.active_tab == 2) {
        SystemConfigModal::pop_digit(draft.sys_config, draft.tab_cursor[2], input);
    }
}

void SettingsModal::apply_misa_profile(SettingsDraft& draft, int profile_idx) {
    if (draft.active_tab == 1) {
        MisaModal::apply_profile(draft.misa, profile_idx);
    }
}

auto SettingsModal::submit(const SettingsDraft& draft, simrv::core::Machine& machine,
                           const std::function<void(TuiRegPage)>& set_reg_page_cb) -> bool {
    // 1. General & UI settings
    machine.runtime_profile.interaction = simrv::core::InteractionMode::Tui;
    machine.runtime_profile.engine = simrv::core::select_execution_engine(
        draft.cycle_accurate, machine.runtime_profile.interaction);
    machine.s_debug_mode = draft.debug_mode;
    machine.s_rollback_enabled = draft.rollback_enabled;
    if (!machine.s_rollback_enabled) {
        machine.cpu.undo_stack.clear();
    }
    if (draft.high_contrast != machine.s_high_contrast) {
        set_high_contrast(draft.high_contrast);
        machine.s_high_contrast = draft.high_contrast;
    }
    machine.s_class_mode = draft.class_mode;

    machine.s_use_mix = draft.use_mix;
    machine.s_bp_trace = draft.bp_trace;
    machine.s_traplog_mode = draft.traplog_mode;
    machine.s_dlog_mode = draft.dlog_mode;
    machine.s_lockstep_mode = draft.lockstep_mode;
    machine.s_gdb_mode = draft.gdb_mode;
    machine.s_smp_quantum = draft.smp_quantum;
    machine.s_smp_multithreaded = draft.smp_multithreaded;
    machine.s_platform_profile = static_cast<simrv::core::PlatformProfile>(draft.platform_profile);
    machine.s_net_mode = draft.net_mode;

    for (size_t hart = 0; hart < machine.num_harts(); ++hart) {
        machine.hart(hart).pipeline_sim.config.record_snapshots =
            machine.runtime_profile.records_cycle_history();
    }

    if (machine.s_use_mix) {
        set_reg_page_cb(TuiRegPage::GPR);
    }

    // 2. MISA Extensions
    uint64_t const new_misa = draft.misa.to_misa_val();
    if (machine.cpu.state().misa != new_misa || machine.s_vlen != draft.misa.vlen) {
        machine.cpu.state().misa = new_misa;
        machine.s_misa_profile = new_misa;
        machine.s_misa_override = true;
        machine.s_misa_xlen = draft.misa.xlen_bits;
        machine.cpu.state().initialize_lower_xlen_fields();
        machine.s_vlen = draft.misa.vlen;
        machine.request_reboot();
    }

    // 3. System Microarchitecture
    SystemConfigModal::submit(draft.sys_config, machine);

    return true;
}

void SettingsModal::render(std::vector<std::string>& content_rows,
                           const std::function<void(const std::string&)>& add_row_cb,
                           const SettingsDraft& draft, const simrv::core::Machine& machine) {
    (void)content_rows;

    const auto style = get_active_theme_style();
    const bool is_ansi = (style == TuiThemeStyle::ClassicAnsi);

    std::string tab0 = (draft.active_tab == 0)
                           ? std::format("\033[7m [1] General / UI \033[0m")
                           : std::format(" {}[1] General / UI\033[0m ", kThemeMuted);
    std::string tab1 = (draft.active_tab == 1)
                           ? std::format("\033[7m [2] MISA Extensions \033[0m")
                           : std::format(" {}[2] MISA Extensions\033[0m ", kThemeMuted);
    std::string tab2 = (draft.active_tab == 2)
                           ? std::format("\033[7m [3] Microarchitecture \033[0m")
                           : std::format(" {}[3] Microarchitecture\033[0m ", kThemeMuted);

    std::string const sep = is_ansi ? " | " : " │ ";
    add_row_cb(std::format("  {}{}{}{}{}", tab0, sep, tab1, sep, tab2));
    add_row_cb("");

    if (draft.active_tab == 0) {
        add_row_cb(
            std::format("{}Use \033[1m[↑/↓]\033[0m to navigate, \033[1m[←/→/Space]\033[0m to "
                        "toggle/adjust, \033[1m[Tab/1-3]\033[0m to switch tabs:\033[0m",
                        kThemeMuted));
        add_row_cb("");

        const bool bp_disabled = !draft.cycle_accurate;
        const bool dlog_disabled = machine.s_appmode;
        const bool lockstep_disabled = machine.s_spike_bin.empty();

        std::string profile_str;
        if (draft.platform_profile == 0)
            profile_str = "\033[1;32m[PCIe (PCIe 1.2 + ECAM)]\033[0m";
        else if (draft.platform_profile == 1)
            profile_str = "\033[1;36m[MMIO (VirtIO-MMIO v2)]\033[0m";
        else
            profile_str = "\033[1;35m[Hybrid (PCIe + MMIO)]\033[0m";

        std::string net_str;
        if (draft.net_mode == "user")
            net_str = "\033[1;32m[User Mode (Echo/Loopback)]\033[0m";
        else if (draft.net_mode == "tap")
            net_str = "\033[1;36m[Linux TAP Bridge]\033[0m";
        else if (draft.net_mode == "socket")
            net_str = "\033[1;33m[Socket Tunnel]\033[0m";
        else
            net_str = "\033[90m[Disabled]\033[0m";

        struct ItemInfo {
            const char* name;
            std::string val;
        };

        const auto settings = std::to_array<ItemInfo>({
            {"Simulation Mode", draft.cycle_accurate
                                    ? "\033[1;36m[CA (Cycle-Accurate)]\033[0m"
                                    : "\033[1;33m[IA (Instruction-Accurate)]\033[0m"},
            {"TUI Diagnostics View", draft.debug_mode
                                         ? "\033[1;32m[Debug Mode (Diagnostics ON)]\033[0m"
                                         : "\033[90m[Normal Mode]\033[0m"},
            {"Step Rollback History",
             draft.rollback_enabled ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
            {"High Contrast Theme",
             draft.high_contrast ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
            {"Educational Class Mode", draft.class_mode
                                           ? "\033[1;32m[ON (Guidance & Glossary)]\033[0m"
                                           : "\033[90m[OFF]\033[0m"},
            {"SMP Active Core Count",
             std::format("\033[1;36m{} Cores (Harts)\033[0m", draft.num_harts)},
            {"SMP Quantum Slice", std::format("\033[1;33m{} cycles\033[0m", draft.smp_quantum)},
            {"SMP Multi-Threaded Engine", draft.smp_multithreaded
                                              ? "\033[1;32m[ON (Worker Threads)]\033[0m"
                                              : "\033[90m[OFF (Quantum Barrier)]\033[0m"},
            {"Platform Profile", profile_str},
            {"VirtIO Network Backend", net_str},
            {"Co-Sim Spike Lockstep",
             lockstep_disabled
                 ? "\033[90m[Disabled (No Spike Bin)]\033[0m"
                 : (draft.lockstep_mode ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m")},
            {"GDB Server Stub (1234)",
             draft.gdb_mode ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
            {"Branch Prediction Trace",
             bp_disabled ? "\033[90m[Disabled (N/A in IA Mode)]\033[0m"
                         : (draft.bp_trace ? "\033[1;32m[ON (creates bptrace.txt)]\033[0m"
                                           : "\033[90m[OFF (creates text file)]\033[0m")},
            {"Instruction Mix Stats",
             draft.use_mix ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
            {"Exception & Trap Log", draft.traplog_mode
                                         ? "\033[1;32m[ON (creates traplog.txt)]\033[0m"
                                         : "\033[90m[OFF (creates text file)]\033[0m"},
            {"Device MMIO Access Log",
             dlog_disabled ? "\033[90m[Disabled in Baremetal Mode]\033[0m"
                           : (draft.dlog_mode ? "\033[1;32m[ON (creates devicelog.txt)]\033[0m"
                                              : "\033[90m[OFF (creates text file)]\033[0m")},
        });

        int const cursor = draft.tab_cursor[0];
        for (std::size_t i = 0; i < settings.size(); ++i) {
            if (i == 0) {
                add_row_cb(
                    std::format("{}\033[1;35m── Core Engine & Diagnostics ──\033[0m", kThemeText));
            } else if (i == 5) {
                add_row_cb("");
                add_row_cb(std::format(
                    "{}\033[1;35m── Symmetric Multiprocessing (SMP) Configuration ──\033[0m",
                    kThemeText));
            } else if (i == 8) {
                add_row_cb("");
                add_row_cb(std::format(
                    "{}\033[1;35m── Platform Profile & Peripheral Devices ──\033[0m", kThemeText));
            } else if (i == 10) {
                add_row_cb("");
                add_row_cb(std::format(
                    "{}\033[1;35m── External Integrations & Debug Stubs ──\033[0m", kThemeText));
            } else if (i == 12) {
                add_row_cb("");
                add_row_cb(std::format(
                    "{}\033[1;35m── Traces & Logging (Creates Text Files) ──\033[0m", kThemeText));
            }
            bool is_sel = (static_cast<int>(i) == cursor);
            std::string prefix = is_sel ? std::format("{}>\033[0m ", kThemeMint) : "  ";
            std::string name_str = std::format(
                "{}{:<29}\033[0m", is_sel ? "\033[1;37m" : kThemeText, settings[i].name);
            add_row_cb(std::format("{}{} : {}", prefix, name_str, settings[i].val));
        }
    } else if (draft.active_tab == 1) {
        add_row_cb(
            std::format("{}Use \033[1m[↑/↓]\033[0m to navigate, \033[1m[Space/Enter]\033[0m to "
                        "toggle, \033[1m[p]\033[0m Base, \033[1m[i]\033[0m IMAC, \033[1m[g]\033[0m "
                        "GC:\033[0m",
                        kThemeMuted));
        add_row_cb("");
        MisaModal::render(content_rows, add_row_cb, draft.misa, draft.tab_cursor[1], machine);
    } else if (draft.active_tab == 2) {
        SystemConfigModal::render(content_rows, add_row_cb, draft.sys_config, draft.tab_cursor[2]);
    }

    add_row_cb("");
    add_row_cb(
        std::format("{}Press \033[1m[Enter]\033[0m to apply settings, \033[1m[Esc]\033[0m "
                    "or \033[1m[q]\033[0m to cancel\033[0m",
                    kThemeMuted));
}

}  // namespace simrv::tui::modals
