#pragma once

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

#include "simrv/core/Logger.hpp"
#include "simrv/pipeline/PipelineConfig.hpp"
#include "simrv/pipeline/PipelineSim.hpp"

namespace simrv::core {

inline auto trim(const std::string& str) -> std::string {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

inline auto load_cpu_config(const std::string& path, simrv::pipeline::CpuConfig& config) -> bool {
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
            if (key == "pipeline_type") {
                const auto parsed = simrv::pipeline::parse_pipeline_type(val_str);
                if (!parsed) {
                    simrv::log::warn("Unsupported pipeline '{}' in CPU config", val_str);
                    return false;
                }
                config.pipeline_type = *parsed;
                continue;
            }
            if (key == "bpred_type" || key == "branch_predictor" || key == "bpred") {
                const auto parsed = simrv::pipeline::parse_branch_predictor_type(val_str);
                if (!parsed) {
                    simrv::log::warn("Unsupported branch predictor '{}' in CPU config", val_str);
                    return false;
                }
                config.branch_predictor.type = *parsed;
                continue;
            }

            uint32_t val = static_cast<uint32_t>(std::stoul(val_str));
            if (key == "mul_latency") {
                config.mul_latency = val;
            } else if (key == "div_latency") {
                config.div_latency = val;
            } else if (key == "fp_alu_latency") {
                config.fp_alu_latency = val;
            } else if (key == "fp_div_latency") {
                config.fp_div_latency = val;
            } else if (key == "csr_flush_penalty") {
                config.csr_flush_penalty = val;
            } else if (key == "fence_flush_penalty") {
                config.fence_flush_penalty = val;
            } else if (key == "enable_forwarding") {
                config.enable_forwarding = (val != 0);
            } else if (key == "bht_size" || key == "bht_entries") {
                config.branch_predictor.bht_entries = val;
            } else if (key == "btb_size" || key == "btb_entries") {
                config.branch_predictor.btb_entries = val;
            } else if (key == "ras_size" || key == "ras_entries") {
                config.branch_predictor.ras_entries = val;
            } else if (key == "ghr_bits") {
                config.branch_predictor.ghr_bits = val;
            } else if (key == "enable_btb") {
                config.branch_predictor.enable_btb = (val != 0);
            } else if (key == "enable_ras") {
                config.branch_predictor.enable_ras = (val != 0);
            } else {
                simrv::log::warn("Unknown CPU config key: {}", key);
            }
        } catch (const std::exception& e) {
            simrv::log::warn("Failed to parse value '{}' for key '{}': {}", val_str, key, e.what());
        }
    }
    return true;
}

}  // namespace simrv::core
