/**
 * @file InterruptTopology.hpp
 * @brief Implemented platform interrupt source capacities.
 */
#pragma once

#include <cstdint>

namespace simrv::core {

/// SimRV's PLIC implements source IDs 1 through 31; source 0 is reserved.
inline constexpr uint32_t kPlicSourceCount = 31;

}  // namespace simrv::core
