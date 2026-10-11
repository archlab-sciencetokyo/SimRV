/**
 * @file CpuModel.hpp
 * @brief Validated, serialisable description of SimRV's scalar in-order CA core.
 *
 * The model is deliberately independent of the UI and of command-line parsing.  IA ignores it;
 * CA projects the pipeline portion into PipelineSim when a machine is (re)created.
 */
#pragma once

#include <algorithm>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/isa/Base.hpp"
#include "simrv/pipeline/PipelineSim.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::pipeline {

enum class CpuModelPreset : uint8_t {
    Tiny,
    Balanced,
    Performance,
    Custom,
};

struct L1CacheConfig {
    uint32_t capacity_bytes = 4096;
    uint32_t associativity = 2;
    uint32_t line_bytes = 32;
    simrv::xlen::LatencyCycles hit_latency = 1;
    simrv::xlen::LatencyCycles miss_latency = 12;
};

struct FrontCacheConfig {
    uint32_t capacity_bytes = 0;
    uint32_t associativity = 1;
    uint32_t line_bytes = 16;
    simrv::xlen::LatencyCycles hit_latency = 1;
    simrv::xlen::LatencyCycles refill_latency = 1;
    simrv::xlen::LatencyCycles backing_refill_latency = 1;
    std::vector<simrv::xlen::LatencyCycles> startup_refill_latencies{};
    bool freeze_pipeline_on_refill = false;
};

struct InterconnectTiming {
    simrv::xlen::LatencyCycles request_latency = 1;
    simrv::xlen::LatencyCycles response_latency = 1;
    simrv::xlen::LatencyCycles data_request_latency = 0;
    simrv::xlen::LatencyCycles data_response_latency = 0;
    simrv::xlen::LatencyCycles startup_data_response_latency = 0;
};

struct DmaTimingConfig {
    bool enabled = false;
    uint32_t setup_latency = 32;
    uint32_t bandwidth_bytes_per_cycle = 8;
    uint32_t memory_contention_penalty = 2;
};

struct AxiConfig {
    bool enabled{false};
    uint32_t data_width_bytes{8};
    uint32_t id_width_bits{4};
    uint32_t max_outstanding_reads{8};
    uint32_t max_outstanding_writes{8};
    uint32_t burst_length_max{256};
    bool trace_axi{false};
};

/** Declarative integration contract for a custom function unit. */
struct CfuConfig {
    bool enabled{false};
    std::string opcode{"custom-0"};
    std::string plugin{};
    std::string rtl_module{};
    std::string interface{"rs1-rs2-rd"};
    uint32_t default_latency{1};  // CFU/plugin latency is total cycles, including issue.

    [[nodiscard]] auto validate() const -> std::expected<void, std::string> {
        if (default_latency == 0) {
            return std::unexpected("CFU default latency must be at least one cycle");
        }
        if (enabled && opcode != "custom-0") {
            return std::unexpected("the current CFU decoder supports only opcode custom-0");
        }
        return {};
    }
};

struct CpuModelConfig {
    CpuModelPreset preset = CpuModelPreset::Balanced;
    isa::IsaPreset isa_preset = isa::IsaPreset::GCBV;
    std::string name = "balanced";
    std::string description{};
    uint8_t supported_xlen = 0;  // 0 = any/both 32 and 64, 32 = RV32 only, 64 = RV64 only
    CpuConfig pipeline{};
    L1CacheConfig instruction_cache{};
    L1CacheConfig data_cache{};
    FrontCacheConfig instruction_front_cache{};
    InterconnectTiming interconnect{};
    DmaTimingConfig dma{};
    AxiConfig axi{};
    CfuConfig cfu{};
    // Multi-letter extensions do not have individual MISA bits. Keep them alongside the base
    // ISA preset so CPU presets can describe extensions such as Zkn/Zkr/Zkt losslessly.
    std::vector<std::string> isa_extensions{};
    bool enable_idle_spans = true;

    [[nodiscard]] auto has_isa_extension(std::string_view extension) const -> bool {
        return std::ranges::find(isa_extensions, extension) != isa_extensions.end();
    }

    [[nodiscard]] auto validate() const -> std::expected<void, std::string>;
};

[[nodiscard]] constexpr auto cpu_model_preset_name(CpuModelPreset preset) -> std::string_view {
    switch (preset) {
        case CpuModelPreset::Tiny:
            return "tiny";
        case CpuModelPreset::Balanced:
            return "balanced";
        case CpuModelPreset::Performance:
            return "performance";
        case CpuModelPreset::Custom:
            return "custom";
    }
    return "custom";
}

[[nodiscard]] inline auto parse_cpu_model_preset(std::string_view value)
    -> std::optional<CpuModelPreset> {
    if (value == "tiny") return CpuModelPreset::Tiny;
    if (value == "balanced") return CpuModelPreset::Balanced;
    if (value == "performance") return CpuModelPreset::Performance;
    if (value == "custom") return CpuModelPreset::Custom;
    return std::nullopt;
}

[[nodiscard]] inline auto make_cpu_model_preset(CpuModelPreset preset) -> CpuModelConfig {
    CpuModelConfig result{};
    result.preset = preset;
    result.name = std::string(cpu_model_preset_name(preset));
    switch (preset) {
        case CpuModelPreset::Tiny:
            result.description = "Minimal 3-stage microcontroller core";
            result.isa_preset = isa::IsaPreset::IMAC;
            result.pipeline.pipeline_type = PipelineType::ThreeStage;
            result.pipeline.enable_forwarding = false;
            result.pipeline.branch_predictor.type = BranchPredictorType::Static;
            result.pipeline.branch_predictor.enable_btb = false;
            result.instruction_cache = {2048, 1, 32, 1, 12};
            result.data_cache = result.instruction_cache;
            break;
        case CpuModelPreset::Balanced:
            result.description = "Default balanced 5-stage general-purpose core";
            result.isa_preset = isa::IsaPreset::GCBV;
            result.pipeline.pipeline_type = PipelineType::FiveStage;
            result.pipeline.enable_forwarding = true;
            result.pipeline.branch_predictor.type = BranchPredictorType::Bimodal;
            result.instruction_cache = {4096, 2, 32, 1, 10};
            result.data_cache = result.instruction_cache;
            break;
        case CpuModelPreset::Performance:
            result.description = "High-throughput 5-stage core with aggressive prediction";
            result.isa_preset = isa::IsaPreset::GCBV;
            result.pipeline.pipeline_type = PipelineType::FiveStage;
            result.pipeline.enable_forwarding = true;
            result.pipeline.enable_instruction_prefetch = true;
            result.pipeline.branch_predictor.type = BranchPredictorType::Tournament;
            result.instruction_cache = {16384, 4, 32, 1, 8};
            result.data_cache = result.instruction_cache;
            break;
        case CpuModelPreset::Custom:
            result.preset = CpuModelPreset::Custom;
            break;
    }
    return result;
}

}  // namespace simrv::pipeline
