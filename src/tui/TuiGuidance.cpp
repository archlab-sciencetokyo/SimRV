/**
 * @file TuiGuidance.cpp
 * @brief Page-specific educational guidance definitions.
 */
#include "simrv/tui/TuiGuidance.hpp"

#include <format>

#include "simrv/pipeline/Decoder.hpp"
#include "simrv/pipeline/OperationTraits.hpp"

namespace simrv::tui {

auto guidance_for_context(const GuidanceContext& context) -> PageGuidance {
    if (!context.image_loaded) {
        return {"Start exploring",
                "No program is loaded, so there is no architectural state to explain yet.",
                "An ELF preserves symbols that teachers can use as named pause points.",
                KeyAction::LoadBinary,
                "Open the program loader.",
                0};
    }
    if (context.shutdown) {
        return {"Program completed",
                "The guest has stopped and its final architectural state remains visible.",
                "Compare the final registers and trace before restarting the same experiment.",
                KeyAction::Reset,
                "Reboot the program from its entry point.",
                0};
    }

    PageGuidance guidance;
    switch (context.page) {
        case TuiRegPage::GPR:
            guidance = {"Integer registers",
                        "GPRs hold addresses, operands, and integer results.",
                        "ABI names connect x-registers to calls, arguments, and stack frames.",
                        KeyAction::ToggleExplain,
                        "Explain the current instruction.",
                        0};
            break;
        case TuiRegPage::FPR:
            guidance = {"Floating-point registers",
                        "FPRs hold IEEE-754 operands and results.",
                        "fflags and frm explain rounding and floating-point exceptions.",
                        KeyAction::CycleRegPage,
                        "Compare the integer or vector register view.",
                        0};
            break;
        case TuiRegPage::VEC:
            guidance = {"Vector registers",
                        "Vector lanes use the active vl, vtype, and VLEN.",
                        "Mask and tail policy determines which inactive lanes remain meaningful.",
                        KeyAction::ToggleExplain,
                        "Explain the current vector instruction.",
                        0};
            break;
        case TuiRegPage::PIPELINE:
            guidance = {context.cycle_accurate ? "Pipeline flow" : "Instruction flow",
                        context.cycle_accurate
                            ? "Stages show in-flight instructions, stalls, and bubbles."
                            : "Functional mode shows the current decoded instruction.",
                        context.cycle_accurate
                            ? "Stalls preserve dependencies; flushes discard wrong-path work."
                            : "Select cycle-accurate mode to observe timing and hazards.",
                        KeyAction::ToggleExplain,
                        "Inspect the selected instruction's semantics.",
                        1};
            break;
        case TuiRegPage::CACHE:
            guidance = {"Cache hierarchy",
                        "Sets and ways show recently accessed cache lines.",
                        "A miss adds memory latency and may stall the pipeline.",
                        KeyAction::CycleToolPage,
                        "Compare cache state with hazards and bus traffic.",
                        2};
            break;
        case TuiRegPage::TLB:
            guidance = {"Address translation",
                        "TLB entries cache virtual-to-physical translations.",
                        "Privilege and page permissions decide whether an access can complete.",
                        KeyAction::InspectAddress,
                        "Inspect an address from the current context.",
                        3};
            break;
        case TuiRegPage::BPRED:
            guidance = {"Branch prediction",
                        "The predictor guesses control flow before resolution.",
                        "A wrong guess flushes younger work and adds control bubbles.",
                        KeyAction::CycleToolPage,
                        "Compare prediction accuracy with pipeline flow.",
                        4};
            break;
        case TuiRegPage::HAZARD:
            guidance = {"Pipeline hazards",
                        "RAW dependencies and structural conflicts can stall.",
                        "Forwarding helps only when a producer's result is available in time.",
                        KeyAction::ConfigureSystem,
                        "Review forwarding and latency configuration.",
                        1};
            break;
        case TuiRegPage::BUS:
            guidance = {"Memory and MMIO",
                        "The bus routes DRAM and device requests by address.",
                        "Device and IRQ state connect software requests to peripherals.",
                        KeyAction::InspectAddress,
                        "Inspect a mapped address or descriptor.",
                        5};
            break;
        case TuiRegPage::TRACE:
            guidance = {"Execution trace",
                        "Trace records committed instruction history.",
                        "It connects control flow to visible architectural state changes.",
                        KeyAction::ToggleTrace,
                        "Enable tracing, then step or run briefly.",
                        0};
            break;
        case TuiRegPage::EXPLAIN:
            guidance = {"Instruction semantics",
                        "Fields determine operands, operation, and encoding.",
                        "Decoded dataflow predicts the next architectural state.",
                        KeyAction::Step,
                        "Step once and compare the resulting state.",
                        0};
            break;
        case TuiRegPage::STACK:
            guidance = {"Stack and memory",
                        "Stack slots hold frames, saved registers, and locals.",
                        "sp and frame pointers connect ABI conventions to memory.",
                        KeyAction::InspectAddress,
                        "Open a selected stack or register address.",
                        0};
            break;
        case TuiRegPage::DISASM:
            guidance = {"Disassembly",
                        "Decoded instructions surround the current program counter.",
                        "PC and breakpoint markers show where execution is paused.",
                        KeyAction::Step,
                        "Step one instruction and follow the PC.",
                        0};
            break;
    }

    using namespace simrv::pipeline::operation;
    if (context.instruction_valid) {
        const auto name = simrv::pipeline::operation_name(context.operation);
        if (context.page == TuiRegPage::GPR && writes_integer(context.operation)) {
            guidance.meaning = std::format("{} targets x{}; changed values are highlighted.", name,
                                           context.destination);
        }
        if (is_memory(context.operation)) {
            guidance.relationship =
                std::format("{} computes an address before the memory access.", name);
            guidance.next_action = KeyAction::ToggleExplain;
            guidance.next_hint = "Inspect its effective address and operands.";
        } else if (is_control(context.operation)) {
            guidance.relationship =
                std::format("{} can redirect the PC and discard sequential work.", name);
            guidance.next_action = KeyAction::ToggleTrace;
            guidance.next_hint = "Trace the taken path, then compare the next PC.";
        }
    }
    if (context.page == TuiRegPage::CACHE && context.cache_misses > 0) {
        guidance.meaning =
            std::format("The selected hart has recorded {} cache misses.", context.cache_misses);
    }
    if (context.page == TuiRegPage::HAZARD &&
        (context.data_hazard_stalls + context.control_hazard_bubbles) > 0) {
        guidance.meaning = std::format("Telemetry records {} data stalls and {} control bubbles.",
                                       context.data_hazard_stalls, context.control_hazard_bubbles);
    }
    return guidance;
}

auto guidance_for_page(TuiRegPage page, bool cycle_accurate) -> PageGuidance {
    return guidance_for_context({.page = page, .cycle_accurate = cycle_accurate});
}

}  // namespace simrv::tui
