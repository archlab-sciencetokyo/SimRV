/**
 * @file CpuModel.hpp
 * @brief Validated, serialisable description of SimRV's scalar in-order CA core.
 *
 * The model is deliberately independent of the UI and of command-line parsing.  IA ignores it;
 * CA projects the pipeline portion into PipelineSim when a machine is (re)created.
 */
#pragma once

#include <bit>
#include <cstdint>
#include <expected>
#include <format>
#include <optional>
#include <string>
#include <string_view>

#include "simrv/isa/Base.hpp"
#include "simrv/isa/Common.hpp"
#include "simrv/pipeline/PipelineSim.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::pipeline {

enum class CpuModelProfile : uint8_t {
    Tiny,
    Balanced,
    Performance,
    Custom,
};

struct L1CacheConfig {
    uint32_t capacity_bytes = 4096;
    uint32_t associativity = 2;
    uint32_t line_bytes = 32;
    LatencyCycles hit_latency = 1;
    LatencyCycles miss_latency = 12;
};

struct InterconnectTiming {
    LatencyCycles request_latency = 1;
    LatencyCycles response_latency = 1;
};

struct CpuModelConfig {
    CpuModelProfile profile = CpuModelProfile::Balanced;
    isa::MisaProfile misa_profile = isa::MisaProfile::GCBV;
    std::string name = "balanced";
    std::string description{};
    uint8_t supported_xlen = 0;  // 0 = any/both 32 and 64, 32 = RV32 only, 64 = RV64 only
    CpuConfig pipeline{};
    L1CacheConfig instruction_cache{};
    L1CacheConfig data_cache{};
    InterconnectTiming interconnect{};
    bool enable_idle_spans = true;

    [[nodiscard]] auto validate() const -> std::expected<void, std::string> {
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
            // The current coherent TileLink fabric transfers one 32-byte beat line.  Capacity
            // and associativity are runtime-modelled; a wider fabric is a separate protocol
            // change, not a cache-only setting.
            if (cache.line_bytes != 32) {
                return std::format(
                    "{} cache line size must be 32 bytes for the current coherent fabric", name);
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
                return std::format("{} cache hit and miss latency must be at least one cycle",
                                   name);
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
        if (interconnect.request_latency == 0 || interconnect.response_latency == 0) {
            return std::unexpected(
                "interconnect request and response latency must be at least one cycle");
        }
        if (pipeline.mul_latency == 0 || pipeline.div_latency == 0 ||
            pipeline.fp_alu_latency == 0 || pipeline.fp_div_latency == 0) {
            return std::unexpected("execution-unit latency must be at least one cycle");
        }
        return {};
    }
};

[[nodiscard]] constexpr auto cpu_model_profile_name(CpuModelProfile profile) -> std::string_view {
    switch (profile) {
        case CpuModelProfile::Tiny:
            return "tiny";
        case CpuModelProfile::Balanced:
            return "balanced";
        case CpuModelProfile::Performance:
            return "performance";
        case CpuModelProfile::Custom:
            return "custom";
    }
    return "custom";
}

[[nodiscard]] inline auto parse_cpu_model_profile(std::string_view value)
    -> std::optional<CpuModelProfile> {
    if (value == "tiny") return CpuModelProfile::Tiny;
    if (value == "balanced") return CpuModelProfile::Balanced;
    if (value == "performance") return CpuModelProfile::Performance;
    if (value == "custom") return CpuModelProfile::Custom;
    return std::nullopt;
}

[[nodiscard]] inline auto make_cpu_model_profile(CpuModelProfile profile) -> CpuModelConfig {
    CpuModelConfig result{};
    result.profile = profile;
    result.name = std::string(cpu_model_profile_name(profile));
    switch (profile) {
        case CpuModelProfile::Tiny:
            result.description = "Minimal 3-stage microcontroller core";
            result.misa_profile = isa::MisaProfile::IMAC;
            result.pipeline.pipeline_type = PipelineType::ThreeStage;
            result.pipeline.enable_forwarding = false;
            result.pipeline.branch_predictor.type = BranchPredictorType::Static;
            result.pipeline.branch_predictor.enable_btb = false;
            result.instruction_cache = {2048, 1, 32, 1, 12};
            result.data_cache = result.instruction_cache;
            break;
        case CpuModelProfile::Balanced:
            result.description = "Default balanced 5-stage general-purpose core";
            result.misa_profile = isa::MisaProfile::GCBV;
            result.pipeline.pipeline_type = PipelineType::FiveStage;
            result.pipeline.enable_forwarding = true;
            result.pipeline.branch_predictor.type = BranchPredictorType::Bimodal;
            result.instruction_cache = {4096, 2, 32, 1, 10};
            result.data_cache = result.instruction_cache;
            break;
        case CpuModelProfile::Performance:
            result.description = "High-throughput 5-stage core with aggressive prediction";
            result.misa_profile = isa::MisaProfile::GCBV;
            result.pipeline.pipeline_type = PipelineType::FiveStage;
            result.pipeline.enable_forwarding = true;
            result.pipeline.enable_instruction_prefetch = true;
            result.pipeline.branch_predictor.type = BranchPredictorType::Tournament;
            result.instruction_cache = {16384, 4, 32, 1, 8};
            result.data_cache = result.instruction_cache;
            break;
        case CpuModelProfile::Custom:
            result.profile = CpuModelProfile::Custom;
            break;
    }
    return result;
}

}  // namespace simrv::pipeline
