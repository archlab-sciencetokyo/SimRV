/**
 * @file TuiDrawer.hpp
 * @brief Domain bottom-drawer specifications for SimRV TUI inspector panels.
 */
#pragma once

#include <cstdint>

#include "simrv/tui/framework/Drawer.hpp"

namespace simrv::tui {

inline constexpr int kInspectorPanelTopRow = 4;

enum class TuiDrawerId : uint8_t {
    Log,
    Mission,
};

using TuiDrawerSpan = framework::DrawerSpan<TuiDrawerId>;
using TuiDrawer = framework::Drawer<TuiDrawerId>;
using TuiDrawerStack = framework::DrawerStack<TuiDrawerId>;

}  // namespace simrv::tui
