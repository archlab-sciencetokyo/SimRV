/**
 * @file Telemetry.hpp
 * @brief Value-oriented simulator lifecycle observations.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace simrv::core {

enum class LifecycleEventKind : uint8_t {
    Initialized,
    Started,
    Running,
    Stopped,
    Completed,
    Failed,
    RebootRequested,
    ExitRequested,
};

/// A presentation- and debugger-neutral machine lifecycle observation.
struct LifecycleEvent {
    LifecycleEventKind kind{};
    uint64_t instruction_count{};
    int exit_status{};
    uint8_t stop_reason{};
};

using LifecycleObserver = std::function<void(const LifecycleEvent&)>;
using LifecycleObserverId = uint64_t;

enum class MachineEventKind : uint8_t {
    InterruptAsserted,
    InterruptDeasserted,
    DmaStarted,
    DmaCompleted,
    DmaCancelled,
};

/// Machine-level device activity, independent of the architectural trace file settings.
struct MachineEvent {
    MachineEventKind kind{};
    uint64_t cycle{};
    uint32_t hart = UINT32_MAX;
    uint32_t interrupt_cause{};
    uint32_t interrupt_source{};
    uint64_t transfer_id{};
    uint64_t byte_count{};
    uint64_t request_cycle{};
    uint64_t start_cycle{};
    uint64_t completion_cycle{};
    std::string component;
};

using MachineEventObserver = std::function<void(const MachineEvent&)>;
using MachineEventObserverId = uint64_t;

}  // namespace simrv::core
