/**
 * @file HostCache.hpp
 * @brief Host microarchitecture and hardware cache topology discovery.
 */
#pragma once

#include <cstddef>

namespace simrv::util {

/**
 * @struct HostCacheTopology
 * @brief Detected host cache hierarchy metrics for environment diagnostics.
 */
struct HostCacheTopology {
    size_t l1d_bytes{48 * 1024};        ///< Host L1 data cache size
    size_t l1i_bytes{64 * 1024};        ///< Host L1 instruction cache size
    size_t l2_bytes{2 * 1024 * 1024};   ///< Host L2 cache size (per core)
    size_t l3_bytes{16 * 1024 * 1024};  ///< Host L3 cache size (shared)
    size_t line_bytes{64};              ///< Host cache line size
    bool detected{false};               ///< True if discovered via POSIX / OS sysfs
};

/**
 * @brief Queries the host OS for CPU cache sizes and cache line length.
 */
[[nodiscard]] auto detect_host_cache_topology() noexcept -> HostCacheTopology;

}  // namespace simrv::util
