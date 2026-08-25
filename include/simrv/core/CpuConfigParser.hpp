#pragma once

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

#include "simrv/core/Logger.hpp"
#include "simrv/pipeline/PipelineConfig.hpp"
#include "simrv/pipeline/CpuModel.hpp"
#include "simrv/pipeline/PipelineSim.hpp"

namespace simrv::core {

inline auto trim(const std::string& str) -> std::string {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

inline auto load_cpu_config(const std::string& path, simrv::pipeline::CpuModelConfig& config)
    -> bool {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Strip comments
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        comment_pos = line.find(';');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }

        line = trim(line);
        if (line.empty()) {
            continue;
        }

        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            continue;
        }

        std::string key = trim(line.substr(0, eq_pos));
        std::string val_str = trim(line.substr(eq_pos + 1));

        if (key.empty() || val_str.empty()) {
            continue;
        }

        try {
            if (key == "profile" || key == "cpu_profile") {
                const auto parsed = simrv::pipeline::parse_cpu_model_profile(val_str);
                if (!parsed || *parsed == simrv::pipeline::CpuModelProfile::Custom) {
                    simrv::log::warn("Unsupported CPU model profile '{}'", val_str);
                    return false;
                }
                config = simrv::pipeline::make_cpu_model_profile(*parsed);
                continue;
            }
            // A field below is an explicit override of the selected preset.  The resulting
            // configuration remains fully reproducible, but is correctly labelled custom.
            config.profile = simrv::pipeline::CpuModelProfile::Custom;
            if (key == "pipeline_type") {
                const auto parsed = simrv::pipeline::parse_pipeline_type(val_str);
                if (!parsed) {
                    simrv::log::warn("Unsupported pipeline '{}' in CPU config", val_str);
                    return false;
                }
                config.pipeline.pipeline_type = *parsed;
                continue;
            }
            if (key == "bpred_type" || key == "branch_predictor" || key == "bpred") {
                const auto parsed = simrv::pipeline::parse_branch_predictor_type(val_str);
                if (!parsed) {
                    simrv::log::warn("Unsupported branch predictor '{}' in CPU config", val_str);
                    return false;
                }
                config.pipeline.branch_predictor.type = *parsed;
                continue;
            }

            uint32_t val = static_cast<uint32_t>(std::stoul(val_str));
            if (key == "mul_latency") {
                config.pipeline.mul_latency = val;
            } else if (key == "div_latency") {
                config.pipeline.div_latency = val;
            } else if (key == "fp_alu_latency") {
                config.pipeline.fp_alu_latency = val;
            } else if (key == "fp_div_latency") {
                config.pipeline.fp_div_latency = val;
            } else if (key == "csr_flush_penalty") {
                config.pipeline.csr_flush_penalty = val;
            } else if (key == "fence_flush_penalty") {
                config.pipeline.fence_flush_penalty = val;
            } else if (key == "enable_forwarding") {
                config.pipeline.enable_forwarding = (val != 0);
            } else if (key == "bht_size" || key == "bht_entries") {
                config.pipeline.branch_predictor.bht_entries = val;
            } else if (key == "btb_size" || key == "btb_entries") {
                config.pipeline.branch_predictor.btb_entries = val;
            } else if (key == "ras_size" || key == "ras_entries") {
                config.pipeline.branch_predictor.ras_entries = val;
            } else if (key == "ghr_bits") {
                config.pipeline.branch_predictor.ghr_bits = val;
            } else if (key == "enable_btb") {
                config.pipeline.branch_predictor.enable_btb = (val != 0);
            } else if (key == "enable_ras") {
                config.pipeline.branch_predictor.enable_ras = (val != 0);
            } else if (key == "icache_capacity" || key == "icache_capacity_bytes") {
                config.instruction_cache.capacity_bytes = val;
            } else if (key == "dcache_capacity" || key == "dcache_capacity_bytes") {
                config.data_cache.capacity_bytes = val;
            } else if (key == "icache_associativity") {
                config.instruction_cache.associativity = val;
            } else if (key == "dcache_associativity") {
                config.data_cache.associativity = val;
            } else if (key == "cache_line_bytes" || key == "line_bytes") {
                config.instruction_cache.line_bytes = val;
                config.data_cache.line_bytes = val;
            } else if (key == "icache_line_bytes") {
                config.instruction_cache.line_bytes = val;
            } else if (key == "dcache_line_bytes") {
                config.data_cache.line_bytes = val;
            } else if (key == "icache_hit_latency") {
                config.instruction_cache.hit_latency = val;
            } else if (key == "dcache_hit_latency") {
                config.data_cache.hit_latency = val;
            } else if (key == "icache_miss_latency") {
                config.instruction_cache.miss_latency = val;
            } else if (key == "dcache_miss_latency") {
                config.data_cache.miss_latency = val;
            } else if (key == "interconnect_request_latency") {
                config.interconnect.request_latency = val;
            } else if (key == "interconnect_response_latency") {
                config.interconnect.response_latency = val;
            } else if (key == "enable_idle_spans") {
                config.enable_idle_spans = (val != 0);
            } else {
                simrv::log::warn("Unknown CPU config key: {}", key);
            }
        } catch (const std::exception& e) {
            simrv::log::warn("Failed to parse value '{}' for key '{}': {}", val_str, key, e.what());
        }
    }
    if (const auto valid = config.validate(); !valid) {
        simrv::log::warn("Invalid CPU configuration: {}", valid.error());
        return false;
    }
    return true;
}

/// Compatibility entry point for callers that only need pipeline timing.
inline auto load_cpu_config(const std::string& path, simrv::pipeline::CpuConfig& config) -> bool {
    simrv::pipeline::CpuModelConfig model{};
    model.pipeline = config;
    if (!load_cpu_config(path, model)) return false;
    config = model.pipeline;
    return true;
}

}  // namespace simrv::core
