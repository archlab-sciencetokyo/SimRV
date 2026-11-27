#include "simrv/pipeline/PipelineConfig.hpp"

#include <algorithm>
#include <cctype>
#include <string>

#include "simrv/pipeline/PipelineTopology.hpp"

namespace simrv::pipeline {

auto parse_pipeline_type(std::string_view name) -> std::optional<PipelineType> {
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const auto& entry : kPipelineCatalog) {
        if (lower == entry.cli_name) {
            return entry.id;
        }
    }
    if (lower == "2-stage" || lower == "2_stage" || lower == "two-stage" || lower == "two_stage" ||
        lower == "2-stage in-order" || lower == "2") {
        return PipelineType::TwoStage;
    }
    if (lower == "3-stage" || lower == "3_stage" || lower == "three-stage" ||
        lower == "three_stage" || lower == "3-stage in-order" || lower == "3") {
        return PipelineType::ThreeStage;
    }
    if (lower == "5-stage" || lower == "5_stage" || lower == "five-stage" ||
        lower == "five_stage" || lower == "5-stage in-order" || lower == "5") {
        return PipelineType::FiveStage;
    }
    if (lower == "6stage" || lower == "6-stage" || lower == "6_stage" || lower == "six-stage" ||
        lower == "six_stage" || lower == "6-stage in-order" || lower == "6" ||
        lower == "6-stage-issue" || lower == "6stage_issue") {
        return PipelineType::SixStageIssue;
    }
    if (lower == "6-stage-fetch" || lower == "6stage_fetch") {
        return PipelineType::SixStageFetch;
    }
    if (lower == "6-stage-mem" || lower == "6stage_mem") {
        return PipelineType::SixStageMem;
    }
    return std::nullopt;
}

auto pipeline_type_name(PipelineType type) -> std::string_view {
    return topology_entry(type).display_name;
}

}  // namespace simrv::pipeline
