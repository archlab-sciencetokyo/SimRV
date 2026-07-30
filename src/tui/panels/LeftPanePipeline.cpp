/**
 * @file LeftPanePipeline.cpp
 * @brief Pipeline visualization pane rendering for the TUI register panel.
 *
 * Designed to be readable by students learning about pipelining:
 *   - Each stage section is labelled with its purpose
 *   - Contextual descriptions replace raw hex dumps
 *   - Computation equations show what the EX stage computed
 *   - Memory access descriptions explain load/store behavior
 */
#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <vector>

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "simrv/pipeline/PipelineModel.hpp"
#include "simrv/pipeline/PipelineSim.hpp"
#include "simrv/tui/panels/LeftPane.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/util/FormatUtil.hpp"
#include "simrv/util/InstructionExplainer.hpp"
#include "simrv/xlen/Helpers.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::tui {

using simrv::isa::Opcode;
using simrv::isa::OperationId;
using enum simrv::isa::OperationId;
using simrv::isa::InstFormat;

namespace {

// ───────────────────────────────────────────────────────────────────────
// Shared helpers
// ───────────────────────────────────────────────────────────────────────

auto reg_name(RegId r) -> std::string {
    uint32_t idx = std::to_underlying(r);
    if (idx >= 32) return std::format("x{}", idx);
    return {kRegNames.at(idx)};
}

auto fp_reg_name(RegId r) -> std::string {
    uint32_t idx = std::to_underlying(r);
    if (idx >= 32) return std::format("f{}", idx);
    return {kFpRegNames.at(idx)};
}

auto reg_with_x(RegId r) -> std::string {
    uint32_t idx = std::to_underlying(r);
    if (idx >= 32) return std::format("x{}", idx);
    return std::format("{} (x{})", kRegNames.at(idx), idx);
}

auto hex_val(Register v) -> std::string {
    if constexpr (sizeof(Register) <= 4) {
        return std::format("0x{:08x}", v);
    } else {
        auto val = static_cast<uint64_t>(v);
        if ((val >> 32) == 0) {
            return std::format("0x{:08x}", static_cast<uint32_t>(val));
        }
        return std::format("0x{:016x}", val);
    }
}

auto short_op_name(const simrv::pipeline::PipelineReg& reg) -> std::string {
    if (!reg.valid) return "-";
    if (static_cast<std::size_t>(reg.op_id) < simrv::pipeline::OPERATION_NAME.size()) {
        return std::string(simrv::pipeline::OPERATION_NAME.at(static_cast<std::size_t>(reg.op_id)));
    }
    return "insn";
}

/// Is this opcode a load instruction?
auto is_load_opcode(Opcode opc) -> bool { return opc == Opcode::Load || opc == Opcode::LoadFp; }

/// Is this opcode a store instruction?
auto is_store_opcode(Opcode opc) -> bool { return opc == Opcode::Store || opc == Opcode::StoreFp; }

/// Is this opcode a branch?
auto is_branch_opcode(Opcode opc) -> bool { return opc == Opcode::Branch; }

/// Build a one-line description of what the EX stage computed.
auto get_computation_desc(const simrv::pipeline::PipelineContext& ctx) -> std::string {
    auto const opc = ctx.opcode;
    std::string_view op_name = "?";
    if (static_cast<std::size_t>(ctx.op_id) < simrv::pipeline::OPERATION_NAME.size()) {
        op_name = simrv::pipeline::OPERATION_NAME.at(static_cast<std::size_t>(ctx.op_id));
    }

    if (is_load_opcode(opc)) {
        return std::format(" {}Computation : {}Memory[{} + {}] → {}\033[0m", kThemeText, kThemeMint,
                           hex_val(ctx.rrs1), ctx.imm, hex_val(ctx.mem_rdata));
    }
    if (is_store_opcode(opc)) {
        return std::format(" {}Computation : {}{} → Memory[{} + {}]\033[0m", kThemeText, kThemeMint,
                           hex_val(ctx.rrs2), hex_val(ctx.rrs1), ctx.imm);
    }
    if (is_branch_opcode(opc)) {
        return std::format(" {}Computation : {}Compare {} vs {} → {}\033[0m", kThemeText,
                           kThemeMint, hex_val(ctx.rrs1), hex_val(ctx.rrs2),
                           ctx.tkn ? "Taken" : "Not Taken");
    }
    if (opc == Opcode::Jal) {
        return std::format(" {}Computation : {}Jump to {} (link {} in rd)\033[0m", kThemeText,
                           kThemeMint, hex_val(ctx.jmp_pc), hex_val(ctx.cpc + 4));
    }
    if (opc == Opcode::Jalr) {
        return std::format(" {}Computation : {}Jump to {} + {} = {} (link in rd)\033[0m",
                           kThemeText, kThemeMint, hex_val(ctx.rrs1), ctx.imm, hex_val(ctx.jmp_pc));
    }
    if (opc == Opcode::Lui) {
        return std::format(" {}Computation : {}imm << 12 = {}\033[0m", kThemeText, kThemeMint,
                           hex_val(ctx.wb_data));
    }
    if (opc == Opcode::Auipc) {
        return std::format(" {}Computation : {}PC + (imm << 12) = {} + {} = {}\033[0m", kThemeText,
                           kThemeMint, hex_val(ctx.cpc),
                           hex_val(static_cast<Register>(ctx.imm) << 12), hex_val(ctx.wb_data));
    }
    // System / CSR
    if (opc == Opcode::System) {
        if (ctx.op_id == ECALL) {
            return std::format(" {}Computation : {}Environment call (trap to handler)\033[0m",
                               kThemeText, kThemePeach);
        }
        if (ctx.op_id == EBREAK) {
            return std::format(" {}Computation : {}Breakpoint (trap to debugger)\033[0m",
                               kThemeText, kThemePeach);
        }
        return std::format(" {}Computation : {}CSR operation → {}\033[0m", kThemeText, kThemeMint,
                           hex_val(ctx.wb_data));
    }
    // Fence
    if (opc == Opcode::MiscMem) {
        return std::format(" {}Computation : {}Memory fence (ordering barrier)\033[0m", kThemeText,
                           kThemeMuted);
    }
    // Default: ALU / FP / Atomic / Vector
    // Show: operand1 <op> operand2 = result
    InstFormat fmt = simrv::isa::get_instruction_format(opc);
    if (fmt == InstFormat::R || fmt == InstFormat::R4) {
        return std::format(" {}Computation : {}{} {} {} = {}\033[0m", kThemeText, kThemeMint,
                           hex_val(ctx.rrs1), op_name, hex_val(ctx.rrs2), hex_val(ctx.wb_data));
    }
    // I-type ALU
    return std::format(" {}Computation : {}{} {} {} = {}\033[0m", kThemeText, kThemeMint,
                       hex_val(ctx.rrs1), op_name, ctx.imm, hex_val(ctx.wb_data));
}

/// Build a description for the MEM stage.
auto get_mem_stage_desc(const simrv::pipeline::PipelineContext& ctx)
    -> std::pair<std::string, std::string> {
    if (is_load_opcode(ctx.opcode)) {
        return {std::format("Load from {}", hex_val(ctx.mem_addr)),
                std::format("Read: {}", hex_val(ctx.mem_rdata))};
    }
    if (is_store_opcode(ctx.opcode)) {
        return {std::format("Store to {}", hex_val(ctx.mem_addr)),
                std::format("Data: {}", hex_val(ctx.mem_wdata))};
    }
    return {"None (not a load/store)", "—"};
}

/// Format the Source 2 description based on instruction format.
auto get_src2_desc(const simrv::pipeline::PipelineContext& ctx, InstFormat fmt) -> std::string {
    if (fmt == InstFormat::I || fmt == InstFormat::U || fmt == InstFormat::J) {
        return std::format("(none — uses imm)");
    }
    if (fmt == InstFormat::S) {
        bool is_fp = is_store_opcode(ctx.opcode) && ctx.opcode == Opcode::StoreFp;
        std::string name = is_fp ? fp_reg_name(ctx.rs2) : reg_name(ctx.rs2);
        return std::format("{} = {}", name, hex_val(ctx.rrs2));
    }
    if (fmt == InstFormat::R || fmt == InstFormat::B || fmt == InstFormat::R4) {
        bool is_fp = simrv::isa::is_rs2_fp(ctx.opcode, ctx.op_id);
        std::string name = is_fp ? fp_reg_name(ctx.rs2) : reg_name(ctx.rs2);
        return std::format("{} = {}", name, hex_val(ctx.rrs2));
    }
    return "—";
}

// ───────────────────────────────────────────────────────────────────────
// Cycle-accurate helpers
// ───────────────────────────────────────────────────────────────────────

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

auto compact_hex(Register v) -> std::string { return hex_val(v); }

auto get_stage_desc(const simrv::pipeline::PipelineReg& reg, uint32_t stall_rem,
                    const std::string& stall_type, bool raw_stall = false) -> std::string {
    if (!reg.valid) {
        return std::format("{}bubble (empty)\033[0m", kThemeMuted);
    }
    std::string_view op_name = "UNKNOWN";
    if (static_cast<std::size_t>(reg.op_id) < simrv::pipeline::OPERATION_NAME.size()) {
        op_name = simrv::pipeline::OPERATION_NAME.at(static_cast<std::size_t>(reg.op_id));
    }

    std::string ops_info;
    if (reg.rs1 != static_cast<RegId>(0) || reg.rs2 != static_cast<RegId>(0) ||
        (reg.writes_reg && reg.rd != static_cast<RegId>(0))) {
        std::vector<std::string> srcs;
        if (reg.rs1 != static_cast<RegId>(0)) srcs.push_back(reg_name(reg.rs1));
        if (reg.rs2 != static_cast<RegId>(0)) srcs.push_back(reg_name(reg.rs2));

        std::string src_str =
            srcs.empty() ? ""
                         : (srcs.size() == 1 ? srcs[0] : std::format("{}, {}", srcs[0], srcs[1]));
        if (reg.writes_reg && reg.rd != static_cast<RegId>(0)) {
            if (!src_str.empty()) {
                ops_info = std::format(" \033[90m[{} → {}]\033[0m", src_str, reg_name(reg.rd));
            } else {
                ops_info = std::format(" \033[90m[→ {}]\033[0m", reg_name(reg.rd));
            }
        } else if (!src_str.empty()) {
            ops_info = std::format(" \033[90m[{}]\033[0m", src_str);
        }
    }

    std::string desc = std::format("\033[1m{}{}\033[0m ({}{}{}){}", kThemeMint, op_name, kThemeSky,
                                   compact_hex(reg.pc), "\033[0m", ops_info);
    if (stall_rem > 0) {
        std::string tag = stall_type;
        if (tag == "I-Cache Miss")
            tag = "I-Cache";
        else if (tag == "D-Cache Miss")
            tag = "D-Cache";
        else if (tag == "TLB Miss")
            tag = "TLB";
        else if (tag == "Divider Busy")
            tag = "Divider";
        desc += std::format(" {}[{}: {}c]\033[0m", kThemeCoral, tag, stall_rem);
    } else if (raw_stall) {
        desc += std::format(" {}[RAW Stall]\033[0m", kThemeCoral);
    } else if (reg.remaining_latency > 0) {
        desc += std::format(" {}[Lat: {}c]\033[0m", kThemePeach, reg.remaining_latency);
    }
    return desc;
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
        return "No active branch";
    }
    uint8_t counter = ps.get_model() ? ps.get_model()->get_bht_entry(pc) : 1;
    switch (counter) {
        case 0:
            return "Strongly Not Taken (00)";
        case 1:
            return "Weakly Not Taken (01)";
        case 2:
            return "Weakly Taken (10)";
        default:
            return "Strongly Taken (11)";
    }
}

auto get_btb_string(const simrv::pipeline::PipelineSim& ps, Register pc) -> std::string {
    if (pc == 0) {
        return "No active branch";
    }
    auto [valid, target] =
        ps.get_model() ? ps.get_model()->get_btb_target(pc) : std::pair<bool, Register>{false, 0};
    if (valid) {
        return std::format("Hit → cached target {}", hex_val(target));
    }
    return "Miss (no cached target)";
}

auto get_active_forwarding_paths(const simrv::pipeline::PipelineSim& ps)
    -> std::vector<std::string> {
    std::vector<std::string> paths;
    if (!ps.config.enable_forwarding) return paths;

    auto d = ps.d_reg();
    if (!d.valid) return paths;

    auto e = ps.e_reg();
    auto m = ps.m_reg();
    auto w = ps.w_reg();

    auto check_src = [&](RegId rs, const std::string& rs_label) -> void {
        if (rs == static_cast<RegId>(0)) return;

        if (e.valid && e.writes_reg && e.rd == rs) {
            if (e.remaining_latency == 0) {
                paths.push_back(std::format("EX→ID ({})", rs_label));
                return;
            }
        }
        if (m.valid && m.writes_reg && m.rd == rs) {
            if (m.remaining_latency == 0) {
                paths.push_back(std::format("MEM→ID ({})", rs_label));
                return;
            }
        }
        if (w.valid && w.writes_reg && w.rd == rs) {
            paths.push_back(std::format("WB→ID ({})", rs_label));
            return;
        }
    };

    check_src(d.rs1, reg_name(d.rs1));
    check_src(d.rs2, reg_name(d.rs2));

    return paths;
}

}  // namespace

// ═══════════════════════════════════════════════════════════════════════
// Top-level dispatch
// ═══════════════════════════════════════════════════════════════════════

auto LeftPane::render_pipeline_stages(const simrv::core::CPU& cpu, int logical_row, int col_width,
                                      int right_width) -> std::string {
    if (machine_.s_cycle_accurate) {
        // CA mode: rows 0-8 timeline, rows 9+ stage details
        if (logical_row < 9) {
            return render_pipeline_timeline(cpu, logical_row, col_width + right_width);
        }
        return render_pipeline_stages_cycle_accurate(cpu, logical_row, col_width, right_width);
    } else {
        return render_pipeline_stages_functional(cpu, logical_row, col_width, right_width);
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Cycle-Accurate mode
// ═══════════════════════════════════════════════════════════════════════

auto LeftPane::render_pipeline_stages_cycle_accurate(const simrv::core::CPU& cpu, int logical_row,
                                                     int col_width, int right_width)
    -> std::string {
    // Adjusted row index relative to the start of the non-timeline area.
    // Timeline occupies rows 0–8, so stage details begin at row 9.
    int const val = logical_row - 9;
    if (val >= 0 && val <= 6) {
        return render_pipeline_stages_ca_core(cpu, val, col_width + right_width);
    }
    if (val >= 7 && val <= 12) {
        return render_pipeline_stages_ca_hazards(cpu, val, col_width, right_width);
    }
    return render_pipeline_stages_ca_pred(cpu, val, col_width + right_width);
}

auto LeftPane::render_pipeline_stages_ca_core(const simrv::core::CPU& cpu, int stage_idx, int width)
    -> std::string {
    auto& ps = cpu.pipeline_sim;
    bool is_raw_stalled = is_raw_hazard_stalled(ps);

    switch (stage_idx) {
        case 0:
            return section_line("Current Pipeline State", width);
        case 1: {
            // Compact stage diagram showing what's in each stage
            std::string diagram = std::format(
                "  \033[1;36mIF:{}\033[0m → \033[1;33mID:{}\033[0m → \033[1;32mEX:{}\033[0m → "
                "\033[1;35mMEM:{}\033[0m → \033[1;34mWB:{}\033[0m",
                short_op_name(ps.f_reg()), short_op_name(ps.d_reg()), short_op_name(ps.e_reg()),
                short_op_name(ps.m_reg()), short_op_name(ps.w_reg()));
            return format_to_width(diagram, width);
        }
        case 2: {
            std::string stall_type = ps.tlb_stall_remaining() > 0 ? "TLB Miss" : "I-Cache Miss";
            uint32_t stall_rem = ps.tlb_stall_remaining() > 0 ? ps.tlb_stall_remaining()
                                                              : ps.icache_stall_remaining();
            return format_to_width(std::format("  \033[1m{}IF  (Fetch)\033[0m     : {}", kThemeSky,
                                               get_stage_desc(ps.f_reg(), stall_rem, stall_type)),
                                   width);
        }
        case 3:
            return format_to_width(std::format("  \033[1m{}ID  (Decode)\033[0m    : {}", kThemeSky,
                                               get_stage_desc(ps.d_reg(), 0, "", is_raw_stalled)),
                                   width);
        case 4:
            return format_to_width(
                std::format(
                    "  \033[1m{}EX  (Execute)\033[0m   : {}", kThemeSky,
                    get_stage_desc(ps.e_reg(), ps.div_busy_cycles_remaining(), "Divider Busy")),
                width);
        case 5:
            return format_to_width(
                std::format(
                    "  \033[1m{}MEM (Memory)\033[0m    : {}", kThemeSky,
                    get_stage_desc(ps.m_reg(), ps.dcache_stall_remaining(), "D-Cache Miss")),
                width);
        case 6:
            return format_to_width(std::format("  \033[1m{}WB  (Write-Back)\033[0m: {}", kThemeSky,
                                               get_stage_desc(ps.w_reg(), 0, "")),
                                   width);
        default:
            return format_to_width("", width);
    }
}

auto LeftPane::render_pipeline_stages_ca_hazards(const simrv::core::CPU& cpu, int stage_idx,
                                                 int col_width, int right_width) -> std::string {
    auto& ps = cpu.pipeline_sim;
    int const width = col_width + right_width;
    bool is_raw_stalled = is_raw_hazard_stalled(ps);

    auto status_str = [](bool active,
                         const char* active_msg = "Active") -> std::pair<const char*, const char*> {
        return active ? std::pair{active_msg, kThemePeach} : std::pair{"None", kThemeMint};
    };

    auto [raw_status, raw_color] = status_str(is_raw_stalled);
    auto [div_status, div_color] = status_str(ps.div_busy_cycles_remaining() > 0);
    auto [ic_status, ic_color] = status_str(ps.icache_stall_remaining() > 0);
    auto [dc_status, dc_color] = status_str(ps.dcache_stall_remaining() > 0);
    auto [tlb_status, tlb_color] = status_str(ps.tlb_stall_remaining() > 0);
    auto [ctrl_status, ctrl_color] = status_str(ps.control_bubble_remaining() > 0, "Flushing");

    switch (stage_idx) {
        case 7:
            return section_line("Hazards & Forwarding", width);
        case 8:
            return render_pair("Data Hazard (RAW)", raw_status, raw_color, "Long-Latency Op",
                               div_status, div_color, col_width, right_width, 18);
        case 9:
            return render_pair("I-Cache Miss", ic_status, ic_color, "D-Cache Miss", dc_status,
                               dc_color, col_width, right_width, 18);
        case 10:
            return render_pair("TLB Miss", tlb_status, tlb_color, "Control Hazard", ctrl_status,
                               ctrl_color, col_width, right_width, 18);
        case 11: {
            if (!ps.config.enable_forwarding) {
                return format_to_width(
                    std::format(" {}Forwarding\033[0m        : {}Disabled\033[0m", kThemeText,
                                kThemeMuted),
                    width);
            }
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
            return format_to_width(std::format(" {}Forwarding\033[0m        : {}{}\033[0m",
                                               kThemeText, paths_color, paths_str),
                                   width);
        }
        default:
            return format_to_width("", width);
    }
}

auto LeftPane::render_pipeline_stages_ca_pred(const simrv::core::CPU& cpu, int stage_idx, int width)
    -> std::string {
    auto& ps = cpu.pipeline_sim;

    switch (stage_idx) {
        case 13:
            return section_line("Branch Prediction", width);
        case 14: {
            Register pc = get_active_branch_pc(ps);
            std::string bht_str = get_bht_string(ps, pc);
            return format_to_width(std::format("  {}Branch History\033[0m : {}{}\033[0m",
                                               kThemeText, kThemeVal, bht_str),
                                   width);
        }
        case 15: {
            Register pc = get_active_branch_pc(ps);
            std::string btb_str = get_btb_string(ps, pc);
            return format_to_width(std::format("  {}Target Buffer\033[0m  : {}{}\033[0m",
                                               kThemeText, kThemeVal, btb_str),
                                   width);
        }
        default:
            return format_to_width("", width);
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Functional (non-cycle-accurate) mode
// ═══════════════════════════════════════════════════════════════════════

auto LeftPane::render_pipeline_stages_functional(const simrv::core::CPU& cpu, int logical_row,
                                                 int col_width, int right_width) -> std::string {
    if (logical_row >= 0 && logical_row < 16) {
        return render_pipeline_stages_functional_low(cpu, logical_row, col_width, right_width);
    }
    return render_pipeline_stages_functional_high(cpu, logical_row, col_width, right_width);
}

auto LeftPane::render_pipeline_stages_functional_low(const simrv::core::CPU& cpu, int logical_row,
                                                     int col_width, int right_width)
    -> std::string {
    if (logical_row >= 0 && logical_row <= 7) {
        return render_pipeline_stages_functional_low_part1(cpu, logical_row, col_width,
                                                           right_width);
    }
    if (logical_row >= 8 && logical_row <= 15) {
        return render_pipeline_stages_functional_low_part2(cpu, logical_row, col_width,
                                                           right_width);
    }
    return format_to_width(std::format(" {}Pipeline page\033[0m", kThemeMuted),
                           col_width + right_width);
}

// Rows 0–7: Current Instruction overview + IF stage + ID stage header
auto LeftPane::render_pipeline_stages_functional_low_part1(const simrv::core::CPU& cpu,
                                                           int logical_row, int col_width,
                                                           int right_width) -> std::string {
    int const width = col_width + right_width;
    auto& ctx = cpu.pipeline_context;

    // Get operation info for labels
    std::string_view op_name = "UNKNOWN";
    if (static_cast<std::size_t>(ctx.op_id) < simrv::pipeline::OPERATION_NAME.size()) {
        op_name = simrv::pipeline::OPERATION_NAME.at(static_cast<std::size_t>(ctx.op_id));
    }
    auto isa_ext = simrv::util::get_isa_extension_name(ctx.op_id);
    InstFormat const fmt = simrv::isa::get_instruction_format(ctx.opcode);
    bool const is_compressed = (ctx.ir_org & 0x3) != 0x3;

    switch (logical_row) {
        // ── Current Instruction ────────────────────────────────────
        case 0:
            return section_line("── Current Instruction", width);
        case 1: {
            // PC with optional symbol + Assembly
            std::string sym = machine_.symbols.lookup(cpu.state().pc);
            std::string pc_str =
                sym.empty() ? hex_val(ctx.cpc) : std::format("{} <{}>", hex_val(ctx.cpc), sym);

            // Build assembly string from operation name
            bool is_dst_fp = simrv::isa::is_destination_fp(ctx.opcode, ctx.op_id);
            bool is_rs1_fp = simrv::isa::is_rs1_fp(ctx.opcode, ctx.op_id);
            bool is_rs2_fp = simrv::isa::is_rs2_fp(ctx.opcode, ctx.op_id);
            std::string rd_n = is_dst_fp ? fp_reg_name(ctx.rd) : reg_name(ctx.rd);
            std::string rs1_n = is_rs1_fp ? fp_reg_name(ctx.rs1) : reg_name(ctx.rs1);
            std::string rs2_n = is_rs2_fp ? fp_reg_name(ctx.rs2) : reg_name(ctx.rs2);

            // Simple assembly representation
            std::string asm_str = std::string(op_name);
            if (fmt == InstFormat::R) {
                asm_str = std::format("{} {}, {}, {}", op_name, rd_n, rs1_n, rs2_n);
            } else if (fmt == InstFormat::I) {
                if (is_load_opcode(ctx.opcode)) {
                    asm_str = std::format("{} {}, {}({})", op_name, rd_n, ctx.imm, rs1_n);
                } else {
                    asm_str = std::format("{} {}, {}, {}", op_name, rd_n, rs1_n, ctx.imm);
                }
            } else if (fmt == InstFormat::S) {
                asm_str = std::format("{} {}, {}({})", op_name, rs2_n, ctx.imm, rs1_n);
            } else if (fmt == InstFormat::B) {
                asm_str = std::format("{} {}, {}, {}", op_name, rs1_n, rs2_n, ctx.imm);
            } else if (fmt == InstFormat::U) {
                asm_str =
                    std::format("{} {}, 0x{:X}", op_name, rd_n, static_cast<uint32_t>(ctx.imm));
            } else if (fmt == InstFormat::J) {
                asm_str = std::format("jal {}, {}", rd_n, ctx.imm);
            }

            return render_pair("PC", pc_str, kThemeMint, "Asm", asm_str, kThemePeach, col_width,
                               right_width, 10);
        }
        case 2: {
            // Hex encoding + Format name
            std::string hex_str = is_compressed
                                       ? std::format("0x{:04X} (compressed)", ctx.ir_org & 0xFFFF)
                                       : std::format("0x{:08X}", ctx.ir_org);
            std::string fmt_str = std::string(simrv::isa::get_instruction_format_name(fmt));
            return render_pair("Encoding", hex_str, kThemeVal, "Format", fmt_str, kThemeVal,
                               col_width, right_width, 10);
        }

        // ── IF (Instruction Fetch) ────────────────────────────────
        case 3:
            return section_line("── IF  Instruction Fetch", width);
        case 4:
            return render_pair("Fetch PC", hex_val(ctx.cpc), kThemeMint, "Raw Word",
                               std::format("0x{:08x}", ctx.ir_org), kThemeVal, col_width,
                               right_width, 10);
        case 5: {
            std::string comp_str = is_compressed ? "Yes → decompressed" : "No";
            return render_pair("Compressed", comp_str, kThemeVal, "Phys Addr", hex_val(ctx.padr1),
                               kThemeVal, col_width, right_width, 10);
        }

        // ── ID (Decode & Operand Read) ────────────────────────────
        case 6:
            return section_line("── ID  Decode & Operand Read", width);
        case 7: {
            std::string op_str = std::format("{} ({})", op_name, isa_ext);
            bool is_dst_fp = simrv::isa::is_destination_fp(ctx.opcode, ctx.op_id);
            std::string dst_str = (fmt == InstFormat::S || fmt == InstFormat::B)
                                      ? "(none — no dest)"
                                      : (is_dst_fp ? fp_reg_name(ctx.rd) : reg_with_x(ctx.rd));
            return render_pair("Operation", op_str, kThemeVal, "Dest Reg", dst_str, kThemeMint,
                               col_width, right_width, 10);
        }
        default:
            return format_to_width("", width);
    }
}

// Rows 8–15: Source operands + EX stage + MEM stage + WB header
auto LeftPane::render_pipeline_stages_functional_low_part2(const simrv::core::CPU& cpu,
                                                           int logical_row, int col_width,
                                                           int right_width) -> std::string {
    int const width = col_width + right_width;
    auto& ctx = cpu.pipeline_context;
    InstFormat const fmt = simrv::isa::get_instruction_format(ctx.opcode);

    bool is_rs1_fp = simrv::isa::is_rs1_fp(ctx.opcode, ctx.op_id);

    switch (logical_row) {
        // ID continued: Source operands
        case 8: {
            // Source 1
            std::string src1_str;
            if (fmt == InstFormat::U || fmt == InstFormat::J) {
                src1_str = "(none)";
            } else {
                std::string name = is_rs1_fp ? fp_reg_name(ctx.rs1) : reg_name(ctx.rs1);
                src1_str = std::format("{} = {}", name, hex_val(ctx.rrs1));
            }

            // Source 2 or Immediate
            std::string src2_label = "Source 2";
            std::string src2_str = get_src2_desc(ctx, fmt);
            if (fmt == InstFormat::I || fmt == InstFormat::S || fmt == InstFormat::B ||
                fmt == InstFormat::U || fmt == InstFormat::J) {
                src2_label = "Immediate";
                src2_str = std::format("{} (0x{:X})", ctx.imm, static_cast<uint32_t>(ctx.imm));
            }

            return render_pair("Source 1", src1_str, kThemeMint, src2_label, src2_str, kThemeMint,
                               col_width, right_width, 10);
        }

        // ── EX (Execute) ──────────────────────────────────────────
        case 9:
            return section_line("── EX  Execute", width);
        case 10:
            return format_to_width(get_computation_desc(ctx), width);
        case 11: {
            std::string branch_str =
                is_branch_opcode(ctx.opcode)
                    ? (ctx.tkn ? std::format("{}Yes → {}\033[0m", kThemePeach, hex_val(ctx.jmp_pc))
                               : std::format("{}No (fall through)\033[0m", kThemeMint))
                    : "No";
            std::string jump_str = (ctx.opcode == Opcode::Jal || ctx.opcode == Opcode::Jalr)
                                       ? hex_val(ctx.jmp_pc)
                                       : "—";
            return render_pair("Branch?", branch_str, kThemeVal, "Jump PC", jump_str, kThemeMint,
                               col_width, right_width, 10);
        }

        // ── MEM (Memory Access) ───────────────────────────────────
        case 12:
            return section_line("── MEM  Memory Access", width);
        case 13: {
            auto [access_str, data_str] = get_mem_stage_desc(ctx);
            return render_pair("Access", access_str, kThemeMint, "Data", data_str, kThemeMint,
                               col_width, right_width, 10);
        }

        // ── WB (Write-Back) ───────────────────────────────────────
        case 14:
            return section_line("── WB  Write-Back", width);
        case 15: {
            bool is_dst_fp = simrv::isa::is_destination_fp(ctx.opcode, ctx.op_id);
            InstFormat wr_fmt = simrv::isa::get_instruction_format(ctx.opcode);
            bool has_dest =
                (wr_fmt == InstFormat::R || wr_fmt == InstFormat::I || wr_fmt == InstFormat::U ||
                 wr_fmt == InstFormat::J || wr_fmt == InstFormat::R4);
            std::string dst_str =
                has_dest ? (is_dst_fp ? fp_reg_name(ctx.rd) : reg_with_x(ctx.rd)) : "(none)";
            std::string result_str = has_dest ? hex_val(ctx.wb_data) : "—";
            if (ctx.fp_wb_enable) {
                result_str = std::format("0x{:016x} (FP)", ctx.fp_wb_data);
            }
            return render_pair("Writes To", dst_str, kThemeMint, "Result", result_str, kThemeMint,
                               col_width, right_width, 10);
        }
        default:
            return format_to_width("", width);
    }
}

// Rows 16–19: Exception / trap info + end marker
auto LeftPane::render_pipeline_stages_functional_high(const simrv::core::CPU& cpu, int logical_row,
                                                       int col_width, int right_width)
    -> std::string {
    int const width = col_width + right_width;
    auto& ctx = cpu.pipeline_context;

    switch (logical_row) {
        case 16: {
            if (ctx.pending_exception.has_value()) {
                static constexpr std::array<const char*, 16> kCauseNames = {
                    "Inst Addr Misaligned",  "Inst Access Fault",
                    "Illegal Instruction",   "Breakpoint",
                    "Load Addr Misaligned",  "Load Access Fault",
                    "Store Addr Misaligned", "Store Access Fault",
                    "Env Call (U-Mode)",     "Env Call (S-Mode)",
                    "Reserved (10)",         "Env Call (M-Mode)",
                    "Inst Page Fault",       "Load Page Fault",
                    "Reserved (14)",         "Store Page Fault"};
                auto cause = static_cast<uint64_t>(*ctx.pending_exception);
                std::string cause_name = (cause < kCauseNames.size())
                                             ? kCauseNames.at(cause)
                                             : std::format("Exception ({})", cause);
                return render_pair("Exception", cause_name, kThemePeach, "Trap Value",
                                   hex_val(ctx.pending_tval), kThemeVal, col_width, right_width,
                                   10);
            }
            return render_pair("Exception", "None", kThemeMint, "Trap Value", "—", kThemeVal,
                               col_width, right_width, 10);
        }
        case 17: {
            // CSR info if relevant
            bool const is_csr = (ctx.op_id >= CSRRW && ctx.op_id <= CSRRCI);
            if (is_csr) {
                uint32_t csr_addr = ctx.imm & 0xFFF;
                std::string csr_nm = simrv::util::csr_name(csr_addr);
                return render_pair("CSR Target", std::format("{} (0x{:03X})", csr_nm, csr_addr),
                                   kThemeVal, "CSR Read", hex_val(ctx.rcsr), kThemeVal, col_width,
                                   right_width, 10);
            }
            return format_to_width("", width);
        }
        case 18:
            return format_to_width("", width);
        case 19:
            return section_line("── End Pipeline View", width);
        default:
            return format_to_width("", width);
    }
}

// ═══════════════════════════════════════════════════════════════════════
// Pipeline Execution Timeline (cycle-accurate mode, rows 0–8)
// ═══════════════════════════════════════════════════════════════════════

auto LeftPane::render_pipeline_timeline(const simrv::core::CPU& cpu, int logical_row, int width)
    -> std::string {
    auto const& ps = cpu.pipeline_sim;
    auto const history = ps.get_cycle_history_copy();

    if (logical_row == 0) {
        return section_line("Execution Timeline (Clock Cycles #NN)", width);
    }

    if (history.empty()) {
        if (logical_row == 1) {
            return format_to_width(std::format("  {}No cycle history yet — step the simulator to "
                                               "see instructions flow.\033[0m",
                                               kThemeMuted),
                                   width);
        }
        return format_to_width("", width);
    }

    int const history_size = static_cast<int>(history.size());

    // Collect unique PCs in this window chronologically.
    struct InstRef {
        Register pc = 0;
        isa::OperationId op_id = isa::OperationId::UNKNOWN;

        auto operator==(const InstRef& o) const -> bool { return pc == o.pc; }
    };
    std::vector<InstRef> active_insts;
    int const scan_start = std::max(0, history_size - 14);
    for (int i = scan_start; i < history_size; ++i) {
        auto const& snap = history.at(i);
        std::array<simrv::pipeline::PipelineCycleSnapshot::StageInfo const*, 5> stages = {
            &snap.w, &snap.m, &snap.e, &snap.d, &snap.f};
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
        active_insts.erase(
            active_insts.begin(),
            active_insts.begin() + (static_cast<int>(active_insts.size()) - kMaxInstRows));
    }

    // Determine max PC width dynamically (10 for 8-digit hex e.g. 0x8000002a, 18 for full 64-bit)
    int max_pc_width = 10;
    for (auto const& ref : active_insts) {
        if (hex_val(ref.pc).size() > 10) {
            max_pc_width = 18;
        }
    }
    int const header_prefix_len = max_pc_width + 8;  // e.g. 10 + 8 = 18
    int const desc_width = header_prefix_len + 2;    // prefix + " |"
    int const cycle_col_width = 5;
    constexpr int kMaxCycleColumns = 14;
    int const max_cycles = std::min(kMaxCycleColumns, (width - desc_width) / cycle_col_width);
    if (max_cycles <= 0) {
        if (logical_row == 1) {
            return format_to_width("  TUI Pane too narrow for timeline.", width);
        }
        return format_to_width("", width);
    }

    // Determine cycle window to display
    int const num_cols = std::min(max_cycles, history_size);
    int const start_idx = history_size - num_cols;

    if (logical_row == 1) {
        std::string header = std::format("{:<{}} |", "Instruction", header_prefix_len);
        for (int i = start_idx; i < history_size; ++i) {
            bool const is_current = (i == history_size - 1);
            if (is_current) {
                header += std::format(" \033[1;33m#\033[0m{:<2d} ", history.at(i).cycle % 100);
            } else {
                header += std::format(" \033[90m#\033[0m{:<2d} ", history.at(i).cycle % 100);
            }
        }
        return format_to_width(header, width);
    }

    int const inst_row = logical_row - 2;
    if (inst_row >= 0 && static_cast<std::size_t>(inst_row) < active_insts.size() && inst_row < 5) {
        auto const& inst = active_insts.at(inst_row);

        // Get assembly mnemonic (truncated to max 5 chars to ensure table alignment)
        std::string_view op_mnem = "UNK";
        if (static_cast<std::size_t>(inst.op_id) < simrv::pipeline::OPERATION_NAME.size()) {
            op_mnem = simrv::pipeline::OPERATION_NAME.at(static_cast<std::size_t>(inst.op_id));
        }
        std::string mnem_5 =
            (op_mnem.size() > 5) ? std::string(op_mnem.substr(0, 5)) : std::string(op_mnem);
        std::string pc_str = hex_val(inst.pc);
        std::string line = std::format("  {:>{}} {:<5} |", pc_str, max_pc_width, mnem_5);

        for (int i = start_idx; i < history_size; ++i) {
            auto const& snap = history.at(i);
            std::string stage_lbl = " .   ";
            // Check stages (check from latest to earliest for correct priority)
            if (snap.w.valid && snap.w.pc == inst.pc) {
                stage_lbl = "\033[1;37m WB\033[0m  ";
            } else if (snap.m.valid && snap.m.pc == inst.pc) {
                stage_lbl =
                    snap.m.stalled ? "\033[38;5;203m MEM*\033[0m" : "\033[1;34m MEM\033[0m ";
            } else if (snap.e.valid && snap.e.pc == inst.pc) {
                stage_lbl =
                    snap.e.stalled ? "\033[38;5;203m EX*\033[0m " : "\033[1;31m EX\033[0m  ";
            } else if (snap.d.valid && snap.d.pc == inst.pc) {
                stage_lbl =
                    snap.d.stalled ? "\033[38;5;203m ID*\033[0m " : "\033[1;33m ID\033[0m  ";
            } else if (snap.f.valid && snap.f.pc == inst.pc) {
                stage_lbl =
                    snap.f.stalled ? "\033[38;5;203m IF*\033[0m " : "\033[1;32m IF\033[0m  ";
            }
            line += stage_lbl;
        }
        return format_to_width(line, width);
    }

    if (logical_row == 7) {
        return section_line("IF → ID → EX → MEM → WB   (* = Stalled)", width);
    }

    return format_to_width("", width);
}

auto LeftPane::get_pipeline_pc_at_row(int logical_row) const -> Register {
    auto const& cpu = machine_.cpu;
    auto const& ps = cpu.pipeline_sim;

    // Execution Timeline instruction rows (logical rows 2..6)
    if (logical_row >= 2 && logical_row <= 6) {
        auto const history = ps.get_cycle_history_copy();
        if (history.empty()) return 0;
        int const history_size = static_cast<int>(history.size());

        struct InstRef {
            Register pc = 0;
            isa::OperationId op_id = isa::OperationId::UNKNOWN;
            auto operator==(const InstRef& o) const -> bool { return pc == o.pc; }
        };
        std::vector<InstRef> active_insts;
        int const scan_start = std::max(0, history_size - 14);
        for (int i = scan_start; i < history_size; ++i) {
            auto const& snap = history.at(i);
            std::array<simrv::pipeline::PipelineCycleSnapshot::StageInfo const*, 5> stages = {
                &snap.w, &snap.m, &snap.e, &snap.d, &snap.f};
            for (auto const* stage : stages) {
                if (stage->valid && stage->pc != 0) {
                    InstRef ref{.pc = stage->pc, .op_id = stage->op_id};
                    if (std::ranges::find(active_insts, ref) == active_insts.end()) {
                        active_insts.push_back(ref);
                    }
                }
            }
        }
        constexpr int kMaxInstRows = 5;
        if (static_cast<int>(active_insts.size()) > kMaxInstRows) {
            active_insts.erase(
                active_insts.begin(),
                active_insts.begin() + (static_cast<int>(active_insts.size()) - kMaxInstRows));
        }

        int const inst_row = logical_row - 2;
        if (inst_row >= 0 && static_cast<std::size_t>(inst_row) < active_insts.size()) {
            return active_insts.at(inst_row).pc;
        }
    }

    // CA Stage Detail rows (logical rows 11..15 in CA mode)
    if (machine_.s_cycle_accurate) {
        switch (logical_row) {
            case 11:
                return ps.f_reg().valid ? ps.f_reg().pc : 0;
            case 12:
                return ps.d_reg().valid ? ps.d_reg().pc : 0;
            case 13:
                return ps.e_reg().valid ? ps.e_reg().pc : 0;
            case 14:
                return ps.m_reg().valid ? ps.m_reg().pc : 0;
            case 15:
                return ps.w_reg().valid ? ps.w_reg().pc : 0;
            default:
                break;
        }
    }
    return 0;
}



auto LeftPane::render_system_or_pipeline_extended(const simrv::core::CPU& cpu, int logical_row,
                                                   int col_width, int right_width,
                                                   bool single_column) -> std::string {
    if (logical_row >= 16 && logical_row <= 24) {
        if (page_ == TuiRegPage::PIPELINE) {
            return render_pipeline_stages(cpu, logical_row, col_width, right_width);
        } else if (!single_column) {
            return render_system_state(cpu, logical_row, col_width, right_width);
        }
    }
    return "";
}

auto LeftPane::render_system_state(const simrv::core::CPU& cpu, int logical_row, int col_width,
                                    int right_width) -> std::string {
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
                           kThemeMint, "priv", priv_str, kThemePink, col_width, right_width,
                           label_pad);
    }
    if (logical_row == 18) {
        std::string misa_str = simrv::xlen::resolve_misa_string(st.misa);
        return render_pair(
            "mstatus", std::format("0x{:0{}x}", st.mstatus, simrv::xlen::kXLenHexDigits), kThemeVal,
            "misa", misa_str, kThemeVal, col_width, right_width, label_pad);
    }
    if (logical_row == 19) {
        return render_pair("mie", std::format("0x{:0{}x}", st.mie, simrv::xlen::kXLenHexDigits),
                           kThemeVal, "mip",
                           std::format("0x{:0{}x}", st.mip, simrv::xlen::kXLenHexDigits), kThemeVal,
                           col_width, right_width, label_pad);
    }
    if (logical_row == 20) {
        return render_pair("mtvec", std::format("0x{:0{}x}", st.mtvec, simrv::xlen::kXLenHexDigits),
                           kThemeVal, "mepc",
                           std::format("0x{:0{}x}", st.mepc, simrv::xlen::kXLenHexDigits),
                           kThemeVal, col_width, right_width, label_pad);
    }
    if (logical_row == 21) {
        return render_pair("stvec", std::format("0x{:0{}x}", st.stvec, simrv::xlen::kXLenHexDigits),
                           kThemeVal, "sepc",
                           std::format("0x{:0{}x}", st.sepc, simrv::xlen::kXLenHexDigits),
                           kThemeVal, col_width, right_width, label_pad);
    }
    if (logical_row == 22) {
        return render_pair("mtval", std::format("0x{:0{}x}", st.mtval, simrv::xlen::kXLenHexDigits),
                           kThemeVal, "satp",
                           std::format("0x{:0{}x}", st.satp, simrv::xlen::kXLenHexDigits),
                           kThemeVal, col_width, right_width, label_pad);
    }
    if (logical_row == 23) {
        return render_pair(
            "scause", std::format("0x{:0{}x}", st.scause, simrv::xlen::kXLenHexDigits), kThemeVal,
            "stval", std::format("0x{:0{}x}", st.stval, simrv::xlen::kXLenHexDigits), kThemeVal,
            col_width, right_width, label_pad);
    }
    if (logical_row == 24) {
        return render_pair(
            "medeleg", std::format("0x{:0{}x}", st.medeleg, simrv::xlen::kXLenHexDigits), kThemeVal,
            "mideleg", std::format("0x{:0{}x}", st.mideleg, simrv::xlen::kXLenHexDigits), kThemeVal,
            col_width, right_width, label_pad);
    }
    return format_to_width("", width);
}

}  // namespace simrv::tui
