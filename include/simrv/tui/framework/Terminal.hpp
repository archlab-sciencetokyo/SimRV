/**
 * @file Terminal.hpp
 * @brief Framework-facing terminal and buffering interfaces.
 */
#pragma once

#include "simrv/tui/LogBuffer.hpp"
#include "simrv/tui/TuiWidget.hpp"
#include "simrv/tui/VirtualTerminal.hpp"

namespace simrv::tui::framework {

// Compatibility aliases keep existing SimRV consumers stable while new generic code uses the
// framework namespace. These types have no simulator-domain dependencies.
using ::simrv::tui::LogBuffer;
using ::simrv::tui::TuiWidget;
using ::simrv::tui::VirtualTerminal;

}  // namespace simrv::tui::framework
