/** @file TuiPreferences.hpp */
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/TuiTypes.hpp"
#include "simrv/tui/framework/Types.hpp"

namespace simrv::tui {

/** Per-user presentation state. It deliberately excludes machine and guest configuration. */
struct TuiPreferences {
    TuiLayout layout = TuiLayout::Split;
    std::vector<WorkbenchSlot> slots{{.page = TuiRegPage::GPR, .scroll_offset = 0},
                                     {.page = TuiRegPage::CONSOLE, .scroll_offset = 0}};
    size_t focused_slot = 0;
    int inspector_width = -1;
    framework::ColumnWidthOverrides column_widths = framework::kNoColumnWidthOverrides;
    TuiThemeStyle theme = TuiThemeStyle::ModernUnicode;
    bool high_contrast = false;
    framework::DensityMode density_mode = framework::DensityMode::Auto;

    [[nodiscard]] static auto default_path() -> std::filesystem::path;
    [[nodiscard]] auto load(const std::filesystem::path& path) -> bool;
    [[nodiscard]] auto save(const std::filesystem::path& path) const -> bool;
};

}  // namespace simrv::tui
