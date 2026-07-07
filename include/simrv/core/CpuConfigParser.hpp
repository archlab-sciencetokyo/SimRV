#pragma once

#include <string>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include "simrv/pipeline/PipelineSim.hpp"
#include "simrv/core/Logger.hpp"

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
            // Handle string-valued keys first
            if (key == "bp_type") {
                using BPT = simrv::pipeline::BranchPredictorType;
                if (val_str == "static-not-taken")  config.bp_type = BPT::StaticNotTaken;
                else if (val_str == "static-taken") config.bp_type = BPT::StaticTaken;
                else if (val_str == "1bit")         config.bp_type = BPT::OneBitBimodal;
                else if (val_str == "gshare")       config.bp_type = BPT::Gshare;
                else                                config.bp_type = BPT::TwoBitBimodal;
                continue;
            }

            uint32_t val = static_cast<uint32_t>(std::stoul(val_str));
            if (key == "icache_miss_penalty") {
                config.icache_miss_penalty = val;
            } else if (key == "dcache_miss_penalty") {
                config.dcache_miss_penalty = val;
            } else if (key == "tlb_miss_penalty") {
                config.tlb_miss_penalty = val;
            } else if (key == "mul_latency") {
                config.mul_latency = val;
            } else if (key == "div_latency") {
                config.div_latency = val;
            } else if (key == "branch_mispredict_penalty") {
                config.branch_mispredict_penalty = val;
            } else if (key == "enable_forwarding") {
                config.enable_forwarding = (val != 0);
            } else if (key == "enable_ex_forwarding") {
                config.enable_ex_forwarding = (val != 0);
            } else if (key == "enable_mem_forwarding") {
                config.enable_mem_forwarding = (val != 0);
            } else if (key == "enable_ooo") {
                config.enable_ooo = (val != 0);
            } else if (key == "rob_size") {
                config.rob_size = val;
            } else if (key == "rs_size") {
                config.rs_size = val;
            } else if (key == "lsq_size") {
                config.lsq_size = val;
            } else if (key == "ooo_dispatch_width") {
                config.ooo_dispatch_width = val;
            } else if (key == "ooo_issue_width") {
                config.ooo_issue_width = val;
            } else if (key == "ooo_commit_width") {
                config.ooo_commit_width = val;
            } else if (key == "btb_entries") {
                config.btb_entries = val;
            } else if (key == "global_history_bits") {
                config.global_history_bits = val;
            } else {
                simrv::log::warn("Unknown CPU config key: {}", key);
            }
        } catch (const std::exception& e) {
            simrv::log::warn("Failed to parse value '{}' for key '{}': {}", val_str, key, e.what());
        }
    }
    return true;
}

} // namespace simrv::core
