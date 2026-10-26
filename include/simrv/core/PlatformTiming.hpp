/**
 * @file PlatformTiming.hpp
 * @brief Authoritative virtual platform clock and architectural timebase frequencies.
 */
#pragma once

#include <cstdint>

namespace simrv::core::timing {

using ClockHz = uint64_t;
using TimebaseHz = uint64_t;
using CycleCount = uint64_t;
using TimeNs = uint64_t;

inline constexpr ClockHz kCpuClockHz = 160'000'000ULL;
inline constexpr TimebaseHz kTimebaseHz = 10'000'000ULL;
static_assert(kCpuClockHz % kTimebaseHz == 0);
static_assert(1'000'000'000ULL % kTimebaseHz == 0);
inline constexpr uint32_t kCyclesPerTimebaseTick = static_cast<uint32_t>(kCpuClockHz / kTimebaseHz);
static_assert((kCyclesPerTimebaseTick & (kCyclesPerTimebaseTick - 1)) == 0);
inline constexpr TimeNs kNanosecondsPerTimebaseTick = 1'000'000'000ULL / kTimebaseHz;

[[nodiscard]] constexpr auto timer_ticks_to_seconds(uint64_t ticks) noexcept -> double {
    return static_cast<double>(ticks) / static_cast<double>(kTimebaseHz);
}

[[nodiscard]] constexpr auto cycles_to_nanoseconds(CycleCount cycles,
                                                   ClockHz clock_hz = kCpuClockHz) noexcept
    -> TimeNs {
    return clock_hz != 0 ? (cycles * 1'000'000'000ULL) / clock_hz : 0;
}

[[nodiscard]] constexpr auto nanoseconds_to_cycles(TimeNs ns,
                                                   ClockHz clock_hz = kCpuClockHz) noexcept
    -> CycleCount {
    return (ns * clock_hz) / 1'000'000'000ULL;
}

[[nodiscard]] constexpr auto timebase_ticks_to_cycles(uint64_t ticks,
                                                      ClockHz clock_hz = kCpuClockHz,
                                                      TimebaseHz timebase_hz = kTimebaseHz) noexcept
    -> CycleCount {
    return timebase_hz != 0 ? (ticks * clock_hz) / timebase_hz : 0;
}

/**
 * @struct DeterministicClockDivider
 * @brief Cycle-exact, drift-free clock divider advancing real-time platform counter (mtime)
 * with respect to CPU core clock frequency.
 */
struct DeterministicClockDivider {
    uint64_t cpu_clock_hz{kCpuClockHz};
    uint64_t timebase_hz{kTimebaseHz};
    uint64_t accumulator{0};
    uint32_t exact_cycles_per_tick{kCyclesPerTimebaseTick};
    bool is_exact_multiple{true};

    constexpr DeterministicClockDivider() noexcept = default;

    constexpr DeterministicClockDivider(uint64_t cpu_clk, uint64_t timebase) noexcept
        : cpu_clock_hz(cpu_clk != 0 ? cpu_clk : kCpuClockHz),
          timebase_hz(timebase != 0 ? timebase : kTimebaseHz),
          accumulator(0),
          exact_cycles_per_tick((timebase_hz != 0 && cpu_clock_hz >= timebase_hz)
                                    ? static_cast<uint32_t>(cpu_clock_hz / timebase_hz)
                                    : 1U),
          is_exact_multiple(timebase_hz != 0 && (cpu_clock_hz % timebase_hz == 0)) {}

    constexpr void reset() noexcept { accumulator = 0; }

    /// Advance by exactly one CA cycle. Returns true if mtime advanced.
    template <typename MtimeLike>
    [[nodiscard]] constexpr auto advance_ca_cycle(uint32_t& rtc_divider, MtimeLike& mtime) noexcept
        -> bool {
        if (is_exact_multiple) {
            if (++rtc_divider >= exact_cycles_per_tick) {
                ++mtime;
                rtc_divider = 0;
                return true;
            }
            return false;
        }
        accumulator += timebase_hz;
        if (accumulator >= cpu_clock_hz) {
            accumulator -= cpu_clock_hz;
            ++mtime;
            rtc_divider = static_cast<uint32_t>(accumulator / timebase_hz);
            return true;
        }
        rtc_divider = static_cast<uint32_t>(accumulator / timebase_hz);
        return false;
    }

    /// Advance by multiple cycles in fast batch execution. Returns elapsed mtime ticks.
    template <typename MtimeLike>
    [[nodiscard]] constexpr auto advance_fast_cycles(uint64_t cycles, uint32_t& rtc_divider,
                                                     MtimeLike& mtime) noexcept -> uint64_t {
        if (cycles == 0) return 0;
        if (is_exact_multiple) {
            rtc_divider += static_cast<uint32_t>(cycles);
            if (rtc_divider >= exact_cycles_per_tick) {
                const uint64_t ticks = rtc_divider / exact_cycles_per_tick;
                mtime += ticks;
                rtc_divider %= exact_cycles_per_tick;
                return ticks;
            }
            return 0;
        }
        accumulator += cycles * timebase_hz;
        if (accumulator >= cpu_clock_hz) {
            const uint64_t ticks = accumulator / cpu_clock_hz;
            mtime += ticks;
            accumulator %= cpu_clock_hz;
            rtc_divider = static_cast<uint32_t>(accumulator / timebase_hz);
            return ticks;
        }
        rtc_divider = static_cast<uint32_t>(accumulator / timebase_hz);
        return 0;
    }
};

}  // namespace simrv::core::timing

namespace simrv {
using core::timing::ClockHz;
using core::timing::CycleCount;
using core::timing::DeterministicClockDivider;
using core::timing::kCpuClockHz;
using core::timing::kCyclesPerTimebaseTick;
using core::timing::kNanosecondsPerTimebaseTick;
using core::timing::kTimebaseHz;
using core::timing::TimebaseHz;
using core::timing::TimeNs;
}  // namespace simrv
