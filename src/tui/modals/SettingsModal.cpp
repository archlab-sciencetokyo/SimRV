/**
 * @file SettingsModal.cpp
 * @brief Implementation of multi-tab Settings modal configuration dialog handling.
 */
#include "simrv/tui/modals/SettingsModal.hpp"

#include <algorithm>
#include <array>
#include <format>

#include "simrv/core/Machine.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/modals/MisaModal.hpp"
#include "simrv/tui/modals/ModalComponents.hpp"
#include "simrv/tui/modals/SystemConfigModal.hpp"

namespace simrv::tui::modals {

void SettingsModal::open(SettingsDraft& draft, const simrv::core::Machine& machine) {
    draft.active_tab = 0;
    draft.tab_cursor[0] = 0;
    draft.tab_cursor[1] = 0;
    draft.tab_cursor[2] = 0;

    // Tab 0: General
    draft.cycle_accurate = machine.runtime_profile.is_cycle_mode();
    draft.debug_mode = machine.debug_diagnostics_enabled();
    draft.high_contrast = machine.high_contrast_enabled();
    draft.class_mode = machine.class_mode_enabled();
    draft.tui_fps = machine.tui_controller() ? machine.tui_controller()->target_fps() : 30;
    draft.use_mix = machine.instruction_mix_enabled();
    draft.bp_trace = machine.branch_trace_enabled();
    draft.traplog_mode = machine.trap_log_enabled();
    draft.dlog_mode = machine.device_log_enabled();
    draft.lockstep_mode = machine.lockstep_enabled();
    draft.gdb_mode = machine.debugger_enabled();
    draft.num_harts = machine.execution_config().num_harts;
    draft.smp_quantum = machine.execution_config().smp_quantum;
    draft.smp_multithreaded = machine.execution_config().smp_multithreaded;
    draft.platform_profile = static_cast<uint8_t>(machine.platform_profile());
    draft.dram_size_mb = machine.memory_geometry().dram_size / (1024ULL * 1024ULL);
    draft.net_mode = machine.network_mode();

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
            constexpr int kNumSettings = 17;
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
    constexpr int kNumSettings = 17;
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
                    } else {
                        draft.use_mix = false;
                    }
                    break;
                case 2:
                    draft.high_contrast = !draft.high_contrast;
                    break;
                case 3:
                    draft.class_mode = !draft.class_mode;
                    break;
                case 4: {  // TUI Target Refresh Rate
                    static constexpr std::array<uint32_t, 4> kFpsOptions = {10, 15, 30, 60};
                    size_t curr_idx = 2;  // default 30
                    for (size_t f = 0; f < kFpsOptions.size(); ++f) {
                        if (draft.tui_fps == kFpsOptions[f]) {
                            curr_idx = f;
                            break;
                        }
                    }
                    if (dir > 0) {
                        curr_idx = (curr_idx + 1) % kFpsOptions.size();
                    } else {
                        curr_idx = (curr_idx + kFpsOptions.size() - 1) % kFpsOptions.size();
                    }
                    draft.tui_fps = kFpsOptions[curr_idx];
                    break;
                }
                case 5: {  // Active SMP Hart Count
                    int v = static_cast<int>(draft.num_harts) + dir;
                    draft.num_harts = static_cast<uint32_t>(std::clamp(v, 1, 16));
                    break;
                }
                case 6:  // SMP Threading Model
                    draft.smp_multithreaded = !draft.smp_multithreaded;
                    break;
                case 7: {  // SMP Scheduler Quantum (insns/slice)
                    static constexpr std::array<uint32_t, 12> kQuantumLevels = {
                        10, 25, 50, 100, 250, 500, 1000, 2500, 5000, 10000, 50000, 100000};
                    size_t curr = 6;  // default 1000
                    for (size_t q = 0; q < kQuantumLevels.size(); ++q) {
                        if (draft.smp_quantum <= kQuantumLevels[q]) {
                            curr = q;
                            break;
                        }
                    }
                    if (dir > 0) {
                        if (curr + 1 < kQuantumLevels.size()) {
                            draft.smp_quantum = kQuantumLevels[curr + 1];
                        }
                    } else {
                        if (curr > 0) {
                            draft.smp_quantum = kQuantumLevels[curr - 1];
                        }
                    }
                    break;
                }
                case 8:  // Platform Profile (0: PCIe, 1: MMIO)
                    draft.platform_profile =
                        static_cast<uint8_t>((draft.platform_profile + 1) % 2);
                    break;
                case 9: {  // Physical RAM Capacity (MB)
                    static constexpr std::array<uint64_t, 8> kRamSizes = {32,  64,   128,  256,
                                                                          512, 1024, 2048, 4096};
                    size_t curr_idx = 3;  // default 256
                    for (size_t r = 0; r < kRamSizes.size(); ++r) {
                        if (draft.dram_size_mb == kRamSizes[r]) {
                            curr_idx = r;
                            break;
                        }
                    }
                    if (dir > 0) {
                        curr_idx = (curr_idx + 1) % kRamSizes.size();
                    } else {
                        curr_idx = (curr_idx + kRamSizes.size() - 1) % kRamSizes.size();
                    }
                    draft.dram_size_mb = kRamSizes[curr_idx];
                    break;
                }
                case 10:  // VirtIO Network Backend
                    if (draft.net_mode == "user")
                        draft.net_mode = (dir > 0) ? "tap" : "none";
                    else if (draft.net_mode == "tap")
                        draft.net_mode = (dir > 0) ? "socket" : "user";
                    else if (draft.net_mode == "socket")
                        draft.net_mode = (dir > 0) ? "none" : "tap";
                    else
                        draft.net_mode = (dir > 0) ? "user" : "socket";
                    break;
                case 11:
                    if (machine && machine->spike_binary().empty()) break;
                    draft.lockstep_mode = !draft.lockstep_mode;
                    break;
                case 12:
                    draft.gdb_mode = !draft.gdb_mode;
                    break;
                case 13:
                    if (!draft.cycle_accurate) break;
                    draft.bp_trace = !draft.bp_trace;
                    break;
                case 14:
                    draft.use_mix = !draft.use_mix;
                    break;
                case 15:
                    draft.traplog_mode = !draft.traplog_mode;
                    break;
                case 16:
                    if (machine && machine->appmode_enabled()) break;
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
    machine.set_debug_diagnostics_enabled(draft.debug_mode);
    if (draft.high_contrast != machine.high_contrast_enabled()) {
        set_high_contrast(draft.high_contrast);
        machine.set_high_contrast_enabled(draft.high_contrast);
    }
    machine.set_class_mode_enabled(draft.class_mode);
    if (machine.tui_controller()) {
        machine.tui_controller()->set_target_fps(draft.tui_fps);
    }

    machine.set_instruction_mix_enabled(draft.use_mix);
    machine.set_branch_trace_enabled(draft.bp_trace);
    machine.set_trap_log_enabled(draft.traplog_mode);
    machine.set_device_log_enabled(draft.dlog_mode);

    auto next = machine.configuration();
    bool const profile_changed = next.platform_profile !=
        static_cast<simrv::core::PlatformProfile>(draft.platform_profile);
    next.platform_profile = static_cast<simrv::core::PlatformProfile>(draft.platform_profile);

    uint64_t const new_dram_size = draft.dram_size_mb * 1024ULL * 1024ULL;
    bool const dram_size_changed = next.memory.dram_size != new_dram_size;
    next.memory.dram_size = new_dram_size;
    next.network.mode = draft.net_mode;
    next.execution.smp_quantum = draft.smp_quantum;
    next.execution.smp_multithreaded = draft.smp_multithreaded;
    next.debug.lockstep_enabled = draft.lockstep_mode;
    next.debug.gdb_enabled = draft.gdb_mode;

    for (size_t hart = 0; hart < machine.num_harts(); ++hart) {
        machine.hart(hart).pipeline_sim.config.record_snapshots =
            machine.runtime_profile.records_cycle_history();
    }

    if (machine.instruction_mix_enabled()) {
        set_reg_page_cb(TuiRegPage::GPR);
    }

    bool need_reboot = dram_size_changed || profile_changed ||
                       next.execution.smp_quantum != machine.execution_config().smp_quantum ||
                       next.execution.smp_multithreaded != machine.execution_config().smp_multithreaded ||
                       next.network.mode != machine.network_mode() ||
                       next.debug.lockstep_enabled != machine.lockstep_enabled() ||
                       next.debug.gdb_enabled != machine.debugger_enabled();

    // 2. MISA Extensions
    uint64_t const new_misa = draft.misa.to_misa_val();
    if (machine.primary_hart().state().misa != new_misa || next.isa.vlen != draft.misa.vlen) {
        next.isa.misa_profile = new_misa;
        next.isa.misa_override = true;
        next.isa.misa_xlen = draft.misa.xlen_bits;
        next.isa.vlen = draft.misa.vlen;
        need_reboot = true;
    }

    if (need_reboot) {
        (void)machine.stage_reconfiguration(std::move(next));
    }

    // 3. System Microarchitecture
    SystemConfigModal::submit(draft.sys_config, machine);

    return true;
}

void SettingsModal::render(std::vector<std::string>& content_rows,
                           const std::function<void(const std::string&)>& add_row_cb,
                           const SettingsDraft& draft, const simrv::core::Machine& machine) {
    (void)content_rows;

    static constexpr std::array<std::string_view, 3> kTabNames = {"General / UI", "MISA Extensions",
                                                                  "Microarchitecture"};

    add_row_cb(build_modal_tab_bar(kTabNames, draft.active_tab));
    add_row_cb("");

    if (draft.active_tab == 0) {
        add_row_cb(
            std::format("{}Use \033[1m[↑/↓]\033[0m to navigate, \033[1m[←/→/Space]\033[0m to "
                        "toggle/adjust, \033[1m[Tab/1-3]\033[0m to switch tabs:\033[0m",
                        kThemeMuted));
        add_row_cb("");

        const bool bp_disabled = !draft.cycle_accurate;
        const bool dlog_disabled = machine.appmode_enabled();
        const bool lockstep_disabled = machine.spike_binary().empty();

        std::string profile_str;
        if (draft.platform_profile == 0)
            profile_str = "\033[1;32m[PCIe (PCIe 1.2 + ECAM)]\033[0m";
        else if (draft.platform_profile == 1)
            profile_str = "\033[1;36m[MMIO (VirtIO-MMIO v2)]\033[0m";

        std::string net_str;
        if (draft.net_mode == "user")
            net_str = "\033[1;32m[User Mode (Echo/Loopback)]\033[0m";
        else if (draft.net_mode == "tap")
            net_str = "\033[1;36m[Linux TAP Bridge]\033[0m";
        else if (draft.net_mode == "socket")
            net_str = "\033[1;33m[Socket Tunnel]\033[0m";
        else
            net_str = "\033[90m[Disabled]\033[0m";

        std::string fps_str;
        if (draft.tui_fps == 10)
            fps_str = "\033[1;33m[10 FPS (Power Saver)]\033[0m";
        else if (draft.tui_fps == 15)
            fps_str = "\033[1;36m[15 FPS (Turbo IA)]\033[0m";
        else if (draft.tui_fps == 60)
            fps_str = "\033[1;35m[60 FPS (Ultra Smooth)]\033[0m";
        else
            fps_str = "\033[1;32m[30 FPS (Standard)]\033[0m";

        constexpr Address dram_base = 0x80000000ULL;
        std::string ram_str =
            std::format("\033[1;36m[{} MB (0x{:08x}–0x{:08x})]\033[0m", draft.dram_size_mb,
                        dram_base, dram_base + (draft.dram_size_mb * 1024ULL * 1024ULL) - 1);

        struct ItemInfo {
            const char* name;
            std::string val;
        };

        const auto settings = std::to_array<ItemInfo>({
            {.name = "Simulation Precision",
             .val = draft.cycle_accurate ? "\033[1;36m[Cycle-Accurate (CA)]\033[0m"
                                         : "\033[1;32m[Instruction-Accurate (IA)]\033[0m"},
            {.name = "Debug Mode & Diagnostics",
             .val = draft.debug_mode ? "\033[1;32m[ON (Diagnostics Active)]\033[0m"
                                     : "\033[90m[OFF]\033[0m"},
            {.name = "Color Theme / Contrast",
             .val = draft.high_contrast ? "\033[1;33m[High Contrast B&W]\033[0m"
                                        : "\033[1;34m[Adaptive / Terminal Colors]\033[0m"},
            {.name = "Presentation Mode",
             .val = draft.class_mode ? "\033[1;35m[Classroom / Large]\033[0m"
                                     : "\033[90m[Standard]\033[0m"},
            {.name = "TUI Target Refresh Rate", .val = fps_str},
            {.name = "Active SMP Hart Count",
             .val = std::format("\033[1;32m[{} Core{}]\033[0m", draft.num_harts,
                                draft.num_harts > 1 ? "s" : "")},
            {.name = "SMP Threading Model",
             .val = draft.smp_multithreaded ? "\033[1;36m[Multi-Threaded (MT-SMP)]\033[0m"
                                            : "\033[1;33m[Single-Threaded Co-op]\033[0m"},
            {.name = "SMP Scheduler Quantum",
             .val = std::format("\033[1;36m[{} insns/slice]\033[0m", draft.smp_quantum)},
            {.name = "Platform Bus Topology", .val = profile_str},
            {.name = "Physical RAM Capacity", .val = ram_str},
            {.name = "Virtual Network Mode", .val = net_str},
            {.name = "Spike Lockstep Compare",
             .val = lockstep_disabled
                        ? "\033[90m[OFF (Requires --spike-bin)]\033[0m"
                        : (draft.lockstep_mode ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m")},
            {.name = "GDB Remote Debug Stub",
             .val = draft.gdb_mode ? "\033[1;32m[ON (Port 1234)]\033[0m" : "\033[90m[OFF]\033[0m"},
            {.name = "Branch Prediction Log",
             .val = bp_disabled ? "\033[90m[OFF (Requires CA mode)]\033[0m"
                                : (draft.bp_trace ? "\033[1;32m[ON (trace/bpred.txt)]\033[0m"
                                                  : "\033[90m[OFF]\033[0m")},
            {.name = "Instruction Mix Summary",
             .val = draft.use_mix ? "\033[1;32m[ON (trace/instmix.txt)]\033[0m"
                                  : "\033[90m[OFF]\033[0m"},
            {.name = "Architectural Trap Log",
             .val = draft.traplog_mode ? "\033[1;32m[ON (trace/traplog.txt)]\033[0m"
                                       : "\033[90m[OFF]\033[0m"},
            {.name = "Device MMIO Access Log",
             .val = dlog_disabled ? "\033[90m[OFF (Disabled in App Mode)]\033[0m"
                                  : (draft.dlog_mode ? "\033[1;32m[ON (trace/dlog.txt)]\033[0m"
                                                     : "\033[90m[OFF]\033[0m")},
        });

        int const cursor = draft.tab_cursor[0];
        for (std::size_t i = 0; i < settings.size(); ++i) {
            if (i == 0) {
                add_row_cb(build_section_divider("Simulation Engine & Visualization"));
            } else if (i == 5) {
                add_row_cb("");
                add_row_cb(build_section_divider("Symmetric Multiprocessing (SMP) Configuration"));
            } else if (i == 8) {
                add_row_cb("");
                add_row_cb(build_section_divider("Platform Profile & Peripheral Devices"));
            } else if (i == 11) {
                add_row_cb("");
                add_row_cb(build_section_divider("External Integrations & Debug Stubs"));
            } else if (i == 13) {
                add_row_cb("");
                add_row_cb(build_section_divider("Traces & Logging (Creates Text Files)"));
            }
            const bool is_sel = (static_cast<int>(i) == cursor);
            add_row_cb(build_menu_item_row(settings[i].name, settings[i].val, is_sel, 29));
        }
    } else if (draft.active_tab == 1) {
        add_row_cb(
            std::format("{}Use \033[1m[↑/↓]\033[0m to navigate, \033[1m[Space/Enter]\033[0m to "
                        "toggle, \033[1m[p]\033[0m Base, \033[1m[i]\033[0m IMAC, \033[1m[g]\033[0m "
                        "GC:\033[0m",
                        kThemeMuted));
        add_row_cb("");
        MisaModal::render(content_rows, add_row_cb, draft.misa, draft.tab_cursor[1], machine,
                          /*show_footer=*/false);
    } else if (draft.active_tab == 2) {
        SystemConfigModal::render(content_rows, add_row_cb, draft.sys_config, draft.tab_cursor[2]);
    }

    add_row_cb("");
    add_row_cb(build_modal_footer({{"[Enter]", "Apply Settings"}, {"[Esc / q]", "Cancel"}}));
}

}  // namespace simrv::tui::modals
