/**
 * @file HostCache.hpp
 * @brief Host microarchitecture and hardware cache topology discovery.
 */
#pragma once

#include <cstddef>
#include <cstdint>

namespace simrv::util {

/**
 * @struct HostCacheTopology
 * @brief Detected host cache hierarchy metrics and derived optimal simulation tuning parameters.
 */
struct HostCacheTopology {
    size_t l1d_bytes{48 * 1024};        ///< Host L1 data cache size
    size_t l1i_bytes{64 * 1024};        ///< Host L1 instruction cache size
    size_t l2_bytes{2 * 1024 * 1024};   ///< Host L2 cache size (per core)
    size_t l3_bytes{16 * 1024 * 1024};  ///< Host L3 cache size (shared)
    size_t line_bytes{64};              ///< Host cache line size
    bool detected{false};               ///< True if discovered via POSIX / OS sysfs

    /**
     * @brief Computes the optimal fast batch quantum.
     * Keeps the simulated loop body resident in L1d/L1i without excess loop bounce.
     */
    [[nodiscard]] constexpr auto optimal_fast_batch_quantum(bool is_tui) const noexcept
        -> uint32_t {
        if (is_tui) return 2048U;
        // 65536 instructions is optimal for staying resident in host L1 caches
        return 65536U;
    }

    /**
     * @brief Computes optimal basic block cache sets to prevent L2 thrashing.
     */
    [[nodiscard]] constexpr auto optimal_block_cache_sets() const noexcept -> size_t {
        if (l2_bytes >= 2 * 1024 * 1024) {
            return 4096;
        }
        if (l2_bytes >= 1024 * 1024) {
            return 2048;
        }
        return 1024;
    }
};

/**
 * @brief Queries the host OS for CPU cache sizes and cache line length.
 */
[[nodiscard]] auto detect_host_cache_topology() noexcept -> HostCacheTopology;

}  // namespace simrv::util
