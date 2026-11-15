#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "simrv/pipeline/PipelineSim.hpp"

namespace simrv::pipeline {

/**
 * @struct PipelineTopologySpec
 * @brief Structural compile-time descriptor of an in-order pipeline stage layout.
 *
 * Used directly as a C++23 Non-Type Template Parameter (NTTP) to specialize
 * `CPU::run_ca_pipeline_cycle_impl<Spec>` and `CPU::execute_ca_cycle_stages_impl<Spec>`
 * without runtime stage-count branching.
 */
struct PipelineTopologySpec {
    uint8_t fetch_stages = 1;
    bool has_decode = true;
    bool has_issue = false;
    bool has_execute = true;
    uint8_t memory_stages = 1;

    [[nodiscard]] consteval auto stage_count() const noexcept -> uint8_t {
        return static_cast<uint8_t>(fetch_stages + static_cast<uint8_t>(has_decode) +
                                    static_cast<uint8_t>(has_issue) +
                                    static_cast<uint8_t>(has_execute) + memory_stages + 1U);
    }

    [[nodiscard]] consteval auto is_valid() const noexcept -> bool {
        if (fetch_stages < 1 || fetch_stages > 2) return false;
        if (memory_stages > 2) return false;
        if (!has_execute && memory_stages > 0) return false;
        if (has_issue && !has_decode) return false;
        return true;
    }

    friend auto operator==(const PipelineTopologySpec&, const PipelineTopologySpec&)
        -> bool = default;
};

template <PipelineTopologySpec Spec>
concept ValidPipelineTopology = Spec.is_valid();

struct PipelineTopologyEntry {
    PipelineType id;
    PipelineTopologySpec spec;
    std::string_view cli_name;
    std::string_view display_name;
};

inline constexpr std::array<PipelineTopologyEntry, 6> kPipelineCatalog = {{
    {
        PipelineType::TwoStage,
        PipelineTopologySpec{
            .fetch_stages = 1,
            .has_decode = false,
            .has_issue = false,
            .has_execute = false,
            .memory_stages = 0,
        },
        "2stage",
        "2-Stage In-Order",
    },
    {
        PipelineType::ThreeStage,
        PipelineTopologySpec{
            .fetch_stages = 1,
            .has_decode = true,
            .has_issue = false,
            .has_execute = false,
            .memory_stages = 0,
        },
        "3stage",
        "3-Stage In-Order",
    },
    {
        PipelineType::FiveStage,
        PipelineTopologySpec{
            .fetch_stages = 1,
            .has_decode = true,
            .has_issue = false,
            .has_execute = true,
            .memory_stages = 1,
        },
        "5stage",
        "5-Stage In-Order",
    },
    {
        PipelineType::SixStageIssue,
        PipelineTopologySpec{
            .fetch_stages = 1,
            .has_decode = true,
            .has_issue = true,
            .has_execute = true,
            .memory_stages = 1,
        },
        "6stage-issue",
        "6-Stage In-Order (Split Issue)",
    },
    {
        PipelineType::SixStageFetch,
        PipelineTopologySpec{
            .fetch_stages = 2,
            .has_decode = true,
            .has_issue = false,
            .has_execute = true,
            .memory_stages = 1,
        },
        "6stage-fetch",
        "6-Stage In-Order (Split Fetch)",
    },
    {
        PipelineType::SixStageMem,
        PipelineTopologySpec{
            .fetch_stages = 1,
            .has_decode = true,
            .has_issue = false,
            .has_execute = true,
            .memory_stages = 2,
        },
        "6stage-mem",
        "6-Stage In-Order (Split Memory)",
    },
}};

[[nodiscard]] constexpr auto topology_entry(PipelineType type) noexcept
    -> const PipelineTopologyEntry& {
    for (const auto& entry : kPipelineCatalog) {
        if (entry.id == type) return entry;
    }
    return kPipelineCatalog[2];  // FiveStage fallback
}

[[nodiscard]] constexpr auto topology_spec(PipelineType type) noexcept -> PipelineTopologySpec {
    return topology_entry(type).spec;
}

[[nodiscard]] constexpr auto pipeline_stage_count(PipelineType type) noexcept -> uint8_t {
    const auto spec = topology_entry(type).spec;
    return static_cast<uint8_t>(spec.fetch_stages + static_cast<uint8_t>(spec.has_decode) +
                                static_cast<uint8_t>(spec.has_issue) +
                                static_cast<uint8_t>(spec.has_execute) + spec.memory_stages + 1U);
}

namespace detail {
consteval auto verify_pipeline_catalog() -> bool {
    for (size_t i = 0; i < kPipelineCatalog.size(); ++i) {
        if (!kPipelineCatalog[i].spec.is_valid()) return false;
        for (size_t j = i + 1; j < kPipelineCatalog.size(); ++j) {
            if (kPipelineCatalog[i].id == kPipelineCatalog[j].id) return false;
            if (kPipelineCatalog[i].spec == kPipelineCatalog[j].spec) return false;
            if (kPipelineCatalog[i].cli_name == kPipelineCatalog[j].cli_name) return false;
        }
    }
    return true;
}
}  // namespace detail

static_assert(detail::verify_pipeline_catalog(),
              "All kPipelineCatalog entries must have unique IDs, specs, and valid topologies");
static_assert(topology_entry(PipelineType::TwoStage).spec.stage_count() == 2);
static_assert(topology_entry(PipelineType::ThreeStage).spec.stage_count() == 3);
static_assert(topology_entry(PipelineType::FiveStage).spec.stage_count() == 5);
static_assert(topology_entry(PipelineType::SixStageIssue).spec.stage_count() == 6);
static_assert(topology_entry(PipelineType::SixStageFetch).spec.stage_count() == 6);
static_assert(topology_entry(PipelineType::SixStageMem).spec.stage_count() == 6);

}  // namespace simrv::pipeline
