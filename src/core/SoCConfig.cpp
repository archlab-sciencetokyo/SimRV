#include "simrv/core/SoCConfig.hpp"

#include <algorithm>
#include <bit>
#include <format>

namespace simrv::core {

auto SmpTopologyConfig::validate() const -> std::expected<void, std::string> {
    if (hart_count == 0 || hart_count > 16) {
        return std::unexpected("SMP hart count must be between 1 and 16");
    }
    if (synchronization_quantum == 0) {
        return std::unexpected("SMP synchronization quantum must be non-zero");
    }
    const auto validate_cache = [](const SharedCacheConfig& cache, std::string_view name,
                                   uint32_t max_capacity,
                                   uint32_t max_ways) -> std::expected<void, std::string> {
        if (!cache.enabled) return {};
        if (cache.capacity_bytes < cache.line_bytes * cache.associativity ||
            !std::has_single_bit(cache.capacity_bytes) ||
            !std::has_single_bit(cache.associativity) || cache.line_bytes != 32 ||
            cache.associativity > max_ways || cache.capacity_bytes > max_capacity ||
            !std::has_single_bit(cache.capacity_bytes / (cache.line_bytes * cache.associativity)) ||
            cache.capacity_bytes % (cache.line_bytes * cache.associativity) != 0) {
            return std::unexpected(std::format(
                "{} cache geometry must use 32-byte lines, power-of-two sets/ways, and fit "
                "within {} bytes",
                name, max_capacity));
        }
        if (cache.hit_latency == 0 || cache.miss_latency == 0) {
            return std::unexpected(
                std::format("{} cache hit and miss latencies must be at least one cycle", name));
        }
        return {};
    };
    if (auto result = validate_cache(l3, "shared L3", 512 * 1024, 16); !result) {
        return result;
    }
    std::vector<uint32_t> assigned_harts;
    std::vector<uint32_t> cluster_ids;
    for (const auto& cluster : clusters) {
        if (std::ranges::find(cluster_ids, cluster.id) != cluster_ids.end()) {
            return std::unexpected("SMP cluster IDs must be unique");
        }
        cluster_ids.push_back(cluster.id);
        if (cluster.harts.empty()) {
            return std::unexpected("SMP clusters must contain at least one hart");
        }
        if (auto result = validate_cache(cluster.l2, "cluster L2", 64 * 1024, 8); !result) {
            return result;
        }
        for (const auto hart : cluster.harts) {
            if (hart >= hart_count ||
                std::ranges::find(assigned_harts, hart) != assigned_harts.end()) {
                return std::unexpected("each SMP hart must belong to exactly one valid cluster");
            }
            assigned_harts.push_back(hart);
        }
    }
    if (!clusters.empty() && assigned_harts.size() != hart_count) {
        return std::unexpected("SMP cluster mapping must assign every configured hart");
    }
    return {};
}

}  // namespace simrv::core
