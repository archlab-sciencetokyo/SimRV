#pragma once

#include <expected>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "simrv/Define.hpp"
#include "simrv/core/Boot.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/RuntimeProfile.hpp"

namespace simrv::util {

enum class CliAction : uint8_t {
    Run,
    ShowHelp,
    ShowVersion,
    ShowLicense,
    ExplainInstruction,
    Attach
};
enum class RequestedExecutionMode : uint8_t { Fast, Detailed, CycleAccurate };

struct RuntimeOptions {
    std::string fn_memimg;
    std::string fn_dskimg;
    std::string fn_dvtree;
    std::string fn_traplog;
    std::string fn_log;
    std::string inspection_output;

    Address start_pc = simrv::boot::kStartPc;
    Counter fincnt = std::numeric_limits<Counter>::max();
    Counter memimg = std::numeric_limits<Counter>::max();
    Counter strace = 0;
    Counter trace_begin = std::numeric_limits<Counter>::max();
    Counter trace_end = std::numeric_limits<Counter>::max();
    Counter enabletimer = 0UL;
    Address isatest_tohost = 0x80001000;
    isa::MisaProfile misa_profile = isa::MisaProfile::GCBV;
    bool misa_override = false;
    unsigned int misa_xlen = 0;

    bool appmode = true;
    bool tuimode = false;
    bool explicit_tui_mode = false;
    bool explicit_cli_mode = false;
    bool verbose = false;
    bool quiet = false;
    std::optional<simrv::log::Level> log_level;
    bool dlog_mode = false;
    bool traplog_mode = false;
    bool use_disk = false;
    bool use_mix = false;
    bool bp_trace = false;
    bool trace_enabled = false;
    bool use_opensbi = false;
    RequestedExecutionMode execution_mode = RequestedExecutionMode::Fast;
    bool execution_mode_explicit = false;
    bool high_contrast = false;
    bool class_mode = false;
    std::string mission;
    std::string pipeline_type = "5stage";
    bool disable_forwarding = false;
    std::string bpred_type;
    uint32_t bht_size = 0;
    uint32_t btb_size = 0;
    uint32_t ras_size = 0;

    // Debug / co-simulation options
    bool gdb_mode = false;
    uint16_t gdb_port = 1234;
    bool lockstep_mode = false;
    std::string spike_bin = "spike";
    std::string spike_elf;

    std::string fn_cpuconfig;
    std::optional<simrv::pipeline::CpuModelProfile> cpu_model_profile;
    std::string fn_cfu_plugin;
    std::string fn_dump_dmem;
    uint64_t step_delay_us = 0;
    uint32_t explain_inst_val = 0;
    double mouse_sensitivity = 1.0;
    unsigned int vlen = 0;
    uint32_t num_harts = 1;
    uint32_t smp_quantum = 100;
    bool smp_multithreaded = false;
    uint64_t dram_size = 0;
    simrv::core::PlatformProfile platform_profile = simrv::core::PlatformProfile::Pcie;
    std::string net_mode = "user";
    bool server_mode = false;
    std::string server_endpoint;
    bool attach_mode = false;
    std::string attach_endpoint;

    [[nodiscard]] auto to_machine_config() const -> simrv::core::MachineConfig;
};

struct ParseResult {
    CliAction action = CliAction::Run;
    RuntimeOptions options{};
};

[[nodiscard]] auto resolve_runtime_profile(const RuntimeOptions& options)
    -> simrv::core::RuntimeProfile;

auto parse_command_line(std::span<char* const> args) -> std::expected<ParseResult, std::string>;
auto apply_runtime_options(simrv::core::Machine* machine, const RuntimeOptions& options)
    -> std::expected<void, std::string>;
[[noreturn]] auto usage(std::string_view prog_name, int status) -> void;
[[noreturn]] auto option_error(std::string_view msg, int status = 1) -> void;
auto needs_memory_image(const ParseResult& result) -> bool;

}  // namespace simrv::util
