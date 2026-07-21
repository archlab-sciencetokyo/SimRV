/**
 * @file RegisterPanePipeline.cpp
 * @brief Pipeline visualization pane rendering for the TUI register panel.
 */
#include "simrv/tui/LeftPane.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/util/FormatUtil.hpp"
#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/xlen/Helpers.hpp"
#include "simrv/xlen/Types.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "simrv/pipeline/PipelineSim.hpp"
#include "simrv/pipeline/PipelineModel.hpp"
#include <format>
#include <string>
#include <vector>
#include <array>
#include <algorithm>

namespace simrv::tui {

using simrv::isa::Opcode;
using simrv::isa::OperationId;
using enum simrv::isa::OperationId;
using simrv::isa::InstFormat;

namespace {

auto has_stage_raw_hazard(const simrv::pipeline::PipelineSim& ps,
                          const simrv::pipeline::PipelineReg& d_reg,
                          const simrv::pipeline::PipelineReg& stage_reg) -> bool {
    if (!stage_reg.valid || !stage_reg.writes_reg || stage_reg.rd == static_cast<RegId>(0)) {
        return false;
    }
    if (ps.config.enable_forwarding) {
        if (stage_reg.remaining_latency == 0) {
            return false;
        }
    }
    bool reads_rs1 = (d_reg.rs1 != static_cast<RegId>(0));
    bool reads_rs2 = (d_reg.rs2 != static_cast<RegId>(0));
    return (reads_rs1 && d_reg.rs1 == stage_reg.rd) || (reads_rs2 && d_reg.rs2 == stage_reg.rd);
}

auto is_raw_hazard_stalled(const simrv::pipeline::PipelineSim& ps) -> bool {
    if (!ps.d_reg().valid) {
        return false;
    }
    return has_stage_raw_hazard(ps, ps.d_reg(), ps.e_reg()) ||
           has_stage_raw_hazard(ps, ps.d_reg(), ps.m_reg());
}

auto get_stage_desc(const simrv::pipeline::PipelineReg& reg, uint32_t stall_rem, const std::string& stall_type, bool raw_stall = false) -> std::string {
    if (!reg.valid) {
        return std::format("{}bubble/empty\033[0m", kThemeMuted);
    }
    std::string_view op_name = "UNKNOWN";
    if (static_cast<std::size_t>(reg.op_id) < simrv::pipeline::OPERATION_NAME.size()) {
        op_name = simrv::pipeline::OPERATION_NAME.at(static_cast<std::size_t>(reg.op_id));
    }
    std::string desc = std::format("\033[1m{}0x{:0{}x}\033[0m ({}{}{})",
                                   kThemeMint, reg.pc, simrv::xlen::kXLenHexDigits,
                                   kThemeSky, op_name, "\033[0m");
    if (stall_rem > 0) {
        desc += std::format(" {}[Stall: {} ({} clk)]\033[0m", kThemeCoral, stall_type, stall_rem);
    } else if (raw_stall) {
        desc += std::format(" {}[Stall: RAW Hazard]\033[0m", kThemeCoral);
    } else if (reg.remaining_latency > 0) {
        desc += std::format(" {}[Lat: {} clk]\033[0m", kThemePeach, reg.remaining_latency);
    }
    return desc;
}

auto get_stall_status(bool active, const char* active_str = "Active") -> std::pair<const char*, const char*> {
    if (active) {
        return {active_str, kThemePeach};
    }
    return {"None", kThemeMint};
}

auto get_active_branch_pc(const simrv::pipeline::PipelineSim& ps) -> Register {
    if (ps.e_reg().valid && (ps.e_reg().is_branch || ps.e_reg().is_jump)) {
        return ps.e_reg().pc;
    }
    if (ps.d_reg().valid && (ps.d_reg().is_branch || ps.d_reg().is_jump)) {
        return ps.d_reg().pc;
    }
    return 0;
}

auto get_bht_string(const simrv::pipeline::PipelineSim& ps, Register pc) -> std::string {
    if (pc == 0) {
        return "N/A";
    }
    uint8_t counter = ps.get_model() ? ps.get_model()->get_bht_entry(pc) : 1;
    switch (counter) {
        case 0: return "Strongly Not Taken (00)";
        case 1: return "Weakly Not Taken (01)";
        case 2: return "Weakly Taken (10)";
        default: return "Strongly Taken (11)";
    }
}

auto get_btb_string(const simrv::pipeline::PipelineSim& ps, Register pc) -> std::string {
    if (pc == 0) {
        return "N/A";
    }
    auto [valid, target] = ps.get_model() ? ps.get_model()->get_btb_target(pc) : std::pair<bool, Register>{false, 0};
    if (valid) {
        return std::format("Hit (Target: 0x{:0{}x})", target, simrv::xlen::kXLenHexDigits);
    }
    return "Miss";
}

auto get_active_forwarding_paths(const simrv::pipeline::PipelineSim& ps) -> std::vector<std::string> {
    std::vector<std::string> paths;
    if (!ps.config.enable_forwarding) return paths;

    auto d = ps.d_reg();
    if (!d.valid) return paths;

    auto e = ps.e_reg();
    auto m = ps.m_reg();
    auto w = ps.w_reg();

    auto check_src = [&](RegId rs, const std::string& rs_name) -> void {
        if (rs == static_cast<RegId>(0)) return;

        if (e.valid && e.writes_reg && e.rd == rs) {
            if (e.remaining_latency == 0) {
                paths.push_back(std::format("EX->ID({})", rs_name));
                return;
            }
        }
        if (m.valid && m.writes_reg && m.rd == rs) {
            if (m.remaining_latency == 0) {
                paths.push_back(std::format("MEM->ID({})", rs_name));
                return;
            }
        }
        if (w.valid && w.writes_reg && w.rd == rs) {
            paths.push_back(std::format("WB->ID({})", rs_name));
            return;
        }
    };

    check_src(d.rs1, "rs1");
    check_src(d.rs2, "rs2");

    return paths;
}

} // namespace

auto LeftPane::render_pipeline_stages(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string {
    if (machine_.s_cycle_accurate) {
        if (logical_row < 16) {
            return render_pipeline_timeline(cpu, logical_row, col_width + right_width);
        }
        return render_pipeline_stages_cycle_accurate(cpu, logical_row, col_width, right_width);
    } else {
        return render_pipeline_stages_functional(cpu, logical_row, col_width, right_width);
    }
}

auto LeftPane::render_pipeline_stages_cycle_accurate(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string {
    int const val = logical_row - 16;
    if (val >= 0 && val <= 5) {
        return render_pipeline_stages_ca_core(cpu, val, col_width + right_width);
    }
    if (val >= 6 && val <= 10) {
        return render_pipeline_stages_ca_hazards(cpu, val, col_width, right_width);
    }
    return render_pipeline_stages_ca_pred(cpu, val, col_width + right_width);
}

auto LeftPane::render_pipeline_stages_ca_core(const simrv::core::CPU& cpu, int stage_idx, int width) -> std::string {
    auto& ps = cpu.pipeline_sim;
    bool is_raw_stalled = is_raw_hazard_stalled(ps);

    switch (stage_idx) {
        case 0:
            return section_line("Pipeline Stages (Cycle-Accurate Mode)", width);
        case 1:
            {
                std::string stall_type = ps.tlb_stall_remaining() > 0 ? "TLB Miss" : "ICache Miss";
                uint32_t stall_rem = ps.tlb_stall_remaining() > 0 ? ps.tlb_stall_remaining() : ps.icache_stall_remaining();
                return format_to_width(std::format("  \033[1m{}IF\033[0m  : {}", kThemeSky, get_stage_desc(ps.f_reg(), stall_rem, stall_type)), width);
            }
        case 2:
            return format_to_width(std::format("  \033[1m{}ID\033[0m  : {}", kThemeSky, get_stage_desc(ps.d_reg(), 0, "", is_raw_stalled)), width);
        case 3:
            return format_to_width(std::format("  \033[1m{}EX\033[0m  : {}", kThemeSky, get_stage_desc(ps.e_reg(), ps.div_busy_cycles_remaining(), "Divider")), width);
        case 4:
            return format_to_width(std::format("  \033[1m{}MEM\033[0m : {}", kThemeSky, get_stage_desc(ps.m_reg(), ps.dcache_stall_remaining(), "DCache Miss")), width);
        case 5:
            return format_to_width(std::format("  \033[1m{}WB\033[0m  : {}", kThemeSky, get_stage_desc(ps.w_reg(), 0, "")), width);
        default:
            return format_to_width("", width);
    }
}

auto LeftPane::render_pipeline_stages_ca_hazards(const simrv::core::CPU& cpu, int stage_idx, int col_width, int right_width) -> std::string {
    auto& ps = cpu.pipeline_sim;
    int const width = col_width + right_width;
    bool is_raw_stalled = is_raw_hazard_stalled(ps);

    auto [raw_status, raw_color] = get_stall_status(is_raw_stalled);
    auto [div_status, div_color] = get_stall_status(ps.div_busy_cycles_remaining() > 0);
    auto [ic_status, ic_color] = get_stall_status(ps.icache_stall_remaining() > 0);
    auto [dc_status, dc_color] = get_stall_status(ps.dcache_stall_remaining() > 0);
    auto [tlb_status, tlb_color] = get_stall_status(ps.tlb_stall_remaining() > 0);
    auto [ctrl_status, ctrl_color] = get_stall_status(ps.control_bubble_remaining() > 0, "Redirecting");

    switch (stage_idx) {
        case 6:
            return section_line("Active Stalls & Hazards", width);
        case 7:
            return render_pair("RAW Stall", raw_status, raw_color,
                               "Divider St", div_status, div_color,
                               col_width, right_width, 10);
        case 8:
            return render_pair("ICache St", ic_status, ic_color,
                               "DCache St", dc_status, dc_color,
                               col_width, right_width, 10);
        case 9:
            return render_pair("TLB Stall", tlb_status, tlb_color,
                               "Ctrl St", ctrl_status, ctrl_color,
                               col_width, right_width, 10);
        case 10:
            {
                auto paths = get_active_forwarding_paths(ps);
                std::string paths_str = "None";
                if (!paths.empty()) {
                    paths_str = "";
                    for (size_t i = 0; i < paths.size(); ++i) {
                        if (i > 0) paths_str += ", ";
                        paths_str += paths[i];
                    }
                }
                const char* paths_color = paths.empty() ? kThemeVal : kThemeMint;
                return render_pair("Active Fwd", paths_str, paths_color,
                                   "Fwd Mode", ps.config.enable_forwarding ? "Enabled" : "Disabled", kThemeVal,
                                   col_width, right_width, 10);
            }
        default:
            return format_to_width("", width);
    }
}



auto LeftPane::render_pipeline_stages_ca_pred(const simrv::core::CPU& cpu, int stage_idx, int width) -> std::string {
    auto& ps = cpu.pipeline_sim;

    switch (stage_idx) {
        case 11:
            return section_line("Branch Prediction & BTB", width);
        case 12:
            {
                Register pc = get_active_branch_pc(ps);
                std::string bht_str = get_bht_string(ps, pc);
                return format_to_width(std::format("  {}BHT State\033[0m : {}{}\033[0m", kThemeText, kThemeVal, bht_str), width);
            }
        case 13:
            {
                Register pc = get_active_branch_pc(ps);
                std::string btb_str = get_btb_string(ps, pc);
                return format_to_width(std::format("  {}BTB State\033[0m : {}{}\033[0m", kThemeText, kThemeVal, btb_str), width);
            }
        case 14:
            return section_line("End Pipeline Visualizer", width);
        default:
            return format_to_width("", width);
    }
}

auto LeftPane::render_pipeline_stages_functional(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string {
    if (logical_row >= 0 && logical_row < 16) {
        return render_pipeline_stages_functional_low(cpu, logical_row, col_width, right_width);
    }
    return render_pipeline_stages_functional_high(cpu, logical_row, col_width, right_width);
}

auto LeftPane::render_pipeline_stages_functional_low(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string {
    if (logical_row >= 0 && logical_row <= 7) {
        return render_pipeline_stages_functional_low_part1(cpu, logical_row, col_width, right_width);
    }
    if (logical_row >= 8 && logical_row <= 15) {
        return render_pipeline_stages_functional_low_part2(cpu, logical_row, col_width, right_width);
    }
    return format_to_width(std::format(" {}Pipeline page\033[0m", kThemeMuted), col_width + right_width);
}

auto LeftPane::render_pipeline_stages_functional_low_part1(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string {
    int const width = col_width + right_width;
    auto& ctx = cpu.pipeline_context;
    switch (logical_row) {
        case 0:
            return section_line("── IF/CVT", width);
        case 1:
            return render_pair(
                "cpc", std::format("0x{:0{}x}", ctx.cpc, simrv::xlen::kXLenHexDigits),
                kThemeMint, "ir_org", std::format("0x{:08x}", ctx.ir_org), kThemeVal,
                col_width, right_width, 8);
        case 2:
            return render_pair("ir", std::format("0x{:08x}", ctx.ir), kThemeVal,
                               "cinsn", std::format("0x{:08x}", ctx.cinsn), kThemeVal,
                               col_width, right_width, 8);
        case 3:
            return section_line("── ID", width);
        case 4:
            return render_pair(
                "opcode", std::to_string(std::to_underlying(ctx.opcode)), kThemeVal,
                "funct3", std::to_string(std::to_underlying(ctx.funct3)), kThemeVal,
                col_width, right_width, 8);
        case 5:
            return render_pair(
                "rd/rs1",
                std::format("{}/{}", std::to_underlying(ctx.rd),
                             std::to_underlying(ctx.rs1)),
                kThemeVal, "rs2/f7",
                std::format("{}/0x{:x}", std::to_underlying(ctx.rs2), ctx.funct7),
                kThemeVal, col_width, right_width, 8);
        case 6:
            return render_pair(
                "imm", std::format("0x{:0{}x}", ctx.imm, simrv::xlen::kXLenHexDigits),
                kThemeVal, "funct12", std::format("0x{:x}", ctx.funct12), kThemeVal,
                col_width, right_width, 8);
        case 7:
            return section_line("── OF/EX", width);
        default:
            return format_to_width("", width);
    }
}

auto LeftPane::render_pipeline_stages_functional_low_part2(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string {
    int const width = col_width + right_width;
    auto& ctx = cpu.pipeline_context;
    switch (logical_row) {
        case 8:
            return render_pair(
                "rrs1", std::format("0x{:0{}x}", ctx.rrs1, simrv::xlen::kXLenHexDigits),
                kThemeMint, "rrs2",
                std::format("0x{:0{}x}", ctx.rrs2, simrv::xlen::kXLenHexDigits),
                kThemeMint, col_width, right_width, 8);
        case 9:
            return render_pair(
                "jmp_pc",
                std::format("0x{:0{}x}", ctx.jmp_pc, simrv::xlen::kXLenHexDigits),
                kThemeMint, "taken", ctx.tkn ? "yes" : "no", kThemeVal,
                col_width, right_width, 8);
        case 10:
            return render_pair(
                "wb_data",
                std::format("0x{:0{}x}", ctx.wb_data, simrv::xlen::kXLenHexDigits),
                kThemeMint, "wb_csr",
                std::format("0x{:0{}x}", ctx.wb_data_csr, simrv::xlen::kXLenHexDigits),
                kThemeVal, col_width, right_width, 8);
        case 11:
            return section_line("── MEM/FP", width);
        case 12:
            return render_pair(
                "mem_addr",
                std::format("0x{:0{}x}", ctx.mem_addr, simrv::xlen::kXLenHexDigits),
                kThemeMint, "mem_w",
                std::format("0x{:0{}x}", ctx.mem_wdata, simrv::xlen::kXLenHexDigits),
                kThemeMint, col_width, right_width, 8);
        case 13:
            return render_pair(
                "mem_r",
                std::format("0x{:0{}x}", ctx.mem_rdata, simrv::xlen::kXLenHexDigits),
                kThemeMint, "fp_wb", std::format("0x{:016x}", ctx.fp_wb_data),
                kThemeVal, col_width, right_width, 8);
        case 14:
            return render_pair("fp_wb_en", ctx.fp_wb_enable ? "on" : "off", kThemeVal,
                               "int<-fp", ctx.int_wb_from_fp ? "on" : "off",
                               kThemeVal, col_width, right_width, 8);
        case 15:
            return section_line("── TRAP/TLB", width);
        default:
            return format_to_width("", width);
    }
}

auto LeftPane::render_pipeline_stages_functional_high(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string {
    int const width = col_width + right_width;
    auto& ctx = cpu.pipeline_context;
    auto exc_text = ctx.pending_exception.has_value()
                        ? std::to_string(std::to_underlying(ctx.pending_exception.value()))
                        : std::string("none");

    switch (logical_row) {
        case 16:
            return render_pair(
                "exc", exc_text, kThemePeach, "tval",
                std::format("0x{:0{}x}", ctx.pending_tval, simrv::xlen::kXLenHexDigits),
                kThemeVal, col_width, right_width, 8);
        case 17:
            return render_pair(
                "padr1", std::format("0x{:0{}x}", ctx.padr1, simrv::xlen::kXLenHexDigits),
                kThemeVal, "padr2",
                std::format("0x{:0{}x}", ctx.padr2, simrv::xlen::kXLenHexDigits),
                kThemeVal, col_width, right_width, 8);
        case 18:
            return render_pair(
                "rcsr", std::format("0x{:0{}x}", ctx.rcsr, simrv::xlen::kXLenHexDigits),
                kThemeVal, "funct5", std::to_string(std::to_underlying(ctx.funct5)),
                kThemeVal, col_width, right_width, 8);
        case 19:
            return section_line("── End Pipeline Snapshot", width);
        default:
            return format_to_width("", width);
    }
}

auto LeftPane::render_system_state(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string {
    auto const& st = cpu.state();
    int const width = col_width + right_width;
    int label_pad = (width < 50) ? 0 : ((width < 65) ? 5 : 8);

    if (logical_row == 16) {
        return section_line("CSRs & Privilege State", width);
    }
    if (logical_row == 17) {
        std::string priv_str = (st.priv == PrivilegeLevel::Machine)      ? "Machine"
                               : (st.priv == PrivilegeLevel::Supervisor) ? "Supervisor"
                                                                         : "User";
        return render_pair("pc", std::format("0x{:0{}x}", st.pc, simrv::xlen::kXLenHexDigits),
                           kThemeMint, "priv", priv_str, kThemePink, col_width, right_width, label_pad);
    }
    if (logical_row == 18) {
        std::string misa_str = simrv::xlen::resolve_misa_string(st.misa);
        return render_pair("mstatus",
                           std::format("0x{:0{}x}", st.mstatus, simrv::xlen::kXLenHexDigits),
                           kThemeVal, "misa", misa_str, kThemeVal, col_width, right_width, label_pad);
    }
    if (logical_row == 19) {
        return render_pair(
            "mie", std::format("0x{:0{}x}", st.mie, simrv::xlen::kXLenHexDigits), kThemeVal,
            "mip", std::format("0x{:0{}x}", st.mip, simrv::xlen::kXLenHexDigits), kThemeVal,
            col_width, right_width, label_pad);
    }
    if (logical_row == 20) {
        return render_pair(
            "mtvec", std::format("0x{:0{}x}", st.mtvec, simrv::xlen::kXLenHexDigits),
            kThemeVal, "mepc", std::format("0x{:0{}x}", st.mepc, simrv::xlen::kXLenHexDigits),
            kThemeVal, col_width, right_width, label_pad);
    }
    if (logical_row == 21) {
        return render_pair(
            "stvec", std::format("0x{:0{}x}", st.stvec, simrv::xlen::kXLenHexDigits),
            kThemeVal, "sepc", std::format("0x{:0{}x}", st.sepc, simrv::xlen::kXLenHexDigits),
            kThemeVal, col_width, right_width, label_pad);
    }
    if (logical_row == 22) {
        return render_pair(
            "mtval", std::format("0x{:0{}x}", st.mtval, simrv::xlen::kXLenHexDigits),
            kThemeVal, "satp", std::format("0x{:0{}x}", st.satp, simrv::xlen::kXLenHexDigits),
            kThemeVal, col_width, right_width, label_pad);
    }
    if (logical_row == 23) {
        return render_pair(
            "scause", std::format("0x{:0{}x}", st.scause, simrv::xlen::kXLenHexDigits),
            kThemeVal, "stval",
            std::format("0x{:0{}x}", st.stval, simrv::xlen::kXLenHexDigits), kThemeVal,
            col_width, right_width, label_pad);
    }
    if (logical_row == 24) {
        return render_pair(
            "medeleg", std::format("0x{:0{}x}", st.medeleg, simrv::xlen::kXLenHexDigits),
            kThemeVal, "mideleg",
            std::format("0x{:0{}x}", st.mideleg, simrv::xlen::kXLenHexDigits), kThemeVal,
            col_width, right_width, label_pad);
    }
    return format_to_width("", width);
}

auto LeftPane::render_system_or_pipeline_extended(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width, bool single_column) -> std::string {
    if (logical_row >= 16 && logical_row <= 24) {
        if (page_ == TuiRegPage::PIPELINE) {
            return render_pipeline_stages(cpu, logical_row, col_width, right_width);
        } else if (!single_column) {
            return render_system_state(cpu, logical_row, col_width, right_width);
        }
    }
    return "";
}

auto LeftPane::render_pipeline_timeline(const simrv::core::CPU& cpu, int logical_row, int width) -> std::string {
    auto const& ps = cpu.pipeline_sim;
    auto const history = ps.get_cycle_history_copy();

    if (logical_row == 0) {
        return section_line("Pipeline Execution Timeline (Last 5 Inst)", width);
    }

    if (history.empty()) {
        if (logical_row == 1) {
            return format_to_width("  No cycle history collected yet.", width);
        }
        return format_to_width("", width);
    }

    // Compute the prefix width of each instruction row dynamically so header
    // and data rows always align regardless of xlen (RV32: 8 hex digits, RV64: 16).
    //   "  0x" (4) + addr_digits + " " (1) + mnem<5 (5) + " |" (2) = addr_digits + 12
    int const addr_digits = static_cast<int>(simrv::xlen::kXLenHexDigits);
    int const desc_width = addr_digits + 12;
    int const cycle_col_width = 5;
    // Cap at 10 cycle columns so the grid never overflows the pane width
    constexpr int kMaxCycleColumns = 10;
    int const max_cycles = std::min(kMaxCycleColumns, (width - desc_width) / cycle_col_width);
    if (max_cycles <= 0) {
        if (logical_row == 1) {
            return format_to_width("  TUI Pane too narrow for timeline.", width);
        }
        return format_to_width("", width);
    }

    // Determine cycle window to display
    int const history_size = static_cast<int>(history.size());
    int const num_cols = std::min(max_cycles, history_size);
    int const start_idx = history_size - num_cols;

    // Collect unique PCs in this window chronologically.
    struct InstRef {
        Register pc = 0;
        isa::OperationId op_id = isa::OperationId::UNKNOWN;
        
        auto operator==(const InstRef& o) const -> bool { return pc == o.pc; }
    };
    std::vector<InstRef> active_insts;
    for (int i = start_idx; i < history_size; ++i) {
        auto const& snap = history.at(i);
        std::array<simrv::pipeline::PipelineCycleSnapshot::StageInfo const*, 5> stages = {&snap.w, &snap.m, &snap.e, &snap.d, &snap.f};
        for (auto const* stage : stages) {
            if (stage->valid && stage->pc != 0) {
                InstRef ref{.pc = stage->pc, .op_id = stage->op_id};
                if (std::ranges::find(active_insts, ref) == active_insts.end()) {
                    active_insts.push_back(ref);
                }
            }
        }
    }

    // Keep only the 5 most-recently-seen instructions to avoid overflow
    constexpr int kMaxInstRows = 5;
    if (static_cast<int>(active_insts.size()) > kMaxInstRows) {
        active_insts.erase(active_insts.begin(), active_insts.begin() + (static_cast<int>(active_insts.size()) - kMaxInstRows));
    }

    if (logical_row == 1) {
        // Header prefix width = addr_digits + 10 chars ("  0x" + addr + " " + mnem<5)
        std::string header = std::format("{:<{}} |", "Instruction PC/Mnem", addr_digits + 10);
        for (int i = start_idx; i < history_size; ++i) {
            header += std::format(" #{:<2d} ", history.at(i).cycle % 100);
        }
        return format_to_width(header, width);
    }

    int const inst_row = logical_row - 2;
    if (inst_row >= 0 && static_cast<std::size_t>(inst_row) < active_insts.size() && inst_row < 5) {
        auto const& inst = active_insts.at(inst_row);
        
        // Get assembly mnemonic
        std::string_view op_name = "UNK";
        if (static_cast<std::size_t>(inst.op_id) < simrv::pipeline::OPERATION_NAME.size()) {
            op_name = simrv::pipeline::OPERATION_NAME.at(static_cast<std::size_t>(inst.op_id));
        }
        std::string line = std::format("  0x{:0{}x} {:<5} |", inst.pc, addr_digits, op_name);

        for (int i = start_idx; i < history_size; ++i) {
            auto const& snap = history.at(i);
            std::string stage_lbl = " .   ";
            // Check stages
            if (snap.w.valid && snap.w.pc == inst.pc) {
                stage_lbl = "\033[1;37m WB\033[0m  ";
            } else if (snap.m.valid && snap.m.pc == inst.pc) {
                stage_lbl = snap.m.stalled ? "\033[38;5;203m ME*\033[0m " : "\033[1;34m ME\033[0m  ";
            } else if (snap.e.valid && snap.e.pc == inst.pc) {
                stage_lbl = snap.e.stalled ? "\033[38;5;203m EX*\033[0m " : "\033[1;31m EX\033[0m  ";
            } else if (snap.d.valid && snap.d.pc == inst.pc) {
                stage_lbl = snap.d.stalled ? "\033[38;5;203m ID*\033[0m " : "\033[1;33m ID\033[0m  ";
            } else if (snap.f.valid && snap.f.pc == inst.pc) {
                stage_lbl = snap.f.stalled ? "\033[38;5;203m IF*\033[0m " : "\033[1;32m IF\033[0m  ";
            }
            line += stage_lbl;
        }
        return format_to_width(line, width);
    }

    if (logical_row == 9) {
        return section_line("Legend: IF: Green | ID: Yellow | EX: Red | ME: Blue | WB: White (*Stall)", width);
    }

    return format_to_width("", width);
}

auto LeftPane::render_cache_stats(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string {
    int const width = col_width + right_width;
    if (!machine_.s_cycle_accurate) {
        switch (logical_row) {
            case 0:
                return section_line("Cache — Not Available", width);
            case 2:
                return format_to_width(
                    std::format("  {}Cache simulation is disabled in high-performance mode.\033[0m", kThemeMuted), width);
            case 3:
                return format_to_width(
                    std::format("  {}Relaunch SimRV with {}--cycle-accurate\033[0m{} (-c) to enable.\033[0m",
                                kThemeMuted, kThemeVal, kThemeMuted), width);
            default:
                return format_to_width("", width);
        }
    }

    auto const& ic = cpu.icache;
    auto const& dc = cpu.dcache;


    auto make_bar = [](double ratio, int bar_width) -> std::string {
        int filled = static_cast<int>(ratio * bar_width);
        if (filled < 0) filled = 0;
        if (filled > bar_width) filled = bar_width;
        std::string bar = kThemeMint;
        for (int i = 0; i < filled; ++i) {
            bar += "█";
        }
        bar += kThemeMuted;
        for (int i = filled; i < bar_width; ++i) {
            bar += "░";
        }
        bar += "\033[0m";
        return bar;
    };

    switch (logical_row) {
        case 0:
            return section_line("L1 Instruction Cache", width);
        case 1:
            {
                uint64_t h = ic.hit_count(), m = ic.miss_count();
                uint64_t tot = h + m;
                double mr = (tot == 0) ? 0.0 : 100.0 * static_cast<double>(m) / static_cast<double>(tot);
                return format_to_width(std::format(
                    "  {}Hits:\033[0m {:>12}   {}Misses:\033[0m {:>10}   {}Miss Rate:\033[0m {:>6.2f}%",
                    kThemeText, simrv::util::format_with_commas(h),
                    kThemeText, simrv::util::format_with_commas(m),
                    kThemeText, mr), width);
            }
        case 2:
            {
                uint64_t h = ic.hit_count(), m = ic.miss_count();
                uint64_t tot = h + m;
                double ratio = (tot == 0) ? 1.0 : static_cast<double>(h) / static_cast<double>(tot);
                int bar_w = std::max(5, width - 38);
                std::string bar = make_bar(ratio, bar_w);
                return format_to_width(std::format(
                    "  {}Hit Rate\033[0m  [{}] {:>6.2f}%   {}32B / 4-way\033[0m",
                    kThemeText, bar, ratio * 100.0, kThemeMuted), width);
            }
        case 3:
            return section_line("L1 Data Cache", width);
        case 4:
            {
                uint64_t h = dc.hit_count(), m = dc.miss_count();
                uint64_t tot = h + m;
                double mr = (tot == 0) ? 0.0 : 100.0 * static_cast<double>(m) / static_cast<double>(tot);
                return format_to_width(std::format(
                    "  {}Hits:\033[0m {:>12}   {}Misses:\033[0m {:>10}   {}Miss Rate:\033[0m {:>6.2f}%",
                    kThemeText, simrv::util::format_with_commas(h),
                    kThemeText, simrv::util::format_with_commas(m),
                    kThemeText, mr), width);
            }
        case 5:
            {
                uint64_t h = dc.hit_count(), m = dc.miss_count();
                uint64_t tot = h + m;
                double ratio = (tot == 0) ? 1.0 : static_cast<double>(h) / static_cast<double>(tot);
                int bar_w = std::max(5, width - 38);
                std::string bar = make_bar(ratio, bar_w);
                return format_to_width(std::format(
                    "  {}Hit Rate\033[0m  [{}] {:>6.2f}%   {}32B / 4-way\033[0m",
                    kThemeText, bar, ratio * 100.0, kThemeMuted), width);
            }
        case 6:
            return section_line("Set Occupancy Map", width);
        case 7:
        case 8:
        case 9:
        case 10:
        case 11:
        case 12:
        case 13:
        case 14:
            {
                int const base_set = (logical_row - 7) * 2;

                auto make_set_str = [](auto const& cache, int set_idx) -> std::string {
                    bool const is_last = (static_cast<uint32_t>(set_idx) == cache.last_accessed_set());
                    bool const was_hit = cache.last_access_was_hit();

                    std::string set_prefix;
                    if (is_last) {
                        if (was_hit) {
                            set_prefix = std::format("\033[1m{}{:02d}:\033[0m[", kThemeMint, set_idx);
                        } else {
                            set_prefix = std::format("\033[1m{}{:02d}:\033[0m[", kThemeCoral, set_idx);
                        }
                    } else {
                        set_prefix = std::format("{}{:02d}:\033[0m[", kThemeMuted, set_idx);
                    }

                    std::string s = set_prefix;
                    for (uint32_t w = 0; w < 4; ++w) {
                        if (cache.is_line_valid(set_idx, w)) {
                            if (is_last) {
                                if (was_hit) {
                                    s += std::format("\033[1m{}#\033[0m", kThemeMint);
                                } else {
                                    s += std::format("\033[1m{}#\033[0m", kThemeCoral);
                                }
                            } else {
                                s += std::format("{}#\033[0m", kThemeMint);
                            }
                        } else {
                            s += std::format("{}.\033[0m", kThemeMuted);
                        }
                    }
                    s += "]";
                    return s;
                };

                std::string ic_left = make_set_str(ic, base_set);
                std::string ic_right = make_set_str(ic, base_set + 1);
                std::string dc_left = make_set_str(dc, base_set);
                std::string dc_right = make_set_str(dc, base_set + 1);

                std::string left_col = std::format(" IC {} {}", ic_left, ic_right);
                std::string right_col = std::format(" DC {} {}", dc_left, dc_right);

                return format_to_width(left_col, col_width) + format_to_width(right_col, right_width);
            }
        case 15:
            return section_line("Occupancy: # (Valid)  . (Empty)  Bold set# = last accessed", width);
        default:
            return format_to_width("", width);
    }
}

}  // namespace simrv::tui
