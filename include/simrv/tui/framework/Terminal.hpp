/**
 * @file Terminal.hpp
 * @brief Framework-facing terminal and buffering interfaces.
 */
#pragma once

#include "simrv/tui/LogBuffer.hpp"
#include "simrv/tui/TuiWidget.hpp"
#include "simrv/tui/VirtualTerminal.hpp"

namespace simrv::tui::framework {

// Source-compatibility re-exports for consumers that used the old framework-facing names.
// New framework code should import these types from simrv::tui::framework directly.
using ::simrv::tui::LogBuffer;
using ::simrv::tui::TuiWidget;
using ::simrv::tui::VirtualTerminal;

}  // namespace simrv::tui::framework
