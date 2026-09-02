/**
 * @file TuiGuidance.hpp
 * @brief Contextual educational guidance for TUI inspection pages.
 */
#pragma once

#include <cstdint>
#include <string>

#include "simrv/isa/OperationId.hpp"
#include "simrv/tui/TuiKeybindings.hpp"
#include "simrv/tui/TuiTypes.hpp"

namespace simrv::tui {

struct PageGuidance {
    std::string title;
    std::string meaning;
    std::string relationship;
    KeyAction next_action;
    std::string next_hint;
    int glossary_topic = 0;
};

struct GuidanceContext {
    TuiRegPage page = TuiRegPage::GPR;
    bool cycle_accurate = false;
    bool instruction_valid = false;
    isa::OperationId operation = isa::OperationId::UNKNOWN;
    uint8_t destination = 0;
    uint64_t cache_misses = 0;
    uint64_t data_hazard_stalls = 0;
    uint64_t control_hazard_bubbles = 0;
    bool image_loaded = true;
    bool shutdown = false;
};

[[nodiscard]] auto guidance_for_page(TuiRegPage page, bool cycle_accurate) -> PageGuidance;
[[nodiscard]] auto guidance_for_context(const GuidanceContext& context) -> PageGuidance;

/// Educational guidance is opt-in and only displaces pane content when sufficient room exists.
[[nodiscard]] constexpr auto should_show_guidance(bool paused, bool guide_enabled, int visible_rows)
    -> bool {
    return paused && guide_enabled && visible_rows >= 16;
}

}  // namespace simrv::tui
