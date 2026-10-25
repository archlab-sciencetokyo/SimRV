/**
 * @file CliParser.cpp
 * @brief Command-line parser implementations.
 */
#include "simrv/util/CliParser.hpp"

#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdlib>
#include <format>
#include <print>
#include <span>
#include <string>
#include <vector>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/CpuConfigParser.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/PlatformMap.h"
#include "simrv/core/Tracer.hpp"
#include "simrv/pipeline/PipelineConfig.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/util/BuildrootImageBundle.hpp"
#include "simrv/util/FormatUtil.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::util {

using isa::IsaPreset;

namespace {

enum class CliArgumentForm : uint8_t { None, Required, Optional, RequiredThenOptional };

struct HelpEntry {
    std::string_view option;
    std::string_view description;
    std::vector<std::string_view> hidden_aliases;
    bool accepts_equals_value = false;
    bool show_in_help = true;
    std::string_view id;
};

struct HelpSection {
    std::string_view title;
    std::vector<HelpEntry> entries;
};

[[nodiscard]] auto cli_help_sections() -> const std::vector<HelpSection>&;
[[nodiscard]] auto cli_option_id(std::string_view arg) -> std::string_view;
[[nodiscard]] auto cli_argument_form(const HelpEntry& entry) -> CliArgumentForm;
[[nodiscard]] auto cli_short_flag_takes_argument(char c) -> bool;

auto parse_scaled_u64(std::string_view num, uint64_t& out) -> bool {
    if (num.empty()) {
        return false;
    }

    uint64_t multiplier = 1;
    const char last = static_cast<char>(std::tolower(static_cast<unsigned char>(num.back())));
    if (last == 'k') {
        multiplier = 1000ULL;
        num.remove_suffix(1);
    } else if (last == 'm') {
        multiplier = 1000000ULL;
        num.remove_suffix(1);
    } else if (last == 'g') {
        multiplier = 1000000000ULL;
        num.remove_suffix(1);
    }

    if (num.empty()) {
        return false;
    }

    uint64_t value = 0;
    const char* begin = num.data();
    const char* end = num.data() + num.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }
    if (value > std::numeric_limits<uint64_t>::max() / multiplier) {
        return false;
    }
    out = value * multiplier;
    return true;
}

auto parse_frequency_hz(std::string_view num, uint64_t& out) -> bool {
    if (num.empty()) {
        return false;
    }
    if (num.ends_with("hz") || num.ends_with("Hz") || num.ends_with("HZ")) {
        num.remove_suffix(2);
    }
    return parse_scaled_u64(num, out);
}

auto parse_u64_base0(std::string_view num, uint64_t& out) -> bool {
    if (num.empty()) {
        return false;
    }

    uint64_t value = 0;
    int base = 10;
    if (num.starts_with("0x") || num.starts_with("0X")) {
        num.remove_prefix(2);
        base = 16;
    } else if (num.starts_with('0') && num.size() > 1) {
        num.remove_prefix(1);
        base = 8;
    }

    const char* begin = num.data();
    const char* end = num.data() + num.size();
    const auto result = std::from_chars(begin, end, value, base);
    if (result.ec != std::errc{} || result.ptr != end) {
        return false;
    }
    out = value;
    return true;
}

auto parse_u32_base0(std::string_view num, uint32_t& out) -> bool {
    uint64_t value = 0;
    if (!parse_u64_base0(num, value) || value > std::numeric_limits<uint32_t>::max()) return false;
    out = static_cast<uint32_t>(value);
    return true;
}

auto parse_trace_pc_range(std::string_view range, RuntimeOptions& options) -> bool {
    const auto separator = range.find('-');
    if (separator == std::string_view::npos || separator == 0 || separator + 1 >= range.size())
        return false;
    uint64_t start = 0;
    uint64_t end = 0;
    if (!parse_u64_base0(range.substr(0, separator), start) ||
        !parse_u64_base0(range.substr(separator + 1), end) || start > end)
        return false;
    options.trace_pc_start = start;
    options.trace_pc_end = end;
    return true;
}

auto next_argument(std::span<char* const> args, std::size_t& index, std::string_view option_name)
    -> std::expected<std::string_view, std::string> {
    if (index + 1 >= args.size()) {
        return std::unexpected(std::format("missing value for {}", option_name));
    }
    ++index;
    return std::string_view(
        args[index]);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
}

auto parse_scaled_required(std::span<char* const> args, std::size_t& index,
                           std::string_view option_name) -> std::expected<uint64_t, std::string> {
    return next_argument(args, index, option_name)
        .and_then([&](std::string_view value_text) -> std::expected<uint64_t, std::string> {
            uint64_t parsed_value = 0;
            if (!parse_scaled_u64(value_text, parsed_value)) {
                return std::unexpected(std::format("invalid numeric value for {}", option_name));
            }
            return parsed_value;
        });
}

auto parse_u32_required(std::span<char* const> args, std::size_t& index,
                        std::string_view option_name) -> std::expected<uint32_t, std::string> {
    return next_argument(args, index, option_name)
        .and_then([&](std::string_view value_text) -> std::expected<uint32_t, std::string> {
            uint32_t parsed_value = 0;
            if (!parse_u32_base0(value_text, parsed_value)) {
                return std::unexpected(std::format("invalid address value for {}", option_name));
            }
            return parsed_value;
        });
}

auto parse_positive_size(std::span<char* const> args, std::size_t& index, std::string_view option,
                         std::string_view label) -> std::expected<uint32_t, std::string> {
    auto text = next_argument(args, index, option);
    if (!text) return std::unexpected(text.error());
    uint32_t value = 0;
    if (!parse_u32_base0(*text, value) || value == 0)
        return std::unexpected(std::format("invalid {} size '{}'", label, *text));
    return value;
}

auto parse_trace_window(RuntimeOptions& options, std::span<char* const> args, std::size_t& index)
    -> std::expected<void, std::string> {
    return parse_scaled_required(args, index, "-r")
        .and_then([&](uint64_t begin) -> std::expected<void, std::string> {
            return parse_scaled_required(args, index, "-r")
                .and_then([&](uint64_t end) -> std::expected<void, std::string> {
                    if (begin > end) {
                        return std::unexpected("-r/--trace-range begin must be <= end");
                    }
                    options.trace_enabled = true;
                    options.trace_begin = begin;
                    options.trace_end = end;
                    return {};
                });
        });
}

inline auto iequals(std::string_view a, std::string_view b) -> bool {
    return std::ranges::equal(a, b, [](char c1, char c2) -> bool {
        return std::tolower(static_cast<unsigned char>(c1)) ==
               std::tolower(static_cast<unsigned char>(c2));
    });
}

struct ParsedIsaPreset {
    IsaPreset preset;
    unsigned int xlen;
    bool has_zb = false;
};

auto parse_isa_base_preset(std::string_view value)
    -> std::expected<std::optional<ParsedIsaPreset>, std::string> {
    // Reject legacy draft single-letter 'b' shorthands with a clear explanation
    if (iequals(value, "gcbv") || iequals(value, "rv32gcbv") || iequals(value, "rv64gcbv") ||
        iequals(value, "gcb") || iequals(value, "rv32gcb") || iequals(value, "rv64gcb")) {
        return std::unexpected(std::format(
            "legacy shorthand '{}' is not a ratified RISC-V ISA string; use ratified 'gcv', "
            "'gc', or multi-letter extensions like 'rv64gcv_zba_zbb_zbs'",
            value));
    }

    if (iequals(value, "i")) {
        return ParsedIsaPreset{.preset = IsaPreset::I, .xlen = 0};
    }
    if (iequals(value, "e")) {
        return ParsedIsaPreset{.preset = IsaPreset::E, .xlen = 32};
    }
    if (iequals(value, "em")) {
        return ParsedIsaPreset{.preset = IsaPreset::EM, .xlen = 32};
    }
    if (iequals(value, "emac")) {
        return ParsedIsaPreset{.preset = IsaPreset::EMAC, .xlen = 32};
    }
    if (iequals(value, "im")) {
        return ParsedIsaPreset{.preset = IsaPreset::IM, .xlen = 0};
    }
    if (iequals(value, "ima")) {
        return ParsedIsaPreset{.preset = IsaPreset::IMA, .xlen = 0};
    }
    if (iequals(value, "imac")) {
        return ParsedIsaPreset{.preset = IsaPreset::IMAC, .xlen = 0};
    }
    if (iequals(value, "g")) {
        return ParsedIsaPreset{.preset = IsaPreset::G, .xlen = 0};
    }
    if (iequals(value, "gc")) {
        return ParsedIsaPreset{.preset = IsaPreset::GC, .xlen = 0};
    }
    if (iequals(value, "gcv")) {
        return ParsedIsaPreset{.preset = IsaPreset::GCV, .xlen = 0};
    }
    if (iequals(value, "gczb")) {
        return ParsedIsaPreset{.preset = IsaPreset::GC, .xlen = 0, .has_zb = true};
    }
    if (iequals(value, "gczbv")) {
        return ParsedIsaPreset{.preset = IsaPreset::GCV, .xlen = 0, .has_zb = true};
    }

    unsigned int parsed_xlen = 0;
    IsaPreset preset = IsaPreset::GCV;
    bool has_zb = false;
    bool valid = false;

    if (iequals(value, "rv32e")) {
        parsed_xlen = 32;
        preset = IsaPreset::E;
        valid = true;
    } else if (iequals(value, "rv32em")) {
        parsed_xlen = 32;
        preset = IsaPreset::EM;
        valid = true;
    } else if (iequals(value, "rv32emac")) {
        parsed_xlen = 32;
        preset = IsaPreset::EMAC;
        valid = true;
    } else if (iequals(value, "rv32i")) {
        parsed_xlen = 32;
        preset = IsaPreset::I;
        valid = true;
    } else if (iequals(value, "rv64i")) {
        parsed_xlen = 64;
        preset = IsaPreset::I;
        valid = true;
    } else if (iequals(value, "rv32im")) {
        parsed_xlen = 32;
        preset = IsaPreset::IM;
        valid = true;
    } else if (iequals(value, "rv64im")) {
        parsed_xlen = 64;
        preset = IsaPreset::IM;
        valid = true;
    } else if (iequals(value, "rv32ima")) {
        parsed_xlen = 32;
        preset = IsaPreset::IMA;
        valid = true;
    } else if (iequals(value, "rv64ima")) {
        parsed_xlen = 64;
        preset = IsaPreset::IMA;
        valid = true;
    } else if (iequals(value, "rv32imac")) {
        parsed_xlen = 32;
        preset = IsaPreset::IMAC;
        valid = true;
    } else if (iequals(value, "rv64imac")) {
        parsed_xlen = 64;
        preset = IsaPreset::IMAC;
        valid = true;
    } else if (iequals(value, "rv32g")) {
        parsed_xlen = 32;
        preset = IsaPreset::G;
        valid = true;
    } else if (iequals(value, "rv64g")) {
        parsed_xlen = 64;
        preset = IsaPreset::G;
        valid = true;
    } else if (iequals(value, "rv32gc")) {
        parsed_xlen = 32;
        preset = IsaPreset::GC;
        valid = true;
    } else if (iequals(value, "rv64gc")) {
        parsed_xlen = 64;
        preset = IsaPreset::GC;
        valid = true;
    } else if (iequals(value, "rv32gcv")) {
        parsed_xlen = 32;
        preset = IsaPreset::GCV;
        valid = true;
    } else if (iequals(value, "rv64gcv")) {
        parsed_xlen = 64;
        preset = IsaPreset::GCV;
        valid = true;
    } else if (iequals(value, "rv32gczb")) {
        parsed_xlen = 32;
        preset = IsaPreset::GC;
        has_zb = true;
        valid = true;
    } else if (iequals(value, "rv64gczb")) {
        parsed_xlen = 64;
        preset = IsaPreset::GC;
        has_zb = true;
        valid = true;
    } else if (iequals(value, "rv32gczbv")) {
        parsed_xlen = 32;
        preset = IsaPreset::GCV;
        has_zb = true;
        valid = true;
    } else if (iequals(value, "rv64gczbv")) {
        parsed_xlen = 64;
        preset = IsaPreset::GCV;
        has_zb = true;
        valid = true;
    }

    if (valid) {
        if (parsed_xlen == 64 && simrv::xlen::kXLenBits == 32) {
            return std::unexpected(std::format(
                "cannot run a 64-bit ISA preset ({}) on a 32-bit simulator build", value));
        }
        return ParsedIsaPreset{.preset = preset, .xlen = parsed_xlen, .has_zb = has_zb};
    }

    return std::nullopt;
}

auto parse_isa_preset(std::string_view value) -> std::expected<ParsedIsaPreset, std::string> {
    auto base_match = parse_isa_base_preset(value);
    if (!base_match) return std::unexpected(base_match.error());
    if (*base_match) return **base_match;

    // Standard ISA strings may attach their first multi-letter extension directly
    // to the single-letter base (e.g. RV64GCZicntr2p0 or RV64GCZba). Underscores separate later
    // multi-letter extensions.
    const auto extension_start = value.find_first_of("zZxX", 4);
    if (extension_start != std::string_view::npos) {
        auto base_end = extension_start;
        if (base_end > 0 && value[base_end - 1] == '_') --base_end;
        const auto base = value.substr(0, base_end);
        auto parsed = parse_isa_preset(base);
        if (!parsed) return parsed;
        auto extensions = value.substr(extension_start);
        if (extensions.empty() || extensions.back() == '_') {
            return std::unexpected("ISA string contains an empty extension name");
        }
        uint64_t seen_extensions = 0;
        bool has_zb = false;
        bool has_v = false;
        while (!extensions.empty()) {
            const auto next = extensions.find('_');
            const auto extension = extensions.substr(0, next);
            const auto supported_extension = [extension](std::string_view name) -> bool {
                if (iequals(extension, name)) return true;
                if (extension.size() <= name.size() ||
                    !iequals(extension.substr(0, name.size()), name))
                    return false;
                const auto version = extension.substr(name.size());
                return iequals(version, "1") || iequals(version, "1p0") || iequals(version, "2") ||
                       iequals(version, "2p0");
            };
            const auto extension_id = [&]() -> uint64_t {
                // CSR, Fence, Counters, Performance Monitoring
                if (supported_extension("zicntr")) return 1ULL << 0;
                if (supported_extension("zicsr")) return 1ULL << 1;
                if (supported_extension("zifencei")) return 1ULL << 2;
                if (supported_extension("zihpm")) return 1ULL << 3;

                // Ratified Bitmanip (Zb)
                if (supported_extension("zba")) return 1ULL << 4;
                if (supported_extension("zbb")) return 1ULL << 5;
                if (supported_extension("zbc")) return 1ULL << 6;
                if (supported_extension("zbs")) return 1ULL << 7;
                if (supported_extension("zb")) return 1ULL << 8;

                // Cryptography Bitmanip & Scalar Crypto
                if (supported_extension("zbkb")) return 1ULL << 9;
                if (supported_extension("zbkc")) return 1ULL << 10;
                if (supported_extension("zbkx")) return 1ULL << 11;
                if (supported_extension("zknd")) return 1ULL << 12;
                if (supported_extension("zkne")) return 1ULL << 13;
                if (supported_extension("zknh")) return 1ULL << 14;
                if (supported_extension("zksed")) return 1ULL << 15;
                if (supported_extension("zksh")) return 1ULL << 16;
                if (supported_extension("zkr")) return 1ULL << 17;
                if (supported_extension("zkt")) return 1ULL << 18;
                if (supported_extension("zkn")) return 1ULL << 19;
                if (supported_extension("zks")) return 1ULL << 20;
                if (supported_extension("zk")) return 1ULL << 21;

                // Vector Sub-extensions
                if (supported_extension("zve32x")) return 1ULL << 22;
                if (supported_extension("zve32f")) return 1ULL << 23;
                if (supported_extension("zve64x")) return 1ULL << 24;
                if (supported_extension("zve64f")) return 1ULL << 25;
                if (supported_extension("zve64d")) return 1ULL << 26;
                if (supported_extension("zvl32b")) return 1ULL << 27;
                if (supported_extension("zvl64b")) return 1ULL << 28;
                if (supported_extension("zvl128b")) return 1ULL << 29;
                if (supported_extension("zvl256b")) return 1ULL << 30;
                if (supported_extension("zvl512b")) return 1ULL << 31;
                if (supported_extension("zvl1024b")) return 1ULL << 32;

                // Ratified Hints, Timers & Cache Management
                if (supported_extension("zihintpause")) return 1ULL << 34;
                if (supported_extension("sstc")) return 1ULL << 35;
                if (supported_extension("zicbom")) return 1ULL << 36;
                if (supported_extension("zicboz")) return 1ULL << 37;
                if (supported_extension("zicbop")) return 1ULL << 38;

                // Ratified Virtual Memory Extensions
                if (supported_extension("svinval")) return 1ULL << 39;
                if (supported_extension("svnapot")) return 1ULL << 40;
                if (supported_extension("svpbmt")) return 1ULL << 41;

                return 0ULL;
            }();
            if (extension_id == 0) {
                return std::unexpected(std::format("unsupported ISA extension '{}'", extension));
            }
            if ((seen_extensions & extension_id) != 0) {
                return std::unexpected(std::format("duplicate ISA extension '{}'", extension));
            }
            seen_extensions |= extension_id;

            if (extension_id >= (1ULL << 4) && extension_id <= (1ULL << 21)) {
                has_zb = true;
            }
            if (extension_id >= (1ULL << 22) && extension_id <= (1ULL << 32)) {
                has_v = true;
            }

            if (next == std::string_view::npos) break;
            extensions.remove_prefix(next + 1);
        }

        const bool final_has_v = has_v || (parsed->preset == IsaPreset::GCV);
        const bool final_has_zb = has_zb || parsed->has_zb;
        parsed->has_zb = final_has_zb;

        if (final_has_v && (parsed->preset == IsaPreset::GC || parsed->preset == IsaPreset::G)) {
            parsed->preset = IsaPreset::GCV;
        }
        return parsed;
    }

    const auto xlen_suffix = simrv::xlen::kIsXLen64 ? "64" : "32";
    auto supported = std::format(
        "e, em, emac, i, im, ima, imac, g, gc, gcv, gczb, gczbv, rv32e, rv32em, rv32emac, rv{}i, "
        "rv{}im, "
        "rv{}ima, rv{}imac, rv{}g, rv{}gc, rv{}gcv, rv{}gczb, rv{}gczbv",
        xlen_suffix, xlen_suffix, xlen_suffix, xlen_suffix, xlen_suffix, xlen_suffix, xlen_suffix,
        xlen_suffix, xlen_suffix, xlen_suffix);
    if constexpr (simrv::xlen::kIsXLen64) {
        supported +=
            ", rv32i, rv32im, rv32ima, rv32imac, rv32g, rv32gc, rv32gcv, rv32gczb, rv32gczbv";
    }
    return std::unexpected(
        std::format("unsupported ISA preset '{}' (supported: {})", value, supported));
}

auto effective_isa_preset(const RuntimeOptions& options) -> IsaPreset {
    if (options.misa_override) {
        return options.isa_preset;
    }
    if (options.cpu_model_preset.has_value()) {
        return pipeline::make_cpu_model_preset(*options.cpu_model_preset).isa_preset;
    }
    return IsaPreset::GCV;
}

auto parse_file_options(std::string_view arg, std::span<char* const> args, std::size_t& i,
                        RuntimeOptions& options) -> std::expected<bool, std::string> {
    const auto option_id = cli_option_id(arg);
    if (option_id == "--buildroot-output") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        auto bundle = resolve_buildroot_image_bundle(*value);
        if (!bundle) return std::unexpected(bundle.error());
        options.fn_memimg = bundle->firmware.string();
        options.fn_dvtree = bundle->dtb.string();
        options.fn_dskimg = bundle->rootfs.string();
        if (bundle->dram_size_bytes.has_value()) {
            options.dram_size = *bundle->dram_size_bytes;
        }
        options.use_disk = true;
        options.appmode = false;
        options.use_opensbi = true;
        return true;
    }
    if (option_id == "--image") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        options.fn_memimg = std::string(*value);
        return true;
    }
    if (option_id == "--disk") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        options.fn_dskimg = std::string(*value);
        options.use_disk = true;
        options.appmode = false;
        return true;
    }
    if (option_id == "--virtiofs") {
        std::string_view path = arg.starts_with("--virtiofs=") ? arg.substr(11) : "";
        if (path.empty()) {
            auto value = next_argument(args, i, "--virtiofs");
            if (!value) return std::unexpected(value.error());
            path = *value;
        }
        if (path.empty()) return std::unexpected("--virtiofs requires a host directory");
        options.fn_virtiofs = std::string(path);
        options.appmode = false;
        return true;
    }
    if (option_id == "--fdt") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        if (*value == "auto") {
            options.auto_fdt = true;
        } else {
            options.fn_dvtree = std::string(*value);
        }
        options.appmode = false;
        return true;
    }
    if (option_id == "--auto-fdt") {
        options.auto_fdt = true;
        options.appmode = false;
        return true;
    }
    if (option_id == "--uefi") {
        options.uefi_enabled = true;
        options.auto_fdt = true;
        options.appmode = false;
        options.platform_profile = simrv::core::PlatformProfile::Pcie;
        if (i + 1 < args.size() && args[i + 1][0] != '-') {
            ++i;
            options.fn_uefi = std::string(args[i]);
        }
        return true;
    }
    if (option_id == "--uefi-vars") {
        auto value = next_argument(args, i, "--uefi-vars");
        if (!value) return std::unexpected(value.error());
        options.fn_uefi_vars = std::string(*value);
        return true;
    }
    if (option_id == "--bootargs") {
        auto value = next_argument(args, i, "--bootargs");
        if (!value) return std::unexpected(value.error());
        options.bootargs = std::string(*value);
        return true;
    }
    if (option_id == "--config") {
        constexpr std::string_view option_name = "--config";
        constexpr std::string_view prefix = "--config=";
        std::string_view val = arg.starts_with(prefix) ? arg.substr(prefix.size()) : "";
        if (val.empty()) {
            auto value = next_argument(args, i, option_name);
            if (!value) return std::unexpected(value.error());
            val = *value;
        }
        options.config_target = std::string(val);
        const auto resolved = simrv::core::resolve_cpu_model_path(val);
        if (resolved.has_value()) {
            options.fn_config = *resolved;
        } else if (std::filesystem::exists(val)) {
            options.fn_config = std::string(val);
        } else if (!simrv::core::SoCConfig::preset(val).has_value()) {
            return std::unexpected(std::format(
                "unknown platform configuration '{}'. Expected a file path or preset (virt-pcie, "
                "virt-mmio, rvcomp).",
                val));
        }
        return true;
    }
    if (option_id == "--cfu-plugin") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        options.fn_cfu_plugin = std::string(*value);
        return true;
    }
    if (option_id == "--bram-prewarm") {
        options.bram_prewarm = true;
        return true;
    }
    if (option_id == "--no-bram-prewarm") {
        options.bram_prewarm = false;
        return true;
    }
    if (option_id == "--trap-log") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        options.fn_traplog = std::string(*value);
        options.traplog_mode = true;
        return true;
    }
    if (option_id == "--arch-trace") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        options.fn_archtrace = std::string(*value);
        if (!options.execution_mode_explicit) {
            options.execution_mode = RequestedExecutionMode::Detailed;
            options.execution_mode_explicit = true;
        }
        return true;
    }
    if (option_id == "--log-file") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        options.fn_log = std::string(*value);
        return true;
    }
    if (option_id == "--trace-dir") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        if (value->empty()) return std::unexpected(std::format("{} requires a directory", arg));
        options.trace_dir = std::string(*value);
        return true;
    }
    return false;
}

auto parse_execution_options(std::string_view arg, std::span<char* const> args, std::size_t& i,
                             RuntimeOptions& options) -> std::expected<bool, std::string> {
    const auto option_id = cli_option_id(arg);
    if (option_id == "--steps") {
        auto value = parse_scaled_required(args, i, arg);
        if (!value) return std::unexpected(value.error());
        options.fincnt = *value;
        return true;
    }
    if (option_id == "--pc") {
        auto value = parse_u32_required(args, i, arg);
        if (!value) return std::unexpected(value.error());
        options.start_pc = *value;
        return true;
    }
    if (option_id == "--timer") {
        auto value = parse_scaled_required(args, i, arg);
        if (!value) return std::unexpected(value.error());
        options.enabletimer = *value;
        return true;
    }
    if (option_id == "--tohost-addr") {
        auto value = parse_u32_required(args, i, arg);
        if (!value) return std::unexpected(value.error());
        options.isatest_tohost = *value;
        return true;
    }
    if (option_id == "--trace-range") {
        auto trace = parse_trace_window(options, args, i);
        if (!trace) return std::unexpected(trace.error());
        return true;
    }
    if (option_id == "--trace") {
        options.trace_enabled = true;
        options.trace_begin = 0;
        options.trace_end = std::numeric_limits<simrv::xlen::Counter>::max();
        options.execution_mode = RequestedExecutionMode::Detailed;
        options.execution_mode_explicit = true;
        return true;
    }
    if (option_id == "--trace-pc-range") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        if (!parse_trace_pc_range(*value, options))
            return std::unexpected("--trace-pc-range expects START-END with START <= END");
        return true;
    }
    if (option_id == "--trace-pc") {
        return std::unexpected(
            "--trace-pc was removed; use --trace-pc-range START-END to filter --arch-trace events");
    }
    if (option_id == "--realtime") {
        options.realtime_pacing = true;
        options.explicit_realtime_mode = true;
        return true;
    }
    if (option_id == "--no-realtime") {
        options.realtime_pacing = false;
        options.explicit_realtime_mode = true;
        return true;
    }
    if (option_id == "--cpu-clock") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        uint64_t hz = 0;
        if (!parse_frequency_hz(*value, hz) || hz == 0) {
            return std::unexpected(std::format("invalid CPU clock frequency value for {}", arg));
        }
        options.cpu_clock_hz = hz;
        return true;
    }
    if (option_id == "--timebase-hz") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        uint64_t hz = 0;
        if (!parse_frequency_hz(*value, hz) || hz == 0) {
            return std::unexpected(std::format("invalid timebase frequency value for {}", arg));
        }
        options.timebase_hz = hz;
        return true;
    }
    return false;
}

auto parse_mode_options(std::string_view arg, std::span<char* const> args, std::size_t& i,
                        ParseResult& result) -> std::expected<bool, std::string> {
    const auto option_id = cli_option_id(arg);
    if (option_id == "--isa") {
        auto value = next_argument(args, i, "--isa");
        if (!value) return std::unexpected(value.error());
        auto parsed_misa = parse_isa_preset(*value);
        if (!parsed_misa) return std::unexpected(parsed_misa.error());
        result.options.isa_preset = parsed_misa->preset;
        result.options.misa_xlen = parsed_misa->xlen;
        result.options.has_zb_extensions = parsed_misa->has_zb;
        result.options.misa_override = true;
        return true;
    }
    if (option_id == "--profile") {
        auto value = next_argument(args, i, "--profile");
        if (!value) return std::unexpected(value.error());
        auto parsed = isa::parse_architecture_profile(*value);
        if (!parsed) {
            return std::unexpected(
                std::format("invalid architecture profile '{}' (valid: rva23, rva23s64, rva23u64, "
                            "rva22, rva22s64, rva20, legacy)",
                            *value));
        }
        result.options.profile = *parsed;
        return true;
    }
    if (option_id == "--vlen") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        uint32_t val = 0;
        if (!parse_u32_base0(*value, val)) {
            return std::unexpected(std::format("invalid numeric value for {}", arg));
        }
        if (val < 128 || val > 1024 || (val & (val - 1)) != 0) {
            return std::unexpected(
                std::format("VLEN must be a power of 2 between 128 and 1024 (got: {})", val));
        }
        result.options.vlen = val;
        return true;
    }
    if (option_id == "--baremetal") {
        result.options.appmode = true;
        return true;
    }
    if (option_id == "--os") {
        result.options.appmode = false;
        return true;
    }
    if (option_id == "--ca") {
        result.options.execution_mode = RequestedExecutionMode::CycleAccurate;
        result.options.execution_mode_explicit = true;
        return true;
    }
    if (option_id == "--ia") {
        result.options.execution_mode = RequestedExecutionMode::Fast;
        result.options.execution_mode_explicit = true;
        return true;
    }
    if (option_id == "--mode") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        if (*value == "fast") {
            result.options.execution_mode = RequestedExecutionMode::Fast;
        } else if (*value == "detailed") {
            result.options.execution_mode = RequestedExecutionMode::Detailed;
        } else if (*value == "cycle-accurate") {
            result.options.execution_mode = RequestedExecutionMode::CycleAccurate;
        } else {
            return std::unexpected(std::format(
                "unsupported execution mode '{}' (supported: fast, detailed, cycle-accurate)",
                *value));
        }
        result.options.execution_mode_explicit = true;
        return true;
    }
    if (option_id == "--pipeline") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        if (!pipeline::parse_pipeline_type(*value).has_value()) {
            return std::unexpected(
                std::format("unsupported pipeline '{}' (supported: 3stage, 5stage)", *value));
        }
        result.options.pipeline_type = std::string(*value);
        return true;
    }
    if (option_id == "--cpu-preset") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        const auto resolved = simrv::core::resolve_cpu_model_path(*value);
        if (resolved.has_value()) {
            result.options.fn_config = *resolved;
            simrv::pipeline::CpuModelConfig loaded{};
            if (!simrv::core::load_cpu_config(*resolved, loaded)) {
                return std::unexpected(
                    std::format("failed to load CPU model configuration from '{}'", *resolved));
            }
            result.options.cpu_model_preset = loaded.preset;
            return true;
        }
        auto parsed = simrv::pipeline::parse_cpu_model_preset(*value);
        if (!parsed) {
            return std::unexpected(
                std::format("unsupported CPU model preset '{}' (supported presets: tiny, balanced, "
                            "performance, or path/name of model .cfg)",
                            *value));
        }
        result.options.cpu_model_preset = parsed;
        return true;
    }
    if (option_id == "--bpred") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        if (!pipeline::parse_branch_predictor_type(*value).has_value()) {
            return std::unexpected(
                std::format("unsupported branch predictor '{}' (supported: none, static, "
                            "bimodal, gshare, tournament)",
                            *value));
        }
        result.options.bpred_type = std::string(*value);
        return true;
    }
    if (option_id == "--bht-size") {
        auto value = parse_positive_size(args, i, arg, "BHT");
        if (!value) return std::unexpected(value.error());
        result.options.bht_size = *value;
        return true;
    }
    if (option_id == "--btb-size") {
        auto value = parse_positive_size(args, i, arg, "BTB");
        if (!value) return std::unexpected(value.error());
        result.options.btb_size = *value;
        return true;
    }
    if (option_id == "--ras-size") {
        auto value = parse_positive_size(args, i, arg, "RAS");
        if (!value) return std::unexpected(value.error());
        result.options.ras_size = *value;
        return true;
    }
    if (option_id == "--harts") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        uint32_t val = 0;
        if (!parse_u32_base0(*value, val) || val == 0 ||
            val > simrv::core::ExecutionConfig::kMaxHarts) {
            return std::unexpected(std::format("SMP core count must be between 1 and {} (got: {})",
                                               simrv::core::ExecutionConfig::kMaxHarts, *value));
        }
        result.options.num_harts = val;
        result.options.explicit_num_harts = true;
        return true;
    }
    if (option_id == "--dram-size") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        uint64_t val = 0;
        if (!parse_scaled_u64(*value, val) || val < 16ULL * 1024ULL * 1024ULL) {
            return std::unexpected(
                std::format("invalid RAM size value for {} (minimum 16MB)", arg));
        }
        result.options.dram_size = val;
        return true;
    }
    return false;
}

auto parse_tui_options(std::string_view arg, std::span<char* const> args, std::size_t& i,
                       ParseResult& result) -> std::expected<bool, std::string> {
    const auto option_id = cli_option_id(arg);
    if (option_id == "--tui") {
        result.options.tuimode = true;
        result.options.explicit_tui_mode = true;
        return true;
    }
    if (option_id == "--cli") {
        result.options.tuimode = false;
        result.options.explicit_cli_mode = true;
        return true;
    }
    if (option_id == "--inspection-output") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        if (value->empty()) return std::unexpected("--inspection-output requires a non-empty path");
        result.options.inspection_output = std::string(*value);
        return true;
    }
    if (option_id == "--high-contrast") {
        result.options.high_contrast = true;
        return true;
    }
    if (option_id == "--mission") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        result.options.mission = std::string(*value);
        return true;
    }
    if (option_id == "--no-forwarding") {
        result.options.disable_forwarding = true;
        return true;
    }
    if (option_id == "--early-out-div") {
        result.options.divider_early_out = true;
        return true;
    }
    if (option_id == "--net") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        std::string_view net_opt = *value;
        if (net_opt != "user" && net_opt != "tap" && net_opt != "socket" && net_opt != "none") {
            return std::unexpected(
                std::format("invalid network mode: {}. Allowed: user, tap, socket, none", net_opt));
        }
        result.options.net_mode = std::string(net_opt);
        return true;
    }
    if (option_id == "--explain-inst") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        uint32_t raw_val = 0;
        if (!parse_u32_base0(*value, raw_val)) {
            return std::unexpected(
                std::format("invalid hex instruction value for --explain-inst: {}", *value));
        }
        result.action = CliAction::ExplainInstruction;
        result.options.explain_inst_val = raw_val;
        return true;
    }
    if (option_id == "--log-mmio") {
        result.options.dlog_mode = true;
        return true;
    }
    if (option_id == "--instmix") {
        result.options.use_mix = true;
        return true;
    }
    if (option_id == "--dump-cpu-model" || option_id == "--scaffold-cpu-model") {
        result.action = CliAction::DumpCpuModel;
        if (option_id == "--scaffold-cpu-model") {
            result.options.dump_cpu_model_preset = "balanced";
            if (i + 1 < args.size() && args[i + 1] != nullptr && args[i + 1][0] != '\0' &&
                (args[i + 1][0] != '-' || std::string_view(args[i + 1]) == "-")) {
                ++i;
                result.options.dump_cpu_model_output = args[i];
            }
        } else {
            auto value = next_argument(args, i, arg);
            if (!value) return std::unexpected(value.error());
            result.options.dump_cpu_model_preset = std::string(*value);
            if (i + 1 < args.size() && args[i + 1] != nullptr && args[i + 1][0] != '\0' &&
                (args[i + 1][0] != '-' || std::string_view(args[i + 1]) == "-")) {
                ++i;
                result.options.dump_cpu_model_output = args[i];
            }
        }
        return true;
    }
    if (option_id == "--dump-soc-manifest") {
        result.action = CliAction::ExportSoCManifest;
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        result.options.soc_manifest_preset = std::string(*value);
        if (i + 1 < args.size() && args[i + 1] != nullptr && args[i + 1][0] != '\0' &&
            (args[i + 1][0] != '-' || std::string_view(args[i + 1]) == "-")) {
            ++i;
            result.options.soc_manifest_output = args[i];
        }
        return true;
    }
    if (option_id == "--validate-config") {
        result.action = CliAction::ValidateCpuModel;
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        result.options.dump_cpu_model_preset = std::string(*value);
        return true;
    }
    return false;
}

auto parse_debug_cosrv_options(std::string_view arg, std::span<char* const> args, std::size_t& i,
                               RuntimeOptions& options) -> std::expected<bool, std::string> {
    const auto option_id = cli_option_id(arg);
    if (option_id == "--log-level") {
        auto value = next_argument(args, i, "--log-level");
        if (!value) return std::unexpected(value.error());
        auto level = simrv::log::parse_level(*value);
        if (!level.has_value()) {
            return std::unexpected(std::format(
                "unsupported log level '{}' (supported: trace, debug, info, warn, error, off)",
                *value));
        }
        options.log_level = level;
        return true;
    }
    if (option_id == "--log-format") {
        auto value = next_argument(args, i, "--log-format");
        if (!value) return std::unexpected(value.error());
        const auto format = simrv::log::parse_format(*value);
        if (!format) {
            return std::unexpected(std::format(
                "unsupported log format '{}' (supported: text, json, json-pretty)", *value));
        }
        options.log_format = *format;
        return true;
    }
    if (option_id == "--log-stderr") {
        options.log_stderr = true;
        return true;
    }
    if (option_id == "--summary") {
        auto value = next_argument(args, i, "--summary");
        if (!value) return std::unexpected(value.error());
        if (value->empty() || *value == "-") {
            return std::unexpected(
                "--summary requires a writable file path (stdout is reserved for guest UART)");
        }
        options.fn_summary = std::string(*value);
        return true;
    }
    if (option_id == "--events") {
        auto value = next_argument(args, i, "--events");
        if (!value) return std::unexpected(value.error());
        if (value->empty() || *value == "-") {
            return std::unexpected(
                "--events requires a writable file path (stdout is reserved for guest UART)");
        }
        options.fn_events = std::string(*value);
        return true;
    }
    if (option_id == "--uart-transcript") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        if (value->empty() || *value == "-") {
            return std::unexpected("--uart-transcript requires a writable file path");
        }
        options.fn_uart_transcript = std::string(*value);
        return true;
    }
    if (option_id == "--trace-level") {
        auto value = parse_u32_required(args, i, arg);
        if (!value) return std::unexpected(value.error());
        if (*value > 4) return std::unexpected("--trace-level must be between 0 and 4");
        options.trace_level = static_cast<uint8_t>(*value);
        return true;
    }
    if (option_id == "--trace-register-writes") {
        options.trace_register_writes = true;
        return true;
    }
    if (option_id == "--trace-events") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        if (value->empty()) return std::unexpected("--trace-events requires a non-empty list");
        std::string_view remaining = *value;
        while (true) {
            const auto comma = remaining.find(',');
            auto event = remaining.substr(0, comma);
            while (!event.empty() && (event.front() == ' ' || event.front() == '\t'))
                event.remove_prefix(1);
            while (!event.empty() && (event.back() == ' ' || event.back() == '\t'))
                event.remove_suffix(1);
            if (event.empty() || std::any_of(event.begin(), event.end(), [](char ch) {
                    return !((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_');
                })) {
                return std::unexpected("--trace-events expects comma-separated event names");
            }
            if (comma == std::string_view::npos) break;
            remaining.remove_prefix(comma + 1);
        }
        options.trace_events = std::string(*value);
        return true;
    }
    if (option_id == "--trace-function" || option_id == "--trace-device") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        if (value->empty()) return std::unexpected(std::format("{} requires a name", arg));
        if (option_id == "--trace-function")
            options.trace_function = std::string(*value);
        else
            options.trace_device = std::string(*value);
        return true;
    }
    if (option_id == "--trace-hart") {
        auto value = parse_u32_required(args, i, arg);
        if (!value) return std::unexpected(value.error());
        options.trace_hart = *value;
        return true;
    }
    if (option_id == "--trace-after-cycle" || option_id == "--trace-before-cycle") {
        auto value = parse_scaled_required(args, i, arg);
        if (!value) return std::unexpected(value.error());
        if (option_id == "--trace-after-cycle")
            options.trace_after_cycle = *value;
        else
            options.trace_before_cycle = *value;
        return true;
    }
    if (option_id == "--save-checkpoint") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        if (value->empty()) return std::unexpected(std::format("{} requires a file path", arg));
        options.fn_save_checkpoint = std::string(*value);
        return true;
    }
    if (option_id == "--load-checkpoint") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        if (value->empty()) return std::unexpected(std::format("{} requires a file path", arg));
        options.fn_load_checkpoint = std::string(*value);
        return true;
    }
    if (option_id == "--checkpoint-every") {
        auto value = parse_scaled_required(args, i, arg);
        if (!value) return std::unexpected(value.error());
        if (*value == 0) return std::unexpected("--checkpoint-every must be greater than zero");
        options.checkpoint_every = *value;
        return true;
    }
    if (option_id == "--checkpoint-dir") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        if (value->empty()) return std::unexpected("--checkpoint-dir requires a directory path");
        options.checkpoint_dir = std::string(*value);
        return true;
    }
    if (option_id == "--quiet") {
        options.quiet = true;
        if (!options.log_level.has_value()) {
            options.log_level = simrv::log::Level::Warn;
        }
        return true;
    }
    if (option_id == "--verbose") {
        options.verbose = true;
        if (!options.log_level.has_value()) {
            options.log_level = simrv::log::Level::Debug;
        }
        return true;
    }
    if (option_id == "--trace-bpred") {
        options.bp_trace = true;
        return true;
    }
    if (option_id == "--gdb") {
        options.gdb_mode = true;
        return true;
    }
    if (option_id == "--gdb-port") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        uint64_t port_val = 0;
        if (!parse_scaled_u64(*value, port_val) || port_val > 65535) {
            return std::unexpected(std::format("invalid GDB port value for {}", arg));
        }
        options.gdb_port = static_cast<uint16_t>(port_val);
        return true;
    }
    if (option_id == "--lockstep") {
        options.lockstep_mode = true;
        return true;
    }
    if (option_id == "--step-delay") {
        auto value = next_argument(args, i, std::string(arg));
        if (!value) return std::unexpected(value.error());
        uint64_t delay_val = 0;
        if (!parse_scaled_u64(*value, delay_val)) {
            return std::unexpected(std::format("invalid step delay value for {}", arg));
        }
        options.step_delay_us = delay_val;
        return true;
    }
    if (option_id == "--spike-bin") {
        auto value = next_argument(args, i, "--spike-bin");
        if (!value) return std::unexpected(value.error());
        options.spike_bin = std::string(*value);
        return true;
    }
    if (option_id == "--spike-elf") {
        auto value = next_argument(args, i, "--spike-elf");
        if (!value) return std::unexpected(value.error());
        options.spike_elf = std::string(*value);
        return true;
    }
    return false;
}

auto is_known_short_flag(char c) -> bool {
    const char option_chars[] = {'-', c};
    return !cli_option_id(std::string_view(option_chars, 2)).empty();
}

auto short_flag_takes_argument(char c) -> bool { return cli_short_flag_takes_argument(c); }

auto expand_short_flags(const std::vector<std::string>& original_args) -> std::vector<std::string> {
    std::vector<std::string> expanded;
    expanded.push_back(original_args[0]);

    for (std::size_t i = 1; i < original_args.size(); ++i) {
        const std::string& arg = original_args[i];
        if (arg == "-smp") {
            expanded.push_back(arg);
            continue;
        }
        if (arg.starts_with('-') && !arg.starts_with("--") && arg.size() > 2) {
            if (std::isdigit(static_cast<unsigned char>(arg[1]))) {
                expanded.push_back(arg);
                continue;
            }

            bool can_expand = true;
            std::vector<std::string> local_expanded;
            for (std::size_t j = 1; j < arg.size(); ++j) {
                char c = arg[j];
                if (is_known_short_flag(c)) {
                    local_expanded.push_back(std::string("-") + c);
                    if (short_flag_takes_argument(c)) {
                        if (j + 1 < arg.size()) {
                            local_expanded.push_back(arg.substr(j + 1));
                        }
                        break;
                    }
                } else {
                    can_expand = false;
                    break;
                }
            }

            if (can_expand) {
                expanded.insert(expanded.end(), local_expanded.begin(), local_expanded.end());
            } else {
                expanded.push_back(arg);
            }
        } else {
            expanded.push_back(arg);
        }
    }
    return expanded;
}

}  // namespace

auto parse_command_line(std::span<char* const> args) -> std::expected<ParseResult, std::string> {
    std::vector<std::string> original_args;
    original_args.reserve(args.size());
    for (auto* ptr : args) {
        original_args.emplace_back(ptr);
    }

    std::vector<std::string> expanded_strings = expand_short_flags(original_args);
    std::vector<char*> expanded_pointers;
    expanded_pointers.reserve(expanded_strings.size());
    for (auto& s : expanded_strings) {
        expanded_pointers.push_back(s.data());
    }
    std::span<char* const> expanded_span(expanded_pointers.data(), expanded_pointers.size());

    ParseResult result{};

    for (std::size_t i = 1; i < expanded_span.size(); ++i) {
        std::string_view const arg = expanded_span[i];
        const std::string_view option_id = cli_option_id(arg);
        if (option_id == "--help") {
            result.action = CliAction::ShowHelp;
            return result;
        }
        if (option_id == "--version") {
            result.action = CliAction::ShowVersion;
            return result;
        }
        if (option_id == "--license") {
            result.action = CliAction::ShowLicense;
            return result;
        }
        if (option_id == "--isa-info") {
            result.action = CliAction::ShowIsaInfo;
            return result;
        }
        if (option_id == "--doctor") {
            result.action = CliAction::Doctor;
            return result;
        }

        // Try parsing file options
        auto res_file = parse_file_options(arg, expanded_span, i, result.options);
        if (!res_file) return std::unexpected(res_file.error());
        if (*res_file) continue;

        // Try parsing execution options
        auto res_exec = parse_execution_options(arg, expanded_span, i, result.options);
        if (!res_exec) return std::unexpected(res_exec.error());
        if (*res_exec) continue;

        // Try parsing mode options
        auto res_mode = parse_mode_options(arg, expanded_span, i, result);
        if (!res_mode) return std::unexpected(res_mode.error());
        if (*res_mode) continue;

        // Try parsing TUI options
        auto res_tui = parse_tui_options(arg, expanded_span, i, result);
        if (!res_tui) return std::unexpected(res_tui.error());
        if (*res_tui) continue;

        // Try parsing debug/cosrv options
        auto res_debug = parse_debug_cosrv_options(arg, expanded_span, i, result.options);
        if (!res_debug) return std::unexpected(res_debug.error());
        if (*res_debug) continue;

        return std::unexpected(std::format("unknown option '{}'", arg));
    }

    if (!result.options.explicit_cli_mode && !result.options.explicit_tui_mode) {
        result.options.tuimode = simrv::util::interactive_tui_available();
    }

    if (result.options.gdb_mode) {
        if (result.options.explicit_tui_mode) {
            return std::unexpected("--gdb and --tui are mutually exclusive");
        }
        if (result.options.lockstep_mode) {
            return std::unexpected("--gdb and --lockstep are mutually exclusive");
        }
        result.options.tuimode = false;
        result.options.explicit_cli_mode = true;
    }

    if (result.options.explicit_cli_mode && result.options.explicit_tui_mode) {
        return std::unexpected("--cli and --tui are mutually exclusive");
    }
    if (result.options.fn_dvtree.empty()) {
        if (const char* env_dtb = std::getenv("SIMRV_LINUX_DTB"); env_dtb && *env_dtb) {
            result.options.fn_dvtree = env_dtb;
        }
    }
    if (!result.options.appmode && result.options.fn_dvtree.empty() && !result.options.auto_fdt &&
        !result.options.uefi_enabled) {
        return std::unexpected(
            "Linux boot requires a device tree; pass -f/--dtb <FILE> or set SIMRV_LINUX_DTB");
    }
    if (!result.options.mission.empty()) {
        if (result.options.explicit_cli_mode) {
            return std::unexpected(
                "--mission requires interactive TUI mode (cannot be used with --cli)");
        }
        result.options.tuimode = true;
    }
    if (result.options.trace_after_cycle && result.options.trace_before_cycle &&
        *result.options.trace_after_cycle > *result.options.trace_before_cycle) {
        return std::unexpected("--trace-after-cycle must not exceed --trace-before-cycle");
    }
    if (result.options.trace_register_writes && result.options.fn_archtrace.empty()) {
        return std::unexpected("--trace-register-writes requires --arch-trace");
    }
    if (result.options.trace_hart &&
        *result.options.trace_hart >= simrv::core::ExecutionConfig::kMaxHarts) {
        return std::unexpected("--trace-hart must identify a supported hart (0-15)");
    }
    if (result.options.trace_pc_start && result.options.trace_pc_end &&
        *result.options.trace_pc_start > *result.options.trace_pc_end) {
        return std::unexpected("--trace-pc-range start must not exceed end");
    }

    if (needs_memory_image(result)) {
        return std::unexpected("-m/--image <FILE> is required to load a memory image");
    }

    return result;
}

auto resolve_runtime_profile(const RuntimeOptions& options) -> simrv::core::RuntimeProfile {
    using simrv::core::ExecutionEngine;
    using simrv::core::InteractionMode;
    simrv::core::RuntimeProfile profile{};
    profile.interaction = options.tuimode ? InteractionMode::Tui : InteractionMode::Cli;
    if (!options.execution_mode_explicit) {
        profile.engine = options.tuimode ? ExecutionEngine::InstructionObservable
                                         : ExecutionEngine::InstructionFast;
    } else {
        switch (options.execution_mode) {
            case RequestedExecutionMode::Fast:
                profile.engine = ExecutionEngine::InstructionFast;
                break;
            case RequestedExecutionMode::Detailed:
                profile.engine = ExecutionEngine::InstructionObservable;
                break;
            case RequestedExecutionMode::CycleAccurate:
                profile.engine =
                    options.tuimode ? ExecutionEngine::CycleObservable : ExecutionEngine::CycleFast;
                break;
        }
    }
    profile.tracing = options.bp_trace || options.trace_enabled ||
                      options.trace_begin != std::numeric_limits<simrv::xlen::Counter>::max();
    profile.lockstep = options.lockstep_mode;
    profile.gdb = options.gdb_mode;
    return profile;
}

auto RuntimeOptions::to_machine_config() const -> simrv::core::MachineConfig {
    simrv::core::MachineConfig cfg{};
    cfg.memory.dram_base = simrv::memory::kDramBaseAddress;
    const auto default_dram_size = appmode ? simrv::memory::kDramSize : (1ULL << 30);
    cfg.memory.dram_size = (dram_size != 0 ? dram_size : default_dram_size);

    cfg.execution.appmode = appmode;
    cfg.execution.start_pc = start_pc;
    cfg.execution.fincnt = fincnt;
    cfg.execution.trace_begin = trace_begin;
    cfg.execution.trace_end = trace_end;
    cfg.execution.enabletimer = enabletimer;
    cfg.execution.num_harts = num_harts;
    if (explicit_realtime_mode) {
        cfg.execution.realtime_pacing = realtime_pacing;
    } else {
        cfg.execution.realtime_pacing =
            tuimode && (fincnt == std::numeric_limits<simrv::xlen::Counter>::max());
    }
    if (auto parsed = pipeline::parse_pipeline_type(pipeline_type)) {
        cfg.execution.pipeline_type = *parsed;
    }
    if (cpu_clock_hz.has_value()) {
        cfg.execution.cpu_clock_hz = *cpu_clock_hz;
    }
    if (timebase_hz.has_value()) {
        cfg.execution.timebase_hz = *timebase_hz;
    }
    cfg.execution.profile = profile;

    cfg.tui.enabled = tuimode;
    cfg.tui.high_contrast = high_contrast;
    cfg.tui.mission = mission;
    cfg.tui.inspection_output = inspection_output;

    cfg.debug.gdb_enabled = gdb_mode;
    cfg.debug.gdb_port = gdb_port;
    cfg.debug.lockstep_enabled = lockstep_mode;
    cfg.debug.spike_bin = spike_bin;
    cfg.debug.spike_elf = spike_elf;
    cfg.debug.dlog_mode = dlog_mode;
    cfg.debug.traplog_mode = traplog_mode;
    cfg.debug.bp_trace = bp_trace;
    cfg.debug.use_mix = use_mix;
    cfg.debug.trace_dir = trace_dir;
    cfg.debug.architecture_trace_path = fn_archtrace;
    cfg.debug.trace_level = trace_level;
    cfg.debug.trace_register_writes = trace_register_writes;
    cfg.debug.trace_events = trace_events;
    cfg.debug.trace_function = trace_function;
    cfg.debug.trace_device = trace_device;
    cfg.debug.trace_hart = trace_hart;
    cfg.debug.trace_pc_start = trace_pc_start;
    cfg.debug.trace_pc_end = trace_pc_end;
    cfg.debug.trace_after_cycle = trace_after_cycle;
    cfg.debug.trace_before_cycle = trace_before_cycle;
    cfg.debug.checkpoint_every = checkpoint_every;
    cfg.debug.checkpoint_dir = checkpoint_dir;

    cfg.isa.isatest_tohost = isatest_tohost;
    cfg.isa.isa_preset = isa_preset_bits(effective_isa_preset(*this));
    if (has_zb_extensions || (!misa_override && !cpu_model_preset.has_value())) {
        cfg.isa.isa_preset |= simrv::isa::misa_extension_bit(simrv::isa::IsaExtension::B);
    }
    cfg.isa.misa_override =
        misa_override ||
        (cpu_model_preset.has_value() &&
         pipeline::make_cpu_model_preset(*cpu_model_preset).isa_preset != IsaPreset::GCV);
    cfg.isa.misa_xlen = misa_xlen;
    cfg.isa.vlen = vlen;

    cfg.files.binary_path = fn_memimg;
    cfg.files.disk_path = fn_dskimg;
    cfg.files.virtiofs_path = fn_virtiofs;
    cfg.files.disk_enabled = use_disk;
    cfg.files.dvtree_path = fn_dvtree;
    cfg.files.config_path = fn_config;
    cfg.files.cfu_plugin_path = fn_cfu_plugin;
    cfg.files.uefi_code_path = fn_uefi;
    cfg.files.uefi_vars_path = fn_uefi_vars;
    cfg.files.uefi_enabled = uefi_enabled;
    cfg.files.auto_fdt = auto_fdt;
    cfg.files.bootargs = bootargs;
    cfg.cpu_model_preset = cpu_model_preset;
    if (!bpred_type.empty()) {
        cfg.branch_predictor_type = pipeline::parse_branch_predictor_type(bpred_type);
    }
    cfg.bht_entries = bht_size;
    cfg.btb_entries = btb_size;
    cfg.ras_entries = ras_size;
    if (bram_prewarm.has_value()) {
        cfg.bram_prewarm = *bram_prewarm;
    } else if (fn_config.find("cfu-provingground") != std::string::npos ||
               fn_config.find("rvproc") != std::string::npos) {
        cfg.bram_prewarm = true;
    }

    cfg.network.mode = net_mode;
    if (const auto preset = simrv::core::SoCConfig::preset(config_target)) {
        cfg.soc = *preset;
    }
    if (!fn_config.empty()) {
        simrv::pipeline::CpuModelConfig model{};
        if (simrv::core::load_platform_config(fn_config, model, cfg.soc)) {
            if (!cfg.soc.cpu_model.empty()) {
                cfg.isa.misa_xlen = model.supported_xlen;
                cfg.isa.isa_preset = simrv::isa::isa_preset_bits(model.isa_preset);
                cfg.isa.misa_override = true;
            }
        }
    }
    if (cfg.soc.dram_base.has_value()) cfg.memory.dram_base = *cfg.soc.dram_base;
    if (dram_size == 0 && cfg.soc.dram_size.has_value()) {
        cfg.memory.dram_size = *cfg.soc.dram_size;
    }
    if (cfg.execution.start_pc == simrv::boot::kStartPc && cfg.soc.reset_pc.has_value()) {
        cfg.execution.start_pc = *cfg.soc.reset_pc;
    }
    if (cfg.isa.isatest_tohost == 0x80001000 && cfg.soc.tohost.has_value()) {
        cfg.isa.isatest_tohost = *cfg.soc.tohost;
    }
    if (uefi_enabled) {
        cfg.soc.enable_pcie = true;
        cfg.platform_profile = simrv::core::PlatformProfile::Pcie;
    } else {
        cfg.platform_profile = cfg.soc.enable_mmio   ? simrv::core::PlatformProfile::Mmio
                               : cfg.soc.enable_pcie ? simrv::core::PlatformProfile::Pcie
                                                     : simrv::core::PlatformProfile::None;
    }
    if (explicit_num_harts) cfg.soc.smp.resize_harts(num_harts);
    cfg.execution.num_harts = cfg.soc.smp.hart_count;
    cfg.execution.smp_mode = cfg.soc.smp.mode;
    cfg.execution.smp_quantum = cfg.soc.smp.synchronization_quantum;
    return cfg;
}

auto apply_runtime_options(simrv::core::Machine* machine, const RuntimeOptions& options)
    -> std::expected<void, std::string> {
    if (!machine) {
        return std::unexpected("internal error: machine pointer is null");
    }

    // The complete machine configuration is consumed by Machine construction.  This adapter only
    // initializes derived runtime services and per-CPU state that depend on the live machine.

    machine->runtime_profile = resolve_runtime_profile(options);
    simrv::log::info("Runtime profile: {} execution, {} interaction{}",
                     machine->runtime_profile.execution_name(),
                     machine->runtime_profile.interaction_name(),
                     machine->runtime_profile.tracing ? ", tracing enabled" : "");
    const auto parsed_pipe = machine->configuration().execution.pipeline_type;
    const auto platform_name =
        options.platform_profile == simrv::core::PlatformProfile::Pcie   ? "pcie"
        : options.platform_profile == simrv::core::PlatformProfile::Mmio ? "mmio"
                                                                         : "none";
    const auto& smp = machine->configuration().soc.smp;
    const auto smp_mode =
        smp.mode == simrv::core::SmpMode::SingleThreaded ? "single-threaded" : "multi-threaded";
    simrv::log::info(
        "Run configuration: RV{}, {}-mode, {} hart(s), SMP {}, quantum {}, {} platform, profile "
        "{}, {} MiB RAM",
        machine->primary_hart().state().regs.xlen, options.appmode ? "bare-metal" : "OS",
        smp.hart_count, smp_mode, smp.synchronization_quantum, platform_name,
        isa::to_profile_string(options.profile),
        machine->configuration().memory.dram_size / (1024ULL * 1024ULL));
    simrv::log::info(
        "Shared caches: L1 private per hart, L2 {} cluster(s), L3 {} ({} KiB)",
        std::ranges::count_if(smp.clusters, [](const auto& cluster) { return cluster.l2.enabled; }),
        smp.l3.enabled ? "enabled" : "disabled", smp.l3.capacity_bytes / 1024);
    simrv::log::info("Guest image: {}", options.fn_memimg.empty() ? "<none>" : options.fn_memimg);
    if (machine->runtime_profile.is_cycle_mode()) {
        simrv::log::info("CA policy: {} pipeline, forwarding {}",
                         pipeline::pipeline_type_name(parsed_pipe),
                         options.disable_forwarding ? "disabled" : "enabled");
    }
    if (!options.fn_log.empty()) simrv::log::info("Developer log: {}", options.fn_log);
    if (options.verbose) {
        simrv::log::info("Runtime settings: harts={}, SMP mode={}, quantum={}, GDB={}, port={}",
                         machine->configuration().execution.num_harts, smp_mode,
                         smp.synchronization_quantum, options.gdb_mode, options.gdb_port);
    }
    if (options.traplog_mode) simrv::log::info("Trap/SBI log: {}", options.fn_traplog);
    if (options.uefi_enabled) {
        simrv::log::info(
            "UEFI mode active: PFlash Bank 0 (code) at 0x{:08x}, Bank 1 (vars) at 0x{:08x}",
            SIMRV_PLATFORM_PFLASH0_BASE, SIMRV_PLATFORM_PFLASH1_BASE);
    }
    if (options.auto_fdt) {
        simrv::log::info("Automatic Flattened Device Tree (FDT) generator active");
    }

    auto apply_config_to_cpu = [&](simrv::core::CPU& cpu) {
        if (options.cpu_model_preset.has_value()) {
            cpu.apply_cpu_model_config(
                simrv::pipeline::make_cpu_model_preset(*options.cpu_model_preset));
        }
        if (!options.fn_cfu_plugin.empty()) {
            cpu.cfu_unit.load_plugin(options.fn_cfu_plugin);
        }
        cpu.pipeline_sim.config.pipeline_type = parsed_pipe;
        cpu.pipeline_sim.config.record_snapshots = machine->runtime_profile.records_cycle_history();
        cpu.pipeline_sim.config.enable_forwarding = !options.disable_forwarding;
        cpu.pipeline_sim.config.divider_early_out = options.divider_early_out;

        if (!options.bpred_type.empty()) {
            if (const auto bp_type = pipeline::parse_branch_predictor_type(options.bpred_type)) {
                cpu.pipeline_sim.config.branch_predictor.type = *bp_type;
            }
        }
        if (options.bht_size != 0) {
            cpu.pipeline_sim.config.branch_predictor.bht_entries = options.bht_size;
        }
        if (options.btb_size != 0) {
            cpu.pipeline_sim.config.branch_predictor.btb_entries = options.btb_size;
        }
        if (options.ras_size != 0) {
            cpu.pipeline_sim.config.branch_predictor.ras_entries = options.ras_size;
        }
        cpu.branch_predictor.configure(cpu.pipeline_sim.config.branch_predictor);
    };

    apply_config_to_cpu(machine->primary_hart());
    for (size_t h = 0; h < machine->num_harts(); ++h) {
        apply_config_to_cpu(machine->hart(h));
    }

    machine->trace().init_trace(options.trace_enabled);
    machine->trace().init_architecture_trace(options.fn_archtrace);
    if (!options.fn_archtrace.empty() && !machine->trace().is_architecture_trace_enabled()) {
        return std::unexpected("cannot open architecture trace file: " + options.fn_archtrace);
    }
    machine->trace().init_dlog(options.dlog_mode);

    if (options.traplog_mode) {
        machine->trace().init_trap_log(options.traplog_mode, options.fn_traplog);
        if (!machine->trace().fp_traplog.is_open()) {
            return std::unexpected("cannot open trap/SBI log file: " + options.fn_traplog);
        }
    }

    // Filename detection preserves legacy FW_PAYLOAD bundles; current published
    // payloads identify themselves with the opensbi-linux-payload name.
    const bool is_fw_payload = (options.fn_memimg.find("fw_payload") != std::string::npos ||
                                options.fn_memimg.find("opensbi") != std::string::npos);
    const bool opensbi_active = options.use_opensbi || !options.fn_dvtree.empty() || is_fw_payload;
    for (size_t h = 0; h < machine->num_harts(); ++h) {
        machine->hart(h).use_opensbi = opensbi_active;
    }

    // Debug / co-simulation flags
    if (machine->telemetry_sink() && options.step_delay_us > 0) {
        machine->telemetry_sink()->set_step_delay_us(options.step_delay_us);
    }

    return {};
}

auto needs_memory_image(const ParseResult& result) -> bool {
    if (result.options.uefi_enabled && !result.options.fn_uefi.empty()) {
        return false;
    }
    return result.options.fn_memimg.empty() && !result.options.tuimode &&
           result.action != CliAction::ExplainInstruction && result.action != CliAction::ShowHelp &&
           result.action != CliAction::ShowVersion && result.action != CliAction::ShowLicense &&
           result.action != CliAction::DumpCpuModel &&
           result.action != CliAction::ExportSoCManifest &&
           result.action != CliAction::ValidateCpuModel;
}

namespace {

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
auto cli_help_sections() -> const std::vector<HelpSection>& {
    static const std::vector<HelpSection> sections{
        {.title = "Basic and image loading",
         .entries =
             {{.option = "-m, --image, --memory <FILE>",
               .description = "Load an ELF or raw binary image.",
               .hidden_aliases = {"--kernel", "--payload"}},
              {.option = "--pc, --start-pc <ADDR>",
               .description = "Set the initial program counter (default: 0x80000000).",
               .hidden_aliases = {"--entry"}},
              {.option = "-b, --baremetal",
               .description = "Run a bare-metal application (default)."},
              {.option = "--os", .description = "Boot an operating system (Linux or RTOS)."},
              {.option = "--uefi [CODE.fd]",
               .description = "Boot via UEFI firmware (PFlash Bank 0 on virt-pcie profile)."},
              {.option = "--uefi-vars <VARS.fd>",
               .description = "Persistent NVRAM storage image for UEFI variables (PFlash Bank 1)."},
              {.option = "--auto-fdt",
               .description = "Synthesize compliant device tree directly into guest DRAM."},
              {.option = "--bootargs <ARGS>",
               .description = "Override or set kernel boot arguments in /chosen node."},
              {.option = "--buildroot-output <DIR>",
               .description =
                   "Load firmware, DTB, and rootfs together from a Buildroot output directory."},
              {.option = "-D, --disk, --rootfs <FILE>",
               .description = "Attach a VirtIO block storage image.",
               .hidden_aliases = {"--drive"}},
              {.option = "--virtiofs <DIR>",
               .description = "Share a host directory with the guest through VirtIO-FS.",
               .accepts_equals_value = true},
              {.option = "-f, --fdt, --dtb <FILE>",
               .description = "Linux device tree (required; patched for configured RAM and harts; "
                              "defaults to SIMRV_LINUX_DTB)."}}},
        {.title = "Architecture and hardware",
         .entries = {{.option = "-j, --harts, --smp <N>",
                      .description = "Override the SoC profile hart count (1–16).",
                      .hidden_aliases = {"-smp", "--cores", "--num-harts"}},
                     {.option = "--dram-size, --ram-size <SIZE>",
                      .description = "Set DRAM size (for example, 128M or 2G).",
                      .hidden_aliases = {"--memory-size", "--mem-size"}},
                     {.option = "--isa <ISA>",
                      .description = "Select a RISC-V ISA profile such as rv32gc or rv64gc."},
                     {.option = "--profile <PROFILE>",
                      .description =
                          "Select formal architecture profile (rva23, rva23s64, rva22, legacy)."},
                     {.option = "--vlen <BITS>",
                      .description = "Set vector register length (128–1024; default: 256)."},
                     {.option = "--config <PRESET | FILE>",
                      .description =
                          "Select platform preset (virt-pcie, virt-mmio, rvcomp) or file path.",
                      .accepts_equals_value = true},
                     {.option = "--dump-soc-manifest <PRESET> [FILE]",
                      .description = "Export the normalized SoC registry as JSON."},
                     {.option = "--net <BACKEND>",
                      .description =
                          "Select the VirtIO network backend: user, tap, socket, or none."}}},
        {.title = "Execution and pipeline",
         .entries = {{.option = "-s, -e, --steps <N>",
                      .description = "Stop after N instructions retire across all harts.",
                      .hidden_aliases = {"--max-steps", "--instructions", "--insts"}},
                     {.option = "-t, -l, --timer <N>",
                      .description = "Raise a CLINT timer interrupt after N cycles."},
                     {.option = "--cpu-clock, --clock-hz <FREQ>",
                      .description = "Set CPU clock frequency (e.g. 100MHz, 160MHz, 1GHz).",
                      .hidden_aliases = {"--clock"}},
                     {.option = "--timebase-hz <FREQ>",
                      .description = "Set RTC timebase frequency (e.g. 10MHz, 32768Hz).",
                      .hidden_aliases = {"--timebase"}},
                     {.option = "-R, --realtime",
                      .description = "Throttle execution to host real time (1:1)."},
                     {.option = "--no-realtime",
                      .description = "Disable real-time pacing and run at maximum speed."},
                     {.option = "--mode <MODE>",
                      .description = "Select fast, detailed, or cycle-accurate execution."},
                     {.option = "--pipeline <TYPE>",
                      .description = "Select the CA pipeline: 3stage or 5stage (default)."},
                     {.option = "--no-forwarding", .description = "Disable operand forwarding."},
                     {.option = "--early-out-div",
                      .description = "Enable realistic data-dependent early-out division latency."},
                     {.option = "--bpred <TYPE>",
                      .description =
                          "Select none, static, bimodal, gshare, or tournament prediction.",
                      .hidden_aliases = {"--bpred-type"}},
                     {.option = "--bht-size <N>",
                      .description = "Set branch history table entries.",
                      .hidden_aliases = {"--bht-entries"}},
                     {.option = "--btb-size <N>",
                      .description = "Set branch target buffer entries.",
                      .hidden_aliases = {"--btb-entries"}},
                     {.option = "--ras-size <N>",
                      .description = "Set return address stack entries.",
                      .hidden_aliases = {"--ras-entries"}},
                     {.option = "--cpu-preset <PRESET>",
                      .description = "Select tiny, balanced, performance, or a model file."},
                     {.option = "--dump-cpu-model <MODEL> [FILE]",
                      .description = "Write a CPU model configuration."},
                     {.option = "--scaffold-cpu-model [FILE]",
                      .description = "Generate a starter CPU model configuration."},
                     {.option = "--validate-config, --validate-cpu-config <FILE>",
                      .description = "Validate a CPU model or platform configuration file."},
                     {.option = "--cfu-plugin <LIB.SO>",
                      .description = "Load a dynamic custom function unit plugin."},
                     {.option = "--bram-prewarm / --no-bram-prewarm",
                      .description = "Control ELF cache prewarming for BRAM parity."}}},
        {.title = "Logging and tracing",
         .entries =
             {{.option = "--log-level <LEVEL>",
               .description = "Set trace, debug, info, warn, error, or off."},
              {.option = "--log-format <FORMAT>",
               .description = "Select text, json, or json-pretty diagnostics."},
              {.option = "-q, --quiet", .description = "Suppress informational startup logs."},
              {.option = "-v, --verbose", .description = "Enable debug diagnostics."},
              {.option = "--log-file <FILE>",
               .description = "Mirror timestamped diagnostics to a file."},
              {.option = "--log-stderr",
               .description = "Route simulator diagnostic logs to stderr instead of stdout."},
              {.option = "--trace-dir <DIR>",
               .description = "Set the root directory for trace artifacts (default: trace).",
               .hidden_aliases = {"--trace-directory"}},
              {.option = "--log-mmio, --dlog", .description = "Record MMIO transactions."},
              {.option = "--trap-log, --traplog <FILE>",
               .description = "Write architectural trap and SBI events."},
              {.option = "--arch-trace <FILE>",
               .description = "Write JSONL architectural retirement and profile events.",
               .hidden_aliases = {"--architecture-trace"}},
              {.option = "--trace-level <0-4>", .description = "Limit architectural event detail."},
              {.option = "--trace-register-writes",
               .description = "Emit opt-in FP and vector register writes."},
              {.option = "--trace-events <LIST>",
               .description = "Filter event names using a comma-separated list."},
              {.option = "--trace-function <NAME>",
               .description = "Filter trace events by symbol name."},
              {.option = "--trace-device <NAME>",
               .description = "Filter trace events by device or component."},
              {.option = "--trace-hart <ID>", .description = "Filter by architectural hart ID."},
              {.option = "--trace-after-cycle <N>",
               .description = "Set the inclusive start of the trace cycle window."},
              {.option = "--trace-before-cycle <N>",
               .description = "Set the inclusive end of the trace cycle window."},
              {.option = "--trace-pc-range <START-END>",
               .description = "Filter architectural trace events by inclusive PC range."},
              {.option = "--trace", .description = "Record a full architectural text trace."},
              {.option = "-r, --trace-range <BEGIN> <END>",
               .description = "Record text trace snapshots within an inclusive mtime range."},
              {.option = "--trace-bpred", .description = "Record branch prediction events."},
              {.option = "--instmix", .description = "Write an instruction mix report on exit."}}},
        {.title = "Interactive mode and reports",
         .entries =
             {{.option = "-u, --tui",
               .description = "Enable the interactive TUI (default on a foreground terminal)."},
              {.option = "-c, --cli", .description = "Force headless command-line execution."},
              {.option = "--mission <FILE>",
               .description =
                   "Load a lesson mission and enable classroom guidance (interactive TUI)."},
              {.option = "--high-contrast", .description = "Enable the high-contrast TUI palette."},
              {.option = "--step-delay <US>",
               .description = "Pause execution for US microseconds per cycle/step.",
               .hidden_aliases = {"--speed"}},
              {.option = "--inspection-output <FILE>",
               .description = "Write paused-state TUI inspection reports."},
              {.option = "--summary <FILE>",
               .description = "Write a JSON execution summary on exit."},
              {.option = "--events <FILE>",
               .description = "Write newline-delimited lifecycle, interrupt, and DMA events."},
              {.option = "--uart-transcript <FILE>",
               .description = "Save guest UART output while keeping its normal destination."},
              {.option = "--save-checkpoint <FILE>",
               .description = "Save architectural state on exit.",
               .hidden_aliases = {"--checkpoint-out"}},
              {.option = "--load-checkpoint <FILE>",
               .description = "Resume from an architectural checkpoint.",
               .hidden_aliases = {"--checkpoint-in"}},
              {.option = "--checkpoint-every <CYCLES>",
               .description = "Write periodic architectural checkpoints."},
              {.option = "--checkpoint-dir <DIR>",
               .description = "Set the periodic checkpoint output directory."}}},
        {.title = "Debugging and information",
         .entries =
             {{.option = "-H, --tohost-addr <ADDR>",
               .description = "Set the tohost address used for test termination.",
               .hidden_aliases = {"--tohost"}},
              {.option = "--explain-inst <HEX>",
               .description = "Disassemble and explain a 32-bit instruction.",
               .hidden_aliases = {"--explain"}},
              {.option = "--gdb", .description = "Start the GDB remote stub in CLI mode."},
              {.option = "--gdb-port <PORT>",
               .description = "Set the GDB server port (default: 1234)."},
              {.option = "--lockstep", .description = "Enable Spike lockstep verification."},
              {.option = "--spike-bin <PATH>", .description = "Set the Spike executable path."},
              {.option = "--spike-elf <PATH>", .description = "Set the ELF image used by Spike."},
              {.option = "--version",
               .description = "Display the version and target architecture."},
              {.option = "--isa-info",
               .description = "Show qualified ISA and frontend capabilities.",
               .hidden_aliases = {"--capabilities"}},
              {.option = "--doctor", .description = "Diagnose terminal and runtime environment."},
              {.option = "--license", .description = "Display the license and copyright notice."},
              {.option = "-h, --help", .description = "Show this help."}}},
        {.title = "",
         .entries = {{.option = "--no-bram-prewarm",
                      .description = "",
                      .show_in_help = false,
                      .id = "--no-bram-prewarm"},
                     {.option = "--ca", .description = "", .show_in_help = false},
                     {.option = "--ia", .description = "", .show_in_help = false},
                     {.option = "--trace-pc", .description = "", .show_in_help = false}}}};
    return sections;
}
#pragma GCC diagnostic pop

auto cli_option_id(std::string_view arg) -> std::string_view {
    for (const auto& section : cli_help_sections()) {
        for (const auto& entry : section.entries) {
            std::string_view canonical = entry.id;
            std::string_view names = entry.option;
            while (canonical.empty() && !names.empty()) {
                const auto comma = names.find(',');
                auto name = names.substr(0, comma);
                while (!name.empty() && name.front() == ' ') name.remove_prefix(1);
                if (const auto space = name.find_first_of(" \t"); space != std::string_view::npos)
                    name = name.substr(0, space);
                if (name.starts_with("--")) {
                    canonical = name;
                    break;
                }
                if (comma == std::string_view::npos) break;
                names.remove_prefix(comma + 1);
            }
            if (canonical.empty()) continue;

            names = entry.option;
            while (!names.empty()) {
                const auto comma = names.find(',');
                auto name = names.substr(0, comma);
                while (!name.empty() && name.front() == ' ') name.remove_prefix(1);
                if (const auto space = name.find_first_of(" \t"); space != std::string_view::npos)
                    name = name.substr(0, space);
                if (arg == name || (entry.accepts_equals_value && arg.starts_with(name) &&
                                    arg.size() > name.size() && arg[name.size()] == '=')) {
                    return canonical;
                }
                if (comma == std::string_view::npos) break;
                names.remove_prefix(comma + 1);
            }
            for (const auto alias : entry.hidden_aliases) {
                if (arg == alias) return canonical;
            }
        }
    }
    return {};
}

auto cli_argument_form(const HelpEntry& entry) -> CliArgumentForm {
    const bool required = entry.option.find('<') != std::string_view::npos;
    const bool optional = entry.option.find('[') != std::string_view::npos;
    if (required && optional) return CliArgumentForm::RequiredThenOptional;
    if (required) return CliArgumentForm::Required;
    if (optional) return CliArgumentForm::Optional;
    return CliArgumentForm::None;
}

auto cli_short_flag_takes_argument(char c) -> bool {
    const char option_chars[] = {'-', c};
    const std::string_view option(option_chars, 2);
    const auto canonical = cli_option_id(option);
    if (canonical.empty()) return false;
    for (const auto& section : cli_help_sections()) {
        for (const auto& entry : section.entries) {
            bool contains_option =
                std::ranges::find(entry.hidden_aliases, option) != entry.hidden_aliases.end();
            std::string_view names = entry.option;
            while (!contains_option && !names.empty()) {
                const auto comma = names.find(',');
                auto name = names.substr(0, comma);
                while (!name.empty() && name.front() == ' ') name.remove_prefix(1);
                if (const auto space = name.find_first_of(" \t"); space != std::string_view::npos)
                    name = name.substr(0, space);
                contains_option = name == option;
                if (comma == std::string_view::npos) break;
                names.remove_prefix(comma + 1);
            }
            if (!contains_option) continue;
            const auto form = cli_argument_form(entry);
            return form == CliArgumentForm::Required ||
                   form == CliArgumentForm::RequiredThenOptional;
        }
    }
    return false;
}

}  // namespace

[[noreturn]] auto usage(std::string_view prog_name, int status) -> void {
    const bool use_color = simrv::util::terminal_color_enabled(STDOUT_FILENO);
    auto style = [use_color](std::string_view ansi_code) -> std::string_view {
        return use_color ? ansi_code : "";
    };
    using namespace simrv::util::ansi;
    const auto& sections = cli_help_sections();

    auto executable = prog_name.substr(prog_name.find_last_of('/') == std::string_view::npos
                                           ? 0
                                           : prog_name.find_last_of('/') + 1);
    winsize terminal_size{};
    unsigned terminal_width = 80;
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &terminal_size) == 0 && terminal_size.ws_col >= 60)
        terminal_width = terminal_size.ws_col;
    terminal_width = std::clamp(terminal_width, 60U, 120U);
    std::size_t option_width = 0;
    for (const auto& section : sections)
        for (const auto& entry : section.entries)
            if (entry.show_in_help) option_width = std::max(option_width, entry.option.size());
    const std::size_t description_width = std::max<std::size_t>(
        20, terminal_width > option_width + 6 ? terminal_width - option_width - 6 : 20);

    std::print(stdout, "{}SimRV{} — RISC-V simulator\n", style(kBoldFgBrightBlue), style(kReset));
    std::print(stdout, "{}Usage:{} {} [options]\n\n", style(kBold), style(kReset), executable);
    for (const auto& section : sections) {
        if (std::ranges::none_of(section.entries,
                                 [](const HelpEntry& entry) { return entry.show_in_help; })) {
            continue;
        }
        std::print(stdout, "{}{}{}\n", style(kBoldFgBrightBlue), section.title, style(kReset));
        for (const auto& entry : section.entries) {
            if (!entry.show_in_help) continue;
            std::size_t position = 0;
            bool first_line = true;
            while (position < entry.description.size()) {
                while (position < entry.description.size() && entry.description[position] == ' ')
                    ++position;
                const auto line_start = position;
                std::size_t line_end = line_start;
                std::size_t line_length = 0;
                while (position < entry.description.size()) {
                    const auto word_end = entry.description.find(' ', position);
                    const auto end =
                        word_end == std::string_view::npos ? entry.description.size() : word_end;
                    const auto word_length = end - position;
                    if (line_length != 0 && line_length + 1 + word_length > description_width)
                        break;
                    line_length += (line_length == 0 ? 0 : 1) + word_length;
                    line_end = end;
                    position = end;
                    if (position < entry.description.size()) ++position;
                }
                std::string wrapped(entry.description.substr(line_start, line_end - line_start));
                std::print(stdout, "  {}{:<{}}{}  {}\n", style(kBrightGreen),
                           first_line ? entry.option : std::string_view{}, option_width,
                           style(kReset), wrapped);
                first_line = false;
            }
        }
        std::println(stdout);
    }
    std::print(stdout,
               "{}Numeric values accept scaled suffixes:{} k/K (10³), m/M (10⁶), g/G (10⁹).\n\n",
               style(kBold), style(kReset));
    std::print(stdout, "{}Examples{}\n", style(kBoldFgBrightBlue), style(kReset));
    std::print(stdout, "  {} -m img/hello.bin -b\n", executable);
    std::print(stdout,
               "  {} --os --image linux/opensbi-linux-payload.elf --disk linux/rootfs.img --dtb "
               "linux/devicetree.dtb\n",
               executable);
    std::print(stdout, "  {} -m linux/opensbi-linux-payload.elf --tui\n", executable);

    std::exit(status);
}

auto usage(std::string_view prog_name, simrv::core::HostExitCode status) -> void {
    usage(prog_name, simrv::core::to_exit_code(status));
}

auto option_error(std::string_view msg, int status) -> void {
    if (simrv::util::terminal_color_enabled(STDERR_FILENO)) {
        std::println(stderr, "\033[1;31msimrv: error:\033[0m {}", msg);
    } else {
        std::println(stderr, "simrv: error: {}", msg);
    }
    std::exit(status);
}

auto option_error(std::string_view msg, simrv::core::HostExitCode status) -> void {
    option_error(msg, simrv::core::to_exit_code(status));
}

}  // namespace simrv::util
