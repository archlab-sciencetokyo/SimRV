/**
 * @file UartTiming.hpp
 * @brief Shared UART reset divisor and baud calculations.
 */
#pragma once

#include <cstdint>

#include "simrv/core/PlatformMap.h"

namespace simrv::device {
inline constexpr uint32_t kUartResetBaudRate = SIMRV_PLATFORM_UART_BAUD_RATE;

[[nodiscard]] constexpr auto uart_reset_divisor(uint64_t clock_hz) noexcept -> uint16_t {
    const uint64_t denominator = 16ULL * kUartResetBaudRate;
    const uint64_t rounded = (clock_hz + denominator / 2) / denominator;
    return static_cast<uint16_t>(rounded < 1 ? 1 : (rounded > 65535 ? 65535 : rounded));
}

[[nodiscard]] constexpr auto uart_baud_rate(uint64_t clock_hz, uint16_t divisor) noexcept
    -> uint32_t {
    return divisor == 0 ? 0 : static_cast<uint32_t>(clock_hz / (16ULL * divisor));
}
}  // namespace simrv::device
