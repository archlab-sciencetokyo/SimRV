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
#include <utility>
#include <vector>

#include "simrv/core/Logger.hpp"
#include "simrv/core/SoCConfig.hpp"
#include "simrv/isa/Base.hpp"
#include "simrv/isa/Zk.hpp"
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

inline auto parse_isa_preset_string(std::string_view val) -> std::optional<simrv::isa::IsaPreset> {
    val = unquote(val);
    if (iequals(val, "i") || iequals(val, "rv32i") || iequals(val, "rv64i")) {
        return simrv::isa::IsaPreset::I;
    }
    if (iequals(val, "e") || iequals(val, "rv32e")) {
        return simrv::isa::IsaPreset::E;
    }
    if (iequals(val, "em") || iequals(val, "rv32em")) {
        return simrv::isa::IsaPreset::EM;
    }
    if (iequals(val, "emac") || iequals(val, "rv32emac")) {
        return simrv::isa::IsaPreset::EMAC;
    }
    if (iequals(val, "im") || iequals(val, "rv32im") || iequals(val, "rv64im")) {
        return simrv::isa::IsaPreset::IM;
    }
    if (iequals(val, "ima") || iequals(val, "rv32ima") || iequals(val, "rv64ima")) {
        return simrv::isa::IsaPreset::IMA;
    }
    if (iequals(val, "imac") || iequals(val, "rv32imac") || iequals(val, "rv64imac")) {
        return simrv::isa::IsaPreset::IMAC;
    }
    if (iequals(val, "g") || iequals(val, "rv32g") || iequals(val, "rv64g")) {
        return simrv::isa::IsaPreset::G;
    }
    if (iequals(val, "gc") || iequals(val, "rv32gc") || iequals(val, "rv64gc")) {
        return simrv::isa::IsaPreset::GC;
    }
    if (iequals(val, "gcbv") || iequals(val, "rv32gcbv") || iequals(val, "rv64gcbv")) {
        return simrv::isa::IsaPreset::GCBV;
    }
    return std::nullopt;
}

inline void add_isa_extension(std::vector<std::string>& extensions, std::string_view value) {
    std::string normalized;
    normalized.reserve(value.size());
    for (const char ch : value) {
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    if (normalized.empty()) return;
    if (simrv::isa::is_zk_extension(normalized)) {
        simrv::isa::add_zk_extension(extensions, normalized);
        return;
    }
    if (std::ranges::find(extensions, normalized) == extensions.end()) {
        extensions.push_back(std::move(normalized));
    }
}

inline void parse_isa_extensions(std::vector<std::string>& extensions, std::string_view value) {
    value = unquote(value);
    std::string token;
    for (const char ch : value) {
        if (ch == ',' || ch == '_' || ch == ' ' || ch == '\t') {
            add_isa_extension(extensions, token);
            token.clear();
        } else {
            token.push_back(ch);
        }
    }
    add_isa_extension(extensions, token);
}

inline auto parse_scaled_u64(std::string_view value) -> std::optional<uint64_t> {
    value = unquote(value);
    if (value.empty()) return std::nullopt;
    uint64_t multiplier = 1;
    const char suffix = static_cast<char>(std::tolower(static_cast<unsigned char>(value.back())));
    if (suffix == 'k' || suffix == 'm' || suffix == 'g') {
        multiplier = suffix == 'k'   ? 1024ULL
                     : suffix == 'm' ? 1024ULL * 1024ULL
                                     : 1024ULL * 1024ULL * 1024ULL;
        value.remove_suffix(1);
    }
    try {
        return std::stoull(std::string(value), nullptr, 0) * multiplier;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

inline auto isa_preset_name(simrv::isa::IsaPreset preset) -> std::string_view {
    switch (preset) {
        case simrv::isa::IsaPreset::E:
            return "e";
        case simrv::isa::IsaPreset::EM:
            return "em";
        case simrv::isa::IsaPreset::EMAC:
            return "emac";
        case simrv::isa::IsaPreset::I:
            return "i";
        case simrv::isa::IsaPreset::IM:
            return "im";
        case simrv::isa::IsaPreset::IMA:
            return "ima";
        case simrv::isa::IsaPreset::IMAC:
            return "imac";
        case simrv::isa::IsaPreset::G:
            return "g";
        case simrv::isa::IsaPreset::GC:
            return "gc";
        case simrv::isa::IsaPreset::GCBV:
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
        const std::filesystem::path env_root(env_dir);
        for (const auto& root : {env_root, env_root.parent_path()}) {
            const std::filesystem::path env_p = root / p;
            if (std::filesystem::is_regular_file(env_p, ec)) {
                return env_p.string();
            }
            const std::filesystem::path env_p_ext = root / p_ext;
            if (std::filesystem::is_regular_file(env_p_ext, ec)) {
                return env_p_ext.string();
            }
        }
        const std::filesystem::path env_model =
            env_root / "models" / (std::string(name_or_path) + ".cfg");
        if (std::filesystem::is_regular_file(env_model, ec)) {
            return env_model.string();
        }
        const std::filesystem::path env_direct = env_root / (std::string(name_or_path) + ".cfg");
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
        InstructionFrontCache,
        InstructionCache,
        DataCache,
        Interconnect,
        Dma,
        Axi,
        Isa,
        Cfu,
        SocMetadata,
        MemoryMetadata,
        BootMetadata,
        UartMetadata
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
            if (detail::iequals(sec_name, "platform")) {
                simrv::log::warn("Deprecated CPU model section [platform]; use [soc]");
                return false;
            } else if (detail::iequals(sec_name, "uart")) {
                simrv::log::warn("Deprecated device section [uart]; use [device.uart]");
                return false;
            } else if (detail::iequals(sec_name, "cpu") || detail::iequals(sec_name, "model")) {
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
            } else if (detail::iequals(sec_name, "instruction_front_cache") ||
                       detail::iequals(sec_name, "l0_instruction_cache") ||
                       detail::iequals(sec_name, "l0_icache")) {
                current_section = Section::InstructionFrontCache;
            } else if (detail::iequals(sec_name, "data_cache") ||
                       detail::iequals(sec_name, "dcache") ||
                       detail::iequals(sec_name, "cache.data")) {
                current_section = Section::DataCache;
            } else if (detail::iequals(sec_name, "interconnect") ||
                       detail::iequals(sec_name, "bus") || detail::iequals(sec_name, "fabric")) {
                current_section = Section::Interconnect;
            } else if (detail::iequals(sec_name, "dma")) {
                current_section = Section::Dma;
            } else if (detail::iequals(sec_name, "axi") || detail::iequals(sec_name, "bus.axi")) {
                current_section = Section::Axi;
            } else if (detail::iequals(sec_name, "isa") ||
                       detail::iequals(sec_name, "extensions")) {
                current_section = Section::Isa;
            } else if (detail::iequals(sec_name, "cfu") ||
                       detail::iequals(sec_name, "custom_unit")) {
                current_section = Section::Cfu;
            } else if (detail::iequals(sec_name, "soc")) {
                current_section = Section::SocMetadata;
            } else if (detail::iequals(sec_name, "memory")) {
                current_section = Section::MemoryMetadata;
            } else if (detail::iequals(sec_name, "boot")) {
                current_section = Section::BootMetadata;
            } else if (detail::iequals(sec_name, "device.uart")) {
                current_section = Section::UartMetadata;
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

        // SoC metadata is parsed separately by parse_soc_config_stream. Keep the
        // combined preset file valid when it is consumed as a CPU model.
        if (current_section == Section::SocMetadata || current_section == Section::MemoryMetadata ||
            current_section == Section::BootMetadata || current_section == Section::UartMetadata) {
            continue;
        }

        try {
            // 1. CPU Section & Presets
            if (key == "preset" || key == "cpu_preset") {
                const auto parsed = simrv::pipeline::parse_cpu_model_preset(val_str);
                if (!parsed || *parsed == simrv::pipeline::CpuModelPreset::Custom) {
                    simrv::log::warn("Unsupported CPU model preset '{}'", val_str);
                    return false;
                }
                config = simrv::pipeline::make_cpu_model_preset(*parsed);
                continue;
            }
            if (key == "name" || key == "model_name") {
                config.name = std::string(val_str);
                const auto parsed = simrv::pipeline::parse_cpu_model_preset(val_str);
                if (parsed.has_value()) {
                    config.preset = *parsed;
                } else {
                    config.preset = simrv::pipeline::CpuModelPreset::Custom;
                }
                continue;
            }
            if (key == "xlen" || key == "supported_xlen") {
                config.supported_xlen = static_cast<uint8_t>(std::stoul(std::string(val_str)));
                continue;
            }
            if (key == "isa_preset") {
                const auto parsed = detail::parse_isa_preset_string(val_str);
                const auto separator = val_str.find_first_of("_ ,\t");
                const auto base = val_str.substr(0, separator);
                const auto base_preset = detail::parse_isa_preset_string(base);
                if (parsed.has_value()) {
                    config.isa_preset = *parsed;
                } else {
                    // Accept strings such as "im_zkn_zkt" while retaining the base
                    // ISA preset for the single-letter portion.
                    if (base_preset.has_value()) {
                        config.isa_preset = *base_preset;
                    } else {
                        simrv::log::warn("Unknown ISA preset '{}'", val_str);
                    }
                    if (separator != std::string_view::npos) {
                        detail::parse_isa_extensions(config.isa_extensions,
                                                     val_str.substr(separator + 1));
                    }
                }
                const auto unquoted = detail::unquote(val_str);
                if (unquoted.starts_with("rv32") || unquoted.starts_with("RV32") ||
                    (base_preset.has_value() && (*base_preset == simrv::isa::IsaPreset::E ||
                                                 *base_preset == simrv::isa::IsaPreset::EM ||
                                                 *base_preset == simrv::isa::IsaPreset::EMAC))) {
                    if (config.supported_xlen == 0) config.supported_xlen = 32;
                } else if (unquoted.starts_with("rv64") || unquoted.starts_with("RV64")) {
                    if (config.supported_xlen == 0) config.supported_xlen = 64;
                }
                continue;
            }
            if ((current_section == Section::Isa && key == "extensions") ||
                key == "isa_extensions") {
                detail::parse_isa_extensions(config.isa_extensions, val_str);
                continue;
            }
            if (key == "description") {
                config.description = std::string(val_str);
                continue;
            }

            if (current_section == Section::Cfu) {
                if (key == "enabled" || key == "enable") {
                    if (const auto b = detail::parse_bool(val_str); b.has_value()) {
                        config.cfu.enabled = *b;
                    }
                    continue;
                }
                if (key == "opcode") {
                    config.cfu.opcode = std::string(val_str);
                    continue;
                }
                if (key == "plugin") {
                    config.cfu.plugin = std::string(val_str);
                    continue;
                }
                if (key == "rtl_module" || key == "module") {
                    config.cfu.rtl_module = std::string(val_str);
                    continue;
                }
                if (key == "interface") {
                    config.cfu.interface = std::string(val_str);
                    continue;
                }
                if (key == "default_latency" || key == "latency") {
                    config.cfu.default_latency =
                        static_cast<uint32_t>(std::stoul(std::string(val_str)));
                    continue;
                }
            }

            // Explicit overrides mark the preset as custom unless matched
            if (config.preset != simrv::pipeline::CpuModelPreset::Tiny &&
                config.preset != simrv::pipeline::CpuModelPreset::Balanced &&
                config.preset != simrv::pipeline::CpuModelPreset::Performance) {
                config.preset = simrv::pipeline::CpuModelPreset::Custom;
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
            if (key == "host_interface_latency") {
                config.pipeline.host_interface_latency =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
                continue;
            }
            if (key == "host_interface_phase_period") {
                config.pipeline.host_interface_phase_period =
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
            if (key == "predict_non_control") {
                if (const auto b = detail::parse_bool(val_str); b.has_value()) {
                    config.pipeline.branch_predictor.predict_non_control = *b;
                }
                continue;
            }
            if (key == "jump_uses_direction_counter") {
                if (const auto b = detail::parse_bool(val_str); b.has_value()) {
                    config.pipeline.branch_predictor.jump_uses_direction_counter = *b;
                }
                continue;
            }
            if (key == "jump_uses_current_btb") {
                if (const auto b = detail::parse_bool(val_str); b.has_value()) {
                    config.pipeline.branch_predictor.jump_uses_current_btb = *b;
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
            if (current_section == Section::InstructionFrontCache) {
                auto& cache = config.instruction_front_cache;
                if (key == "capacity_bytes" || key == "capacity") {
                    cache.capacity_bytes = static_cast<uint32_t>(std::stoul(std::string(val_str)));
                    continue;
                }
                if (key == "associativity" || key == "ways") {
                    cache.associativity = static_cast<uint32_t>(std::stoul(std::string(val_str)));
                    continue;
                }
                if (key == "line_bytes" || key == "line_size") {
                    cache.line_bytes = static_cast<uint32_t>(std::stoul(std::string(val_str)));
                    continue;
                }
                if (key == "hit_latency") {
                    cache.hit_latency = static_cast<uint32_t>(std::stoul(std::string(val_str)));
                    continue;
                }
                if (key == "refill_latency" || key == "miss_latency") {
                    cache.refill_latency = static_cast<uint32_t>(std::stoul(std::string(val_str)));
                    continue;
                }
                if (key == "backing_refill_latency") {
                    cache.backing_refill_latency =
                        static_cast<uint32_t>(std::stoul(std::string(val_str)));
                    continue;
                }
                if (key == "startup_refill_latencies") {
                    cache.startup_refill_latencies.clear();
                    std::istringstream values(std::string(detail::unquote(val_str)));
                    std::string value;
                    while (std::getline(values, value, ',')) {
                        cache.startup_refill_latencies.push_back(
                            static_cast<uint32_t>(std::stoul(value)));
                    }
                    continue;
                }
                if (key == "freeze_pipeline_on_refill") {
                    if (const auto value = detail::parse_bool(val_str); value.has_value()) {
                        cache.freeze_pipeline_on_refill = *value;
                    }
                    continue;
                }
            } else if (current_section == Section::InstructionCache) {
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
            } else if (key == "data_request_latency" && current_section == Section::Interconnect) {
                config.interconnect.data_request_latency =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
            } else if (key == "data_response_latency" && current_section == Section::Interconnect) {
                config.interconnect.data_response_latency =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
            } else if (key == "startup_data_response_latency" &&
                       current_section == Section::Interconnect) {
                config.interconnect.startup_data_response_latency =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
            } else if (current_section == Section::Dma && (key == "enabled" || key == "enable")) {
                if (const auto b = detail::parse_bool(val_str); b.has_value()) {
                    config.dma.enabled = *b;
                }
            } else if (current_section == Section::Dma &&
                       (key == "setup_latency" || key == "setup")) {
                config.dma.setup_latency = static_cast<uint32_t>(std::stoul(std::string(val_str)));
            } else if (current_section == Section::Dma &&
                       (key == "bandwidth_bytes_per_cycle" || key == "bandwidth" ||
                        key == "bytes_per_cycle")) {
                config.dma.bandwidth_bytes_per_cycle =
                    std::max(1u, static_cast<uint32_t>(std::stoul(std::string(val_str))));
            } else if (current_section == Section::Dma &&
                       (key == "memory_contention_penalty" || key == "contention_penalty")) {
                config.dma.memory_contention_penalty =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
            } else if (current_section == Section::Axi && (key == "enabled" || key == "enable")) {
                if (const auto b = detail::parse_bool(val_str); b.has_value()) {
                    config.axi.enabled = *b;
                }
            } else if (current_section == Section::Axi &&
                       (key == "data_width_bytes" || key == "width_bytes" || key == "data_width")) {
                config.axi.data_width_bytes =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
            } else if (current_section == Section::Axi &&
                       (key == "id_width_bits" || key == "id_width")) {
                config.axi.id_width_bits = static_cast<uint32_t>(std::stoul(std::string(val_str)));
            } else if (current_section == Section::Axi && key == "max_outstanding_reads") {
                config.axi.max_outstanding_reads =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
            } else if (current_section == Section::Axi && key == "max_outstanding_writes") {
                config.axi.max_outstanding_writes =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
            } else if (current_section == Section::Axi &&
                       (key == "burst_length_max" || key == "max_burst_length" ||
                        key == "burst_len")) {
                config.axi.burst_length_max =
                    static_cast<uint32_t>(std::stoul(std::string(val_str)));
            } else if (current_section == Section::Axi && (key == "trace_axi" || key == "trace")) {
                if (const auto b = detail::parse_bool(val_str); b.has_value()) {
                    config.axi.trace_axi = *b;
                }
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

/** Parse the platform portion of a combined CPU/SoC preset file. */
inline auto parse_soc_config_stream(std::istream& stream, simrv::core::SoCConfig& config) -> bool {
    enum class Section { Global, Soc, Memory, Boot, Device };
    Section section = Section::Global;
    simrv::core::SoCDeviceConfig* device = nullptr;
    std::string line;
    while (std::getline(stream, line)) {
        const auto comment_hash = line.find('#');
        if (comment_hash != std::string::npos) line = line.substr(0, comment_hash);
        const auto comment_semi = line.find(';');
        if (comment_semi != std::string::npos) line = line.substr(0, comment_semi);
        const auto trimmed = detail::trim(line);
        if (trimmed.empty()) continue;

        if (trimmed.front() == '[' && trimmed.back() == ']') {
            const auto name = detail::trim(trimmed.substr(1, trimmed.size() - 2));
            device = nullptr;
            if (detail::iequals(name, "platform")) {
                simrv::log::warn("Deprecated SoC section [platform]; use [soc]");
                return false;
            } else if (detail::iequals(name, "uart")) {
                simrv::log::warn("Deprecated device section [uart]; use [device.uart]");
                return false;
            } else if (detail::iequals(name, "soc")) {
                section = Section::Soc;
            } else if (detail::iequals(name, "memory")) {
                section = Section::Memory;
            } else if (detail::iequals(name, "boot")) {
                section = Section::Boot;
            } else {
                auto device_name = name;
                const bool explicit_device = device_name.starts_with("device.");
                if (explicit_device) device_name.remove_prefix(7);
                if (explicit_device && soc_device_kind(device_name)) {
                    const auto kind = soc_device_kind(device_name);
                    section = Section::Device;
                    const auto existing = std::ranges::find_if(
                        config.devices, [kind](const auto& item) { return item.kind == *kind; });
                    if (existing == config.devices.end()) {
                        config.devices.push_back(soc_device_default(*kind));
                        device = &config.devices.back();
                    } else {
                        device = &*existing;
                    }
                } else {
                    section = Section::Global;
                }
            }
            continue;
        }

        const auto eq_pos = trimmed.find('=');
        if (eq_pos == std::string_view::npos) continue;
        const auto key = detail::trim(trimmed.substr(0, eq_pos));
        const auto value = detail::unquote(detail::trim(trimmed.substr(eq_pos + 1)));
        if (key.empty() || value.empty()) continue;

        if (section == Section::Soc) {
            if (key == "name")
                config.name = std::string(value);
            else if (key == "cpu_model" || key == "cpu_preset")
                config.cpu_model = std::string(value);
            else if (key == "platform") {
                config.enable_pcie = detail::iequals(value, "pcie");
                config.enable_mmio = detail::iequals(value, "mmio");
            } else if (key == "enable_pcie") {
                if (const auto parsed = detail::parse_bool(value)) config.enable_pcie = *parsed;
            } else if (key == "enable_mmio") {
                if (const auto parsed = detail::parse_bool(value)) config.enable_mmio = *parsed;
            } else if (key == "device_policy") {
                config.disable_unlisted_devices =
                    detail::iequals(value, "explicit") || detail::iequals(value, "listed-only");
            } else if (key == "disable_unlisted_devices") {
                if (const auto parsed = detail::parse_bool(value)) {
                    config.disable_unlisted_devices = *parsed;
                }
            }
        } else if (section == Section::Memory) {
            if (const auto parsed = detail::parse_scaled_u64(value)) {
                if (key == "dram_base" || key == "base")
                    config.dram_base = *parsed;
                else if (key == "dram_size" || key == "size")
                    config.dram_size = *parsed;
            }
        } else if (section == Section::Boot) {
            if (const auto parsed = detail::parse_scaled_u64(value)) {
                if (key == "reset_pc" || key == "start_pc")
                    config.reset_pc = *parsed;
                else if (key == "tohost")
                    config.tohost = *parsed;
            }
        } else if (section == Section::Device && device != nullptr) {
            if (const auto parsed = detail::parse_scaled_u64(value)) {
                if (key == "base")
                    device->base = *parsed;
                else if (key == "size")
                    device->size = *parsed;
                else if (key == "irq")
                    device->irq = static_cast<uint32_t>(*parsed);
            } else if (key == "name") {
                device->name = std::string(value);
            }
        }
    }
    return true;
}

inline auto parse_soc_config(const std::filesystem::path& path, simrv::core::SoCConfig& config)
    -> bool {
    std::ifstream file(path);
    return file.is_open() && parse_soc_config_stream(file, config);
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
 * @brief Serialize a CPU model configuration to formatted .cfg INI text.
 */
inline void serialize_cpu_config(const simrv::pipeline::CpuModelConfig& config, std::ostream& out,
                                 std::string_view model_name = "",
                                 std::string_view description = "") {
    const auto name =
        model_name.empty()
            ? (config.name.empty() ? simrv::pipeline::cpu_model_preset_name(config.preset)
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
    out << "isa_preset = \"" << detail::isa_preset_name(config.isa_preset) << "\"\n";
    out << "\n";

    if (!config.isa_extensions.empty()) {
        out << "[isa]\n";
        out << "extensions = \"";
        for (size_t index = 0; index < config.isa_extensions.size(); ++index) {
            if (index != 0) out << ',';
            out << config.isa_extensions[index];
        }
        out << "\"\n\n";
    }

    out << "[cfu]\n";
    out << "enabled = " << (config.cfu.enabled ? "true" : "false") << "\n";
    out << "opcode = \"" << config.cfu.opcode << "\"\n";
    out << "default_latency = " << config.cfu.default_latency << "\n";
    if (!config.cfu.plugin.empty()) out << "plugin = \"" << config.cfu.plugin << "\"\n";
    if (!config.cfu.rtl_module.empty()) {
        out << "rtl_module = \"" << config.cfu.rtl_module << "\"\n";
    }
    out << "interface = \"" << config.cfu.interface << "\"\n\n";

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
    out << "host_interface_latency = " << config.pipeline.host_interface_latency << "\n";
    out << "host_interface_phase_period = " << config.pipeline.host_interface_phase_period << "\n";
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
    out << "predict_non_control = " << (bp.predict_non_control ? "true" : "false") << "\n";
    out << "jump_uses_direction_counter = " << (bp.jump_uses_direction_counter ? "true" : "false")
        << "\n";
    out << "jump_uses_current_btb = " << (bp.jump_uses_current_btb ? "true" : "false") << "\n";
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

    if (config.instruction_front_cache.capacity_bytes != 0) {
        out << "[instruction_front_cache]\n";
        out << "capacity_bytes = " << config.instruction_front_cache.capacity_bytes << "\n";
        out << "associativity = " << config.instruction_front_cache.associativity << "\n";
        out << "line_bytes = " << config.instruction_front_cache.line_bytes << "\n";
        out << "hit_latency = " << config.instruction_front_cache.hit_latency << "\n";
        out << "refill_latency = " << config.instruction_front_cache.refill_latency << "\n";
        out << "backing_refill_latency = " << config.instruction_front_cache.backing_refill_latency
            << "\n";
        out << "freeze_pipeline_on_refill = "
            << (config.instruction_front_cache.freeze_pipeline_on_refill ? "true" : "false")
            << "\n";
        if (!config.instruction_front_cache.startup_refill_latencies.empty()) {
            out << "startup_refill_latencies = \"";
            for (size_t index = 0;
                 index < config.instruction_front_cache.startup_refill_latencies.size(); ++index) {
                if (index != 0) out << ',';
                out << config.instruction_front_cache.startup_refill_latencies[index];
            }
            out << "\"\n";
        }
        out << "\n";
    }

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
    if (config.interconnect.data_request_latency != 0) {
        out << "data_request_latency = " << config.interconnect.data_request_latency << "\n";
    }
    if (config.interconnect.data_response_latency != 0) {
        out << "data_response_latency = " << config.interconnect.data_response_latency << "\n";
    }
    if (config.interconnect.startup_data_response_latency != 0) {
        out << "startup_data_response_latency = "
            << config.interconnect.startup_data_response_latency << "\n";
    }
    out << "\n";

    out << "[dma]\n";
    out << "enabled = " << (config.dma.enabled ? "true" : "false") << "\n";
    out << "setup_latency = " << config.dma.setup_latency << "\n";
    out << "bandwidth_bytes_per_cycle = " << config.dma.bandwidth_bytes_per_cycle << "\n";
    out << "memory_contention_penalty = " << config.dma.memory_contention_penalty << "\n\n";

    out << "[axi]\n";
    out << "enabled = " << (config.axi.enabled ? "true" : "false") << "\n";
    out << "data_width_bytes = " << config.axi.data_width_bytes << "\n";
    out << "id_width_bits = " << config.axi.id_width_bits << "\n";
    out << "max_outstanding_reads = " << config.axi.max_outstanding_reads << "\n";
    out << "max_outstanding_writes = " << config.axi.max_outstanding_writes << "\n";
    out << "burst_length_max = " << config.axi.burst_length_max << "\n";
    out << "trace_axi = " << (config.axi.trace_axi ? "true" : "false") << "\n";
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
