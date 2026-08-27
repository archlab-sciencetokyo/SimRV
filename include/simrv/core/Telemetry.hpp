/**
 * @file Telemetry.hpp
 * @brief Value-oriented simulator lifecycle observations.
 */
#pragma once

#include <cstdint>
#include <functional>

namespace simrv::core {

enum class LifecycleEventKind : uint8_t {
    Started,
    Stopped,
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

}  // namespace simrv::core
