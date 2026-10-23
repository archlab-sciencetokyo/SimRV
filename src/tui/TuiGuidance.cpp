/**
 * @file TuiGuidance.cpp
 * @brief Page-specific educational guidance definitions.
 */
#include "simrv/tui/TuiGuidance.hpp"

#include <format>

#include "simrv/core/TrapController.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "simrv/pipeline/OperationTraits.hpp"
#include "simrv/util/InstructionExplainer.hpp"

namespace simrv::tui {

auto guidance_for_context(const GuidanceContext& context) -> PageGuidance {
    if (!context.image_loaded) {
        return {
            .title = "Start exploring",
            .meaning = "No program is loaded, so there is no architectural state to explain yet.",
            .relationship = "An ELF preserves symbols that teachers can use as named pause points.",
            .next_action = KeyAction::LoadBinary,
            .next_hint = "Open the program loader.",
            .glossary_topic = 0};
    }
    if (context.shutdown) {
        return {
            .title = "Program completed",
            .meaning = "The guest has stopped and its final architectural state remains visible.",
            .relationship =
                "Compare the final registers and trace before restarting the same experiment.",
            .next_action = KeyAction::Reset,
            .next_hint = "Reboot the program from its entry point.",
            .glossary_topic = 0};
    }

    PageGuidance guidance;
    switch (context.page) {
        case TuiRegPage::GPR:
        case TuiRegPage::LIVE:
            guidance = {.title = "Integer registers",
                        .meaning = "GPRs hold addresses, operands, and integer results.",
                        .relationship =
                            "ABI names connect x-registers to calls, arguments, and stack frames.",
                        .next_action = KeyAction::ToggleExplain,
                        .next_hint = "Explain the current instruction.",
                        .glossary_topic = 0};
            break;
        case TuiRegPage::FPR:
            guidance = {
                .title = "Floating-point registers",
                .meaning = "FPRs hold IEEE-754 operands and results.",
                .relationship = "fflags and frm explain rounding and floating-point exceptions.",
                .next_action = KeyAction::CycleRegPage,
                .next_hint = "Compare the integer or vector register view.",
                .glossary_topic = 0};
            break;
        case TuiRegPage::VEC:
            guidance = {
                .title = "Vector registers",
                .meaning = "Vector lanes use the active vl, vtype, and VLEN.",
                .relationship =
                    "Mask and tail policy determines which inactive lanes remain meaningful.",
                .next_action = KeyAction::ToggleExplain,
                .next_hint = "Explain the current vector instruction.",
                .glossary_topic = 0};
            break;
        case TuiRegPage::PIPELINE:
            guidance = {.title = context.cycle_accurate ? "Pipeline flow" : "Instruction flow",
                        .meaning = context.cycle_accurate
                                       ? "Stages show in-flight instructions, stalls, and bubbles."
                                       : "Functional mode shows the current decoded instruction.",
                        .relationship =
                            context.cycle_accurate
                                ? "Stalls preserve dependencies; flushes discard wrong-path work."
                                : "Select cycle-accurate mode to observe timing and hazards.",
                        .next_action = KeyAction::ToggleExplain,
                        .next_hint = "Inspect the selected instruction's semantics.",
                        .glossary_topic = 1};
            break;
        case TuiRegPage::CACHE:
            guidance = {.title = "Cache hierarchy",
                        .meaning = "Sets and ways show recently accessed cache lines.",
                        .relationship = "A miss adds memory latency and may stall the pipeline.",
                        .next_action = KeyAction::CycleToolPage,
                        .next_hint = "Compare cache state with hazards and bus traffic.",
                        .glossary_topic = 2};
            break;
        case TuiRegPage::TLB:
            guidance = {.title = "Address translation",
                        .meaning = "TLB entries cache virtual-to-physical translations.",
                        .relationship =
                            "Privilege and page permissions decide whether an access can complete.",
                        .next_action = KeyAction::InspectAddress,
                        .next_hint = "Inspect an address from the current context.",
                        .glossary_topic = 3};
            break;
        case TuiRegPage::BPRED:
            guidance = {
                .title = "Branch prediction",
                .meaning = "The predictor guesses control flow before resolution.",
                .relationship = "A wrong guess flushes younger work and adds control bubbles.",
                .next_action = KeyAction::CycleToolPage,
                .next_hint = "Compare prediction accuracy with pipeline flow.",
                .glossary_topic = 4};
            break;
        case TuiRegPage::HAZARD:
            guidance = {
                .title = "Pipeline hazards",
                .meaning = "RAW dependencies and structural conflicts can stall younger stages.",
                .relationship =
                    "Forwarding resolves data hazards in 0 bubbles when producer results align.",
                .next_action = KeyAction::Settings,
                .next_hint = "Review forwarding and latency configuration in Settings.",
                .glossary_topic = 1};
            break;
        case TuiRegPage::BUS:
            guidance = {.title = "Memory and MMIO",
                        .meaning = "The bus routes DRAM and device requests by physical address.",
                        .relationship =
                            "CLINT/ACLINT timebase and IRQ routing connect hardware timers to CPU.",
                        .next_action = KeyAction::InspectAddress,
                        .next_hint = "Inspect a mapped address or descriptor.",
                        .glossary_topic = 5};
            break;
        case TuiRegPage::TRACE:
            guidance = {
                .title = "Execution trace",
                .meaning = "Trace records committed instruction history.",
                .relationship = "It connects control flow to visible architectural state changes.",
                .next_action = KeyAction::CycleToolPage,
                .next_hint = "Focus Trace while stepping or running briefly.",
                .glossary_topic = 0};
            break;
        case TuiRegPage::EXPLAIN:
            guidance = {.title = "Instruction semantics",
                        .meaning = "Fields determine operands, operation, and encoding.",
                        .relationship = "Decoded dataflow predicts the next architectural state.",
                        .next_action = KeyAction::Step,
                        .next_hint = "Step once and compare the resulting state.",
                        .glossary_topic = 0};
            break;
        case TuiRegPage::STACK:
            guidance = {.title = "Stack and memory",
                        .meaning = "Stack slots hold frames, saved registers, and locals.",
                        .relationship = "sp and frame pointers connect ABI conventions to memory.",
                        .next_action = KeyAction::InspectAddress,
                        .next_hint = "Open a selected stack or register address.",
                        .glossary_topic = 0};
            break;
        case TuiRegPage::CONSOLE:
        case TuiRegPage::DISPLAY:
            guidance = {
                .title = "Console",
                .meaning = "Terminal or display output from the simulated system.",
                .relationship = "Direct guest UART terminal and framebuffer display output.",
                .next_action = KeyAction::RunPause,
                .next_hint = "Run or pause guest execution.",
                .glossary_topic = 0};
            break;
        case TuiRegPage::LOG:
            guidance = {
                .title = "Execution log",
                .meaning = "Structured event stream from simulation, devices, and subsystems.",
                .relationship = "Filter by severity or open the full log viewer modal with [L].",
                .next_action = KeyAction::OpenLogModal,
                .next_hint = "Open the interactive full-screen log viewer.",
                .glossary_topic = 0};
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
            guidance.next_action = KeyAction::CycleToolPage;
            guidance.next_hint = "Open Trace, then compare the next PC.";
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
    if (context.page == TuiRegPage::BUS && context.cpu_clock_hz > 0 && context.timebase_hz > 0) {
        const double cpu_mhz = static_cast<double>(context.cpu_clock_hz) / 1'000'000.0;
        const double timebase_mhz = static_cast<double>(context.timebase_hz) / 1'000'000.0;
        const double cycles_per_tick =
            static_cast<double>(context.cpu_clock_hz) / static_cast<double>(context.timebase_hz);
        guidance.meaning = std::format(
            "Bus routes DRAM & MMIO at {:.1f} MHz CPU clock ({:.1f} MHz timer timebase).", cpu_mhz,
            timebase_mhz);
        guidance.relationship = std::format(
            "CLINT/ACLINT mtime increments at 1 tick per {:.1f} CPU cycles ({:.1f} ns/tick).",
            cycles_per_tick, 1000.0 / timebase_mhz);
    }

    if (context.has_pending_trap) {
        auto trap_name =
            simrv::core::trap_cause_name(static_cast<simrv::xlen::TrapCause>(context.trap_cause));
        guidance.title = "Trap / Exception diagnostic";
        guidance.meaning = std::format("A trap ({}) is active or pending on this hart.", trap_name);
        guidance.relationship =
            "Traps redirect execution to mtvec/stvec and update mepc/sepc and mcause/scause.";
        guidance.next_action = KeyAction::Step;
        guidance.next_hint = "Step into the trap handler.";
        guidance.glossary_topic = 5;
    } else if (context.is_csr_op) {
        auto details = simrv::util::get_csr_details(context.csr_address);
        std::string csr_nm =
            details ? std::string(details->name) : simrv::util::csr_name(context.csr_address);
        guidance.title = std::format("CSR Access: {}", csr_nm);
        guidance.meaning =
            details ? std::format("Accessing {} ({}).", csr_nm, details->description)
                    : std::format("Accessing CSR {} at 0x{:03X}.", csr_nm, context.csr_address);
        guidance.relationship =
            "CSR instructions read/modify control state subject to current privilege checks.";
        guidance.next_action = KeyAction::ToggleExplain;
        guidance.next_hint = "Inspect CSR architectural details.";
        guidance.glossary_topic = 5;
    } else if (context.page == TuiRegPage::TLB) {
        if (context.translation_enabled) {
            guidance.meaning =
                "Address translation is active (satp mode > 0). Virtual addresses map via page "
                "tables.";
            guidance.relationship =
                "Page table walks translate VPN to PPN; TLB caches recent translations.";
        } else {
            guidance.meaning =
                "Address translation is disabled (Bare mode). Virtual addresses match physical "
                "addresses.";
            guidance.relationship =
                "Operating systems enable paging (satp) when initializing virtual memory.";
        }
        guidance.glossary_topic = 3;
    } else if (context.is_vector_op) {
        guidance.title = "Vector instruction execution";
        guidance.meaning =
            "Vector operations execute across element lanes configured by vtype (SEW, LMUL) and "
            "vl.";
        guidance.relationship =
            "Vector registers v0-v31 operate under masking and tail/mask policies.";
        guidance.next_action = KeyAction::ToggleExplain;
        guidance.next_hint = "Explain the vector instruction bitfields.";
        guidance.glossary_topic = 0;
    } else if (context.is_cfu_op) {
        guidance.title = std::format("Custom Function Unit (CFU): func{} (0x{:02X})",
                                     context.cfu_funct3, context.cfu_funct7);
        std::string backend_desc = context.cfu_has_plugin ? "external accelerator plugin"
                                                          : "built-in hardware model (rvproc_cfu)";
        guidance.meaning = std::format(
            "Executing custom hardware instruction on {} with {} cycle latency ({} stall(s)).",
            backend_desc, context.cfu_latency, context.cfu_stall_cycles);
        guidance.relationship =
            "CFU instructions dispatch domain-specific coprocessor operations directly from the "
            "pipeline, writing results to integer register rd without standard ALU sequences.";
        guidance.next_action = KeyAction::ToggleExplain;
        guidance.next_hint = "Inspect CFU accelerator function and timing.";
        guidance.glossary_topic = 1;
    }

    return guidance;
}

auto guidance_for_page(TuiRegPage page, bool cycle_accurate) -> PageGuidance {
    return guidance_for_context({.page = page, .cycle_accurate = cycle_accurate});
}

}  // namespace simrv::tui
