/**
 * @file Types.hpp
 * @brief Domain-independent types shared by the SimRV TUI framework.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace simrv::tui::framework {

enum class HorizontalAlign : std::uint8_t { Left, Center, Right };

enum class Role : std::uint8_t {
    Surface,
    Border,
    Text,
    Muted,
    Value,
    Accent,
    Success,
    Warning,
    Danger,
};

enum class RowKind : std::uint8_t { Content, Controls, Divider, ScrollIndicator };

struct Row {
    std::string text;
    HorizontalAlign alignment = HorizontalAlign::Left;
    Role role = Role::Text;
    RowKind kind = RowKind::Content;
};

struct ControlSpan {
    int start = 0;
    int width = 0;
    std::string id;

    [[nodiscard]] constexpr auto contains(int column) const -> bool {
        return column >= start && column < start + width;
    }
};

struct RenderedControl {
    std::string text;
    int width = 0;
    std::vector<ControlSpan> spans;
};

}  // namespace simrv::tui::framework
