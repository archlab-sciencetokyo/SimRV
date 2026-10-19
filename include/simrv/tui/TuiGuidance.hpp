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
#include "simrv/xlen/IdentifierTypes.hpp"

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

    // Architectural execution context
    simrv::xlen::PrivilegeLevel privilege = simrv::xlen::PrivilegeLevel::Machine;
    bool has_pending_trap = false;
    uint64_t trap_cause = 0;
    bool translation_enabled = false;
    bool is_vector_op = false;
    bool is_csr_op = false;
    uint32_t csr_address = 0;
    bool is_cfu_op = false;
    uint32_t cfu_funct7 = 0;
    uint32_t cfu_funct3 = 0;
    bool cfu_has_plugin = false;
    std::string cfu_plugin_path{};
    uint32_t cfu_latency = 1;
    uint32_t cfu_stall_cycles = 0;

    // Platform timing context
    uint64_t cpu_clock_hz = 0;
    uint64_t timebase_hz = 0;
};

[[nodiscard]] auto guidance_for_page(TuiRegPage page, bool cycle_accurate) -> PageGuidance;
[[nodiscard]] auto guidance_for_context(const GuidanceContext& context) -> PageGuidance;

/// Educational guidance is opt-in and only displaces pane content when sufficient room exists.
[[nodiscard]] constexpr auto should_show_guidance(bool paused, bool guide_enabled, int visible_rows,
                                                  int width = 80) -> bool {
    // The guide reserves four fixed rows. Below this width its labelled prompts would be clipped
    // instead of being readable, so preserve the inspector and let the user widen the pane.
    return paused && guide_enabled && visible_rows >= 16 && width >= 56;
}

}  // namespace simrv::tui
