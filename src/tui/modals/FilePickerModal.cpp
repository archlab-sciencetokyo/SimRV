/**
 * @file FilePickerModal.cpp
 * @brief Implementation of file picker modal dialog.
 */
#include "simrv/tui/modals/FilePickerModal.hpp"

#include <algorithm>
#include <format>
#include <string>
#include <vector>

#include "simrv/tui/TuiModal.hpp"
#include "simrv/tui/modals/ModalComponents.hpp"

namespace simrv::tui::modals {

void FilePickerModal::render_content(std::vector<std::string>& content_rows, int /*width*/,
                                     int /*height*/) {
    if (!host_) return;
    content_rows.push_back(std::format("Directory: {}", host_->file_picker_directory_.string()));
    content_rows.push_back(
        std::format("Filter: {}_  \033[2m(Type to filter; arrows navigate, Enter opens, Esc returns)\033[0m",
                    host_->file_picker_filter_));
    content_rows.push_back("");
    if (host_->file_picker_entries_.empty()) {
        content_rows.push_back("No matching files or directories");
    } else {
        constexpr int kMaxEntries = 14;
        const int count = static_cast<int>(host_->file_picker_entries_.size());
        int first = std::max(0, host_->file_picker_cursor_ - kMaxEntries / 2);
        first = std::min(first, std::max(0, count - kMaxEntries));
        const int last = std::min(count, first + kMaxEntries);
        const auto parent_entry = (host_->file_picker_directory_ / "..").lexically_normal();
        for (int i = first; i < last; ++i) {
            const auto& entry = host_->file_picker_entries_[static_cast<size_t>(i)];
            if (entry == parent_entry) {
                content_rows.push_back(
                    std::format("{} ..", i == host_->file_picker_cursor_ ? ">" : " "));
                continue;
            }
            if (host_->file_picker_select_directory_ && entry == std::filesystem::path(".")) {
                content_rows.push_back(std::format("{} [Use this directory]",
                                                   i == host_->file_picker_cursor_ ? ">" : " "));
                continue;
            }
            std::error_code error;
            const bool directory = std::filesystem::is_directory(entry, error) && !error;
            content_rows.push_back(std::format("{} {}{}",
                                               i == host_->file_picker_cursor_ ? ">" : " ",
                                               entry.filename().string(), directory ? "/" : ""));
        }
    }
    content_rows.push_back("");
    content_rows.push_back(
        build_modal_footer({{.key = "[Enter]", .action = "Select / Open Directory"},
                            {.key = "[Type]", .action = "Filter"},
                            {.key = "[Esc]", .action = "Back"}}));
}

auto FilePickerModal::handle_key(uint8_t byte, TuiKey key) -> framework::ModalAction {
    if (!host_) return framework::ModalAction::Continue;
    host_->file_picker_input(byte, key);
    return framework::ModalAction::Continue;
}

void FilePickerModal::handle_wheel(int delta) {
    if (host_) host_->move_file_picker_cursor(delta);
}

auto FilePickerModal::handle_horizontal(int delta) -> bool {
    if (!host_ || delta == 0) return false;
    if (delta < 0) {
        auto& directory = host_->file_picker_directory_;
        const auto parent = directory.parent_path();
        if (!parent.empty() && parent != directory) {
            directory = parent.lexically_normal();
            host_->file_picker_filter_.clear();
            host_->file_picker_cursor_ = 0;
            host_->refresh_file_picker();
        }
        return true;
    }

    const auto& entries = host_->file_picker_entries_;
    if (entries.empty()) return true;
    const auto& selected = entries[static_cast<size_t>(host_->file_picker_cursor_)];
    if (selected == (host_->file_picker_directory_ / "..").lexically_normal()) {
        handle_horizontal(-1);
        return true;
    }
    std::error_code error;
    if (std::filesystem::is_directory(selected, error) && !error) {
        host_->file_picker_directory_ = selected.lexically_normal();
        host_->file_picker_filter_.clear();
        host_->file_picker_cursor_ = 0;
        host_->refresh_file_picker();
    }
    return true;
}

auto FilePickerModal::handle_control_click(int /*content_row*/, size_t /*action_index*/)
    -> framework::ModalAction {
    return framework::ModalAction::Continue;
}

auto FilePickerModal::handle_click(framework::ScreenCoord local_pt) -> framework::ModalAction {
    if (!host_) return framework::ModalAction::Continue;
    int content_row = local_pt.y - 1;
    if (content_row >= 3 && !host_->file_picker_entries_.empty()) {
        constexpr int kMaxEntries = 14;
        const int count = static_cast<int>(host_->file_picker_entries_.size());
        int first = std::max(0, host_->file_picker_cursor_ - kMaxEntries / 2);
        first = std::min(first, std::max(0, count - kMaxEntries));
        int target = first + (content_row - 3);
        if (target >= 0 && target < count) {
            host_->file_picker_cursor_ = target;
            host_->accept_file_picker_entry();
        }
    }
    return framework::ModalAction::Continue;
}

}  // namespace simrv::tui::modals
