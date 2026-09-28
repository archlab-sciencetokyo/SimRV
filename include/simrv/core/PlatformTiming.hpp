/**
 * @file PlatformTiming.hpp
 * @brief Authoritative virtual platform clock and architectural timebase frequencies.
 */
#pragma once

#include <cstdint>

namespace simrv::core::timing {

inline constexpr uint64_t kCpuClockHz = 160'000'000ULL;
inline constexpr uint64_t kTimebaseHz = 10'000'000ULL;
static_assert(kCpuClockHz % kTimebaseHz == 0);
static_assert(1'000'000'000ULL % kTimebaseHz == 0);
inline constexpr uint32_t kCyclesPerTimebaseTick = static_cast<uint32_t>(kCpuClockHz / kTimebaseHz);
static_assert((kCyclesPerTimebaseTick & (kCyclesPerTimebaseTick - 1)) == 0);
inline constexpr uint64_t kNanosecondsPerTimebaseTick = 1'000'000'000ULL / kTimebaseHz;

[[nodiscard]] constexpr auto timer_ticks_to_seconds(uint64_t ticks) -> double {
    return static_cast<double>(ticks) / static_cast<double>(kTimebaseHz);
}

}  // namespace simrv::core::timing
