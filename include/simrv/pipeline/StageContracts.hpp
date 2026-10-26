/**
 * @file StageContracts.hpp
 * @brief Formal execution contracts and stage result statuses across the SimRV pipeline.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "simrv/isa/TrapTypes.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::pipeline {

/**
 * @enum StageStatus
 * @brief Formal execution status returned by individual pipeline stages.
 */
enum class StageStatus : uint8_t {
    Success = 0,           ///< Stage completed execution successfully without stall or exception.
    Stalled = 1,           ///< Stage stalled due to data hazard, structural limit, or latency.
    InterconnectWait = 2,  ///< Stage blocked waiting on memory bus / cache transaction.
    Exception = 3,         ///< Stage encountered a synchronous exception or misaligned fault.
};

[[nodiscard]] constexpr auto to_string(StageStatus status) noexcept -> std::string_view {
    switch (status) {
        case StageStatus::Success:
            return "Success";
        case StageStatus::Stalled:
            return "Stalled";
        case StageStatus::InterconnectWait:
            return "InterconnectWait";
        case StageStatus::Exception:
            return "Exception";
    }
    return "Unknown";
}

/**
 * @brief Derives the formal stage execution status from pending exception and interconnect state.
 */
[[nodiscard]] constexpr auto stage_status_from_state(
    const std::optional<simrv::isa::ExceptionCode>& exception,
    bool waiting_for_interconnect = false) noexcept -> StageStatus {
    if (waiting_for_interconnect) {
        return StageStatus::InterconnectWait;
    }
    if (exception.has_value()) {
        return StageStatus::Exception;
    }
    return StageStatus::Success;
}

}  // namespace simrv::pipeline
