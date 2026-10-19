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
