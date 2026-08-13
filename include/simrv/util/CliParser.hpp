#pragma once

#include <expected>
#include <limits>
#include <span>
#include <string>
#include <string_view>

#include "simrv/Define.hpp"
#include "simrv/core/Boot.hpp"
#include "simrv/core/Machine.hpp"

namespace simrv::util {

enum class CliAction : uint8_t { Run, ShowHelp, ShowVersion, ExplainInstruction };

struct RuntimeOptions {
    std::string fn_memimg;
    std::string fn_dskimg;
    std::string fn_dvtree;
    std::string fn_traplog;

    Address start_pc = simrv::boot::kStartPc;
    Counter fincnt = std::numeric_limits<Counter>::max();
    Counter memimg = std::numeric_limits<Counter>::max();
    Counter strace = 0;
    Counter trace_begin = std::numeric_limits<Counter>::max();
    Counter trace_end = std::numeric_limits<Counter>::max();
    Counter enabletimer = 0UL;
    Address isatest_tohost = 0x80001000;
    isa::MisaProfile misa_profile = isa::MisaProfile::GC;
    bool misa_override = false;
    unsigned int misa_xlen = 0;

    bool appmode = true;
    bool tuimode = false;
    bool explicit_tui_mode = false;
    bool explicit_cli_mode = false;
    bool gui_mode = false;
    bool debugmode = false;
    bool dlog_mode = false;
    bool traplog_mode = false;
    bool use_disk = false;
    bool use_mix = false;
    bool bp_trace = false;
    bool trace_enabled = false;
    bool use_opensbi = false;
    bool cycle_accurate = false;
    bool high_performance = true;
    bool high_contrast = false;
    bool disable_forwarding = false;
    std::string bp_type = "2bit";
    uint32_t btb_size = 128;
    bool disable_ex_forwarding = false;
    bool disable_mem_forwarding = false;

    // Debug / co-simulation options
    bool gdb_mode = false;
    uint16_t gdb_port = 1234;
    bool lockstep_mode = false;
    std::string spike_bin = "spike";
    std::string spike_elf;

    std::string fn_cpuconfig;
    bool debug_mode = false;
    bool rollback = false;
    uint64_t step_delay_us = 0;
    uint32_t explain_inst_val = 0;
    double mouse_sensitivity = 1.0;
    unsigned int vlen = 0;
};

struct ParseResult {
    CliAction action = CliAction::Run;
    RuntimeOptions options{};
};

auto parse_command_line(std::span<char* const> args) -> std::expected<ParseResult, std::string>;
auto apply_runtime_options(simrv::core::Machine* machine, const RuntimeOptions& options)
    -> std::expected<void, std::string>;
[[noreturn]] auto usage(std::string_view prog_name, int status) -> void;
[[noreturn]] auto option_error(std::string_view msg, int status = 1) -> void;
auto needs_memory_image(const ParseResult& result) -> bool;

}  // namespace simrv::util
