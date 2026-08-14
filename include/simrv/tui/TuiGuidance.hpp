/**
 * @file TuiGuidance.hpp
 * @brief Contextual educational guidance for TUI inspection pages.
 */
#pragma once

#include <string_view>

#include "simrv/tui/TuiKeybindings.hpp"
#include "simrv/tui/TuiTypes.hpp"

namespace simrv::tui {

struct PageGuidance {
    std::string_view title;
    std::string_view meaning;
    std::string_view relationship;
    KeyAction next_action;
    std::string_view next_hint;
};

[[nodiscard]] auto guidance_for_page(TuiRegPage page, bool cycle_accurate) -> PageGuidance;

}  // namespace simrv::tui
