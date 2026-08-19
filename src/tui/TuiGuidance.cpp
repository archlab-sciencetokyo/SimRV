/**
 * @file TuiGuidance.cpp
 * @brief Page-specific educational guidance definitions.
 */
#include "simrv/tui/TuiGuidance.hpp"

namespace simrv::tui {

auto guidance_for_page(TuiRegPage page, bool cycle_accurate) -> PageGuidance {
    switch (page) {
        case TuiRegPage::GPR:
            return {"Integer registers", "GPRs hold addresses, operands, and integer results.",
                    "Highlighted values changed at the last architectural commit.",
                    KeyAction::ToggleExplain, "Open the current instruction explanation."};
        case TuiRegPage::FPR:
            return {"Floating-point registers", "FPRs hold IEEE-754 operands and results.",
                    "Read them with fflags and frm to understand rounding and exceptions.",
                    KeyAction::CycleRegPage, "Compare the integer or vector register view."};
        case TuiRegPage::VEC:
            return {"Vector registers", "Vector lanes are interpreted using vl, vtype, and VLEN.",
                    "Inactive tail and masked lanes may follow the current agnostic policy.",
                    KeyAction::ToggleExplain, "Explain the current vector instruction."};
        case TuiRegPage::PIPELINE:
            return {cycle_accurate ? "Pipeline flow" : "Instruction flow",
                    cycle_accurate ? "Stages show in-flight instructions and bubbles."
                                   : "Functional mode shows the current decoded instruction.",
                    "Stalls preserve dependencies; flushes discard wrong-path work.",
                    KeyAction::ToggleExplain, "Inspect the selected instruction's semantics."};
        case TuiRegPage::CACHE:
            return {"Cache hierarchy", "Sets and ways show recently accessed cache lines.",
                    "Miss penalties feed pipeline stalls and change CPI.", KeyAction::CycleToolPage,
                    "Compare cache behavior with hazards and bus traffic."};
        case TuiRegPage::TLB:
            return {"Address translation", "TLB entries cache virtual-to-physical translations.",
                    "Permissions and privilege determine whether an access can complete.",
                    KeyAction::InspectAddress, "Inspect an address from the current context."};
        case TuiRegPage::BPRED:
            return {"Branch prediction", "Predictors guess control flow before resolution.",
                    "A misprediction flushes younger work and adds the configured penalty.",
                    KeyAction::CycleToolPage, "Compare prediction accuracy with pipeline flow."};
        case TuiRegPage::HAZARD:
            return {"Pipeline hazards", "RAW dependencies and structural conflicts can stall.",
                    "Forwarding removes stalls only when a result is available in time.",
                    KeyAction::ConfigureSystem, "Review forwarding and latency configuration."};
        case TuiRegPage::BUS:
            return {"Memory and MMIO", "The bus routes DRAM and device transactions by address.",
                    "Device status and IRQ state connect software requests to peripherals.",
                    KeyAction::InspectAddress, "Inspect a mapped address or descriptor."};
        case TuiRegPage::TRACE:
            return {"Execution trace", "Trace records committed instruction history.",
                    "Use it to relate control flow to architectural state changes.",
                    KeyAction::ToggleTrace, "Enable tracing, then step or run briefly."};
        case TuiRegPage::EXPLAIN:
            return {"Instruction semantics", "Fields determine operands, operation, and encoding.",
                    "The computation links decoded fields to the next architectural state.",
                    KeyAction::Step, "Step once and compare the resulting state."};
        case TuiRegPage::STACK:
            return {"Stack and memory", "Stack slots hold frames, saved registers, and local data.",
                    "SP and frame pointers connect ABI conventions to memory addresses.",
                    KeyAction::InspectAddress, "Open a selected stack or register address."};
    }
    return {"Inspection", "Inspect the current machine state.",
            "Relate visible values to the instruction being executed.", KeyAction::Help,
            "Open help for available actions."};
}

}  // namespace simrv::tui
