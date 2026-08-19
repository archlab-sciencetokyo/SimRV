/**
 * @file LoadModal.cpp
 * @brief Implementation of binary image and disk image loading modal dialogs.
 */
#include "simrv/tui/modals/LoadModal.hpp"

#include <filesystem>
#include <format>

#include "simrv/core/Machine.hpp"
#include "simrv/tui/TuiTheme.hpp"

namespace simrv::tui::modals {

void LoadModal::open(ModalType type, std::string& input, bool& load_appmode,
                     const simrv::core::Machine& machine) {
    input.clear();
    if (type == ModalType::LoadBinary) {
        input = machine.s_fn_memimg;
        load_appmode = machine.s_appmode;
    } else if (type == ModalType::LoadDiskImage) {
        input = machine.s_fn_dskimg;
    }
}

auto LoadModal::submit(ModalType type, const std::string& input, bool load_appmode,
                       simrv::core::Machine& machine, std::string& staged_binary_path,
                       bool& staged_mode_change, bool& staged_target_appmode,
                       const std::function<void(const std::string&)>& set_status_override_cb)
    -> bool {
    if (type == ModalType::LoadBinary) {
        if (!input.empty() && !std::filesystem::exists(input)) {
            if (set_status_override_cb) {
                set_status_override_cb(std::format("Binary file not found: {}", input));
            }
            return false;
        }
        staged_binary_path = input;
        staged_mode_change = true;
        staged_target_appmode = load_appmode;
        if (load_appmode) {
            machine.set_pending_reboot(input, true, "");
            staged_binary_path.clear();
            staged_mode_change = false;
            if (set_status_override_cb) {
                set_status_override_cb("Loaded binary image. Resetting system...");
            }
            machine.request_reboot();
        }
        return true;
    } else if (type == ModalType::LoadDiskImage) {
        if (!input.empty() && !std::filesystem::exists(input)) {
            if (set_status_override_cb) {
                set_status_override_cb(std::format("Disk image file not found: {}", input));
            }
            return false;
        }
        std::string bin_to_load = staged_binary_path;
        bool target_appmode = staged_target_appmode;

        staged_binary_path.clear();
        staged_mode_change = false;

        machine.set_pending_reboot(bin_to_load, target_appmode, input);

        if (set_status_override_cb) {
            set_status_override_cb("Loaded binary and disk image. Resetting system...");
        }
        machine.request_reboot();
        return true;
    }
    return false;
}

void LoadModal::render(ModalType type, std::vector<std::string>& content_rows,
                       const std::string& input, bool load_appmode,
                       const std::string& staged_binary_path) {
    if (type == ModalType::LoadBinary) {
        content_rows.push_back(std::format("{}Enter binary image filepath:\033[0m", kThemeText));
        content_rows.push_back(std::format("  \033[1m>\033[0m {}{}_\033[0m", kThemeMint, input));
        content_rows.push_back(std::format(
            "{}e.g. img/hello.bin, linux-images/rv64/fw_payload.bin\033[0m", kThemeMuted));
        content_rows.push_back(
            std::format("{}[Tab]\033[0m {} Mode: {}{}{}\033[0m  {}← toggle\033[0m", kThemeSky,
                        kThemeMuted, load_appmode ? kThemePeach : kThemeMint,
                        load_appmode ? "App (Baremetal)" : "OS (Linux/RTOS)", "", kThemeMuted));
    } else if (type == ModalType::LoadDiskImage) {
        content_rows.push_back(std::format("{}Enter disk image filepath:\033[0m", kThemeText));
        content_rows.push_back(std::format("  \033[1m>\033[0m {}{}_\033[0m", kThemeMint, input));
        content_rows.push_back(std::format(
            "{}e.g. linux-images/rv64/root.img, root.ext4, root.bin\033[0m", kThemeMuted));
        if (!staged_binary_path.empty()) {
            content_rows.push_back(std::format("{}Staged memory image: {}{}\033[0m", kThemeMuted,
                                               kThemeSky, staged_binary_path));
        }
        content_rows.push_back(
            std::format("{}[Esc]\033[0m {} or {}[Enter]\033[0m{} with empty path to skip\033[0m",
                        kThemeSky, kThemeMuted, kThemeSky, kThemeMuted));
    }
}

}  // namespace simrv::tui::modals
