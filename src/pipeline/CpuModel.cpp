#include "simrv/pipeline/CpuModel.hpp"

#include <algorithm>
#include <bit>
#include <format>

namespace simrv::pipeline {

auto CpuModelConfig::validate() const -> std::expected<void, std::string> {
    if (supported_xlen != 0 && supported_xlen != 32 && supported_xlen != 64) {
        return std::unexpected(
            std::format("CPU model '{}' specifies unsupported XLEN={} (expected 32 or 64)",
                        name.empty() ? "custom" : name, supported_xlen));
    }
    if (supported_xlen == 64 && simrv::xlen::kXLenBits == 32) {
        return std::unexpected(
            std::format("CPU model '{}' requires XLEN=64 (this simulator build is RV32)",
                        name.empty() ? "custom" : name));
    }
    if ((isa_preset == isa::IsaPreset::E || isa_preset == isa::IsaPreset::EM ||
         isa_preset == isa::IsaPreset::EMAC) &&
        supported_xlen != 32) {
        return std::unexpected(std::format("CPU model '{}' uses RV32E and must specify XLEN=32",
                                           name.empty() ? "custom" : name));
    }
    const auto valid_cache = [](const L1CacheConfig& cache,
                                std::string_view name) -> std::optional<std::string> {
        const auto power_of_two = [](uint32_t value) {
            return value != 0 && std::has_single_bit(value);
        };
        if (!power_of_two(cache.capacity_bytes) || !power_of_two(cache.associativity) ||
            !power_of_two(cache.line_bytes)) {
            return std::format(
                "{} cache capacity, associativity, and line size must be powers of two", name);
        }
        // CPU model configurations that do not implement Zic64b may retain 32-byte geometry.
        // The RVA23 default uses the 64-byte coherent block required by Zic64b.
        if (cache.line_bytes != 32 && cache.line_bytes != 64) {
            return std::format(
                "{} cache line size must be 32 or 64 bytes", name);
        }
        if (cache.associativity > 8 ||
            cache.capacity_bytes < cache.associativity * cache.line_bytes) {
            return std::format("{} cache geometry has no complete set", name);
        }
        if (cache.capacity_bytes > 32 * 1024) {
            return std::format("{} cache exceeds the 32 KiB runtime FPGA cache backing", name);
        }
        const uint32_t sets = cache.capacity_bytes / (cache.associativity * cache.line_bytes);
        if (!power_of_two(sets)) {
            return std::format("{} cache set count must be a power of two", name);
        }
        if (cache.hit_latency == 0 || cache.miss_latency == 0) {
            return std::format("{} cache hit and miss latency must be at least one cycle", name);
        }
        return std::nullopt;
    };
    if (const auto error = valid_cache(instruction_cache, "instruction"); error) {
        return std::unexpected(*error);
    }
    if (const auto error = valid_cache(data_cache, "data"); error) {
        return std::unexpected(*error);
    }
    if (instruction_cache.line_bytes != data_cache.line_bytes) {
        return std::unexpected(
            "instruction and data caches must use one shared coherent line size");
    }
    if (instruction_front_cache.capacity_bytes != 0) {
        const auto& cache = instruction_front_cache;
        if (!std::has_single_bit(cache.capacity_bytes) ||
            !std::has_single_bit(cache.associativity) || !std::has_single_bit(cache.line_bytes) ||
            cache.capacity_bytes < cache.associativity * cache.line_bytes ||
            cache.capacity_bytes % (cache.associativity * cache.line_bytes) != 0 ||
            !std::has_single_bit(cache.capacity_bytes / (cache.associativity * cache.line_bytes))) {
            return std::unexpected("instruction front-cache geometry must form power-of-two sets");
        }
        if (cache.hit_latency == 0 || cache.refill_latency == 0 ||
            cache.backing_refill_latency == 0) {
            return std::unexpected("instruction front-cache latencies must be at least one cycle");
        }
        if (std::ranges::any_of(cache.startup_refill_latencies,
                                [](simrv::xlen::LatencyCycles latency) { return latency == 0; })) {
            return std::unexpected(
                "instruction front-cache startup refill latencies must be at least one cycle");
        }
    }
    if (interconnect.request_latency == 0 || interconnect.response_latency == 0) {
        return std::unexpected(
            "interconnect request and response latency must be at least one cycle");
    }
    if (const auto cfu_result = cfu.validate(); !cfu_result) {
        return std::unexpected(cfu_result.error());
    }
    return {};
}

}  // namespace simrv::pipeline
