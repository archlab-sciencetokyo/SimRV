/**
 * @file CliParser.cpp
 * @brief Command-line parser implementations.
 */
#include "simrv/util/CliParser.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/xlen/Types.hpp"
#include "simrv/util/FormatUtil.hpp"

#include <unistd.h>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <format>
#include <print>
#include <string>
#include <span>

namespace simrv::util {

using isa::MisaProfile;

namespace {

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

auto parse_u32_base0(std::string_view num, uint32_t& out) -> bool {
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
    out = static_cast<uint32_t>(value);
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

auto parse_trace_window(RuntimeOptions& options, std::span<char* const> args, std::size_t& index)
    -> std::expected<void, std::string> {
    return parse_scaled_required(args, index, "-t")
        .and_then([&](uint64_t begin) -> std::expected<void, std::string> {
            return parse_scaled_required(args, index, "-t")
                .and_then([&](uint64_t end) -> std::expected<void, std::string> {
                    if (begin > end) {
                        return std::unexpected("-t begin must be <= end");
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

struct ParsedMisa {
    MisaProfile profile;
    unsigned int xlen;
};

auto parse_misa_profile(std::string_view value) -> std::expected<ParsedMisa, std::string> {
    if (iequals(value, "i")) {
        return ParsedMisa{.profile = MisaProfile::I, .xlen = 0};
    }
    if (iequals(value, "imac")) {
        return ParsedMisa{.profile = MisaProfile::IMAC, .xlen = 0};
    }
    if (iequals(value, "gc")) {
        return ParsedMisa{.profile = MisaProfile::GC, .xlen = 0};
    }

    unsigned int parsed_xlen = 0;
    MisaProfile profile = MisaProfile::GC;
    bool valid = false;

    if (iequals(value, "rv32i")) {
        parsed_xlen = 32;
        profile = MisaProfile::I;
        valid = true;
    } else if (iequals(value, "rv64i")) {
        parsed_xlen = 64;
        profile = MisaProfile::I;
        valid = true;
    } else if (iequals(value, "rv32imac")) {
        parsed_xlen = 32;
        profile = MisaProfile::IMAC;
        valid = true;
    } else if (iequals(value, "rv64imac")) {
        parsed_xlen = 64;
        profile = MisaProfile::IMAC;
        valid = true;
    } else if (iequals(value, "rv32gc")) {
        parsed_xlen = 32;
        profile = MisaProfile::GC;
        valid = true;
    } else if (iequals(value, "rv64gc")) {
        parsed_xlen = 64;
        profile = MisaProfile::GC;
        valid = true;
    }

    if (valid) {
        if (parsed_xlen == 64 && simrv::xlen::kXLenBits == 32) {
            return std::unexpected(std::format(
                "cannot run a 64-bit MISA profile ({}) on a 32-bit simulator build", value));
        }
        return ParsedMisa{.profile = profile, .xlen = parsed_xlen};
    }

    const auto xlen_suffix = simrv::xlen::kIsXLen64 ? "64" : "32";
    auto supported =
        std::format("i, imac, gc, rv{}i, rv{}imac, rv{}gc", xlen_suffix, xlen_suffix, xlen_suffix);
    if constexpr (simrv::xlen::kIsXLen64) {
        supported += ", rv32i, rv32imac, rv32gc";
    }
    return std::unexpected(
        std::format("unsupported MISA profile '{}' (supported: {})", value, supported));
}

auto effective_misa_profile(const RuntimeOptions& options) -> MisaProfile {
    if (options.misa_override) {
        return options.misa_profile;
    }
    return MisaProfile::GC;
}

auto is_image_option(std::string_view arg) -> bool {
    return arg == "-m" || arg == "-k" || arg == "-i" || arg == "--image" || arg == "--kernel";
}

auto is_disk_option(std::string_view arg) -> bool {
    return arg == "-D" || arg == "--disk";
}

auto is_fdt_option(std::string_view arg) -> bool {
    return arg == "-f" || arg == "--fdt" || arg == "--dtb" || arg == "-c";
}

auto is_traplog_option(std::string_view arg) -> bool {
    return arg == "--trap-log" || arg == "-P";
}

auto is_steps_option(std::string_view arg) -> bool {
    return arg == "-s" || arg == "--steps" || arg == "-e";
}

auto is_timer_option(std::string_view arg) -> bool {
    return arg == "-t" || arg == "--timer" || arg == "-l";
}

auto is_tohost_addr_option(std::string_view arg) -> bool {
    return arg == "-H" || arg == "--tohost-addr";
}

auto is_trace_range_option(std::string_view arg) -> bool {
    return arg == "--trace-range" || arg == "-r";
}

auto is_trace_pc_period_option(std::string_view arg) -> bool {
    return arg == "--trace-pc-period" || arg == "-q";
}

auto is_dump_init_option(std::string_view arg) -> bool {
    return arg == "--dump-init" || arg == "-I";
}

auto is_baremetal_option(std::string_view arg) -> bool {
    return arg == "-b" || arg == "--baremetal" || arg == "-a" || arg == "--app";
}

auto is_os_option(std::string_view arg) -> bool {
    return arg == "--linux" || arg == "--os" || arg == "-o";
}

auto is_ca_option(std::string_view arg) -> bool {
    return arg == "--ca" || arg == "--cycle-accurate" || arg == "-C" || arg == "--high-accuracy" || arg == "--accuracy-mode";
}
auto is_ia_option(std::string_view arg) -> bool {
    return arg == "--ia" || arg == "--high-performance" || arg == "--perf-mode";
}

auto is_tui_option(std::string_view arg) -> bool {
    return arg == "--tui" || arg == "-u";
}

auto is_cli_option(std::string_view arg) -> bool {
    return arg == "--cli" || arg == "-c" || arg == "--headless" || arg == "--no-tui";
}

auto is_gui_option(std::string_view arg) -> bool {
    return arg == "--gui" || arg == "-G";
}

auto is_high_contrast_option(std::string_view arg) -> bool {
    return arg == "--high-contrast" || arg == "--contrast";
}

auto is_no_forwarding_option(std::string_view arg) -> bool {
    return arg == "--no-forwarding" || arg == "--disable-forwarding";
}

auto is_bp_type_option(std::string_view arg) -> bool {
    return arg == "--bp-type" || arg == "--bp";
}

auto is_btb_size_option(std::string_view arg) -> bool {
    return arg == "--btb-size" || arg == "--btb";
}

auto is_disable_ex_forwarding_option(std::string_view arg) -> bool {
    return arg == "--disable-ex-forwarding" || arg == "--no-ex-forwarding";
}

auto is_disable_mem_forwarding_option(std::string_view arg) -> bool {
    return arg == "--disable-mem-forwarding" || arg == "--no-mem-forwarding";
}

auto is_explain_inst_option(std::string_view arg) -> bool {
    return arg == "--explain-inst" || arg == "--explain";
}

auto is_opensbi_option(std::string_view arg) -> bool {
    return arg == "-B" || arg == "--opensbi";
}

auto is_debug_mode_option(std::string_view arg) -> bool {
    return arg == "-d" || arg == "--debug-mode";
}

auto is_log_mmio_option(std::string_view arg) -> bool {
    return arg == "--log-mmio" || arg == "-M";
}

auto is_instmix_option(std::string_view arg) -> bool {
    return arg == "--instmix" || arg == "-x";
}

auto is_trace_bpred_option(std::string_view arg) -> bool {
    return arg == "--trace-bpred" || arg == "-w";
}


auto is_debug_option(std::string_view arg) -> bool {
    return arg == "-g" || arg == "-v" || arg == "--debug" || arg == "--verbose";
}

auto is_gdb_port_option(std::string_view arg) -> bool {
    return arg == "--gdb-port" || arg == "--port" || arg == "-p";
}

auto is_gdb_option(std::string_view arg) -> bool {
    return arg == "--gdb" || arg == "-G";
}

auto is_help_option(std::string_view arg) -> bool {
    return arg == "-h" || arg == "--help";
}

auto parse_file_options(std::string_view arg, std::span<char* const> args, std::size_t& i, RuntimeOptions& options)
    -> std::expected<bool, std::string> {
    if (is_image_option(arg)) {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        options.fn_memimg = std::string(*value);
        return true;
    }
    if (is_disk_option(arg)) {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        options.fn_dskimg = std::string(*value);
        options.use_disk = true;
        options.appmode = false;
        return true;
    }
    if (is_fdt_option(arg)) {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        options.fn_dvtree = std::string(*value);
        options.appmode = false;
        return true;
    }
    if (arg == "--cpu-config") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        options.fn_cpuconfig = std::string(*value);
        return true;
    }
    if (is_traplog_option(arg)) {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        options.fn_traplog = std::string(*value);
        options.traplog_mode = true;
        return true;
    }
    return false;
}

auto parse_execution_options(std::string_view arg, std::span<char* const> args, std::size_t& i, RuntimeOptions& options)
    -> std::expected<bool, std::string> {
    if (is_steps_option(arg)) {
        auto value = parse_scaled_required(args, i, arg);
        if (!value) return std::unexpected(value.error());
        options.fincnt = *value;
        return true;
    }
    if (is_timer_option(arg)) {
        auto value = parse_scaled_required(args, i, arg);
        if (!value) return std::unexpected(value.error());
        options.enabletimer = *value;
        return true;
    }
    if (is_tohost_addr_option(arg)) {
        auto value = parse_u32_required(args, i, arg);
        if (!value) return std::unexpected(value.error());
        options.isatest_tohost = *value;
        return true;
    }
    if (is_trace_range_option(arg)) {
        auto trace = parse_trace_window(options, args, i);
        if (!trace) return std::unexpected(trace.error());
        return true;
    }
    if (is_trace_pc_period_option(arg)) {
        auto value = parse_scaled_required(args, i, arg);
        if (!value) return std::unexpected(value.error());
        options.strace = *value;
        return true;
    }
    if (is_dump_init_option(arg)) {
        auto value = parse_scaled_required(args, i, arg);
        if (!value) return std::unexpected(value.error());
        options.memimg = *value;
        return true;
    }
    return false;
}

auto parse_mode_options(std::string_view arg, std::span<char* const> args, std::size_t& i, ParseResult& result)
    -> std::expected<bool, std::string> {
    if (arg == "--misa") {
        auto value = next_argument(args, i, "--misa");
        if (!value) return std::unexpected(value.error());
        auto parsed_misa = parse_misa_profile(*value);
        if (!parsed_misa) return std::unexpected(parsed_misa.error());
        result.options.misa_profile = parsed_misa->profile;
        result.options.misa_xlen = parsed_misa->xlen;
        result.options.misa_override = true;
        return true;
    }
    if (arg == "--vlen" || arg == "-VLEN") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        uint32_t val = 0;
        if (!parse_u32_base0(*value, val)) {
            return std::unexpected(std::format("invalid numeric value for {}", arg));
        }
        if (val < 32 || val > 1024 || (val & (val - 1)) != 0) {
            return std::unexpected(std::format("VLEN must be a power of 2 between 32 and 1024 (got: {})", val));
        }
        result.options.vlen = val;
        return true;
    }
    if (is_baremetal_option(arg)) {
        result.options.appmode = true;
        return true;
    }
    if (is_os_option(arg)) {
        result.options.appmode = false;
        return true;
    }
    if (is_ca_option(arg)) {
        result.options.cycle_accurate = true;
        result.options.high_performance = false;
        return true;
    }
    if (is_ia_option(arg)) {
        result.options.cycle_accurate = false;
        result.options.high_performance = true;
        return true;
    }
    return false;
}

auto parse_tui_options(std::string_view arg, std::span<char* const> args, std::size_t& i, ParseResult& result)
    -> std::expected<bool, std::string> {
    if (is_tui_option(arg)) {
        result.options.tuimode = true;
        return true;
    }
    if (is_cli_option(arg)) {
        result.options.tuimode = false;
        return true;
    }
    if (is_gui_option(arg)) {
        result.options.gui_mode = true;
        return true;
    }
    if (arg == "--mouse-sensitivity" || arg == "--mouse-speed") {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        try {
            double val = std::stod(std::string(*value));
            result.options.mouse_sensitivity = val;
        } catch (...) {
            return std::unexpected(std::format("invalid double value for {}: {}", arg, *value));
        }
        return true;
    }
    if (is_high_contrast_option(arg)) {
        result.options.high_contrast = true;
        return true;
    }
    if (is_no_forwarding_option(arg)) {
        result.options.disable_forwarding = true;
        return true;
    }
    if (is_bp_type_option(arg)) {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        std::string val_str{ *value };
        if (val_str != "static-taken" && val_str != "static-not-taken" &&
            val_str != "1bit" && val_str != "2bit" && val_str != "gshare") {
            return std::unexpected(std::format("invalid branch predictor type: {}. Allowed: static-taken, static-not-taken, 1bit, 2bit, gshare", val_str));
        }
        result.options.bp_type = val_str;
        return true;
    }
    if (is_btb_size_option(arg)) {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        uint32_t val = 0;
        if (!parse_u32_base0(*value, val)) {
            return std::unexpected(std::format("invalid integer value for --btb-size: {}", *value));
        }
        result.options.btb_size = val;
        return true;
    }
    if (is_disable_ex_forwarding_option(arg)) {
        result.options.disable_ex_forwarding = true;
        return true;
    }
    if (is_disable_mem_forwarding_option(arg)) {
        result.options.disable_mem_forwarding = true;
        return true;
    }
    if (is_explain_inst_option(arg)) {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        uint32_t raw_val = 0;
        if (!parse_u32_base0(*value, raw_val)) {
            return std::unexpected(std::format("invalid hex instruction value for --explain-inst: {}", *value));
        }
        result.action = CliAction::ExplainInstruction;
        result.options.explain_inst_val = raw_val;
        return true;
    }
    if (is_opensbi_option(arg)) {
        simrv::log::warn(
            "Option '{}' is deprecated. OpenSBI is automatically enabled when a device tree is loaded.",
            arg);
        return true;
    }
    if (is_debug_mode_option(arg)) {
        result.options.debug_mode = true;
        return true;
    }
    if (is_log_mmio_option(arg)) {
        result.options.dlog_mode = true;
        return true;
    }
    if (is_instmix_option(arg)) {
        result.options.use_mix = true;
        return true;
    }
    return false;
}

auto parse_debug_cosrv_options(std::string_view arg, std::span<char* const> args, std::size_t& i, RuntimeOptions& options)
    -> std::expected<bool, std::string> {
    if (is_trace_bpred_option(arg)) {
        options.bp_trace = true;
        return true;
    }
    if (is_debug_option(arg)) {
        options.debugmode = true;
        return true;
    }
    if (is_gdb_option(arg)) {
        options.gdb_mode = true;
        return true;
    }
    if (is_gdb_port_option(arg)) {
        auto value = next_argument(args, i, arg);
        if (!value) return std::unexpected(value.error());
        uint64_t port_val = 0;
        if (!parse_scaled_u64(*value, port_val) || port_val == 0 || port_val > 65535) {
            return std::unexpected(std::format("invalid GDB port value for {}", arg));
        }
        options.gdb_port = static_cast<uint16_t>(port_val);
        return true;
    }
    if (arg == "--lockstep") {
        options.lockstep_mode = true;
        return true;
    }
    if (arg == "--rollback") {
        options.rollback = true;
        return true;
    }
    if (arg == "--step-delay" || arg == "--speed") {
        auto value = next_argument(args, i, std::string(arg));
        if (!value) return std::unexpected(value.error());
        uint64_t delay_val = 0;
        if (!parse_scaled_u64(*value, delay_val)) {
            return std::unexpected(std::format("invalid step delay value for {}", arg));
        }
        options.step_delay_us = delay_val;
        return true;
    }
    if (arg == "--spike-bin") {
        auto value = next_argument(args, i, "--spike-bin");
        if (!value) return std::unexpected(value.error());
        options.spike_bin = std::string(*value);
        return true;
    }
    if (arg == "--spike-elf") {
        auto value = next_argument(args, i, "--spike-elf");
        if (!value) return std::unexpected(value.error());
        options.spike_elf = std::string(*value);
        return true;
    }
    return false;
}

auto is_known_short_flag(char c) -> bool {
    switch (c) {
        case 'm': case 'k': case 'i':
        case 'D':
        case 'f': case 'c':
        case 'P':
        case 's': case 'e':
        case 't': case 'l':
        case 'H':
        case 'r':
        case 'q':
        case 'I':
        case 'p':
        case 'b': case 'a':
        case 'u':
        case 'G':
        case 'C':
        case 'd':
        case 'M':
        case 'x':
        case 'w':
        case 'g': case 'v':
        case 'B':
        case 'h':
            return true;
        default:
            return false;
    }
}

auto short_flag_takes_argument(char c) -> bool {
    switch (c) {
        case 'm': case 'k': case 'i':
        case 'D':
        case 'f': case 'c':
        case 'P':
        case 's': case 'e':
        case 't': case 'l':
        case 'H':
        case 'r':
        case 'q':
        case 'I':
        case 'p':
            return true;
        default:
            return false;
    }
}

auto expand_short_flags(const std::vector<std::string>& original_args) -> std::vector<std::string> {
    std::vector<std::string> expanded;
    expanded.push_back(original_args[0]);

    for (std::size_t i = 1; i < original_args.size(); ++i) {
        const std::string& arg = original_args[i];
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

} // namespace

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
    result.options.tuimode = (::isatty(STDIN_FILENO) != 0);

    for (std::size_t i = 1; i < expanded_span.size(); ++i) {
        std::string_view const arg = expanded_span[i];
        if (is_help_option(arg)) {
            result.action = CliAction::ShowHelp;
            return result;
        }
        if (arg == "--version") {
            result.action = CliAction::ShowVersion;
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

        return std::unexpected(std::format("unknown option : {}", arg));
    }

    if (needs_memory_image(result)) {
        return std::unexpected("-m/--image <FILE> is required to load a memory image");
    }

    return result;
}

auto apply_runtime_options(simrv::core::Machine* machine, const RuntimeOptions& options)
    -> std::expected<void, std::string> {
    machine->s_fn_memimg = options.fn_memimg;
    machine->s_fn_dskimg = options.fn_dskimg;
    machine->s_fn_dvtree = options.fn_dvtree;
    machine->s_fn_traplog = options.fn_traplog;

    machine->s_start_pc = options.start_pc;
    machine->s_fincnt = options.fincnt;
    machine->s_memimg = options.memimg;
    machine->s_strace = options.strace;
    machine->s_trace_begin = options.trace_begin;
    machine->s_trace_end = options.trace_end;
    machine->s_enabletimer = options.enabletimer;
    machine->s_isatest_tohost = options.isatest_tohost;
    machine->s_misa_profile = misa_profile_bits(effective_misa_profile(options));
    machine->s_misa_override = options.misa_override;
    machine->s_misa_xlen = options.misa_xlen;
    machine->s_vlen = options.vlen;

    machine->s_appmode = options.appmode;
    simrv::memory::g_appmode = options.appmode;
    machine->s_start_pc = options.start_pc;
    simrv::memory::g_dram_base = simrv::memory::kDramBaseAddress;
    machine->s_tuimode = options.tuimode;
    machine->s_gui_mode = options.gui_mode;
    machine->s_high_contrast = options.high_contrast;
    machine->s_debugmode = options.debugmode;
    machine->s_debug_mode = options.debug_mode;
    machine->s_mouse_sensitivity = options.mouse_sensitivity;
    machine->s_dlog_mode = options.dlog_mode;
    machine->s_traplog_mode = options.traplog_mode;
    machine->s_use_disk = options.use_disk;
    machine->s_use_mix = options.use_mix;
    machine->s_bp_trace = options.bp_trace;

    machine->s_cycle_accurate = options.cycle_accurate;
    machine->s_high_performance = options.high_performance;
    machine->s_fn_cpuconfig = options.fn_cpuconfig;
    machine->cpu.pipeline_sim.config.enable_forwarding = !options.disable_forwarding;
    machine->cpu.pipeline_sim.config.enable_ex_forwarding = !options.disable_ex_forwarding;
    machine->cpu.pipeline_sim.config.enable_mem_forwarding = !options.disable_mem_forwarding;
    machine->cpu.pipeline_sim.config.btb_entries = options.btb_size;

    {
        using BPT = pipeline::BranchPredictorType;
        if (options.bp_type == "static-not-taken")  machine->cpu.pipeline_sim.config.bp_type = BPT::StaticNotTaken;
        else if (options.bp_type == "static-taken") machine->cpu.pipeline_sim.config.bp_type = BPT::StaticTaken;
        else if (options.bp_type == "1bit")         machine->cpu.pipeline_sim.config.bp_type = BPT::OneBitBimodal;
        else if (options.bp_type == "gshare")       machine->cpu.pipeline_sim.config.bp_type = BPT::Gshare;
        else                                        machine->cpu.pipeline_sim.config.bp_type = BPT::TwoBitBimodal; // default "2bit"
    }

    machine->tracer.init_trace(options.trace_enabled);
    machine->tracer.init_dlog(options.dlog_mode);

    machine->cpu.trap_log_stream = nullptr;
    if (options.traplog_mode) {
        machine->tracer.init_trap_log(options.traplog_mode, options.fn_traplog);
        if (!machine->tracer.fp_traplog.is_open()) {
            return std::unexpected("cannot open trap/SBI log file: " + options.fn_traplog);
        }
        machine->cpu.trap_log_stream = &machine->tracer.fp_traplog;
    }

    machine->cpu.use_opensbi = options.use_opensbi || !options.fn_dvtree.empty();

    // Debug / co-simulation flags
    machine->s_gdb_mode = options.gdb_mode;
    machine->s_gdb_port = options.gdb_port;
    machine->s_lockstep_mode = options.lockstep_mode;
    machine->s_spike_bin = options.spike_bin;
    machine->s_spike_elf = options.spike_elf;
    machine->s_rollback_enabled = options.rollback;
    if (machine->tui && options.step_delay_us > 0) {
        machine->tui->step_delay_us_.store(options.step_delay_us, std::memory_order_relaxed);
    }

    return {};
}

auto needs_memory_image(const ParseResult& result) -> bool {
    return result.options.fn_memimg.empty() && !result.options.tuimode &&
           result.action != CliAction::ExplainInstruction &&
           result.action != CliAction::ShowHelp &&
           result.action != CliAction::ShowVersion;
}

[[noreturn]] auto usage(std::string_view prog_name, int status) -> void {
    const auto xlen_suffix = simrv::xlen::kIsXLen64 ? "64" : "32";
    const bool use_color = simrv::util::is_terminal(STDOUT_FILENO);

    auto style = [use_color](std::string_view ansi_code) -> std::string_view {
        return use_color ? ansi_code : "";
    };

    using namespace simrv::util::ansi;

    // Banner
    std::print(stdout, "\n{}{}{} SimRV - High-Performance RISC-V Functional Simulator {}\n\n",
               style(kBold), style(kBrightWhite), style(kReset), style(kReset));

    // Usage
    std::print(stdout, "{}Usage:{} {}{} [options]{}\n\n", style(kBold), style(kReset),
               style(kBrightGreen), prog_name, style(kReset));

    // Required
    std::print(stdout, "{}{}:{}{}\n", style(kBoldFgBrightBlue), "Required", style(kReset),
               style(kReset));
    std::print(stdout,
               "  {}-m, -k, -i, --image, --kernel {}{}<FILE>{} Memory image file to load\n\n",
               style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));

    // Devices & Files
    std::print(stdout, "{}{}:{}{}\n", style(kBoldFgBrightBlue), "Devices and Files", style(kReset),
               style(kReset));
    std::print(
        stdout,
        "  {}-D, --disk {}{}<FILE>{}      Disk image file (enables block storage virtio-disk)\n",
        style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(stdout,
               "  {}-f, -c, --fdt, --dtb {}{}<FILE>{} Device-tree binary file (FDT / DTB "
               "configuration)\n\n",
               style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));

    // Execution Control
    std::print(stdout, "{}{}:{}{}\n", style(kBoldFgBrightBlue),
               "Execution Control (runs on high-performance simrv::pipeline engine)", style(kReset),
               style(kReset));
    std::print(stdout, "  {}-s, -e, --steps {}{}<N>{}    Stop execution after N instructions\n",
               style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(
        stdout,
        "  {}-t, -l, --timer {}{}<N>{}    Enable timer after N cycles (CLINT ticker threshold)\n",
        style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(
        stdout,
        "  {}-b, -a, --baremetal, --app{} Binary mode (baremetal application execution, default)\n",
        style(kBrightGreen), style(kReset));
    std::print(
        stdout,
        "  {}-o, --linux, --os{}           OS mode (Linux kernel / RTOS boot mode)\n",
        style(kBrightGreen), style(kReset));
    std::print(
        stdout,
        "  {}-u, --tui{}                  Enable interactive TUI split-screen monitor mode\n",
        style(kBrightGreen), style(kReset));
    std::print(
        stdout,
        "  {}-G, --gui{}                  Enable external SDL3 graphical window\n",
        style(kBrightGreen), style(kReset));
    std::print(
        stdout,
        "  {}--mouse-sensitivity, --mouse-speed {}{}<FACTOR>{} Adjust mouse relative speed scaling factor (default: 1.0)\n",
        style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(stdout,
               "  {}--high-contrast, --contrast{} Toggle TUI colors to high-contrast palette\n",
               style(kBrightGreen), style(kReset));

    std::print(stdout,
               "  {}-C, --cycle-accurate, --ca{} Enable structural cycle-accurate performance "
               "simulation mode\n",
               style(kBrightGreen), style(kReset));
    std::print(
        stdout,
        "  {}--high-accuracy, --accuracy-mode{} Alias for --cycle-accurate (High-Accuracy Mode)\n",
        style(kBrightGreen), style(kReset));
    std::print(stdout,
               "  {}--high-performance, --perf-mode, --ia{} Enable optimized simulation mode "
               "bypassing caches/coroutines (default)\n",
               style(kBrightGreen), style(kReset));
    std::print(stdout,
               "  {}--cpu-config {}{}<FILE>{}    Load CPU latency configuration from a text file\n",
               style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(
        stdout,
        "  {}--misa {}{}<PROFILE>{}      Select CPU MISA profile: rv{}i | rv{}imac | rv{}gc\n\n",
        style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset), xlen_suffix,
        xlen_suffix, xlen_suffix);

    // Tracing and Debug
    std::print(stdout, "{}{}:{}{}\n", style(kBoldFgBrightBlue),
               "Tracing and Debug (outputs generated inside trace/ subdirectory)", style(kReset),
               style(kReset));
    std::print(stdout,
               "  {}-r, --trace-range {}{}<BG> <EN>{} Write trace snapshot to trace/trace.txt for "
               "instruction range\n",
               style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(stdout,
               "  {}--trace-pc-period {}{}<N>{}   Generate periodic trace/tracepc.txt every N "
               "instructions\n",
               style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(stdout,
               "  {}--trace-bpred{}           Generate trace/bpred.txt branch prediction trace\n",
               style(kBrightGreen), style(kReset));
    std::print(stdout,
               "  {}-I, --dump-init {}{}<N>{}     Dump initial architectural and TLB state "
               "artifacts at cycle N\n",
               style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(stdout, "  {}-g, -v, --debug, --verbose{} Enable verbose debug logging output\n",
               style(kBrightGreen), style(kReset));
    std::print(stdout,
               "  {}-M, --log-mmio{}              Enable interactive disk/console MMIO transaction "
               "logging\n",
               style(kBrightGreen), style(kReset));
    std::print(
        stdout,
        "  {}--trap-log {}{}<FILE>{}     Write comprehensive trap/SBI diagnostic logs to file\n",
        style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(stdout,
               "  {}--instmix{}               Write instruction mix metrics report to "
               "trace/instmix.txt on exit\n\n",
               style(kBrightGreen), style(kReset));

    // Termination Control
    std::print(stdout, "{}{}:{}{}\n", style(kBoldFgBrightBlue), "Termination Control", style(kReset),
               style(kReset));
    std::print(
        stdout,
        "  {}-H, --tohost-addr {}{}<AD>{} Custom tohost device MMIO address for termination\n\n",
        style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));

    // Debug / co-simulation
    std::print(stdout, "{}{}:{}{}\n", style(kBoldFgBrightBlue), "Debug / co-simulation",
               style(kReset), style(kReset));
    std::print(stdout, "  {}-g, --debug, -v{}            Enable debug logging in MMIO paths\n",
               style(kBrightGreen), style(kReset));
    std::print(stdout,
               "  {}-d, --debug-mode{}            Enable TUI debug diagnostics panel/symbol view\n",
               style(kBrightGreen), style(kReset));
    std::print(stdout,
               "  {}--explain-inst {}{}<HEX>{} Explain a hex instruction and exit (e.g., 0x005202b3)\n",
               style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(stdout, "  {}--version{}         Show compiler-injected version details and exit\n",
               style(kBrightGreen), style(kReset));
    std::print(stdout,
               "  {}--no-forwarding{}    Disable operand forwarding in cycle-accurate simulation\n",
               style(kBrightGreen), style(kReset));
    std::print(stdout,
               "  {}--no-ex-forwarding{}  Disable EX-to-EX forwarding path only\n",
               style(kBrightGreen), style(kReset));
    std::print(stdout,
               "  {}--no-mem-forwarding{} Disable MEM-to-EX forwarding path only\n",
               style(kBrightGreen), style(kReset));
    std::print(stdout,
               "  {}--bp-type {}{}<TYPE>{}  Branch predictor type [static-not-taken|static-taken|1bit|2bit|gshare] (default: 2bit)\n",
               style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(stdout,
               "  {}--btb-size {}{}<N>{}    Branch target buffer entries (default: 128, 0 = disabled)\n",
               style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(
        stdout,
        "  {}-G, --gdb{}                  Enable GDB RSP stub (waits for client before running)\n",
        style(kBrightGreen), style(kReset));
    std::print(
        stdout,
        "  {}-p, --port, --gdb-port {}{}<PORT>{} Override GDB stub listen port (default: 1234)\n",
        style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(
        stdout,
        "  {}--lockstep{}         Enable Spike lockstep instruction-by-instruction verification\n",
        style(kBrightGreen), style(kReset));
    std::print(
        stdout,
        "  {}--spike-bin {}{}<PATH>{} Path to spike binary for lockstep (default: spike in PATH)\n",
        style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));
    std::print(
        stdout,
        "  {}--spike-elf {}{}<PATH>{} Path to spike ELF image for lockstep co-simulation\n\n",
        style(kBrightGreen), style(kBrightBlack), style(kReset), style(kReset));

    // scaled suffixes
    std::print(
        stdout,
        "{}Numeric values accept standard scaled suffixes:{} k/K (1e3), m/M (1e6), g/G (1e9)\n\n",
        style(kBold), style(kReset));

    // Examples
    std::print(stdout, "{}{}:{}{}\n", style(kBoldFgBrightBlue), "Examples", style(kReset),
               style(kReset));
    std::print(stdout, "  {}{}{} -m img/hello.bin -b\n", style(kBrightBlack), prog_name,
               style(kReset));
    std::print(stdout,
               "  {}{}{} --image linux-images/rv{}/fw_payload.bin --disk "
               "linux-images/rv{}/root.bin --fdt linux-images/rv{}/devicetree.dtb\n",
               style(kBrightBlack), prog_name, style(kReset), xlen_suffix, xlen_suffix,
               xlen_suffix);
    std::print(stdout, "  {}{}{} --image linux-images/rv{}/fw_payload.bin -u\n",
               style(kBrightBlack), prog_name, style(kReset), xlen_suffix);

    std::exit(status);
}

auto option_error(std::string_view msg, int status) -> void {
    simrv::log::error("{}", msg);
    std::exit(status);
}

} // namespace simrv::util
