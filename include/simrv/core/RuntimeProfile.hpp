/**
 * @file RuntimeProfile.hpp
 * @brief Resolved runtime policy shared by command-line, core, and UI code.
 */
#pragma once

#include <string_view>

namespace simrv::core {

enum class ExecutionEngine : unsigned char {
    InstructionFast,
    InstructionObservable,
    CycleFast,
    CycleObservable
};
enum class InteractionMode : unsigned char { Cli, Tui };

[[nodiscard]] constexpr auto select_execution_engine(bool cycle_mode, InteractionMode interaction)
    -> ExecutionEngine {
    if (!cycle_mode) {
        return interaction == InteractionMode::Tui ? ExecutionEngine::InstructionObservable
                                                   : ExecutionEngine::InstructionFast;
    }
    return interaction == InteractionMode::Tui ? ExecutionEngine::CycleObservable
                                               : ExecutionEngine::CycleFast;
}

struct RuntimeProfile {
    ExecutionEngine engine = ExecutionEngine::InstructionFast;
    InteractionMode interaction = InteractionMode::Cli;
    bool debug_diagnostics = false;
    bool tracing = false;
    bool lockstep = false;
    bool gdb = false;

    [[nodiscard]] constexpr auto is_cycle_mode() const -> bool {
        return engine == ExecutionEngine::CycleFast || engine == ExecutionEngine::CycleObservable;
    }
    [[nodiscard]] constexpr auto is_instruction_mode() const -> bool { return !is_cycle_mode(); }
    [[nodiscard]] constexpr auto is_instruction_fast() const -> bool {
        return engine == ExecutionEngine::InstructionFast;
    }
    [[nodiscard]] constexpr auto records_cycle_history() const -> bool {
        return engine == ExecutionEngine::CycleObservable;
    }
    [[nodiscard]] constexpr auto allows_fast_batch() const -> bool {
        const bool fast_instruction_engine = engine == ExecutionEngine::InstructionFast;
        const bool sampled_tui_engine =
            engine == ExecutionEngine::InstructionObservable && interaction == InteractionMode::Tui;
        return (fast_instruction_engine || sampled_tui_engine) && !debug_diagnostics && !tracing &&
               !lockstep && !gdb;
    }
    [[nodiscard]] constexpr auto fast_batch_quantum() const -> unsigned {
        return interaction == InteractionMode::Tui ? 2048U : 65536U;
    }
    [[nodiscard]] constexpr auto execution_name() const -> std::string_view {
        switch (engine) {
            case ExecutionEngine::InstructionFast:
                return "instruction-fast";
            case ExecutionEngine::InstructionObservable:
                return "instruction-observable";
            case ExecutionEngine::CycleFast:
                return "cycle-fast";
            case ExecutionEngine::CycleObservable:
                return "cycle-observable";
        }
        return "unknown";
    }
    [[nodiscard]] constexpr auto interaction_name() const -> std::string_view {
        switch (interaction) {
            case InteractionMode::Cli:
                return "CLI";
            case InteractionMode::Tui:
                return "TUI";
        }
        return "unknown";
    }
};

}  // namespace simrv::core
