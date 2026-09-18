#include "simrv/pipeline/PipelineConfig.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace simrv::pipeline {

auto parse_pipeline_type(std::string_view name) -> std::optional<PipelineType> {
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "3stage" || lower == "3-stage" || lower == "3_stage" || lower == "three-stage" ||
        lower == "three_stage" || lower == "3-stage in-order" || lower == "3") {
        return PipelineType::ThreeStage;
    }
    if (lower == "5stage" || lower == "5-stage" || lower == "5_stage" || lower == "five-stage" ||
        lower == "five_stage" || lower == "5-stage in-order" || lower == "5") {
        return PipelineType::FiveStage;
    }
    return std::nullopt;
}

auto pipeline_type_name(PipelineType type) -> std::string_view {
    return type == PipelineType::ThreeStage ? "3-Stage In-Order" : "5-Stage In-Order";
}

}  // namespace simrv::pipeline
