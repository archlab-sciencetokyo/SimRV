/**
 * @file TuiModal.cpp
 * @brief Implementation of TUI Modal dialogs and stable overlay rendering.
 */

#include "simrv/tui/TuiModal.hpp"
#include <charconv>
#include <format>
#include "simrv/core/Machine.hpp"
#include "simrv/tui/LeftPane.hpp"
#include "simrv/tui/TuiTheme.hpp"

namespace simrv::tui {

TuiModal::TuiModal(simrv::core::Machine& machine) : machine_(machine) {}

void TuiModal::open(ModalType type, LeftPane* left_pane, uint64_t step_granularity, uint64_t step_delay_us) {
    active_modal_ = type;
    input_.clear();
    switch (type) {
        case ModalType::SetBreakpoint:
            input_ = std::format("0x{:08x}", machine_.cpu.state().pc);
            break;
        case ModalType::SetStepSize:
            input_ = std::to_string(step_granularity);
            break;
        case ModalType::SetSpeed: {
            uint64_t hz = step_delay_us > 0 ? (1000000 / step_delay_us) : 0;
            input_ = std::to_string(hz);
            break;
        }
        case ModalType::InspectAddress:
            input_ = std::format("0x{:08x}", left_pane ? left_pane->get_inspect_addr() : 0);
            break;
        case ModalType::LoadBinary:
            input_ = machine_.s_fn_memimg;
            load_appmode_ = machine_.s_appmode;  // Pre-fill from current machine mode
            break;
        case ModalType::LoadDiskImage:
            input_ = machine_.s_fn_dskimg;
            break;
        case ModalType::Settings:
            settings_cursor_ = 0;
            break;
        default:
            break;
    }
}

void TuiModal::move_settings_cursor(int delta) {
    constexpr int kNumSettings = 12;
    settings_cursor_ = (settings_cursor_ + delta + kNumSettings) % kNumSettings;
}

void TuiModal::toggle_setting_at_cursor() {
    toggle_setting_by_index(settings_cursor_);
}

void TuiModal::toggle_setting_by_index(int index) {
    switch (index) {
        case 0: machine_.s_cycle_accurate = !machine_.s_cycle_accurate; break;
        case 1: machine_.s_debug_mode = !machine_.s_debug_mode; break;
        case 2: machine_.s_appmode = !machine_.s_appmode; break;
        case 3:
            machine_.s_rollback_enabled = !machine_.s_rollback_enabled;
            if (!machine_.s_rollback_enabled) machine_.cpu.undo_stack.clear();
            break;
        case 4: machine_.s_high_contrast = !machine_.s_high_contrast; break;
        case 5: machine_.s_use_mix = !machine_.s_use_mix; break;
        case 6: machine_.s_bp_trace = !machine_.s_bp_trace; break;
        case 7: machine_.s_traplog_mode = !machine_.s_traplog_mode; break;
        case 8: machine_.s_dlog_mode = !machine_.s_dlog_mode; break;
        case 9: machine_.s_high_performance = !machine_.s_high_performance; break;
        case 10: machine_.s_lockstep_mode = !machine_.s_lockstep_mode; break;
        case 11: machine_.s_gdb_mode = !machine_.s_gdb_mode; break;
        default: break;
    }
}

void TuiModal::close() {
    active_modal_ = ModalType::None;
    input_.clear();
}

auto TuiModal::submit(LeftPane* left_pane, std::atomic<uint64_t>& step_granularity,
                      std::atomic<uint64_t>& step_delay_us,
                      const std::function<void(TuiRegPage)>& set_reg_page_cb,
                      const std::function<void(const std::string&)>& set_status_override_cb,
                      const std::function<void()>& on_speed_changed_cb) -> bool {
    if (active_modal_ == ModalType::None) return false;
    std::string text = input_;
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.erase(text.begin());
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.pop_back();

    if (!text.empty() && active_modal_ != ModalType::Help) {
        switch (active_modal_) {
            case ModalType::SetBreakpoint: {
                Address addr = 0;
                bool ok = false;
                if (text.starts_with("0x") || text.starts_with("0X")) {
                    auto result = std::from_chars(text.data() + 2, text.data() + text.size(), addr, 16);
                    ok = (result.ec == std::errc{});
                } else if (std::all_of(text.begin(), text.end(), ::isxdigit)) {
                    auto result = std::from_chars(text.data(), text.data() + text.size(), addr, 16);
                    ok = (result.ec == std::errc{});
                } else {
                    auto sym_opt = machine_.symbols.lookup_name(text);
                    if (sym_opt.has_value()) {
                        addr = *sym_opt;
                        ok = true;
                    }
                }
                if (ok) {
                    machine_.breakpoints.add_pc_breakpoint(addr);
                    set_status_override_cb(std::format("Breakpoint set at 0x{:08x}", addr));
                } else {
                    set_status_override_cb(std::format("Symbol/address not found: {}", text));
                }
                break;
            }
            case ModalType::SetStepSize: {
                uint64_t val = 0;
                auto result = std::from_chars(text.data(), text.data() + text.size(), val);
                if (result.ec == std::errc{} && val > 0) {
                    step_granularity.store(val, std::memory_order_relaxed);
                    set_status_override_cb(std::format("StepN set to {} instructions", val));
                } else {
                    set_status_override_cb(std::format("Invalid step size: {}", text));
                }
                break;
            }
            case ModalType::SetSpeed: {
                uint64_t val = 0;
                auto result = std::from_chars(text.data(), text.data() + text.size(), val);
                if (result.ec == std::errc{}) {
                    const uint64_t old_delay = step_delay_us.load(std::memory_order_relaxed);
                    if (val == 0) {
                        step_delay_us.store(0, std::memory_order_relaxed);
                        set_status_override_cb("Speed set to Max (no delay)");
                    } else {
                        uint64_t delay = 1000000 / val;
                        step_delay_us.store(delay, std::memory_order_relaxed);
                        set_status_override_cb(std::format("Speed set to {} Hz (delay: {}us)", val, delay));
                    }
                    if (on_speed_changed_cb &&
                        old_delay != step_delay_us.load(std::memory_order_relaxed)) {
                        on_speed_changed_cb();
                    }
                } else {
                    set_status_override_cb(std::format("Invalid speed frequency: {}", text));
                }
                break;
            }
            case ModalType::InspectAddress: {
                Address addr = 0;
                bool ok = false;
                if (text.starts_with("0x") || text.starts_with("0X")) {
                    auto result = std::from_chars(text.data() + 2, text.data() + text.size(), addr, 16);
                    ok = (result.ec == std::errc{});
                } else if (std::all_of(text.begin(), text.end(), ::isxdigit)) {
                    auto result = std::from_chars(text.data(), text.data() + text.size(), addr, 16);
                    ok = (result.ec == std::errc{});
                } else {
                    auto sym_opt = machine_.symbols.lookup_name(text);
                    if (sym_opt.has_value()) {
                        addr = *sym_opt;
                        ok = true;
                    }
                }
                if (ok) {
                    if (left_pane) {
                        left_pane->set_inspect_addr(addr);
                    }
                    if (set_reg_page_cb) {
                        set_reg_page_cb(TuiRegPage::STACK);
                    }
                    set_status_override_cb(std::format("Inspecting memory at 0x{:08x}", addr));
                } else {
                    set_status_override_cb(std::format("Invalid address/symbol: {}", text));
                }
                break;
            }
            case ModalType::LoadBinary: {
                bool const mode_change = (load_appmode_ != machine_.s_appmode);
                // OS/RTOS mode may require an optional disk image, so stage the binary first.
                if (!load_appmode_) {
                    staged_binary_path_ = text;
                    staged_mode_change_ = mode_change;
                    staged_target_appmode_ = load_appmode_;
                    active_modal_ = ModalType::LoadDiskImage;
                    input_ = machine_.s_fn_dskimg;
                    set_status_override_cb(
                        "RTOS mode selected: optionally set disk image, or press Enter to skip");
                    return false;
                }

                if (mode_change) {
                    // App/OS mode differs: reboot into the selected image/mode.
                    machine_.pending_binary_path = text;
                    machine_.pending_appmode = load_appmode_;
                    machine_.pending_disk_path =
                        std::string{};  // App mode does not use a disk image.
                    set_status_override_cb(std::format("Switching to {} mode with: {}",
                                                       load_appmode_ ? "App" : "OS", text));
                    active_modal_ = ModalType::None;
                    input_.clear();
                    staged_binary_path_.clear();
                    machine_.request_reboot();
                    return true;
                }

                if (machine_.load_program_binary(text)) {
                    set_status_override_cb(
                        std::format("Loaded binary: {} - press [c] to run", text));
                    active_modal_ = ModalType::None;
                    input_.clear();
                    staged_binary_path_.clear();
                    return false;
                }
                set_status_override_cb(std::format("Failed to load binary: {}", text));
                // Keep modal open so user can try again
                return false;
            }
            case ModalType::LoadDiskImage: {
                const bool skip_disk = text.empty();

                if (!staged_binary_path_.empty()) {
                    if (staged_mode_change_) {
                        machine_.pending_binary_path = staged_binary_path_;
                        machine_.pending_appmode = staged_target_appmode_;
                        machine_.pending_disk_path = skip_disk
                                                         ? std::optional<std::string>(std::string{})
                                                         : std::optional<std::string>(text);
                        set_status_override_cb(std::format(
                            "Switching to {} mode with: {}{}",
                            staged_target_appmode_ ? "App" : "OS", staged_binary_path_,
                            skip_disk ? " (disk skipped)" : std::format(", disk: {}", text)));
                        active_modal_ = ModalType::None;
                        input_.clear();
                        staged_binary_path_.clear();
                        staged_mode_change_ = false;
                        machine_.request_reboot();
                        return true;
                    }

                    if (!machine_.load_program_binary(staged_binary_path_)) {
                        set_status_override_cb(
                            std::format("Failed to load binary: {}", staged_binary_path_));
                        active_modal_ = ModalType::LoadBinary;
                        input_ = staged_binary_path_;
                        staged_binary_path_.clear();
                        staged_mode_change_ = false;
                        return false;
                    }

                    if (!skip_disk) {
                        if (machine_.load_disk_image(text)) {
                            set_status_override_cb(
                                std::format("Loaded binary: {} with disk: {} - press [c] to run",
                                            staged_binary_path_, text));
                        } else {
                            set_status_override_cb(std::format(
                                "Loaded binary: {} - disk load failed, continuing without disk",
                                staged_binary_path_));
                            machine_.s_use_disk = false;
                            machine_.s_fn_dskimg.clear();
                        }
                    } else {
                        machine_.s_use_disk = false;
                        machine_.s_fn_dskimg.clear();
                        set_status_override_cb(
                            std::format("Loaded binary: {} (disk skipped) - press [c] to run",
                                        staged_binary_path_));
                    }
                } else {
                    if (!skip_disk && machine_.load_disk_image(text)) {
                        set_status_override_cb(std::format("Disk image loaded: {}", text));
                    } else if (skip_disk) {
                        machine_.s_use_disk = false;
                        machine_.s_fn_dskimg.clear();
                        set_status_override_cb("Disk image skipped");
                    } else {
                        set_status_override_cb("Disk image load failed");
                    }
                }

                active_modal_ = ModalType::None;
                input_.clear();
                staged_binary_path_.clear();
                staged_mode_change_ = false;
                return false;
            }
            default:
                break;
        }
    }
    active_modal_ = ModalType::None;
    input_.clear();
    return false;
}

void TuiModal::render_overlay(std::vector<std::string>& lines, int term_width, int term_height) const {
    if (active_modal_ == ModalType::None || lines.empty()) return;

    bool is_help = (active_modal_ == ModalType::Help);
    int box_w = is_help ? std::min(76, term_width - 4) : std::min(58, term_width - 4);
    if (box_w < 35) return;

    std::string m_bg = kThemeModalBg;
    std::string m_rst = std::string("\033[0m") + m_bg;

    std::vector<std::string> content_rows;
    auto add_row = [&](const std::string& formatted_str) {
        std::string s = formatted_str;
        std::string target = "\033[0m";
        size_t pos = 0;
        while ((pos = s.find(target, pos)) != std::string::npos) {
            s.replace(pos, target.length(), m_rst);
            pos += m_rst.length();
        }
        content_rows.push_back(s);
    };

    std::string title;
    switch (active_modal_) {
        case ModalType::SetBreakpoint:
            title = " SET BREAKPOINT ";
            add_row(std::format("{}Enter PC Address (hex) or Symbol:\033[0m", kThemeText));
            add_row(std::format("  \033[1m>\033[0m {}{}_\033[0m", kThemeMint, input_));
            break;
        case ModalType::SetStepSize:
            title = " SET STEP SIZE (N) ";
            add_row(std::format("{}Enter Step Count N (instructions):\033[0m", kThemeText));
            add_row(std::format("  \033[1m>\033[0m {}{}_\033[0m", kThemeMint, input_));
            break;
        case ModalType::SetSpeed:
            title = " SET SIMULATION FREQUENCY ";
            add_row(std::format("{}Enter Target Frequency (Hz, 0=Max):\033[0m", kThemeText));
            add_row(std::format("  \033[1m>\033[0m {}{}_\033[0m", kThemeMint, input_));
            break;
        case ModalType::InspectAddress:
            title = " INSPECT MEMORY ADDRESS ";
            add_row(std::format("{}Enter Target Address (hex) or Symbol:\033[0m", kThemeText));
            add_row(std::format("  \033[1m>\033[0m {}{}_\033[0m", kThemeMint, input_));
            break;
        case ModalType::LoadBinary:
            title = " LOAD PROGRAM BINARY ";
            add_row(std::format("{}Enter binary image filepath:\033[0m", kThemeText));
            add_row(std::format("  \033[1m>\033[0m {}{}_\033[0m", kThemeMint, input_));
            add_row(std::format("{}e.g. img/hello.bin, linux-images/rv64/fw_payload.bin\033[0m",
                                kThemeMuted));
            add_row(std::format("{}[Tab]\033[0m {} Mode: {}{}{}\033[0m  {}← toggle\033[0m",
                                kThemeSky, kThemeMuted, load_appmode_ ? kThemePeach : kThemeMint,
                                load_appmode_ ? "App (Baremetal)" : "OS (Linux/RTOS)", "",
                                kThemeMuted));
            break;
        case ModalType::LoadDiskImage:
            title = " LOAD DISK IMAGE (Optional) ";
            add_row(std::format("{}Enter disk image filepath:\033[0m", kThemeText));
            add_row(std::format("  \033[1m>\033[0m {}{}_\033[0m", kThemeMint, input_));
            add_row(std::format("{}e.g. linux-images/rv64/root.bin\033[0m", kThemeMuted));
            if (!staged_binary_path_.empty()) {
                add_row(std::format("{}Staged memory image: {}{}\033[0m", kThemeMuted, kThemeSky,
                                    staged_binary_path_));
            }
            add_row(std::format(
                "{}[Esc]\033[0m {} or {}[Enter]\033[0m{} with empty path to skip\033[0m", kThemeSky,
                kThemeMuted, kThemeSky, kThemeMuted));
            break;
        case ModalType::Settings: {
            title = " SIMULATOR SETTINGS ";
            add_row(std::format("{}Use \033[1m[↑/↓]\033[0m + \033[1m[Enter/Space]\033[0m or key \033[1m[1-9,0,a,g]\033[0m to toggle:\033[0m", kThemeMuted));
            add_row("");

            struct SettingItem {
                const char* key;
                const char* name;
                std::string val;
            };

            std::array<SettingItem, 12> settings = {{
                {" 1", "Simulation Mode",           machine_.s_cycle_accurate ? "\033[1;36m[CA (Cycle-Accurate)]\033[0m" : "\033[1;33m[IA (Instruction-Accurate)]\033[0m"},
                {" 2", "TUI Diagnostics View",      machine_.s_debug_mode ? "\033[1;32m[Debug Mode (Diagnostics ON)]\033[0m" : "\033[90m[Normal Mode]\033[0m"},
                {" 3", "Target Environment",        machine_.s_appmode ? "\033[1;35m[App (Baremetal)]\033[0m" : "\033[1;32m[OS (Linux/RTOS)]\033[0m"},
                {" 4", "Step Rollback History",     machine_.s_rollback_enabled ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
                {" 5", "High Contrast Theme",       machine_.s_high_contrast ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
                {" 6", "Instruction Mix Stats",     machine_.s_use_mix ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
                {" 7", "Branch Prediction Trace",   machine_.s_bp_trace ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
                {" 8", "Exception & Trap Log",      machine_.s_traplog_mode ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
                {" 9", "Device MMIO Access Log",    machine_.s_dlog_mode ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
                {"10", "High-Performance Engine",   machine_.s_high_performance ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
                {" a", "Co-Sim Spike Lockstep",     machine_.s_lockstep_mode ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
                {" g", "GDB Server Stub (1234)",    machine_.s_gdb_mode ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
            }};

            for (std::size_t i = 0; i < settings.size(); ++i) {
                bool is_sel = (static_cast<int>(i) == settings_cursor_);
                std::string prefix = is_sel ? std::format("{}>\033[0m ", kThemeMint) : "  ";
                std::string num_key = std::format("{}[{}]\033[0m", is_sel ? kThemeMint : kThemeSky, settings[i].key);
                std::string name_str = std::format("{}{:<27}\033[0m", is_sel ? "\033[1;37m" : kThemeText, settings[i].name);
                add_row(std::format("{}{} {} : {}", prefix, num_key, name_str, settings[i].val));
            }
            add_row("");
            add_row(std::format("{}Press \033[1m[Esc]\033[0m or \033[1m[q]\033[0m to close\033[0m", kThemeMuted));
            break;
        }
        case ModalType::Help:
            title = " SIMULATOR KEYBOARD SHORTCUTS ";
            if (term_height < 32 && box_w >= 70) {
                // Dual-column layout for small screen height
                struct Shortcut { const char* key; const char* desc; };
                static constexpr std::array<Shortcut, 27> help_items = {{
                    {"[s] / [Space]", "Step 1 inst"},
                    {"[n]",          "Step N insts"},
                    {"[b] / [Alt-b]", "Undo / Toggle Rollback"},
                    {"[o] / [Alt-o]", "Load Binary / Disk"},
                    {"[,] / [Alt-s]", "Simulator Settings"},
                    {"[Alt-a]",      "Toggle IA / CA Mode"},
                    {"[Alt-d]",      "Toggle Debug Mode"},
                    {"[:]",          "Set PC Breakpoint"},
                    {"[k]",          "Toggle PC Breakpoint"},
                    {"[g]",          "Set N Step Size"},
                    {"[f]",          "Set Frequency (Hz)"},
                    {"[m]",          "Inspect Memory"},
                    {"[c] / [Ctrl-P]", "Run / Pause"},
                    {"[Tab]",        "Cycle Layout"},
                    {"[r] / [Alt-r]", "Cycle Registers"},
                    {"[l] / [Alt-l]", "Cycle Tool Tabs"},
                    {"[p] / [Alt-p]", "Cycle Right Pane"},
                    {"[e] / [Alt-e]", "Jump Explainer"},
                    {"[v] / [Alt-v]", "Toggle Trace"},
                    {"[u/d] / [PgUp/Dn]", "Scroll Logs"},
                    {"[w/s] / [Alt-w/s]", "Scroll Regs"},
                    {"[ [ / ] ]",     "Resize Pane"},
                    {"[F1] / [h] / [?]", "Show Help"},
                    {"[Alt-h]",      "High Contrast"},
                    {"[Alt-t]",      "Sakura Pastel"},
                    {"[Esc]",        "Close Modal"},
                    {"[Ctrl-Q]",     "Quit Simulator"}
                }};
                std::size_t half = (help_items.size() + 1) / 2;
                for (std::size_t i = 0; i < half; ++i) {
                    std::string left_item = std::format("{}{:<17}\033[0m {}{:<18}\033[0m", kThemeSky, help_items[i].key, kThemeText, help_items[i].desc);
                    std::string right_item;
                    if (i + half < help_items.size()) {
                        const auto& r = help_items[i + half];
                        right_item = std::format("{}{:<17}\033[0m {}{}\033[0m", kThemeSky, r.key, kThemeText, r.desc);
                    }
                    add_row(std::format("{} │ {}", left_item, right_item));
                }
            } else {
                // Full single-column layout for taller screens
                add_row(std::format(" {}{:<22}\033[0m {}Single instruction step\033[0m", kThemeSky, "[s] / [Space]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Step N instructions\033[0m", kThemeSky, "[n]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Undo / Step back 1 instruction\033[0m", kThemeSky, "[b]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Load Program Binary or Disk modal\033[0m", kThemeSky, "[o] / [Alt-o]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Set PC Breakpoint modal\033[0m", kThemeSky, "[:]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Toggle PC breakpoint at current PC\033[0m", kThemeSky, "[k]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Set Step Size (N) modal\033[0m", kThemeSky, "[g]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Set Speed Frequency (Hz) modal\033[0m", kThemeSky, "[f]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Inspect Memory Address modal\033[0m", kThemeSky, "[m]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Run / Pause simulation loop\033[0m", kThemeSky, "[c] / [Ctrl-P]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Cycle TUI panel layout\033[0m", kThemeSky, "[Tab]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Cycle register sub-views (GPR/FPR/VEC)\033[0m", kThemeSky, "[r] / [Alt-r]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Cycle tool tabs (Pipe/Cache/Trace/Exp/Stack)\033[0m", kThemeSky, "[l] / [Alt-l]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Cycle Right Pane mode\033[0m", kThemeSky, "[p] / [Alt-p]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Jump to Explainer / Trap Details\033[0m", kThemeSky, "[e] / [Alt-e]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Toggle trace recording\033[0m", kThemeSky, "[v] / [Alt-v]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Scroll trace / log views\033[0m", kThemeSky, "[u/d] / [Up/Dn/PgUp/Dn]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Scroll registers view\033[0m", kThemeSky, "[w/s] / [Alt-w/s]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Adjust left pane width\033[0m", kThemeSky, "[ [ / ] ] / [Left/Right]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Show this help dialog\033[0m", kThemeSky, "[F1] / [h] / [?]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Toggle High Contrast theme\033[0m", kThemeSky, "[Alt-h]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Toggle Sakura Pastel theme\033[0m", kThemeSky, "[Alt-t]", kThemeText));
                add_row(std::format(" {}{:<22}\033[0m {}Close modal dialog\033[0m", kThemeSky, "[Esc]", kThemeText));
            }
            break;
        default:
            break;
    }

    std::vector<std::string> box_lines;
    int inner_w = box_w - 2;

    auto pad_center = [&](const std::string& text, int width) -> std::string {
        int text_len = get_display_width(text);
        if (text_len >= width) return format_to_width(text, width);
        int pad_l = (width - text_len) / 2;
        int pad_r = width - text_len - pad_l;
        return std::string(static_cast<size_t>(pad_l), ' ') + text + std::string(static_cast<size_t>(pad_r), ' ');
    };

    auto pad_left = [&](const std::string& text, int width) -> std::string {
        int text_len = get_display_width(text);
        if (text_len >= width) return format_to_width(text, width);
        return text + std::string(static_cast<size_t>(width - text_len), ' ');
    };

    std::string top_border = std::format("{}{}╔{}{}{}╗\033[0m", m_bg, kThemeBorder, pad_center(std::format("═ \033[1m{}{}{}{} ═", kThemeSky, title, m_rst, kThemeBorder), inner_w), m_bg, kThemeBorder);
    box_lines.push_back(top_border);
    box_lines.push_back(std::format("{}{}║{}{}{}║\033[0m", m_bg, kThemeBorder, pad_center("", inner_w), m_bg, kThemeBorder));

    for (const auto& row : content_rows) {
        box_lines.push_back(std::format("{}{}║{}{}{}║\033[0m", m_bg, kThemeBorder, pad_left(" " + row, inner_w), m_bg, kThemeBorder));
    }

    box_lines.push_back(std::format("{}{}║{}{}{}║\033[0m", m_bg, kThemeBorder, pad_center("", inner_w), m_bg, kThemeBorder));
    if (!is_help) {
        box_lines.push_back(std::format("{}{}║{}{}{}║\033[0m", m_bg, kThemeBorder, pad_center(std::format("{}[Enter]\033[0m{} {}Submit  |  {}[Esc]\033[0m{} {}Cancel", kThemeVal, m_rst, kThemeMuted, kThemeVal, m_rst, kThemeMuted), inner_w), m_bg, kThemeBorder));
    } else {
        box_lines.push_back(std::format("{}{}║{}{}{}║\033[0m", m_bg, kThemeBorder, pad_center(std::format("{}Press {}[Esc]\033[0m{}, {}[Enter]\033[0m{}, {}[h]\033[0m{} or {}[F1]\033[0m{} to close", kThemeMuted, kThemeVal, m_rst, kThemeVal, m_rst, kThemeVal, m_rst, kThemeVal, m_rst), inner_w), m_bg, kThemeBorder));
    }
    box_lines.push_back(std::format("{}{}╚{}╝\033[0m", m_bg, kThemeBorder, make_repeated_string("═", inner_w)));

    int box_h = static_cast<int>(box_lines.size());
    int start_y = (static_cast<int>(lines.size()) - box_h) / 2;
    int start_x = (term_width - box_w) / 2;
    if (start_y < 0) start_y = 0;
    if (start_x < 0) start_x = 0;

    for (size_t i = 0; i < box_lines.size(); ++i) {
        int r = start_y + static_cast<int>(i);
        if (r >= 0 && static_cast<size_t>(r) < lines.size()) {
            lines[r] = overlay_string(lines[r], box_lines[i], start_x, box_w);
        }
    }
}

}  // namespace simrv::tui
