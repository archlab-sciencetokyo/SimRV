#pragma once

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/core/Logger.hpp"
#include "simrv/isa/Base.hpp"
#include "simrv/pipeline/CpuModel.hpp"
#include "simrv/pipeline/PipelineConfig.hpp"
#include "simrv/pipeline/PipelineSim.hpp"

namespace simrv::core {

namespace detail {

inline auto trim(std::string_view str) -> std::string_view {
    const auto first = str.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return "";
    const auto last = str.find_last_not_of(" \t\r\n");
    return str.substr(first, (last - first + 1));
}

inline auto unquote(std::string_view str) -> std::string_view {
    str = trim(str);
    if (str.size() >= 2) {
        if ((str.front() == '"' && str.back() == '"') ||
            (str.front() == '\'' && str.back() == '\'')) {
            return str.substr(1, str.size() - 2);
        }
    }
    return str;
}

inline auto iequals(std::string_view a, std::string_view b) -> bool {
    return std::ranges::equal(a, b, [](char ca, char cb) {
        return std::tolower(static_cast<unsigned char>(ca)) ==
               std::tolower(static_cast<unsigned char>(cb));
    });
}

inline auto parse_bool(std::string_view val) -> std::optional<bool> {
    val = unquote(val);
    if (iequals(val, "true") || iequals(val, "1") || iequals(val, "yes") || iequals(val, "on")) {
        return true;
    }
    if (iequals(val, "false") || iequals(val, "0") || iequals(val, "no") || iequals(val, "off")) {
        return false;
    }
    return std::nullopt;
}

inline auto parse_misa_profile_string(std::string_view val)
    -> std::optional<simrv::isa::MisaProfile> {
    val = unquote(val);
    if (iequals(val, "i") || iequals(val, "rv32i") || iequals(val, "rv64i")) {
        return simrv::isa::MisaProfile::I;
    }
    if (iequals(val, "im") || iequals(val, "rv32im") || iequals(val, "rv64im")) {
        return simrv::isa::MisaProfile::IM;
    }
    if (iequals(val, "ima") || iequals(val, "rv32ima") || iequals(val, "rv64ima")) {
        return simrv::isa::MisaProfile::IMA;
    }
    if (iequals(val, "imac") || iequals(val, "rv32imac") || iequals(val, "rv64imac")) {
        return simrv::isa::MisaProfile::IMAC;
    }
    if (iequals(val, "gc") || iequals(val, "rv32gc") || iequals(val, "rv64gc")) {
        return simrv::isa::MisaProfile::GC;
    }
    if (iequals(val, "gcbv") || iequals(val, "rv32gcbv") || iequals(val, "rv64gcbv")) {
        return simrv::isa::MisaProfile::GCBV;
    }
    return std::nullopt;
}

inline auto misa_profile_name(simrv::isa::MisaProfile profile) -> std::string_view {
    switch (profile) {
        case simrv::isa::MisaProfile::I:
            return "i";
        case simrv::isa::MisaProfile::IM:
            return "im";
        case simrv::isa::MisaProfile::IMA:
            return "ima";
        case simrv::isa::MisaProfile::IMAC:
            return "imac";
        case simrv::isa::MisaProfile::GC:
            return "gc";
        case simrv::isa::MisaProfile::GCBV:
            return "gcbv";
    }
    return "gcbv";
}

}  // namespace detail

/**
 * @brief Canonical lookup resolution for CPU model configuration files.
 *
 * Lookup order:
 * 1. Exact path if existing file.
 * 2. Exact path + ".cfg" if existing file.
 * 3. ./configs/models/<name>.cfg
 * 4. $SIMRV_CONFIG_DIR/models/<name>.cfg or $SIMRV_CONFIG_DIR/<name>.cfg
 */
inline auto resolve_cpu_model_path(std::string_view name_or_path) -> std::optional<std::string> {
    if (name_or_path.empty()) return std::nullopt;

    std::error_code ec;
    std::filesystem::path p(name_or_path);
    if (std::filesystem::is_regular_file(p, ec)) {
        return p.string();
    }

    std::filesystem::path p_ext = p;
    p_ext += ".cfg";
    if (std::filesystem::is_regular_file(p_ext, ec)) {
        return p_ext.string();
    }

    const std::vector<std::filesystem::path> search_roots = {
        std::filesystem::path("."), std::filesystem::path(".."), std::filesystem::path("../..")};

    for (const auto& root : search_roots) {
        const std::filesystem::path rel_direct = root / p;
        if (std::filesystem::is_regular_file(rel_direct, ec)) {
            return rel_direct.string();
        }
        const std::filesystem::path rel_direct_ext = root / (std::string(name_or_path) + ".cfg");
        if (std::filesystem::is_regular_file(rel_direct_ext, ec)) {
            return rel_direct_ext.string();
        }
        const std::filesystem::path rel_models =
            root / "configs" / "models" / (std::string(name_or_path) + ".cfg");
        if (std::filesystem::is_regular_file(rel_models, ec)) {
            return rel_models.string();
        }
    }

#if defined(__linux__)
    const std::filesystem::path exe_path = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec && !exe_path.empty()) {
        const auto exe_dir = exe_path.parent_path();
        const std::vector<std::filesystem::path> exe_roots = {exe_dir, exe_dir.parent_path(),
                                                              exe_dir.parent_path().parent_path()};
        for (const auto& root : exe_roots) {
            const std::filesystem::path rel_models =
                root / "configs" / "models" / (std::string(name_or_path) + ".cfg");
            if (std::filesystem::is_regular_file(rel_models, ec)) {
                return rel_models.string();
            }
        }
    }
#endif

    const char* env_dir = std::getenv("SIMRV_CONFIG_DIR");
    if (env_dir != nullptr && *env_dir != '\0') {
        const std::filesystem::path env_p =
            std::filesystem::path(env_dir) / "models" / (std::string(name_or_path) + ".cfg");
        if (std::filesystem::is_regular_file(env_p, ec)) {
            return env_p.string();
        }
        const std::filesystem::path env_direct =
            std::filesystem::path(env_dir) / (std::string(name_or_path) + ".cfg");
        if (std::filesystem::is_regular_file(env_direct, ec)) {
            return env_direct.string();
        }
    }

    return std::nullopt;
}

/**
 * @brief Parse CPU model configuration from text stream supporting sections and flat keys.
 *
 * Performs syntactic and model parsing without enforcing simulator architecture validation.
 */
inline auto parse_cpu_config_stream(std::istream& stream, simrv::pipeline::CpuModelConfig& config)
    -> bool {
    enum class Section {
        Global,
        Cpu,
        Pipeline,
        BranchPredictor,
        InstructionCache,
        DataCache,
        Interconnect
    };
    Section current_section = Section::Global;

    std::string line;
    while (std::getline(stream, line)) {
        // Strip comments
        const auto comment_hash = line.find('#');
        if (comment_hash != std::string::npos) line = line.substr(0, comment_hash);
        const auto comment_semi = line.find(';');
        if (comment_semi != std::string::npos) line = line.substr(0, comment_semi);

        const auto trimmed = detail::trim(line);
        if (trimmed.empty()) continue;

        // Section headers: [section_name]
        if (trimmed.front() == '[' && trimmed.back() == ']') {
            const auto sec_name = detail::trim(trimmed.substr(1, trimmed.size() - 2));
            if (detail::iequals(sec_name, "cpu") || detail::iequals(sec_name, "model")) {
                current_section = Section::Cpu;
            } else if (detail::iequals(sec_name, "pipeline") || detail::iequals(sec_name, "core")) {
                current_section = Section::Pipeline;
            } else if (detail::iequals(sec_name, "branch_predictor") ||
                       detail::iequals(sec_name, "bpu") || detail::iequals(sec_name, "bpred")) {
                current_section = Section::BranchPredictor;
            } else if (detail::iequals(sec_name, "instruction_cache") ||
                       detail::iequals(sec_name, "icache") ||
                       detail::iequals(sec_name, "cache.instruction")) {
                current_section = Section::InstructionCache;
            } else if (detail::iequals(sec_name, "data_cache") ||
                       detail::iequals(sec_name, "dcache") ||
                       detail::iequals(sec_name, "cache.data")) {
                current_section = Section::DataCache;
            } else if (detail::iequals(sec_name, "interconnect") ||
                       detail::iequals(sec_name, "bus") || detail::iequals(sec_name, "fabric")) {
                current_section = Section::Interconnect;
            } else {
                simrv::log::warn("Unknown CPU config section: [{}]", sec_name);
            }
            continue;
        }

        const auto eq_pos = trimmed.find('=');
        if (eq_pos == std::string_view::npos) continue;

        const auto key = detail::trim(trimmed.substr(0, eq_pos));
        const auto val_raw = detail::trim(trimmed.substr(eq_pos + 1));
        const auto val_str = detail::unquote(val_raw);
        if (key.empty() || val_str.empty()) continue;

        try {
            // 1. CPU Section & Profile Presets
            if (key == "profile" || key == "cpu_profile") {
                const auto parsed = simrv::pipeline::parse_cpu_model_profile(val_str);
                if (!parsed || *parsed == simrv::pipeline::CpuModelProfile::Custom) {
                    simrv::log::warn("Unsupported CPU model profile '{}'", val_str);
                    return false;
                }
                config = simrv::pipeline::make_cpu_model_profile(*parsed);
                continue;
            }
            if (key == "name" || key == "model_name") {
                config.name = std::string(val_str);
                const auto parsed = simrv::pipeline::parse_cpu_model_profile(val_str);
                if (parsed.has_value()) {
                    config.profile = *parsed;
                } else {
                    config.profile = simrv::pipeline::CpuModelProfile::Custom;
                }
                continue;
            }
            if (key == "xlen" || key == "supported_xlen") {
                config.supported_xlen = static_cast<uint8_t>(std::stoul(std::string(val_str)));
                continue;
            }
            if (key == "misa" || key == "isa" || key == "misa_profile") {
                const auto parsed = detail::parse_misa_profile_string(val_str);
                if (parsed.has_value()) {
                    config.misa_profile = *parsed;
                } else {
                    simrv::log::warn("Unknown MISA profile '{}'", val_str);
                }
                const auto unquoted = detail::unquote(val_str);
                if (unquoted.starts_with("rv32") || unquoted.starts_with("RV32")) {
                    if (config.supported_xlen == 0) config.supported_xlen = 32;
                } else if (unquoted.starts_with("rv64") || unquoted.starts_with("RV64")) {
                    if (config.supported_xlen == 0) config.supported_xlen = 64;
                }
                continue;
            }
            if (key == "description") {
                config.description = std::string(val_str);
                continue;
            }

            // Explicit overrides mark profile as custom unless matched
            if (config.profile != simrv::pipeline::CpuModelProfile::Tiny &&
                config.profile != simrv::pipeline::CpuModelProfile::Balanced &&
                config.profile != simrv::pipeline::CpuModelProfile::Performance) {
                config.profile = simrv::pipeline::CpuModelProfile::Custom;
            }

            // 2. Pipeline settings
            if (key == "pipeline_type" || (current_section == Section::Pipeline && key == "type")) {
                const auto parsed = simrv::pipeline::parse_pipeline_type(val_str);
                if (!parsed) {
                    simrv::log::warn("Unsupported pipeline '{}' in CPU config", val_str);
                    return false;
                }
                config.pipeline.pipeline_type = *parsed;
                continue;
            }
            if (key == "enable_forwarding" || key == "forwarding") {
                if (const auto b = detail::parse_bool(val_str); b.has_value()) {
                    config.pipeline.enable_forwarding = *b;
                }
                continue;
            }
            if (key == "mul_latency") {
                config.pipeline.mul_latency =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
                continue;
            }
            if (key == "div_latency") {
                config.pipeline.div_latency =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
                continue;
            }
            if (key == "fp_alu_latency") {
                config.pipeline.fp_alu_latency =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
                continue;
            }
            if (key == "fp_div_latency") {
                config.pipeline.fp_div_latency =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
                continue;
            }
            if (key == "branch_mispredict_penalty" || key == "mispredict_penalty") {
                config.pipeline.branch_mispredict_penalty =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
                continue;
            }
            if (key == "cycle_counter_start_delay") {
                config.pipeline.cycle_counter_start_delay =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
                continue;
            }
            if (key == "csr_flush_penalty") {
                config.pipeline.csr_flush_penalty =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
                continue;
            }
            if (key == "fence_flush_penalty") {
                config.pipeline.fence_flush_penalty =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
                continue;
            }

            // 3. Branch Predictor settings
            if (key == "bpred_type" || key == "branch_predictor" || key == "bpred" ||
                (current_section == Section::BranchPredictor && key == "type")) {
                const auto parsed = simrv::pipeline::parse_branch_predictor_type(val_str);
                if (!parsed) {
                    simrv::log::warn("Unsupported branch predictor '{}' in CPU config", val_str);
                    return false;
                }
                config.pipeline.branch_predictor.type = *parsed;
                continue;
            }
            if (key == "bht_size" || key == "bht_entries") {
                config.pipeline.branch_predictor.bht_entries =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
                continue;
            }
            if (key == "btb_size" || key == "btb_entries") {
                config.pipeline.branch_predictor.btb_entries =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
                continue;
            }
            if (key == "ras_size" || key == "ras_entries") {
                config.pipeline.branch_predictor.ras_entries =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
                continue;
            }
            if (key == "ghr_bits") {
                config.pipeline.branch_predictor.ghr_bits =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
                continue;
            }
            if (key == "pc_shift") {
                config.pipeline.branch_predictor.pc_shift =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
                continue;
            }
            if (key == "bht_initial_state") {
                config.pipeline.branch_predictor.bht_initial_state =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
                continue;
            }
            if (key == "enable_btb") {
                if (const auto b = detail::parse_bool(val_str); b.has_value()) {
                    config.pipeline.branch_predictor.enable_btb = *b;
                }
                continue;
            }
            if (key == "enable_ras") {
                if (const auto b = detail::parse_bool(val_str); b.has_value()) {
                    config.pipeline.branch_predictor.enable_ras = *b;
                }
                continue;
            }
            if (key == "untagged_btb") {
                if (const auto b = detail::parse_bool(val_str); b.has_value()) {
                    config.pipeline.branch_predictor.untagged_btb = *b;
                }
                continue;
            }
            if (key == "registered_btb_read") {
                if (const auto b = detail::parse_bool(val_str); b.has_value()) {
                    config.pipeline.branch_predictor.registered_btb_read = *b;
                }
                continue;
            }

            // 4. Cache settings
            if (current_section == Section::InstructionCache) {
                if (key == "capacity_bytes" || key == "capacity") {
                    config.instruction_cache.capacity_bytes =
                        static_cast<uint32_t>(std::stoul(std::string(val_str)));
                    continue;
                }
                if (key == "associativity" || key == "ways") {
                    config.instruction_cache.associativity =
                        static_cast<uint32_t>(std::stoul(std::string(val_str)));
                    continue;
                }
                if (key == "line_bytes" || key == "line_size") {
                    config.instruction_cache.line_bytes =
                        static_cast<uint32_t>(std::stoul(std::string(val_str)));
                    continue;
                }
                if (key == "hit_latency") {
                    config.instruction_cache.hit_latency =
                        static_cast<uint32_t>(std::stoul(std::string(val_str)));
                    continue;
                }
                if (key == "miss_latency") {
                    config.instruction_cache.miss_latency =
                        static_cast<uint32_t>(std::stoul(std::string(val_str)));
                    continue;
                }
            } else if (current_section == Section::DataCache) {
                if (key == "capacity_bytes" || key == "capacity") {
                    config.data_cache.capacity_bytes =
                        static_cast<uint32_t>(std::stoul(std::string(val_str)));
                    continue;
                }
                if (key == "associativity" || key == "ways") {
                    config.data_cache.associativity =
                        static_cast<uint32_t>(std::stoul(std::string(val_str)));
                    continue;
                }
                if (key == "line_bytes" || key == "line_size") {
                    config.data_cache.line_bytes =
                        static_cast<uint32_t>(std::stoul(std::string(val_str)));
                    continue;
                }
                if (key == "hit_latency") {
                    config.data_cache.hit_latency =
                        static_cast<uint32_t>(std::stoul(std::string(val_str)));
                    continue;
                }
                if (key == "miss_latency") {
                    config.data_cache.miss_latency =
                        static_cast<uint32_t>(std::stoul(std::string(val_str)));
                    continue;
                }
            }

            // Flat cache keys
            if (key == "icache_capacity" || key == "icache_capacity_bytes") {
                config.instruction_cache.capacity_bytes =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
            } else if (key == "dcache_capacity" || key == "dcache_capacity_bytes") {
                config.data_cache.capacity_bytes =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
            } else if (key == "icache_associativity") {
                config.instruction_cache.associativity =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
            } else if (key == "dcache_associativity") {
                config.data_cache.associativity =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
            } else if (key == "cache_line_bytes" || key == "line_bytes") {
                const auto val = static_cast<uint32_t>(std::stoul(std::string(val_str)));
                config.instruction_cache.line_bytes = val;
                config.data_cache.line_bytes = val;
            } else if (key == "icache_line_bytes") {
                config.instruction_cache.line_bytes =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
            } else if (key == "dcache_line_bytes") {
                config.data_cache.line_bytes =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
            } else if (key == "icache_hit_latency") {
                config.instruction_cache.hit_latency =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
            } else if (key == "dcache_hit_latency") {
                config.data_cache.hit_latency =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
            } else if (key == "icache_miss_latency") {
                config.instruction_cache.miss_latency =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
            } else if (key == "dcache_miss_latency") {
                config.data_cache.miss_latency =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
            } else if (key == "interconnect_request_latency" ||
                       (current_section == Section::Interconnect && key == "request_latency")) {
                config.interconnect.request_latency =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
            } else if (key == "interconnect_response_latency" ||
                       (current_section == Section::Interconnect && key == "response_latency")) {
                config.interconnect.response_latency =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
            } else if (key == "enable_idle_spans") {
                if (const auto b = detail::parse_bool(val_str); b.has_value()) {
                    config.enable_idle_spans = *b;
                }
            } else {
                simrv::log::warn("Unknown CPU config key: {}", key);
            }
        } catch (const std::exception& e) {
            simrv::log::warn("Failed to parse value '{}' for key '{}': {}", val_str, key, e.what());
        }
    }

    return true;
}

/**
 * @brief Parse CPU configuration from an in-memory string without validation against machine XLEN.
 */
inline auto parse_cpu_config_string(std::string_view content,
                                    simrv::pipeline::CpuModelConfig& config) -> bool {
    std::istringstream stream{std::string(content)};
    return parse_cpu_config_stream(stream, config);
}

/**
 * @brief Parse CPU configuration from a filesystem path without validation against machine XLEN.
 */
inline auto parse_cpu_config(const std::filesystem::path& path,
                             simrv::pipeline::CpuModelConfig& config) -> bool {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }
    return parse_cpu_config_stream(file, config);
}

/**
 * @brief Parse CPU configuration from a string path without validation against machine XLEN.
 */
inline auto parse_cpu_config(const std::string& path, simrv::pipeline::CpuModelConfig& config)
    -> bool {
    return parse_cpu_config(std::filesystem::path(path), config);
}

/**
 * @brief Load CPU configuration from a stream and validate against the current simulator.
 */
inline auto load_cpu_config_stream(std::istream& stream, simrv::pipeline::CpuModelConfig& config)
    -> bool {
    if (!parse_cpu_config_stream(stream, config)) {
        return false;
    }
    const auto valid = config.validate();
    if (!valid) {
        simrv::log::warn("Invalid CPU configuration: {}", valid.error());
        return false;
    }
    return true;
}

/**
 * @brief Load CPU configuration from a filesystem path and validate against the current simulator.
 */
inline auto load_cpu_config(const std::filesystem::path& path,
                            simrv::pipeline::CpuModelConfig& config) -> bool {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }
    return load_cpu_config_stream(file, config);
}

/**
 * @brief Load CPU configuration from a string path and validate against the current simulator.
 */
inline auto load_cpu_config(const std::string& path, simrv::pipeline::CpuModelConfig& config)
    -> bool {
    return load_cpu_config(std::filesystem::path(path), config);
}

/**
 * @brief Load CPU configuration from an in-memory string and validate against the current
 * simulator.
 */
inline auto load_cpu_config_string(std::string_view content,
                                   simrv::pipeline::CpuModelConfig& config) -> bool {
    std::istringstream stream{std::string(content)};
    return load_cpu_config_stream(stream, config);
}

/**
 * @brief Compatibility entry point for callers that only need pipeline timing.
 */
inline auto load_cpu_config(const std::filesystem::path& path, simrv::pipeline::CpuConfig& config)
    -> bool {
    simrv::pipeline::CpuModelConfig model{};
    model.pipeline = config;
    if (!load_cpu_config(path, model)) return false;
    config = model.pipeline;
    return true;
}

inline auto load_cpu_config(const std::string& path, simrv::pipeline::CpuConfig& config) -> bool {
    return load_cpu_config(std::filesystem::path(path), config);
}

/**
 * @brief Serialize a CPU model configuration to formatted .cfg INI text.
 */
inline void serialize_cpu_config(const simrv::pipeline::CpuModelConfig& config, std::ostream& out,
                                 std::string_view model_name = "",
                                 std::string_view description = "") {
    const auto name =
        model_name.empty()
            ? (config.name.empty() ? simrv::pipeline::cpu_model_profile_name(config.profile)
                                   : std::string_view(config.name))
            : model_name;

    out << "# SimRV CPU Model Configuration\n";
    out << "[cpu]\n";
    out << "name = \"" << name << "\"\n";
    const auto desc = description.empty() ? config.description : std::string(description);
    if (!desc.empty()) {
        out << "description = \"" << desc << "\"\n";
    }
    if (config.supported_xlen != 0) {
        out << "xlen = " << static_cast<unsigned int>(config.supported_xlen) << "\n";
    }
    out << "misa = \"" << detail::misa_profile_name(config.misa_profile) << "\"\n";
    out << "\n";

    out << "[pipeline]\n";
    out << "type = \""
        << (config.pipeline.pipeline_type == simrv::pipeline::PipelineType::ThreeStage
                ? "three-stage"
                : "five-stage")
        << "\"\n";
    out << "enable_forwarding = " << (config.pipeline.enable_forwarding ? "true" : "false") << "\n";
    out << "mul_latency = " << config.pipeline.mul_latency << "\n";
    out << "div_latency = " << config.pipeline.div_latency << "\n";
    out << "fp_alu_latency = " << config.pipeline.fp_alu_latency << "\n";
    out << "fp_div_latency = " << config.pipeline.fp_div_latency << "\n";
    out << "branch_mispredict_penalty = " << config.pipeline.branch_mispredict_penalty << "\n";
    out << "cycle_counter_start_delay = " << config.pipeline.cycle_counter_start_delay << "\n";
    out << "csr_flush_penalty = " << config.pipeline.csr_flush_penalty << "\n";
    out << "fence_flush_penalty = " << config.pipeline.fence_flush_penalty << "\n";
    out << "\n";

    const auto& bp = config.pipeline.branch_predictor;
    out << "[branch_predictor]\n";
    auto bp_name = [](simrv::pipeline::BranchPredictorType t) -> std::string_view {
        switch (t) {
            case simrv::pipeline::BranchPredictorType::Disabled:
                return "none";
            case simrv::pipeline::BranchPredictorType::Static:
                return "static";
            case simrv::pipeline::BranchPredictorType::Bimodal:
                return "bimodal";
            case simrv::pipeline::BranchPredictorType::GShare:
                return "gshare";
            case simrv::pipeline::BranchPredictorType::Tournament:
                return "tournament";
        }
        return "bimodal";
    };
    out << "type = \"" << bp_name(bp.type) << "\"\n";
    out << "btb_entries = " << bp.btb_entries << "\n";
    out << "bht_entries = " << bp.bht_entries << "\n";
    out << "ras_entries = " << bp.ras_entries << "\n";
    out << "ghr_bits = " << bp.ghr_bits << "\n";
    out << "pc_shift = " << static_cast<unsigned int>(bp.pc_shift) << "\n";
    out << "enable_btb = " << (bp.enable_btb ? "true" : "false") << "\n";
    out << "enable_ras = " << (bp.enable_ras ? "true" : "false") << "\n";
    out << "untagged_btb = " << (bp.untagged_btb ? "true" : "false") << "\n";
    out << "registered_btb_read = " << (bp.registered_btb_read ? "true" : "false") << "\n";
    out << "bht_initial_state = " << static_cast<unsigned int>(bp.bht_initial_state) << "\n";
    out << "\n";

    out << "[instruction_cache]\n";
    out << "capacity_bytes = " << config.instruction_cache.capacity_bytes << "\n";
    out << "associativity = " << config.instruction_cache.associativity << "\n";
    out << "line_bytes = " << config.instruction_cache.line_bytes << "\n";
    out << "hit_latency = " << config.instruction_cache.hit_latency << "\n";
    out << "miss_latency = " << config.instruction_cache.miss_latency << "\n";
    out << "\n";

    out << "[data_cache]\n";
    out << "capacity_bytes = " << config.data_cache.capacity_bytes << "\n";
    out << "associativity = " << config.data_cache.associativity << "\n";
    out << "line_bytes = " << config.data_cache.line_bytes << "\n";
    out << "hit_latency = " << config.data_cache.hit_latency << "\n";
    out << "miss_latency = " << config.data_cache.miss_latency << "\n";
    out << "\n";

    out << "[interconnect]\n";
    out << "request_latency = " << config.interconnect.request_latency << "\n";
    out << "response_latency = " << config.interconnect.response_latency << "\n";
}

/**
 * @brief Save a CPU model configuration to a .cfg file path.
 */
inline auto save_cpu_config(const std::string& path, const simrv::pipeline::CpuModelConfig& config,
                            std::string_view model_name = "", std::string_view description = "")
    -> bool {
    std::filesystem::path fs_path(path);
    std::error_code ec;
    if (fs_path.has_parent_path()) {
        std::filesystem::create_directories(fs_path.parent_path(), ec);
    }
    std::ofstream out(path);
    if (!out.is_open()) return false;
    serialize_cpu_config(config, out, model_name, description);
    return true;
}

}  // namespace simrv::core
