#pragma once

#include <optional>
#include <string_view>

#include "simrv/pipeline/PipelineSim.hpp"

namespace simrv::pipeline {

[[nodiscard]] auto parse_pipeline_type(std::string_view name) -> std::optional<PipelineType>;
[[nodiscard]] auto pipeline_type_name(PipelineType type) -> std::string_view;

}  // namespace simrv::pipeline
