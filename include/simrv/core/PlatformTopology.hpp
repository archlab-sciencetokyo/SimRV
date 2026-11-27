/**
 * @file PlatformTopology.hpp
 * @brief Fixed topology capacities shared by the simulated platform.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace simrv::core::topology {

/// Maximum hart count supported by every modeled per-hart interrupt controller.
inline constexpr uint32_t kMaxSimulatedHarts = 16;

/// The modeled PLIC exposes machine and supervisor contexts for each hart.
inline constexpr std::size_t kPlicContextsPerHart = 2;
inline constexpr std::size_t kMaxPlicContexts =
    static_cast<std::size_t>(kMaxSimulatedHarts) * kPlicContextsPerHart;

static_assert(kMaxPlicContexts == 32);

}  // namespace simrv::core::topology
