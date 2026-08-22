/**
 * @file PipelineFactory.hpp
 * @brief Factory for instantiating different RISC-V pipeline microarchitecture models.
 */
#pragma once

#include <memory>
#include <string_view>

#include "simrv/pipeline/PipelineModel.hpp"
#include "simrv/pipeline/PipelineSim.hpp"

namespace simrv::pipeline {

/**
 * @brief Factory function to create a pipeline model instance.
 * @param type The selected pipeline microarchitecture (FiveStage, ThreeStage, DualIssue).
 * @param config Microarchitectural configuration and latency parameters.
 * @return Unique pointer to the created PipelineModel.
 */
auto create_pipeline(PipelineType type, const CpuConfig& config) -> std::unique_ptr<PipelineModel>;

/**
 * @brief Parse a pipeline type name from CLI string.
 * @param name String name (e.g., "5stage", "fivestage", "rocket", "3stage", "threestage", "dual",
 * "dualissue").
 * @return Parsed PipelineType enum value.
 */
auto parse_pipeline_type(std::string_view name) -> PipelineType;

/**
 * @brief Get human-readable name of the pipeline type.
 * @param type PipelineType enum.
 * @return Display name string.
 */
auto pipeline_type_name(PipelineType type) -> std::string_view;

}  // namespace simrv::pipeline
