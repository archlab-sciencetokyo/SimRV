/**
 * @file InspectionReport.cpp
 * @brief JSON serialization for portable educational inspection evidence.
 */
#include "simrv/tui/InspectionReport.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>
#include <sstream>

#include "simrv/core/BuildInfo.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "simrv/pipeline/PipelineConfig.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::tui {
namespace {

auto json_escape(std::string_view value) -> std::string {
    std::string escaped;
    escaped.reserve(value.size());
    for (const unsigned char ch : value) {
        switch (ch) {
            case '"':
                escaped += "\\\"";
                break;
            case '\\':
                escaped += "\\\\";
                break;
            case '\b':
                escaped += "\\b";
                break;
            case '\f':
                escaped += "\\f";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                if (ch < 0x20U) {
                    escaped += std::format("\\u{:04x}", ch);
                } else {
                    escaped.push_back(static_cast<char>(ch));
                }
                break;
        }
    }
    return escaped;
}

auto quoted(std::string_view value) -> std::string { return '"' + json_escape(value) + '"'; }

template <typename Value>
auto hex_value(Value value) -> std::string {
    return std::format("0x{:0{}x}", static_cast<uint64_t>(value), simrv::xlen::kXLenHexDigits);
}

auto privilege_name(PrivilegeLevel privilege) -> std::string_view {
    switch (privilege) {
        case PrivilegeLevel::User:
            return "user";
        case PrivilegeLevel::Supervisor:
            return "supervisor";
        case PrivilegeLevel::Machine:
            return "machine";
    }
    return "unknown";
}

auto platform_name(simrv::core::PlatformProfile profile) -> std::string_view {
    return profile == simrv::core::PlatformProfile::Pcie ? "pcie" : "mmio";
}

}  // namespace

auto make_inspection_report(const simrv::core::Machine& machine, size_t hart,
                            std::span<const std::string> recent_trace) -> std::string {
    const size_t selected_hart = machine.num_harts() == 0 ? 0 : hart % machine.num_harts();
    const auto& cpu = machine.hart(selected_hart);
    const auto& state = cpu.state();
    const auto& context = cpu.pipeline_context;
    const auto snapshot = machine.tui_execution_snapshot(selected_hart);
    const auto& config = machine.configuration();

    std::ostringstream out;
    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"simulator\": {\"version\": " << quoted(simrv::buildinfo::kVersion)
        << ", \"git_sha\": " << quoted(simrv::buildinfo::kGitSha)
        << ", \"xlen\": " << simrv::xlen::kXLenBits << "},\n";
    out << "  \"configuration\": {\"engine\": " << quoted(machine.runtime_profile.execution_name())
        << ", \"platform\": " << quoted(platform_name(config.platform_profile))
        << ", \"baremetal\": " << (config.execution.appmode ? "true" : "false")
        << ", \"harts\": " << config.execution.num_harts << ", \"pipeline\": "
        << quoted(simrv::pipeline::pipeline_type_name(config.execution.pipeline_type))
        << ", \"dram_base\": " << quoted(hex_value(config.memory.dram_base))
        << ", \"dram_size\": " << config.memory.dram_size << ", \"vlen\": " << state.regs.vlen
        << "},\n";
    out << "  \"selected_hart\": " << selected_hart << ",\n";
    out << "  \"instruction\": {\"pc\": " << quoted(hex_value(state.pc))
        << ", \"raw\": " << quoted(std::format("0x{:08x}", context.ir_org))
        << ", \"operation\": " << quoted(simrv::pipeline::operation_name(context.op_id)) << "},\n";
    out << "  \"architectural_state\": {\n";
    out << "    \"privilege\": " << quoted(privilege_name(state.priv)) << ",\n";
    out << "    \"gpr\": [";
    for (size_t reg = 0; reg < simrv::core::RegisterFile::kNumRegisters; ++reg) {
        if (reg != 0) out << ", ";
        out << quoted(hex_value(state.regs.read(static_cast<RegId>(reg))));
    }
    out << "],\n";
    out << "    \"csr\": {\"mstatus\": " << quoted(hex_value(state.mstatus))
        << ", \"mepc\": " << quoted(hex_value(state.mepc))
        << ", \"mcause\": " << quoted(hex_value(state.mcause))
        << ", \"mtval\": " << quoted(hex_value(state.mtval))
        << ", \"satp\": " << quoted(hex_value(state.satp))
        << ", \"sepc\": " << quoted(hex_value(state.sepc))
        << ", \"scause\": " << quoted(hex_value(state.scause))
        << ", \"stval\": " << quoted(hex_value(state.stval)) << "}\n";
    out << "  },\n";
    out << "  \"counters\": {\"instructions\": " << snapshot.instruction_count
        << ", \"cycles\": " << snapshot.cycle_count << ", \"icache_hits\": " << snapshot.icache_hits
        << ", \"icache_misses\": " << snapshot.icache_misses
        << ", \"dcache_hits\": " << snapshot.dcache_hits
        << ", \"dcache_misses\": " << snapshot.dcache_misses
        << ", \"data_hazard_stalls\": " << snapshot.ca_stats.data_hazard_stalls
        << ", \"control_hazard_bubbles\": " << snapshot.ca_stats.control_hazard_bubbles << "},\n";
    out << "  \"recent_trace\": [";
    const size_t first = recent_trace.size() > 32 ? recent_trace.size() - 32 : 0;
    for (size_t index = first; index < recent_trace.size(); ++index) {
        if (index != first) out << ", ";
        out << quoted(recent_trace[index]);
    }
    out << "]\n";
    out << "}\n";
    return out.str();
}

auto write_inspection_report(const std::filesystem::path& path, std::string_view report,
                             bool overwrite)
    -> std::expected<InspectionReportWriteStatus, std::string> {
    if (path.empty()) return std::unexpected("inspection output path is empty");
    std::error_code error;
    if (std::filesystem::exists(path, error) && !overwrite) {
        return InspectionReportWriteStatus::WouldOverwrite;
    }
    if (error) {
        return std::unexpected(
            std::format("cannot inspect output path '{}': {}", path.string(), error.message()));
    }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return std::unexpected(std::format("cannot open inspection output '{}'", path.string()));
    }
    output << report;
    output.close();
    if (!output) {
        return std::unexpected(std::format("cannot write inspection output '{}'", path.string()));
    }
    return InspectionReportWriteStatus::Written;
}

}  // namespace simrv::tui
