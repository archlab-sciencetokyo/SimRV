/**
 * @file SettingsModal.cpp
 * @brief Implementation of multi-tab Settings modal configuration dialog handling.
 */
#include "simrv/tui/modals/SettingsModal.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <sstream>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/CpuConfigParser.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/memory/RamView.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/modals/MisaModal.hpp"
#include "simrv/tui/modals/ModalComponents.hpp"
#include "simrv/tui/modals/SystemConfigModal.hpp"

namespace simrv::tui::modals {
namespace {

auto theme_style_label(TuiThemeStyle style) -> const char* {
    switch (style) {
        case TuiThemeStyle::ClassicAnsi:
            return "Classic ANSI";
        case TuiThemeStyle::SakuraPastel:
            return "Sakura Pastel";
        case TuiThemeStyle::ModernUnicode:
        default:
            return "Modern Unicode";
    }
}

}  // namespace

void SettingsModal::render_content(std::vector<std::string>& content_rows, int /*width*/,
                                   int /*height*/) {
    auto add_row = [&](std::string s) { content_rows.push_back(std::move(s)); };
    render(content_rows, add_row, draft_, *machine_);
}

auto SettingsModal::cursor_row() const -> int {
    return draft_.active_tab == 0 ? general_setting_row(draft_.tab_cursor[0])
                                  : 3 + draft_.tab_cursor[draft_.active_tab];
}

void SettingsModal::scroll_to_top() { move_cursor(draft_, -999999); }

void SettingsModal::scroll_to_bottom() { move_cursor(draft_, 999999); }

void SettingsModal::handle_wheel(int delta) { move_cursor(draft_, delta); }

auto SettingsModal::handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction {
    if (byte == 27 || key == simrv::tui::TuiKey::Esc || byte == 'q' || byte == 'Q') {
        return framework::ModalAction::Close;
    }
    if (key == simrv::tui::TuiKey::Enter || key == simrv::tui::TuiKey::Newline) {
        if (draft_.active_tab == 2 && draft_.tab_cursor[2] == 13) {
            if (host_) host_->open_save_cpu_config();
            return framework::ModalAction::Continue;
        }
        if (draft_.active_tab == 2 && draft_.tab_cursor[2] == 14) {
            if (host_) host_->open_load_cpu_config();
            return framework::ModalAction::Continue;
        }
        return framework::ModalAction::Submit;
    }
    if (key == simrv::tui::TuiKey::Tab) {
        cycle_tab(draft_, 1);
        return framework::ModalAction::Continue;
    }
    if (key == simrv::tui::TuiKey::BackTab) {
        cycle_tab(draft_, -1);
        return framework::ModalAction::Continue;
    }
    if (byte == '1') {
        set_tab(draft_, 0);
        return framework::ModalAction::Continue;
    }
    if (byte == '2') {
        set_tab(draft_, 1);
        return framework::ModalAction::Continue;
    }
    if (byte == '3') {
        set_tab(draft_, 2);
        return framework::ModalAction::Continue;
    }
    if (byte == ' ') {
        if (draft_.active_tab == 2 && draft_.tab_cursor[2] == 13) {
            if (host_) host_->open_save_cpu_config();
            return framework::ModalAction::Continue;
        }
        if (draft_.active_tab == 2 && draft_.tab_cursor[2] == 14) {
            if (host_) host_->open_load_cpu_config();
            return framework::ModalAction::Continue;
        }
        toggle_setting(draft_, machine_);
        return framework::ModalAction::Continue;
    }
    if ((byte == 's' || byte == 'S') && draft_.active_tab == 2) {
        if (host_) host_->open_save_cpu_config();
        return framework::ModalAction::Continue;
    }
    if ((byte == 'l' || byte == 'L') && draft_.active_tab == 2) {
        if (host_) host_->open_load_cpu_config();
        return framework::ModalAction::Continue;
    }
    if (byte == 'p' || byte == 'P') {
        apply_isa_preset(draft_, 0);
        return framework::ModalAction::Continue;
    }
    if (byte == 'i' || byte == 'I') {
        apply_isa_preset(draft_, 1);
        return framework::ModalAction::Continue;
    }
    if (byte == 'g' || byte == 'G') {
        apply_isa_preset(draft_, 2);
        return framework::ModalAction::Continue;
    }
    if (byte >= '0' && byte <= '9') {
        std::string dummy;
        push_digit(draft_, dummy, static_cast<char>(byte));
        return framework::ModalAction::Continue;
    }
    if (byte == 8 || byte == 127 || key == simrv::tui::TuiKey::Backspace) {
        std::string dummy;
        pop_digit(draft_, dummy);
        return framework::ModalAction::Continue;
    }
    return framework::ModalAction::Continue;
}

auto SettingsModal::handle_control_click(int content_row, size_t action_index)
    -> framework::ModalAction {
    if (content_row == 0) {
        set_tab(draft_, static_cast<uint8_t>(action_index));
        return framework::ModalAction::Continue;
    }
    return action_index == 0 ? framework::ModalAction::Submit : framework::ModalAction::Close;
}

auto SettingsModal::handle_click(framework::ScreenCoord local_pt, framework::ScreenSize box_size)
    -> framework::ModalAction {
    if (draft_.active_tab != 0) return framework::ModalAction::Continue;
    const int item = general_setting_at_row(local_pt.y - 1);
    if (item < 0) return framework::ModalAction::Continue;
    draft_.tab_cursor[0] = item;
    if (box_size.width > 0 && local_pt.x > box_size.width / 2 + 10) {
        adjust_setting(draft_, 1);
    } else if (box_size.width > 0 && local_pt.x > box_size.width / 2) {
        adjust_setting(draft_, -1);
    } else {
        toggle_setting(draft_, machine_);
    }
    return framework::ModalAction::Continue;
}

auto SettingsModal::handle_click(framework::ScreenCoord local_pt) -> framework::ModalAction {
    return handle_click(local_pt, framework::ScreenSize{.width = 0, .height = 0});
}

void SettingsModal::open(SettingsDraft& draft, const simrv::core::Machine& machine,
                         framework::DensityMode density_mode, TuiThemeStyle theme_style) {
    draft.active_tab = 0;
    draft.tab_cursor[0] = 0;
    draft.tab_cursor[1] = 0;
    draft.tab_cursor[2] = 0;

    // Tab 0: General
    draft.cycle_accurate = machine.runtime_profile.is_cycle_mode();
    draft.high_contrast = machine.high_contrast_enabled();
    draft.theme_style = theme_style;
    draft.density_mode = density_mode;
    draft.use_mix = machine.instruction_mix_enabled();
    draft.bp_trace = machine.branch_trace_enabled();
    draft.traplog_mode = machine.trap_log_enabled();
    draft.dlog_mode = machine.device_log_enabled();
    draft.lockstep_mode = machine.lockstep_enabled();
    draft.num_harts = machine.execution_config().num_harts;
    draft.smp_quantum = machine.execution_config().smp_quantum;
    draft.smp_multi_threaded =
        machine.execution_config().smp_mode.value_or(machine.configuration().soc.smp.mode) ==
        simrv::core::SmpMode::MultiThreaded;
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
            constexpr int kNumSettings = static_cast<int>(kGeneralSettingDescriptors.size());
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
    constexpr int kNumSettings = static_cast<int>(kGeneralSettingDescriptors.size());
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
            const int index = draft.tab_cursor[0];
            const auto setting =
                index >= 0 && static_cast<size_t>(index) < kGeneralSettingDescriptors.size()
                    ? kGeneralSettingDescriptors[static_cast<size_t>(index)].setting
                    : GeneralSetting::Count;
            switch (setting) {
                case GeneralSetting::ExecutionPrecision:
                    draft.cycle_accurate = !draft.cycle_accurate;
                    draft.sys_config.cycle_accurate = draft.cycle_accurate;
                    break;
                case GeneralSetting::HighContrastColors:
                    draft.high_contrast = !draft.high_contrast;
                    break;
                case GeneralSetting::ThemeStyle: {
                    const int current = static_cast<int>(draft.theme_style);
                    const int next = (current + (dir >= 0 ? 1 : 2)) % 3;
                    draft.theme_style = static_cast<TuiThemeStyle>(next);
                    break;
                }
                case GeneralSetting::PanelDensity: {
                    const auto current = std::to_underlying(draft.density_mode);
                    const auto next = static_cast<int>(current) + (dir >= 0 ? 1 : 2);
                    draft.density_mode = static_cast<framework::DensityMode>(next % 3);
                    break;
                }
                case GeneralSetting::HartCount: {
                    int v = static_cast<int>(draft.num_harts) + dir;
                    draft.num_harts = static_cast<uint32_t>(std::clamp(
                        v, 1, static_cast<int>(simrv::core::ExecutionConfig::kMaxHarts)));
                    break;
                }
                case GeneralSetting::SmpExecutionMode:
                    draft.smp_multi_threaded = !draft.smp_multi_threaded;
                    break;
                case GeneralSetting::SmpQuantum: {
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
                case GeneralSetting::PlatformBus:
                    draft.platform_profile = static_cast<uint8_t>((draft.platform_profile + 1) % 2);
                    break;
                case GeneralSetting::DramSize: {
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
                case GeneralSetting::NetworkMode:
                    if (draft.net_mode == "user")
                        draft.net_mode = (dir > 0) ? "tap" : "none";
                    else if (draft.net_mode == "tap")
                        draft.net_mode = (dir > 0) ? "socket" : "user";
                    else if (draft.net_mode == "socket")
                        draft.net_mode = (dir > 0) ? "none" : "tap";
                    else
                        draft.net_mode = (dir > 0) ? "user" : "socket";
                    break;
                case GeneralSetting::SpikeLockstep:
                    if (machine && machine->spike_binary().empty()) break;
                    draft.lockstep_mode = !draft.lockstep_mode;
                    break;
                case GeneralSetting::BranchPredictionLog:
                    if (!draft.cycle_accurate) break;
                    draft.bp_trace = !draft.bp_trace;
                    break;
                case GeneralSetting::InstructionMixLog:
                    draft.use_mix = !draft.use_mix;
                    break;
                case GeneralSetting::TrapLog:
                    draft.traplog_mode = !draft.traplog_mode;
                    break;
                case GeneralSetting::DeviceLog:
                    if (machine && machine->appmode_enabled()) break;
                    draft.dlog_mode = !draft.dlog_mode;
                    break;
                case GeneralSetting::Count:
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

void SettingsModal::apply_isa_preset(SettingsDraft& draft, int preset_idx) {
    if (draft.active_tab == 1) {
        MisaModal::apply_preset(draft.misa, preset_idx);
    }
}

auto SettingsModal::submit(const SettingsDraft& draft, simrv::core::Machine& machine,
                           const std::function<void(TuiRegPage)>& set_reg_page_cb,
                           const std::function<void(simrv::core::MachineConfig, SysConfigDraft,
                                                    bool)>& confirm_reconfiguration) -> bool {
    // 1. General & UI settings
    machine.runtime_profile.interaction = simrv::core::InteractionMode::Tui;
    const auto target_engine = simrv::core::select_execution_engine(
        draft.cycle_accurate, machine.runtime_profile.interaction);
    if (draft.high_contrast != machine.high_contrast_enabled()) {
        set_high_contrast(draft.high_contrast);
        machine.set_high_contrast_enabled(draft.high_contrast);
    }
    machine.set_instruction_mix_enabled(draft.use_mix);
    machine.set_branch_trace_enabled(draft.bp_trace);
    machine.set_trap_log_enabled(draft.traplog_mode);
    machine.set_device_log_enabled(draft.dlog_mode);

    auto next = machine.configuration();
    bool const profile_changed =
        next.platform_profile != static_cast<simrv::core::PlatformProfile>(draft.platform_profile);
    next.platform_profile = static_cast<simrv::core::PlatformProfile>(draft.platform_profile);

    uint64_t const new_dram_size = draft.dram_size_mb * 1024ULL * 1024ULL;
    bool const dram_size_changed = next.memory.dram_size != new_dram_size;
    next.memory.dram_size = new_dram_size;
    next.network.mode = draft.net_mode;
    next.execution.num_harts = draft.num_harts;
    next.execution.smp_quantum = draft.smp_quantum;
    next.execution.smp_mode = draft.smp_multi_threaded ? simrv::core::SmpMode::MultiThreaded
                                                       : simrv::core::SmpMode::SingleThreaded;
    next.soc.smp.resize_harts(draft.num_harts);
    next.soc.smp.mode = *next.execution.smp_mode;
    next.soc.smp.synchronization_quantum = draft.smp_quantum;
    next.debug.lockstep_enabled = draft.lockstep_mode;

    for (size_t hart = 0; hart < machine.num_harts(); ++hart) {
        machine.hart(hart).pipeline_sim.config.record_snapshots =
            machine.runtime_profile.records_cycle_history();
    }

    if (machine.instruction_mix_enabled()) {
        set_reg_page_cb(TuiRegPage::GPR);
    }

    bool need_reboot =
        dram_size_changed || profile_changed ||
        next.execution.num_harts != machine.execution_config().num_harts ||
        next.execution.smp_quantum != machine.execution_config().smp_quantum ||
        next.execution.smp_mode.value_or(next.soc.smp.mode) !=
            machine.execution_config().smp_mode.value_or(machine.configuration().soc.smp.mode) ||
        next.network.mode != machine.network_mode() ||
        next.debug.lockstep_enabled != machine.lockstep_enabled();

    auto requested_cpu_model = machine.primary_hart().cpu_model_config;
    bool cpu_model_changed = false;
    if (draft.sys_config.cycle_accurate) {
        if (!SystemConfigModal::apply_to_model(draft.sys_config, requested_cpu_model)) return false;
        std::ostringstream current_model_text;
        std::ostringstream requested_model_text;
        simrv::core::serialize_cpu_config(machine.primary_hart().cpu_model_config,
                                          current_model_text);
        simrv::core::serialize_cpu_config(requested_cpu_model, requested_model_text);
        cpu_model_changed = current_model_text.str() != requested_model_text.str();
        need_reboot = need_reboot || cpu_model_changed;
    }

    // 2. MISA Extensions
    uint64_t const new_misa = draft.misa.to_misa_val();
    const auto current_vlen = machine.isa_config().vlen ? machine.isa_config().vlen
                                                        : machine.primary_hart().state().regs.vlen;
    const auto current_misa = simrv::isa::misa_with_mxl(machine.primary_hart().state().misa,
                                                        machine.primary_hart().state().regs.xlen);
    if (machine.primary_hart().state().misa != 0 &&
        (current_misa != new_misa || current_vlen != draft.misa.vlen)) {
        next.isa.isa_preset = new_misa;
        next.isa.misa_override = true;
        next.isa.misa_xlen = draft.misa.xlen_bits;
        next.isa.vlen = draft.misa.vlen;
        need_reboot = true;
    }

    if (need_reboot) {
        if (draft.sys_config.cycle_accurate)
            next.cpu_model_config_override = std::move(requested_cpu_model);
        if (confirm_reconfiguration) {
            confirm_reconfiguration(std::move(next), draft.sys_config, draft.cycle_accurate);
            return false;
        }
        machine.switch_execution_engine(target_engine);
        (void)machine.stage_reconfiguration(std::move(next));
    } else {
        machine.switch_execution_engine(target_engine);
        // 3. System Microarchitecture applies immediately only when no machine restart is needed.
        if (cpu_model_changed) SystemConfigModal::submit(draft.sys_config, machine);
    }

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
            std::format("{}[↑/↓] Move  [←/→/Space] Change  [Tab/1-3] Tabs:\033[0m", kThemeMuted));
        add_row_cb(
            std::format("{}Theme, contrast, and density save on Apply. Machine changes may need a "
                        "restart.\033[0m",
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

        constexpr simrv::xlen::Address dram_base = simrv::memory::kDramBaseAddress;
        std::string ram_str =
            std::format("\033[1;36m[{} MB (0x{:08x}–0x{:08x})]\033[0m", draft.dram_size_mb,
                        dram_base, dram_base + (draft.dram_size_mb * 1024ULL * 1024ULL) - 1);

        const std::array<std::string, kGeneralSettingDescriptors.size()> values = {
            draft.cycle_accurate ? "\033[1;36m[Cycle-Accurate (CA)]\033[0m"
                                 : "\033[1;32m[Instruction-Accurate (IA)]\033[0m",
            draft.high_contrast ? "\033[1;33m[High Contrast B&W]\033[0m"
                                : "\033[1;34m[Adaptive / Terminal Colors]\033[0m",
            std::format("\033[1;36m[{}]\033[0m", theme_style_label(draft.theme_style)),
            draft.density_mode == framework::DensityMode::Compact ? "\033[1;36m[Compact]\033[0m"
            : draft.density_mode == framework::DensityMode::Comfortable
                ? "\033[1;32m[Comfortable]\033[0m"
                : "\033[1;33m[Auto]\033[0m",
            std::format("\033[1;32m[{} Core{}]\033[0m", draft.num_harts,
                        draft.num_harts > 1 ? "s" : ""),
            draft.smp_multi_threaded ? "\033[1;36m[Multi-Threaded]\033[0m"
                                     : "\033[1;33m[Single-Threaded]\033[0m",
            std::format("\033[1;36m[{} insns/slice]\033[0m", draft.smp_quantum),
            profile_str,
            ram_str,
            net_str,
            lockstep_disabled
                ? "\033[90m[OFF (Requires --spike-bin)]\033[0m"
                : (draft.lockstep_mode ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"),
            bp_disabled ? "\033[90m[OFF (Requires CA mode)]\033[0m"
                        : (draft.bp_trace ? "\033[1;32m[ON (<trace-dir>/bpred.txt)]\033[0m"
                                          : "\033[90m[OFF]\033[0m"),
            draft.use_mix ? "\033[1;32m[ON (<trace-dir>/instmix.txt)]\033[0m"
                          : "\033[90m[OFF]\033[0m",
            draft.traplog_mode ? "\033[1;32m[ON (<trace-dir>/traplog.txt)]\033[0m"
                               : "\033[90m[OFF]\033[0m",
            dlog_disabled ? "\033[90m[OFF (Disabled in App Mode)]\033[0m"
                          : (draft.dlog_mode ? "\033[1;32m[ON (<trace-dir>/dlog.txt)]\033[0m"
                                             : "\033[90m[OFF]\033[0m"),
        };

        int const cursor = draft.tab_cursor[0];
        for (std::size_t i = 0; i < kGeneralSettingDescriptors.size(); ++i) {
            const auto& descriptor = kGeneralSettingDescriptors[i];
            if (!descriptor.section.empty()) {
                if (i != 0) add_row_cb("");
                add_row_cb(build_section_divider(descriptor.section));
            }
            const bool is_sel = (static_cast<int>(i) == cursor);
            const int label_width =
                framework::active_render_context().density == framework::Density::Compact ? 20 : 29;
            add_row_cb(build_menu_item_row(descriptor.label, values[i], is_sel, label_width));
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
    add_row_cb(build_modal_footer({{.key = "[Enter]", .action = "Apply Settings"},
                                   {.key = "[Esc / q]", .action = "Cancel"}}));
}

}  // namespace simrv::tui::modals
