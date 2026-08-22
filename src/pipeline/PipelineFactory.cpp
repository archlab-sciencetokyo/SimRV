/**
 * @file PipelineFactory.cpp
 * @brief Implementation of RISC-V pipeline model factory.
 */
#include "simrv/pipeline/PipelineFactory.hpp"

#include <algorithm>
#include <string>

#include "simrv/pipeline/DualIssuePipeline.hpp"
#include "simrv/pipeline/InOrderPipeline.hpp"
#include "simrv/pipeline/ThreeStagePipeline.hpp"

namespace simrv::pipeline {

auto create_pipeline(PipelineType type, const CpuConfig& config) -> std::unique_ptr<PipelineModel> {
    switch (type) {
        case PipelineType::ThreeStage:
            return std::make_unique<ThreeStagePipeline>(config);
        case PipelineType::DualIssue:
            return std::make_unique<DualIssuePipeline>(config);
        case PipelineType::FiveStage:
        default:
            return std::make_unique<InOrderPipeline>(config);
    }
}

auto parse_pipeline_type(std::string_view name) -> PipelineType {
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    if (lower == "3" || lower == "3stage" || lower == "threestage" || lower == "embedded" ||
        lower == "ibex") {
        return PipelineType::ThreeStage;
    }
    if (lower == "dual" || lower == "dualissue" || lower == "superscalar" || lower == "swerv") {
        return PipelineType::DualIssue;
    }
    return PipelineType::FiveStage;
}

auto pipeline_type_name(PipelineType type) -> std::string_view {
    switch (type) {
        case PipelineType::ThreeStage:
            return "3-Stage Embedded (Ibex/E21)";
        case PipelineType::DualIssue:
            return "Dual-Issue Superscalar (SweRV EH1)";
        case PipelineType::FiveStage:
        default:
            return "5-Stage Rocket";
    }
}

}  // namespace simrv::pipeline
