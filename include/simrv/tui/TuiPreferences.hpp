/** @file TuiPreferences.hpp */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/TuiTypes.hpp"

namespace simrv::tui {

/** Per-user presentation state. It deliberately excludes machine and guest configuration. */
struct TuiPreferences {
    TuiLayout layout = TuiLayout::Split;
    std::vector<WorkbenchSlot> slots{{TuiRegPage::GPR, 0}, {TuiRegPage::CONSOLE, 0}};
    size_t focused_slot = 0;
    int inspector_width = -1;
    framework::ColumnWidthOverrides column_widths = framework::kNoColumnWidthOverrides;
    TuiThemeStyle theme = TuiThemeStyle::ModernUnicode;
    bool high_contrast = false;
    bool class_mode = false;
    bool student_guide = false;
    uint32_t target_fps = 60;
    double mouse_sensitivity = 1.0;

    [[nodiscard]] static auto default_path() -> std::filesystem::path;
    [[nodiscard]] auto load(const std::filesystem::path& path) -> bool;
    [[nodiscard]] auto save(const std::filesystem::path& path) const -> bool;
};

}  // namespace simrv::tui
