/**
 * @file RegisterPane.cpp
 * @brief Implements RegisterPane widget rendering.
 */
#include "simrv/tui/RegisterPane.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/util/FormatUtil.hpp"
#include "simrv/util/InstructionExplainer.hpp"
#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/xlen/Helpers.hpp"
#include "simrv/xlen/Types.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include <chrono>
#include <format>
#include <string>
#include <vector>
#include <array>
#include <algorithm>

namespace simrv::tui {

namespace {

static constexpr std::array<const char*, 32> kRegNames = {
    "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0/fp", "s1", "a0",
    "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3",    "s4", "s5",
    "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5",    "t6"};

static constexpr std::array<const char*, 32> kFpRegNames = {
    "ft0", "ft1", "ft2", "ft3", "ft4",  "ft5",  "ft6", "ft7", "fs0",  "fs1", "fa0",
    "fa1", "fa2", "fa3", "fa4", "fa5",  "fa6",  "fa7", "fs2", "fs3",  "fs4", "fs5",
    "fs6", "fs7", "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"};

auto make_progress_bar(double ratio, int width, const std::string& color_code) -> std::string {
    int filled = static_cast<int>(ratio * width);
    if (filled < 0) filled = 0;
    if (filled > width) filled = width;
    std::string bar;
    bar += color_code;
    for (int i = 0; i < filled; ++i) {
        bar += "█";
    }
    bar += kSakuraMuted;
    for (int i = filled; i < width; ++i) {
        bar += "░";
    }
    bar += "\033[0m";
    return bar;
}

auto format_compact(uint64_t val) -> std::string {
    if (val >= 1000000000ULL) {
        return std::format("{:5.1f}G", static_cast<double>(val) / 1000000000.0);
    }
    if (val >= 1000000ULL) {
        return std::format("{:5.1f}M", static_cast<double>(val) / 1000000.0);
    }
    if (val >= 1000ULL) {
        return std::format("{:5.1f}K", static_cast<double>(val) / 1000.0);
    }
    return std::format("{:6d}", val);
}

}  // namespace

auto RegisterPane::render_row(int row_idx, int width) -> std::string {
    last_width_ = width;
    int logical_row = row_idx + scroll_offset_;

    if (page_ == TuiRegPage::EXPLAIN) {
        auto explain_rows = get_explain_rows(width);
        int total_logical_rows = static_cast<int>(explain_rows.size());
        if (logical_row >= total_logical_rows || logical_row < 0) {
            return format_to_width("", width);
        }
        return explain_rows.at(static_cast<std::size_t>(logical_row));
    }

    bool const is_reg_page = (page_ == TuiRegPage::GPR || page_ == TuiRegPage::FPR || page_ == TuiRegPage::VEC);
    bool const single_column = is_reg_page && ([&]() {
        if (page_ == TuiRegPage::GPR) {
            return (simrv::xlen::kIsXLen64 && width < 58) || (!simrv::xlen::kIsXLen64 && width < 42);
        }
        return width < 58;
    }());

    int adj_logical_row = (single_column && logical_row >= 32) ? (logical_row - 16) : logical_row;

    int base_rows = machine_.s_cycle_accurate ? 43 : 35;
    if (single_column) {
        base_rows += 16;
    }
    int debug_rows = machine_.s_debug_mode ? 4 : 0;
    int total_logical_rows = base_rows + debug_rows;

    if (logical_row >= total_logical_rows) {
        return format_to_width("", width);
    }

    auto section_line = [&](const std::string& title) -> std::string {
        if (title.starts_with("─") || title.starts_with(" ")) {
            std::string full = title + " ";
            int dash_len = width - get_display_width(full);
            if (dash_len < 0) dash_len = 0;
            return std::format("\033[1;38;5;254m{} \033[0m{}{}", title, kSakuraBorder, make_repeated_string("─", dash_len));
        } else {
            std::string text = " " + title + " ";
            int dash_len = width - get_display_width(text);
            if (dash_len < 0) dash_len = 0;
            int left_dashes = std::min(4, dash_len / 2);
            int right_dashes = dash_len - left_dashes;
            return std::format("{}{} \033[1;38;5;254m{}\033[0m {}{}", 
                               kSakuraBorder, make_repeated_string("─", left_dashes),
                               title,
                               kSakuraBorder, make_repeated_string("─", right_dashes));
        }
    };

    if (!paused_) {
        int max_active_row = single_column ? 40 : 24;
        if (logical_row >= 0 && logical_row <= max_active_row) {
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            constexpr std::array<const char*, 10> spinner = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
            std::string spin = spinner.at((static_cast<std::size_t>(now_ms / 80)) % 10);

            int target_row_offset = single_column ? 8 : 0;
            if (logical_row == 10 + target_row_offset) {
                std::string text = std::format("\033[38;5;218m●\033[0m \033[1;38;5;218mSIMULATOR ACTIVE\033[0m");
                int spaces = std::max(0, (width - 18) / 2);
                std::string line = std::string(spaces, ' ') + text;
                return format_to_width(line, width);
            }
            if (logical_row == 11 + target_row_offset) {
                std::string text = std::format("[  \033[38;5;121m{}\033[0m  Executing instructions... ]", spin);
                int spaces = std::max(0, (width - 33) / 2);
                std::string line = std::string(spaces, ' ') + text;
                return format_to_width(line, width);
            }
            if (logical_row == 12 + target_row_offset) {
                std::string text = std::format("Press \033[38;5;183m[Ctrl-P]\033[0m or \033[38;5;183mClick Mouse\033[0m to pause");
                int spaces = std::max(0, (width - 38) / 2);
                std::string line = std::string(spaces, ' ') + text;
                return format_to_width(line, width);
            }
            return format_to_width("", width);
        }
    }

    auto get_row_uncached = [&]() -> std::string {
        auto& cpu = machine_.cpu;
        auto& st = cpu.state();
        int col_width = width / 2;
        int right_width = width - col_width;

        int label_pad = (width < 50) ? 0 : ((width < 65) ? 5 : 8);
        auto make_field = [label_pad](const std::string& label, const std::string& value,
                             const char* value_color = kSakuraVal) -> std::string {
            if (label_pad == 0) {
                return std::format(" {}{}\033[0m: {}{}\033[0m", kSakuraText, label, value_color, value);
            } else {
                return std::format(" {}{:<{}}\033[0m: {}{}\033[0m", kSakuraText, label, label_pad, value_color, value);
            }
        };

        auto render_pair = [&](const std::string& l1, const std::string& v1, const char* c1,
                               const std::string& l2, const std::string& v2,
                               const char* c2) -> std::string {
            return format_to_width(make_field(l1, v1, c1), col_width) +
                   format_to_width(make_field(l2, v2, c2), right_width);
        };

        if (single_column) {
            if (logical_row >= 0 && logical_row < 32) {
                int reg = logical_row;
                if (page_ == TuiRegPage::GPR) {
                    auto val = st.regs.read(static_cast<RegId>(reg));
                    std::string name = kRegNames.at(static_cast<std::size_t>(reg));
                    bool changed = paused_ && (cached_gpr_.at(static_cast<std::size_t>(reg)) != val);
                    std::string c = changed ? kSakuraPeach : kSakuraMint;
                    std::string col_color = std::format(" {}x{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:0{}x}\033[0m",
                                                        kSakuraText, reg, kSakuraVal, name, c, val, simrv::xlen::kXLenHexDigits);
                    return format_to_width(col_color, width);
                } else if (page_ == TuiRegPage::FPR) {
                    auto val = st.regs.read_fp(static_cast<RegId>(reg));
                    std::string name = kFpRegNames.at(static_cast<std::size_t>(reg));
                    bool changed = paused_ && (cached_fpr_.at(static_cast<std::size_t>(reg)) != val);
                    std::string c = changed ? kSakuraPeach : kSakuraMint;
                    std::string col_color = std::format(" {}f{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:016x}\033[0m",
                                                        kSakuraText, reg, kSakuraVal, name, c, val);
                    return format_to_width(col_color, width);
                } else if (page_ == TuiRegPage::VEC) {
                    std::string col_color = std::format(" {}v{:<2}\033[0m       : {}0x0000000000000000\033[0m",
                                                        kSakuraText, reg, kSakuraMuted);
                    return format_to_width(col_color, width);
                }
            }
        } else {
            if (logical_row >= 0 && logical_row < 16) {
                int reg1 = logical_row;
                int reg2 = logical_row + 16;

                if (page_ == TuiRegPage::GPR) {
                    auto val1 = st.regs.read(static_cast<RegId>(reg1));
                    auto val2 = st.regs.read(static_cast<RegId>(reg2));

                    std::string name1 = kRegNames.at(static_cast<std::size_t>(reg1));
                    std::string name2 = kRegNames.at(static_cast<std::size_t>(reg2));

                    bool changed = paused_ && (cached_gpr_.at(static_cast<std::size_t>(reg1)) != val1);
                    std::string c1 = changed ? kSakuraPeach : kSakuraMint;
                    bool changed2 = paused_ && (cached_gpr_.at(static_cast<std::size_t>(reg2)) != val2);
                    std::string c2 = changed2 ? kSakuraPeach : kSakuraMint;

                    std::string col1_color =
                        std::format(" {}x{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:0{}x}\033[0m",
                                    kSakuraText, reg1, kSakuraVal, name1, c1, val1, simrv::xlen::kXLenHexDigits);
                    std::string col2_color =
                        std::format(" {}x{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:0{}x}\033[0m",
                                    kSakuraText, reg2, kSakuraVal, name2, c2, val2, simrv::xlen::kXLenHexDigits);

                    return format_to_width(col1_color, col_width) +
                           format_to_width(col2_color, width - col_width);
                } else if (page_ == TuiRegPage::FPR) {
                    auto val1 = st.regs.read_fp(static_cast<RegId>(reg1));
                    auto val2 = st.regs.read_fp(static_cast<RegId>(reg2));

                    std::string name1 = kFpRegNames.at(static_cast<std::size_t>(reg1));
                    std::string name2 = kFpRegNames.at(static_cast<std::size_t>(reg2));

                    bool changed = paused_ && (cached_fpr_.at(static_cast<std::size_t>(reg1)) != val1);
                    std::string c1 = changed ? kSakuraPeach : kSakuraMint;
                    bool changed2 = paused_ && (cached_fpr_.at(static_cast<std::size_t>(reg2)) != val2);
                    std::string c2 = changed2 ? kSakuraPeach : kSakuraMint;

                    std::string col1_color = std::format(
                        " {}f{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:016x}\033[0m", kSakuraText, reg1,
                        kSakuraVal, name1, c1, val1);
                    std::string col2_color = std::format(
                        " {}f{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:016x}\033[0m", kSakuraText, reg2,
                        kSakuraVal, name2, c2, val2);

                    return format_to_width(col1_color, col_width) +
                           format_to_width(col2_color, width - col_width);
                } else if (page_ == TuiRegPage::VEC) {
                    std::string col1_color = std::format(
                        " {}v{:<2}\033[0m       : {}0x0000000000000000\033[0m", kSakuraText, reg1, kSakuraMuted);
                    std::string col2_color = std::format(
                        " {}v{:<2}\033[0m       : {}0x0000000000000000\033[0m", kSakuraText, reg2, kSakuraMuted);

                    return format_to_width(col1_color, col_width) +
                           format_to_width(col2_color, width - col_width);
                } else {
                if (machine_.s_cycle_accurate) {
                    auto& ps = cpu.pipeline_sim;

                    auto get_stage_desc = [](const simrv::pipeline::PipelineReg& reg, uint32_t stall_rem, const std::string& stall_type, bool raw_stall = false) -> std::string {
                        if (!reg.valid) {
                            return "\033[38;5;244mbubble/empty\033[0m";
                        }
                        std::string_view op_name = "UNKNOWN";
                        if (static_cast<std::size_t>(reg.op_id) < simrv::pipeline::OPERATION_NAME.size()) {
                            op_name = simrv::pipeline::OPERATION_NAME.at(static_cast<std::size_t>(reg.op_id));
                        }
                        std::string desc = std::format("\033[1;38;5;121m0x{:0{}x}\033[0m (\033[1;38;5;183m{}\033[0m)", 
                                                       reg.pc, simrv::xlen::kXLenHexDigits, op_name);
                        if (stall_rem > 0) {
                            desc += std::format(" \033[38;5;203m[Stall: {} ({} clk)]\033[0m", stall_type, stall_rem);
                        } else if (raw_stall) {
                            desc += " \033[38;5;203m[Stall: RAW Hazard]\033[0m";
                        } else if (reg.remaining_latency > 0) {
                            desc += std::format(" \033[38;5;218m[Lat: {} clk]\033[0m", reg.remaining_latency);
                        }
                        return desc;
                    };

                    bool is_raw_stalled = false;
                    if (ps.d_reg_.valid) {
                        bool reads_rs1 = (ps.d_reg_.rs1 != static_cast<RegId>(0));
                        bool reads_rs2 = (ps.d_reg_.rs2 != static_cast<RegId>(0));
                        if (reads_rs1 || reads_rs2) {
                            if (ps.e_reg_.valid && ps.e_reg_.writes_reg && ps.e_reg_.rd != static_cast<RegId>(0)) {
                                if ((reads_rs1 && ps.d_reg_.rs1 == ps.e_reg_.rd) || (reads_rs2 && ps.d_reg_.rs2 == ps.e_reg_.rd)) {
                                    if (ps.e_reg_.remaining_latency > 0) is_raw_stalled = true;
                                }
                            }
                            if (ps.m_reg_.valid && ps.m_reg_.writes_reg && ps.m_reg_.rd != static_cast<RegId>(0)) {
                                if ((reads_rs1 && ps.d_reg_.rs1 == ps.m_reg_.rd) || (reads_rs2 && ps.d_reg_.rs2 == ps.m_reg_.rd)) {
                                    if (ps.m_reg_.remaining_latency > 0) is_raw_stalled = true;
                                }
                            }
                        }
                    }

                    switch (logical_row) {
                        case 0:
                            return section_line("Pipeline Stages (Cycle-Accurate Mode)");
                        case 1:
                            {
                                std::string stall_type = ps.tlb_stall_remaining_ > 0 ? "TLB Miss" : "ICache Miss";
                                uint32_t stall_rem = ps.tlb_stall_remaining_ > 0 ? ps.tlb_stall_remaining_ : ps.icache_stall_remaining_;
                                return format_to_width(std::format("  \033[1;38;5;189mIF\033[0m  : {}", get_stage_desc(ps.f_reg_, stall_rem, stall_type)), width);
                            }
                        case 2:
                            return format_to_width(std::format("  \033[1;38;5;189mID\033[0m  : {}", get_stage_desc(ps.d_reg_, 0, "", is_raw_stalled)), width);
                        case 3:
                            return format_to_width(std::format("  \033[1;38;5;189mEX\033[0m  : {}", get_stage_desc(ps.e_reg_, ps.div_busy_cycles_remaining_, "Divider")), width);
                        case 4:
                            return format_to_width(std::format("  \033[1;38;5;189mMEM\033[0m : {}", get_stage_desc(ps.m_reg_, ps.dcache_stall_remaining_, "DCache Miss")), width);
                        case 5:
                            return format_to_width(std::format("  \033[1;38;5;189mWB\033[0m  : {}", get_stage_desc(ps.w_reg_, 0, "")), width);
                        case 6:
                            return section_line("Active Stalls & Hazards");
                        case 7:
                            return render_pair("RAW Stall", is_raw_stalled ? "Active" : "None", is_raw_stalled ? kSakuraPeach : kSakuraMint,
                                               "Divider St", ps.div_busy_cycles_remaining_ > 0 ? "Active" : "None", ps.div_busy_cycles_remaining_ > 0 ? kSakuraPeach : kSakuraMint);
                        case 8:
                            return render_pair("ICache St", ps.icache_stall_remaining_ > 0 ? "Active" : "None", ps.icache_stall_remaining_ > 0 ? kSakuraPeach : kSakuraMint,
                                               "DCache St", ps.dcache_stall_remaining_ > 0 ? "Active" : "None", ps.dcache_stall_remaining_ > 0 ? kSakuraPeach : kSakuraMint);
                        case 9:
                            return render_pair("TLB Stall", ps.tlb_stall_remaining_ > 0 ? "Active" : "None", ps.tlb_stall_remaining_ > 0 ? kSakuraPeach : kSakuraMint,
                                               "Ctrl St", ps.control_bubble_remaining_ > 0 ? "Redirecting" : "None", ps.control_bubble_remaining_ > 0 ? kSakuraPeach : kSakuraMint);
                        case 10:
                            return section_line("Branch Prediction & BTB");
                        case 11:
                            {
                                Register pc = 0;
                                if (ps.e_reg_.valid && (ps.e_reg_.is_branch || ps.e_reg_.is_jump)) pc = ps.e_reg_.pc;
                                else if (ps.d_reg_.valid && (ps.d_reg_.is_branch || ps.d_reg_.is_jump)) pc = ps.d_reg_.pc;
                                
                                std::string bht_str = "N/A";
                                if (pc != 0) {
                                    const uint32_t bht_idx = (pc >> 1) & 0xFF;
                                    uint8_t counter = ps.branch_history_table_.at(bht_idx);
                                    bht_str = counter == 0 ? "Strongly Not Taken (00)" :
                                              counter == 1 ? "Weakly Not Taken (01)" :
                                              counter == 2 ? "Weakly Taken (10)" : "Strongly Taken (11)";
                                }
                                return format_to_width(std::format("  {}BHT State\033[0m : {}{}\033[0m", kSakuraText, kSakuraVal, bht_str), width);
                            }
                        case 12:
                            {
                                Register pc = 0;
                                if (ps.e_reg_.valid && (ps.e_reg_.is_branch || ps.e_reg_.is_jump)) pc = ps.e_reg_.pc;
                                else if (ps.d_reg_.valid && (ps.d_reg_.is_branch || ps.d_reg_.is_jump)) pc = ps.d_reg_.pc;

                                std::string btb_str = "N/A";
                                if (pc != 0) {
                                    const uint32_t btb_idx = (pc >> 1) & 0x7F;
                                    auto& btb_entry = ps.btb_.at(btb_idx);
                                    if (btb_entry.valid && btb_entry.pc == pc) {
                                        btb_str = std::format("Hit (Target: 0x{:0{}x})", btb_entry.target, simrv::xlen::kXLenHexDigits);
                                    } else {
                                        btb_str = "Miss";
                                    }
                                }
                                return format_to_width(std::format("  {}BTB State\033[0m : {}{}\033[0m", kSakuraText, kSakuraVal, btb_str), width);
                            }
                        case 13:
                            return section_line("End Pipeline Visualizer");
                        default:
                            return format_to_width("", width);
                    }
                } else {
                    auto& ctx = cpu.pipeline_context;

                    switch (logical_row) {
                        case 0:
                            return section_line("── IF/CVT");
                        case 1:
                            return render_pair(
                                "cpc", std::format("0x{:0{}x}", ctx.cpc, simrv::xlen::kXLenHexDigits),
                                kSakuraMint, "ir_org", std::format("0x{:08x}", ctx.ir_org), kSakuraVal);
                        case 2:
                            return render_pair("ir", std::format("0x{:08x}", ctx.ir), kSakuraVal,
                                               "cinsn", std::format("0x{:08x}", ctx.cinsn), kSakuraVal);
                        case 3:
                            return section_line("── ID");
                        case 4:
                            return render_pair(
                                "opcode", std::to_string(std::to_underlying(ctx.opcode)), kSakuraVal,
                                "funct3", std::to_string(std::to_underlying(ctx.funct3)), kSakuraVal);
                        case 5:
                            return render_pair(
                                "rd/rs1",
                                std::format("{}/{}", std::to_underlying(ctx.rd),
                                             std::to_underlying(ctx.rs1)),
                                kSakuraVal, "rs2/f7",
                                std::format("{}/0x{:x}", std::to_underlying(ctx.rs2), ctx.funct7),
                                kSakuraVal);
                        case 6:
                            return render_pair(
                                "imm", std::format("0x{:0{}x}", ctx.imm, simrv::xlen::kXLenHexDigits),
                                kSakuraVal, "funct12", std::format("0x{:x}", ctx.funct12), kSakuraVal);
                        case 7:
                            return section_line("── OF/EX");
                        case 8:
                            return render_pair(
                                "rrs1", std::format("0x{:0{}x}", ctx.rrs1, simrv::xlen::kXLenHexDigits),
                                kSakuraMint, "rrs2",
                                std::format("0x{:0{}x}", ctx.rrs2, simrv::xlen::kXLenHexDigits),
                                kSakuraMint);
                        case 9:
                            return render_pair(
                                "jmp_pc",
                                std::format("0x{:0{}x}", ctx.jmp_pc, simrv::xlen::kXLenHexDigits),
                                kSakuraMint, "taken", ctx.tkn ? "yes" : "no", kSakuraVal);
                        case 10:
                            return render_pair(
                                "wb_data",
                                std::format("0x{:0{}x}", ctx.wb_data, simrv::xlen::kXLenHexDigits),
                                kSakuraMint, "wb_csr",
                                std::format("0x{:0{}x}", ctx.wb_data_csr, simrv::xlen::kXLenHexDigits),
                                kSakuraVal);
                        case 11:
                            return section_line("── MEM/FP");
                        case 12:
                            return render_pair(
                                "mem_addr",
                                std::format("0x{:0{}x}", ctx.mem_addr, simrv::xlen::kXLenHexDigits),
                                kSakuraMint, "mem_w",
                                std::format("0x{:0{}x}", ctx.mem_wdata, simrv::xlen::kXLenHexDigits),
                                kSakuraMint);
                        case 13:
                            return render_pair(
                                "mem_r",
                                std::format("0x{:0{}x}", ctx.mem_rdata, simrv::xlen::kXLenHexDigits),
                                kSakuraMint, "fp_wb", std::format("0x{:016x}", ctx.fp_wb_data),
                                kSakuraVal);
                        case 14:
                            return render_pair("fp_wb_en", ctx.fp_wb_enable ? "on" : "off", kSakuraVal,
                                               "int<-fp", ctx.int_wb_from_fp ? "on" : "off",
                                               kSakuraVal);
                        case 15:
                            return section_line("── TRAP/TLB");
                        default:
                            return format_to_width(std::format(" {}Pipeline page\033[0m", kSakuraMuted), width);
                    }
                }
            }
        }
    }

        if (page_ == TuiRegPage::PIPELINE) {
            if (machine_.s_cycle_accurate) {
                return format_to_width("", width);
            }
            auto& ctx = cpu.pipeline_context;
            auto exc_text = ctx.pending_exception.has_value()
                                ? std::to_string(std::to_underlying(ctx.pending_exception.value()))
                                : std::string("none");

            switch (logical_row) {
                case 16:
                    return render_pair(
                        "exc", exc_text, kSakuraPeach, "tval",
                        std::format("0x{:0{}x}", ctx.pending_tval, simrv::xlen::kXLenHexDigits),
                        kSakuraVal);
                case 17:
                    return render_pair(
                        "padr1", std::format("0x{:0{}x}", ctx.padr1, simrv::xlen::kXLenHexDigits),
                        kSakuraVal, "padr2",
                        std::format("0x{:0{}x}", ctx.padr2, simrv::xlen::kXLenHexDigits),
                        kSakuraVal);
                case 18:
                    return render_pair(
                        "rcsr", std::format("0x{:0{}x}", ctx.rcsr, simrv::xlen::kXLenHexDigits),
                        kSakuraVal, "funct5", std::to_string(std::to_underlying(ctx.funct5)),
                        kSakuraVal);
                case 19:
                    return section_line("── End Pipeline Snapshot");
                default:
                    return format_to_width("", width);
            }
        }

        if (adj_logical_row == 16) {
            return section_line("CSRs & Privilege State");
        }

        if (adj_logical_row == 17) {
            std::string priv_str = (st.priv == PrivilegeLevel::Machine)      ? "Machine"
                                   : (st.priv == PrivilegeLevel::Supervisor) ? "Supervisor"
                                                                             : "User";
            return render_pair("pc", std::format("0x{:0{}x}", st.pc, simrv::xlen::kXLenHexDigits),
                               kSakuraMint, "priv", priv_str, kSakuraPink);
        }

        if (adj_logical_row == 18) {
            std::string misa_str = simrv::xlen::resolve_misa_string(st.misa);

            return render_pair("mstatus",
                               std::format("0x{:0{}x}", st.mstatus, simrv::xlen::kXLenHexDigits),
                               kSakuraVal, "misa", misa_str, kSakuraVal);
        }

        if (adj_logical_row == 19) {
            return render_pair(
                "mie", std::format("0x{:0{}x}", st.mie, simrv::xlen::kXLenHexDigits), kSakuraVal,
                "mip", std::format("0x{:0{}x}", st.mip, simrv::xlen::kXLenHexDigits), kSakuraVal);
        }

        if (adj_logical_row == 20) {
            return render_pair(
                "mtvec", std::format("0x{:0{}x}", st.mtvec, simrv::xlen::kXLenHexDigits),
                kSakuraVal, "mepc", std::format("0x{:0{}x}", st.mepc, simrv::xlen::kXLenHexDigits),
                kSakuraVal);
        }

        if (adj_logical_row == 21) {
            return render_pair(
                "stvec", std::format("0x{:0{}x}", st.stvec, simrv::xlen::kXLenHexDigits),
                kSakuraVal, "sepc", std::format("0x{:0{}x}", st.sepc, simrv::xlen::kXLenHexDigits),
                kSakuraVal);
        }

        if (adj_logical_row == 22) {
            return render_pair(
                "mtval", std::format("0x{:0{}x}", st.mtval, simrv::xlen::kXLenHexDigits),
                kSakuraVal, "satp", std::format("0x{:0{}x}", st.satp, simrv::xlen::kXLenHexDigits),
                kSakuraVal);
        }

        if (adj_logical_row == 23) {
            return render_pair(
                "scause", std::format("0x{:0{}x}", st.scause, simrv::xlen::kXLenHexDigits),
                kSakuraVal, "stval",
                std::format("0x{:0{}x}", st.stval, simrv::xlen::kXLenHexDigits), kSakuraVal);
        }

        if (adj_logical_row == 24) {
            return render_pair(
                "medeleg", std::format("0x{:0{}x}", st.medeleg, simrv::xlen::kXLenHexDigits),
                kSakuraVal, "mideleg",
                std::format("0x{:0{}x}", st.mideleg, simrv::xlen::kXLenHexDigits), kSakuraVal);
        }

        if (machine_.s_debug_mode && logical_row >= base_rows && logical_row < total_logical_rows) {
            int debug_row = logical_row - base_rows;
            if (debug_row == 0) {
                return section_line("Debug Diagnostics");
            }
            if (debug_row == 1) {
                std::string sym = machine_.symbols.lookup(st.pc);
                if (sym.empty()) {
                    sym = "none";
                } else {
                    sym = "<" + sym + ">";
                }
                return format_to_width(std::format(" {}{:<8}\033[0m: {}{}\033[0m", kSakuraText, "symbol", kSakuraPeach, sym), width);
            }
            if (debug_row == 2) {
                std::string gdb_status = "disabled";
                if (machine_.gdb_stub) {
                    gdb_status = machine_.gdb_stub->is_connected() ? "connected" : "listening";
                }
                std::string lockstep_status = machine_.spike_lockstep ? "active" : "disabled";
                return render_pair("gdb_stub", gdb_status, machine_.gdb_stub ? kSakuraMint : kSakuraMuted,
                                   "lockstep", lockstep_status, machine_.spike_lockstep ? kSakuraMint : kSakuraMuted);
            }
            if (debug_row == 3) {
                std::string tohost_str = std::format("0x{:x}", machine_.tohost);
                std::string traplog_status = machine_.s_traplog_mode ? "active" : "disabled";
                return render_pair("tohost", tohost_str, machine_.tohost != 0 ? kSakuraPeach : kSakuraVal,
                                   "traplog", traplog_status, machine_.s_traplog_mode ? kSakuraMint : kSakuraMuted);
            }
        }

        return "";
    };

    int max_cache_row = single_column ? 40 : 24;
    if (logical_row >= 0 && logical_row <= max_cache_row) {
        std::string res = get_row_uncached();
        if (static_cast<std::size_t>(logical_row) < cached_left_rows_.size()) {
            cached_left_rows_.at(static_cast<std::size_t>(logical_row)) = res;
        }
        last_width_ = width;
        return res;
    }

    if (machine_.s_debug_mode && logical_row >= base_rows && logical_row < total_logical_rows) {
        return get_row_uncached();
    }

    auto& cpu = machine_.cpu;

    if (!machine_.s_cycle_accurate) {
        if (adj_logical_row == 25) {
            return section_line("Performance & Machine Stats");
        }
        if (adj_logical_row == 26) {
            std::string insns = std::format("  Executed Insns : {}{}\033[0m", kSakuraMint, simrv::util::format_with_commas(cpu.e_icount));
            return format_to_width(insns, width);
        }
        if (adj_logical_row == 27) {
            double sim_time_seconds = static_cast<double>(cpu.clint_mmio.mtime) / 10000000.0;
            std::string time = std::format("  Simulated Time : {}{:.6f} s\033[0m {}(0x{:x})\033[0m",
                                           kSakuraMint, sim_time_seconds, kSakuraMuted, cpu.clint_mmio.mtime);
            return format_to_width(time, width);
        }
        if (adj_logical_row == 28) {
            std::string time = std::format("  Active Runtime : {}{:.6f} s\033[0m",
                                           kSakuraMint, active_runtime_);
            return format_to_width(time, width);
        }
        if (adj_logical_row == 29) {
            std::string mode = std::format("  Simulation Mode: {}Functional (High-Perf)\033[0m", kSakuraVal);
            return format_to_width(mode, width);
        }
        if (adj_logical_row == 30) {
            std::string extensions = simrv::xlen::resolve_misa_string(cpu.state().misa);
            std::string isa = std::format("  ISA Extensions : {}{}\033[0m", kSakuraVal, extensions);
            return format_to_width(isa, width);
        }
        if (adj_logical_row == 31) {
            std::string mem_name = machine_.s_fn_memimg;
            auto pos = mem_name.find_last_of("/\\");
            if (pos != std::string::npos) mem_name = mem_name.substr(pos + 1);
            if (mem_name.empty()) mem_name = "None";
            std::string img = std::format("  Memory Image   : {}{}\033[0m", kSakuraSky, mem_name);
            return format_to_width(img, width);
        }
        if (adj_logical_row == 32) {
            std::string dsk_name = "Disabled";
            if (machine_.s_use_disk) {
                dsk_name = machine_.s_fn_dskimg;
                auto pos = dsk_name.find_last_of("/\\");
                if (pos != std::string::npos) dsk_name = dsk_name.substr(pos + 1);
                if (dsk_name.empty()) dsk_name = "None";
            }
            std::string dsk = std::format("  Disk Image     : {}{}\033[0m", kSakuraSky, dsk_name);
            return format_to_width(dsk, width);
        }
        if (adj_logical_row == 33) {
            uint64_t max_val = 1;
            for (auto val : kips_history_) {
                if (val > max_val) max_val = val;
            }

            std::string suffix =
                std::format("] {} Max:{}", simrv::util::format_with_commas(kips_), simrv::util::format_with_commas(max_val));
            std::string prefix = "  Speed (KIPS)   : [";
            int spark_width =
                width - static_cast<int>(prefix.length()) - static_cast<int>(suffix.length());
            if (spark_width < 5) spark_width = 5;

            std::string spark = get_sparkline_string(spark_width);
            std::string color = std::format(
                "  {}Speed (KIPS)\033[0m   : [{}{}\033[0m] {}{}\033[0m {}{}Max:{}\033[0m",
                kSakuraText, kSakuraMint, spark, kSakuraMint, simrv::util::format_with_commas(kips_), kSakuraMuted, kSakuraMuted, simrv::util::format_with_commas(max_val));

            return format_to_width(color, width);
        }
        if (adj_logical_row == 34) {
            return format_to_width("", width);
        }
        return format_to_width("", width);
    }

    if (adj_logical_row == 25) {
        return section_line("Statistics & Performance");
    }

    if (adj_logical_row == 26) {
        uint64_t i_hits = cpu.icache.hit_count();
        uint64_t i_misses = cpu.icache.miss_count();
        uint64_t i_total = i_hits + i_misses;
        double i_ratio =
            (i_total == 0) ? 0.0 : static_cast<double>(i_hits) / static_cast<double>(i_total);

        std::string suffix = std::format(" {:5.1f}% (H:{} M:{})", i_ratio * 100.0,
                                         format_compact(i_hits), format_compact(i_misses));
        std::string prefix = "  L1-I Cache     : [";
        int bar_width =
            width - static_cast<int>(prefix.length()) - static_cast<int>(suffix.length()) - 1;
        if (bar_width < 5) bar_width = 5;

        std::string bar = make_progress_bar(i_ratio, bar_width, kSakuraSky);
        std::string color = std::format(
            "  {}L1-I Cache\033[0m     : [{}] {}{:5.1f}%\033[0m {}(H:{} M:{})\033[0m",
            kSakuraText, bar, kSakuraSky, i_ratio * 100.0, kSakuraMuted, format_compact(i_hits), format_compact(i_misses));

        return format_to_width(color, width);
    }

    if (adj_logical_row == 27) {
        uint64_t d_hits = cpu.dcache.hit_count();
        uint64_t d_misses = cpu.dcache.miss_count();
        uint64_t d_total = d_hits + d_misses;
        double d_ratio =
            (d_total == 0) ? 0.0 : static_cast<double>(d_hits) / static_cast<double>(d_total);

        std::string suffix = std::format(" {:5.1f}% (H:{} M:{})", d_ratio * 100.0,
                                         format_compact(d_hits), format_compact(d_misses));
        std::string prefix = "  L1-D Cache     : [";
        int bar_width =
            width - static_cast<int>(prefix.length()) - static_cast<int>(suffix.length()) - 1;
        if (bar_width < 5) bar_width = 5;

        std::string bar = make_progress_bar(d_ratio, bar_width, kSakuraPink);
        std::string color = std::format(
            "  {}L1-D Cache\033[0m     : [{}] {}{:5.1f}%\033[0m {}(H:{} M:{})\033[0m",
            kSakuraText, bar, kSakuraPink, d_ratio * 100.0, kSakuraMuted, format_compact(d_hits), format_compact(d_misses));

        return format_to_width(color, width);
    }

    uint64_t cycles = cpu.clint_mmio.mcycle;
    uint64_t icount = cpu.e_icount;
    double ipc = (cycles == 0) ? 0.0 : static_cast<double>(icount) / static_cast<double>(cycles);
    double cpi = (icount == 0) ? 0.0 : static_cast<double>(cycles) / static_cast<double>(icount);

    if (adj_logical_row == 28) {
        std::string color = std::format(
            "  {}IPC / CPI\033[0m      : {}{:.2f} IPC\033[0m  /  {}{:.2f} CPI\033[0m",
            kSakuraText, kSakuraMint, ipc, kSakuraPeach, cpi);
        return format_to_width(color, width);
    }

    uint64_t stalls = cpu.pipeline_sim.stall_cycles();
    uint64_t bubbles = cpu.pipeline_sim.bubble_cycles();
    uint64_t total_stalls_bubbles = stalls + bubbles;
    double stall_pct = (cycles == 0) ? 0.0 : (static_cast<double>(total_stalls_bubbles) * 100.0) / static_cast<double>(cycles);

    if (adj_logical_row == 29) {
        std::string color = std::format(
            "  {}Stall Ratio\033[0m    : {}{:5.1f}%\033[0m (Stall:{} clk, Bubble:{} clk)",
            kSakuraText, kSakuraCoral, stall_pct, format_compact(stalls), format_compact(bubbles));
        return format_to_width(color, width);
    }

    uint64_t data_stalls = cpu.pipeline_sim.data_hazard_stalls();
    uint64_t ctrl_bubbles = cpu.pipeline_sim.control_hazard_bubbles();
    uint64_t ic_stalls = cpu.pipeline_sim.icache_stalls();
    uint64_t dc_stalls = cpu.pipeline_sim.dcache_stalls();
    uint64_t cache_stalls = ic_stalls + dc_stalls;

    if (adj_logical_row == 30) {
        double data_pct = (cycles == 0) ? 0.0 : (static_cast<double>(data_stalls) * 100.0) / static_cast<double>(cycles);
        std::string color = std::format(
            "    {}- Data RAW\033[0m   : {}{:>10}\033[0m clk {}({:5.1f}%)\033[0m",
            kSakuraMuted, kSakuraPeach, simrv::util::format_with_commas(data_stalls), kSakuraMuted, data_pct);
        return format_to_width(color, width);
    }

    if (adj_logical_row == 31) {
        double ctrl_pct = (cycles == 0) ? 0.0 : (static_cast<double>(ctrl_bubbles) * 100.0) / static_cast<double>(cycles);
        std::string color = std::format(
            "    {}- Control\033[0m    : {}{:>10}\033[0m clk {}({:5.1f}%)\033[0m",
            kSakuraMuted, kSakuraPeach, simrv::util::format_with_commas(ctrl_bubbles), kSakuraMuted, ctrl_pct);
        return format_to_width(color, width);
    }

    if (adj_logical_row == 32) {
        double cache_pct = (cycles == 0) ? 0.0 : (static_cast<double>(cache_stalls) * 100.0) / static_cast<double>(cycles);
        std::string color = std::format(
            "    {}- Cache\033[0m      : {}{:>10}\033[0m clk {}({:5.1f}%)\033[0m",
            kSakuraMuted, kSakuraPeach, simrv::util::format_with_commas(cache_stalls), kSakuraMuted, cache_pct);
        return format_to_width(color, width);
    }

    if (adj_logical_row == 33) {
        uint64_t max_val = 1;
        for (auto val : kips_history_) {
            if (val > max_val) max_val = val;
        }

        std::string suffix =
            std::format("] {} Max:{}", simrv::util::format_with_commas(kips_), simrv::util::format_with_commas(max_val));
        std::string prefix = "  Speed (KIPS)   : [";
        int spark_width =
            width - static_cast<int>(prefix.length()) - static_cast<int>(suffix.length());
        if (spark_width < 5) spark_width = 5;

        std::string spark = get_sparkline_string(spark_width);
        std::string color = std::format(
            "  {}Speed (KIPS)\033[0m   : [{}{}\033[0m] {}{}\033[0m {}{}Max:{}\033[0m",
            kSakuraText, kSakuraMint, spark, kSakuraMint, simrv::util::format_with_commas(kips_), kSakuraMuted, kSakuraMuted, simrv::util::format_with_commas(max_val));

        return format_to_width(color, width);
    }

    if (adj_logical_row == 34) {
        uint64_t alu_count = 0;
        uint64_t mem_count = 0;
        uint64_t ctrl_count = 0;
        uint64_t sys_count = 0;

        for (int i = 0; i < OperationIdCount; ++i) {
            uint64_t count = cpu.e_instmix.at(static_cast<std::size_t>(i));
            if (i == OperationId::JAL || i == OperationId::JALR ||
                (i >= OperationId::BEQ && i <= OperationId::BGEU)) {
                ctrl_count += count;
            } else if ((i >= OperationId::LB && i <= OperationId::SD) || i == OperationId::LR_W ||
                       i == OperationId::SC_W ||
                       (i >= OperationId::AMOSWAP_W && i <= OperationId::AMOMAXU_W)) {
                mem_count += count;
            } else if (i == OperationId::ECALL || i == OperationId::EBREAK ||
                       i == OperationId::URET || i == OperationId::SRET || i == OperationId::MRET ||
                       i == OperationId::WFI || i == OperationId::SFENCE_VMA) {
                sys_count += count;
            } else {
                alu_count += count;
            }
        }

        uint64_t total = alu_count + mem_count + ctrl_count + sys_count;
        double alu_p = (total == 0)
                           ? 0.0
                           : static_cast<double>(alu_count * 100ULL) / static_cast<double>(total);
        double mem_p = (total == 0)
                           ? 0.0
                           : static_cast<double>(mem_count * 100ULL) / static_cast<double>(total);
        double ctrl_p = (total == 0)
                            ? 0.0
                            : static_cast<double>(ctrl_count * 100ULL) / static_cast<double>(total);
        double sys_p = (total == 0)
                           ? 0.0
                           : static_cast<double>(sys_count * 100ULL) / static_cast<double>(total);

        std::string color = std::format(
            "  {}Inst Mix\033[0m       : {}ALU:{:.1f}%\033[0m {}MEM:{:.1f}%\033[0m "
            "{}CTRL:{:.1f}%\033[0m {}SYS:{:.1f}%\033[0m",
            kSakuraText, kSakuraMint, alu_p, kSakuraSky, mem_p, kSakuraPeach, ctrl_p, kSakuraPink, sys_p);

        return format_to_width(color, width);
    }

    if (adj_logical_row == 35) {
        return section_line("Machine & Hardware Info");
    }

    if (adj_logical_row == 36) {
        double sim_time_seconds = static_cast<double>(cpu.clint_mmio.mtime) / 10000000.0;
        std::string time = std::format("  Simulated Time : {}{:.6f} s\033[0m {}(0x{:x})\033[0m",
                                       kSakuraMint, sim_time_seconds, kSakuraMuted, cpu.clint_mmio.mtime);
        return format_to_width(time, width);
    }

    if (adj_logical_row == 37) {
        std::string time = std::format("  Active Runtime : {}{:.6f} s\033[0m",
                                       kSakuraMint, active_runtime_);
        return format_to_width(time, width);
    }

    if (adj_logical_row == 38) {
        std::string mode = std::format("  Simulation Mode: {}Cycle-Accurate (CA)\033[0m", kSakuraVal);
        return format_to_width(mode, width);
    }

    if (adj_logical_row == 39) {
        std::string extensions = simrv::xlen::resolve_misa_string(cpu.state().misa);
        std::string isa = std::format("  ISA Extensions : {}{}\033[0m", kSakuraVal, extensions);
        return format_to_width(isa, width);
    }

    if (adj_logical_row == 40) {
        std::string mem_name = machine_.s_fn_memimg;
        auto pos = mem_name.find_last_of("/\\");
        if (pos != std::string::npos) mem_name = mem_name.substr(pos + 1);
        if (mem_name.empty()) mem_name = "None";
        std::string img = std::format("  Memory Image   : {}{}\033[0m", kSakuraSky, mem_name);
        return format_to_width(img, width);
    }

    if (adj_logical_row == 41) {
        std::string dsk_name = "Disabled";
        if (machine_.s_use_disk) {
            dsk_name = machine_.s_fn_dskimg;
            auto pos = dsk_name.find_last_of("/\\");
            if (pos != std::string::npos) dsk_name = dsk_name.substr(pos + 1);
            if (dsk_name.empty()) dsk_name = "None";
        }
        std::string dsk = std::format("  Disk Image     : {}{}\033[0m", kSakuraSky, dsk_name);
        return format_to_width(dsk, width);
    }

    if (adj_logical_row == 42) {
        return format_to_width("", width);
    }

    return format_to_width("", width);
}

auto RegisterPane::get_sparkline_string(int width) -> std::string {
    if (kips_history_.empty()) {
        return std::string(static_cast<std::size_t>(width), ' ');
    }
    uint64_t max_val = 1;
    for (auto val : kips_history_) {
        if (val > max_val) max_val = val;
    }

    std::string s;
    int history_size = static_cast<int>(kips_history_.size());
    int pad = width - history_size;
    if (pad > 0) {
        s += std::string(static_cast<std::size_t>(pad), ' ');
    }

    constexpr std::array<const char*, 8> blocks = {" ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    int start_hist = (history_size > width) ? (history_size - width) : 0;
    for (int i = start_hist; i < history_size; ++i) {
        double ratio = static_cast<double>(kips_history_.at(static_cast<std::size_t>(i))) /
                       static_cast<double>(max_val);
        int block_idx = static_cast<int>(ratio * 7.0);
        if (block_idx < 0) block_idx = 0;
        if (block_idx > 7) block_idx = 7;
        s += blocks.at(static_cast<std::size_t>(block_idx));
    }
    return s;
}

void RegisterPane::update_cache() {
    auto& st = machine_.cpu.state();
    for (int i = 0; i < 32; ++i) {
        cached_gpr_.at(static_cast<std::size_t>(i)) = st.regs.read(static_cast<RegId>(i));
        cached_fpr_.at(static_cast<std::size_t>(i)) = st.regs.read_fp(static_cast<RegId>(i));
    }
}

auto RegisterPane::get_explain_rows(int width) -> std::vector<std::string> {
    auto& cpu = machine_.cpu;
    auto& st = cpu.state();

    auto section_line = [&](const std::string& title) -> std::string {
        if (title.starts_with("─") || title.starts_with(" ")) {
            std::string full = title + " ";
            int dash_len = width - get_display_width(full);
            if (dash_len < 0) dash_len = 0;
            return std::format("\033[1;38;5;254m{} \033[0m{}{}", title, kSakuraBorder, make_repeated_string("─", dash_len));
        } else {
            std::string text = " " + title + " ";
            int dash_len = width - get_display_width(text);
            if (dash_len < 0) dash_len = 0;
            int left_dashes = std::min(4, dash_len / 2);
            int right_dashes = dash_len - left_dashes;
            return std::format("{}{} \033[1;38;5;254m{}\033[0m {}{}", 
                               kSakuraBorder, make_repeated_string("─", left_dashes),
                               title,
                               kSakuraBorder, make_repeated_string("─", right_dashes));
        }
    };

    auto& ctx = cpu.pipeline_context;
    if (ctx.cpc != st.pc) {
        auto& mutable_cpu = const_cast<simrv::core::CPU&>(cpu);
        auto saved_pc = st.pc;
        bool fetch_success = mutable_cpu.fetch_stage(machine_, st.pc);
        if (fetch_success) {
            (void)mutable_cpu.decode_stage(machine_);
        }
        st.pc = saved_pc;
    }

    bool const is_compressed = (ctx.ir_org & 0x3) != 0x3;
    uint32_t decompressed_inst = ctx.ir;
    
    enum class InstFormat : uint8_t {
        R, I, S, B, U, J, R4, Unknown
    };
    auto get_format = [](::Opcode op) -> InstFormat {
        switch (op) {
            case ::Opcode::Load:
            case ::Opcode::LoadFp:
            case ::Opcode::MiscMem:
            case ::Opcode::OpImm:
            case ::Opcode::OpImm32:
            case ::Opcode::Jalr:
            case ::Opcode::System:
                return InstFormat::I;
            case ::Opcode::Store:
            case ::Opcode::StoreFp:
                return InstFormat::S;
            case ::Opcode::Branch:
                return InstFormat::B;
            case ::Opcode::Auipc:
            case ::Opcode::Lui:
                return InstFormat::U;
            case ::Opcode::Jal:
                return InstFormat::J;
            case ::Opcode::Op:
            case ::Opcode::Op32:
            case ::Opcode::Amo:
            case ::Opcode::OpFp:
                return InstFormat::R;
            case ::Opcode::MAdd:
            case ::Opcode::MSub:
            case ::Opcode::NMSub:
            case ::Opcode::NMAdd:
                return InstFormat::R4;
            default:
                return InstFormat::Unknown;
        }
    };

    auto get_format_name = [](InstFormat fmt) -> std::string_view {
        switch (fmt) {
            case InstFormat::R: return "R-Type (Register-Register)";
            case InstFormat::I: return "I-Type (Register-Immediate / Load / Jump)";
            case InstFormat::S: return "S-Type (Store)";
            case InstFormat::B: return "B-Type (Branch)";
            case InstFormat::U: return "U-Type (Upper Immediate)";
            case InstFormat::J: return "J-Type (Unconditional Jump)";
            case InstFormat::R4: return "R4-Type (Fused Multiply-Add)";
            default: return "Unknown / Custom Format";
        }
    };

    InstFormat const fmt = get_format(ctx.opcode);
    auto const [mnemonic, behavior_desc] = simrv::util::get_operation_details(ctx.op_id);

    std::string assembly = std::string(mnemonic);
    auto get_reg_name = [&](RegId reg, bool is_fp) -> std::string {
        uint32_t r = std::to_underlying(reg);
        if (r >= 32) return std::format("r{}", r);
        return is_fp ? kFpRegNames.at(r) : kRegNames.at(r);
    };

    bool const is_dst_fp = (ctx.opcode == ::Opcode::LoadFp) ||
                          (ctx.opcode == ::Opcode::OpFp && ctx.op_id != FCVT_W_S && ctx.op_id != FCVT_WU_S && ctx.op_id != FEQ_S && ctx.op_id != FLT_S && ctx.op_id != FLE_S && ctx.op_id != FCLASS_S && ctx.op_id != FCVT_W_D && ctx.op_id != FCVT_WU_D && ctx.op_id != FEQ_D && ctx.op_id != FLT_D && ctx.op_id != FLE_D && ctx.op_id != FCLASS_D && ctx.op_id != FMV_X_W && ctx.op_id != FMV_X_D);
    bool const is_src_fp = (ctx.opcode == ::Opcode::StoreFp) ||
                          (ctx.opcode == ::Opcode::OpFp);

    std::string rd_name = get_reg_name(ctx.rd, is_dst_fp);
    std::string rs1_name = get_reg_name(ctx.rs1, is_src_fp && ctx.op_id != FCVT_S_W && ctx.op_id != FCVT_S_WU && ctx.op_id != FCVT_D_W && ctx.op_id != FCVT_D_WU && ctx.op_id != FMV_W_X && ctx.op_id != FMV_D_X);
    std::string rs2_name = get_reg_name(ctx.rs2, is_src_fp);

    if (ctx.op_id != UNKNOWN) {
        if (fmt == InstFormat::R) {
            if (ctx.op_id == SFENCE_VMA) {
                assembly = std::format("sfence.vma {}, {}", rs1_name, rs2_name);
            } else if (ctx.op_id >= LR_W && ctx.op_id <= SC_W) {
                if (ctx.op_id == LR_W) {
                    assembly = std::format("lr.w {}, ({})", rd_name, rs1_name);
                } else {
                    assembly = std::format("sc.w {}, {}, ({})", rd_name, rs2_name, rs1_name);
                }
            } else if (ctx.op_id >= AMOSWAP_W && ctx.op_id <= AMOMAXU_W) {
                assembly = std::format("{}.aqrl {}, {}, ({})", mnemonic, rd_name, rs2_name, rs1_name);
            } else if (ctx.op_id == FSQRT_S || ctx.op_id == FSQRT_D ||
                       ctx.op_id == FCLASS_S || ctx.op_id == FCLASS_D || ctx.op_id == FMV_X_W || ctx.op_id == FMV_X_D ||
                       ctx.op_id == FMV_W_X || ctx.op_id == FMV_D_X ||
                       (ctx.op_id >= FCVT_W_S && ctx.op_id <= FCVT_LU_S) ||
                       (ctx.op_id >= FCVT_W_D && ctx.op_id <= FCVT_LU_D) ||
                       (ctx.op_id >= FCVT_S_W && ctx.op_id <= FCVT_S_LU) ||
                       (ctx.op_id >= FCVT_D_W && ctx.op_id <= FCVT_D_LU)) {
                assembly = std::format("{} {}, {}", mnemonic, rd_name, rs1_name);
            } else {
                assembly = std::format("{} {}, {}, {}", mnemonic, rd_name, rs1_name, rs2_name);
            }
        } else if (fmt == InstFormat::I) {
            bool const is_load = (ctx.opcode == ::Opcode::Load) || (ctx.opcode == ::Opcode::LoadFp);
            bool const is_csr = (ctx.op_id >= CSRRW && ctx.op_id <= CSRRCI);
            if (is_load) {
                assembly = std::format("{} {}, {}({})", mnemonic, rd_name, ctx.imm, rs1_name);
            } else if (ctx.op_id == JALR) {
                assembly = std::format("jalr {}, {}({})", rd_name, ctx.imm, rs1_name);
            } else if (is_csr) {
                std::string csr_str = simrv::util::csr_name(ctx.imm & 0xFFF);
                if (ctx.op_id == CSRRWI || ctx.op_id == CSRRSI || ctx.op_id == CSRRCI) {
                    assembly = std::format("{} {}, {}, {}", mnemonic, rd_name, csr_str, std::to_underlying(ctx.rs1));
                } else {
                    assembly = std::format("{} {}, {}, {}", mnemonic, rd_name, csr_str, rs1_name);
                }
            } else if (ctx.op_id == ECALL || ctx.op_id == EBREAK) {
                assembly = mnemonic;
            } else if (ctx.op_id == FENCE) {
                assembly = "fence";
            } else if (ctx.op_id == FENCE_I) {
                assembly = "fence.i";
            } else if (ctx.op_id == SLLI || ctx.op_id == SRLI || ctx.op_id == SRAI ||
                       ctx.op_id == SLLIW || ctx.op_id == SRLIW || ctx.op_id == SRAIW) {
                uint32_t shamt = ctx.imm & 0x3F;
                assembly = std::format("{} {}, {}, {}", mnemonic, rd_name, rs1_name, shamt);
            } else {
                assembly = std::format("{} {}, {}, {}", mnemonic, rd_name, rs1_name, ctx.imm);
            }
        } else if (fmt == InstFormat::S) {
            bool const is_store_fp = (ctx.opcode == ::Opcode::StoreFp);
            assembly = std::format("{} {}, {}({})", mnemonic, get_reg_name(ctx.rs2, is_store_fp), ctx.imm, rs1_name);
        } else if (fmt == InstFormat::B) {
            assembly = std::format("{} {}, {}, {}", mnemonic, rs1_name, rs2_name, ctx.imm);
        } else if (fmt == InstFormat::U) {
            assembly = std::format("{} {}, 0x{:X}", mnemonic, rd_name, static_cast<uint32_t>(ctx.imm));
        } else if (fmt == InstFormat::J) {
            assembly = std::format("jal {}, {}", rd_name, ctx.imm);
        } else if (fmt == InstFormat::R4) {
            uint32_t rs3_val = (ctx.ir_org >> 27) & 0x1F;
            std::string rs3_name = kFpRegNames.at(rs3_val);
            assembly = std::format("{} {}, {}, {}, {}", mnemonic, rd_name, rs1_name, rs2_name, rs3_name);
        }
    } else {
        assembly = "unknown / illegal instruction";
    }

    std::vector<std::string> explain_rows;
    explain_rows.push_back(section_line("Instruction Explainer"));

    std::string sym = machine_.symbols.lookup(st.pc);
    std::string pc_label = sym.empty() ? std::format("0x{:0{}x}", st.pc, simrv::xlen::kXLenHexDigits)
                                       : std::format("0x{:0{}x} <{}>", st.pc, simrv::xlen::kXLenHexDigits, sym);
    explain_rows.push_back(format_to_width(std::format("  {}PC     : {}{}\033[0m", kSakuraText, kSakuraMint, pc_label), width));

    std::string hex_str;
    if (is_compressed) {
        uint32_t raw_16 = ctx.ir_org & 0xFFFF;
        hex_str = std::format("0x{:04X} (compressed) -> 0x{:08X}", raw_16, decompressed_inst);
    } else {
        hex_str = std::format("0x{:08X}", ctx.ir_org);
    }
    explain_rows.push_back(format_to_width(std::format("  {}Hex    : {}{}\033[0m", kSakuraText, kSakuraMint, hex_str), width));

    std::string bin_str;
    if (is_compressed) {
        bin_str = std::format("{:016b}", ctx.ir_org & 0xFFFF);
    } else {
        bin_str = std::format("{:032b}", ctx.ir_org);
    }
    explain_rows.push_back(format_to_width(std::format("  {}Bin    : {}{}\033[0m", kSakuraText, kSakuraVal, bin_str), width));

    explain_rows.push_back(format_to_width(std::format("  {}Asm    : {}{}\033[0m", kSakuraText, kSakuraPeach, assembly), width));
    explain_rows.push_back(format_to_width(std::format("  {}Format : {}{}\033[0m", kSakuraText, kSakuraVal, get_format_name(fmt)), width));
    explain_rows.push_back(format_to_width(std::format("  {}ISA Ext: {}{}\033[0m", kSakuraText, kSakuraMint, simrv::util::get_isa_extension_name(ctx.op_id)), width));
    explain_rows.push_back(format_to_width("", width));

    explain_rows.push_back(section_line("Visual Bit Fields"));
    if (fmt == InstFormat::R) {
        explain_rows.push_back(format_to_width("   31     25 24   20 19   15 14 12 11    7 6       0", width));
        explain_rows.push_back(format_to_width("  +---------+-------+-------+-----+-------+---------+", width));
        explain_rows.push_back(format_to_width("  | funct7  |  rs2  |  rs1  |  f3 |  rd   | opcode  |", width));
        explain_rows.push_back(format_to_width(std::format("  | {:07b} | {:05b} | {:05b} | {:03b} | {:05b} | {:07b} |",
            (ctx.ir_org >> 25) & 0x7F, (ctx.ir_org >> 20) & 0x1F, (ctx.ir_org >> 15) & 0x1F,
            (ctx.ir_org >> 12) & 0x07, (ctx.ir_org >> 7) & 0x1F, ctx.ir_org & 0x7F), width));
        explain_rows.push_back(format_to_width("  +---------+-------+-------+-----+-------+---------+", width));
    } else if (fmt == InstFormat::I) {
        explain_rows.push_back(format_to_width("   31         20  19  15  1412  11   7   6     0", width));
        explain_rows.push_back(format_to_width("  +--------------+-------+-----+-------+---------+", width));
        explain_rows.push_back(format_to_width("  |  immediate   |  rs1  |  f3 |  rd   | opcode  |", width));
        explain_rows.push_back(format_to_width(std::format("  | {:012b} | {:05b} | {:03b} | {:05b} | {:07b} |",
            (ctx.ir_org >> 20) & 0xFFF, (ctx.ir_org >> 15) & 0x1F, (ctx.ir_org >> 12) & 0x07,
            (ctx.ir_org >> 7) & 0x1F, ctx.ir_org & 0x7F), width));
        explain_rows.push_back(format_to_width("  +--------------+-------+-----+-------+---------+", width));
    } else if (fmt == InstFormat::S) {
        explain_rows.push_back(format_to_width("   11     5  24  20  19  15  1412  4   0   6     0", width));
        explain_rows.push_back(format_to_width("  +---------+-------+-------+-----+-------+---------+", width));
        explain_rows.push_back(format_to_width("  | imm115  |  rs2  |  rs1  |  f3 | imm40 | opcode  |", width));
        explain_rows.push_back(format_to_width(std::format("  | {:07b} | {:05b} | {:05b} | {:03b} | {:05b} | {:07b} |",
            (ctx.ir_org >> 25) & 0x7F, (ctx.ir_org >> 20) & 0x1F, (ctx.ir_org >> 15) & 0x1F,
            (ctx.ir_org >> 12) & 0x07, (ctx.ir_org >> 7) & 0x1F, ctx.ir_org & 0x7F), width));
        explain_rows.push_back(format_to_width("  +---------+-------+-------+-----+-------+---------+", width));
    } else if (fmt == InstFormat::B) {
        explain_rows.push_back(format_to_width("   12     5  24  20  19  15  1412  4  11   6     0", width));
        explain_rows.push_back(format_to_width("  +---------+-------+-------+-----+-------+---------+", width));
        explain_rows.push_back(format_to_width("  | imm125  |  rs2  |  rs1  |  f3 | imm411| opcode  |", width));
        explain_rows.push_back(format_to_width(std::format("  | {:07b} | {:05b} | {:05b} | {:03b} | {:05b} | {:07b} |",
            (ctx.ir_org >> 25) & 0x7F, (ctx.ir_org >> 20) & 0x1F, (ctx.ir_org >> 15) & 0x1F,
            (ctx.ir_org >> 12) & 0x07, (ctx.ir_org >> 7) & 0x1F, ctx.ir_org & 0x7F), width));
        explain_rows.push_back(format_to_width("  +---------+-------+-------+-----+-------+---------+", width));
    } else if (fmt == InstFormat::U) {
        explain_rows.push_back(format_to_width("     31                 12 11   7   6     0", width));
        explain_rows.push_back(format_to_width("  +--------------------------+-------+---------+", width));
        explain_rows.push_back(format_to_width("  |        immediate         |  rd   | opcode  |", width));
        explain_rows.push_back(format_to_width(std::format("  |   {:020b}   | {:05b} | {:07b} |",
            (ctx.ir_org >> 12) & 0xFFFFF, (ctx.ir_org >> 7) & 0x1F, ctx.ir_org & 0x7F), width));
        explain_rows.push_back(format_to_width("  +--------------------------+-------+---------+", width));
    } else if (fmt == InstFormat::J) {
        explain_rows.push_back(format_to_width("     31                  1 11   7   6     0", width));
        explain_rows.push_back(format_to_width("  +--------------------------+-------+---------+", width));
        explain_rows.push_back(format_to_width("  |        immediate         |  rd   | opcode  |", width));
        explain_rows.push_back(format_to_width(std::format("  |   {:020b}   | {:05b} | {:07b} |",
            (ctx.ir_org >> 12) & 0xFFFFF, (ctx.ir_org >> 7) & 0x1F, ctx.ir_org & 0x7F), width));
        explain_rows.push_back(format_to_width("  +--------------------------+-------+---------+", width));
    } else if (fmt == InstFormat::R4) {
        explain_rows.push_back(format_to_width("   31  27 2625      24  20  19  15  1412  11   7   6     0", width));
        explain_rows.push_back(format_to_width("  +-------+----+----+-------+-------+-----+-------+---------+", width));
        explain_rows.push_back(format_to_width("  |  rs3  |fmt | .. |  rs2  |  rs1  |  f3 |  rd   | opcode  |", width));
        explain_rows.push_back(format_to_width(std::format("  | {:05b} | {:02b} | 00 | {:05b} | {:05b} | {:03b} | {:05b} | {:07b} |",
            (ctx.ir_org >> 27) & 0x1F, (ctx.ir_org >> 25) & 0x03, (ctx.ir_org >> 20) & 0x1F, (ctx.ir_org >> 15) & 0x1F,
            (ctx.ir_org >> 12) & 0x07, (ctx.ir_org >> 7) & 0x1F, ctx.ir_org & 0x7F), width));
        explain_rows.push_back(format_to_width("  +-------+----+----+-------+-------+-----+-------+---------+", width));
    } else {
        explain_rows.push_back(format_to_width("  (Unknown instruction format layout)", width));
    }
    explain_rows.push_back(format_to_width("", width));

    explain_rows.push_back(section_line("Field Decoded Values"));

    auto read_reg_str = [&](RegId reg, bool is_fp) -> std::string {
        uint32_t r = std::to_underlying(reg);
        if (r == 0 && !is_fp) return "0x00000000";
        Register val = is_fp ? st.regs.read_fp(reg) : st.regs.read(reg);
        return std::format("0x{:0{}x}", val, simrv::xlen::kXLenHexDigits);
    };

    explain_rows.push_back(format_to_width(std::format("  opcode : 0x{:02X} ({:07b})", std::to_underlying(ctx.opcode), std::to_underlying(ctx.opcode)), width));
    
    if (fmt == InstFormat::R || fmt == InstFormat::I || fmt == InstFormat::U || fmt == InstFormat::J || fmt == InstFormat::R4) {
        explain_rows.push_back(format_to_width(std::format("  rd     : {} = {}", rd_name, read_reg_str(ctx.rd, is_dst_fp)), width));
    }
    if (fmt == InstFormat::R || fmt == InstFormat::I || fmt == InstFormat::S || fmt == InstFormat::B || fmt == InstFormat::R4) {
        explain_rows.push_back(format_to_width(std::format("  rs1    : {} = {}", rs1_name, read_reg_str(ctx.rs1, is_src_fp && ctx.op_id != FCVT_S_W && ctx.op_id != FCVT_S_WU && ctx.op_id != FCVT_D_W && ctx.op_id != FCVT_D_WU && ctx.op_id != FMV_W_X && ctx.op_id != FMV_D_X)), width));
    }
    if (fmt == InstFormat::R || fmt == InstFormat::S || fmt == InstFormat::B || fmt == InstFormat::R4) {
        explain_rows.push_back(format_to_width(std::format("  rs2    : {} = {}", rs2_name, read_reg_str(ctx.rs2, is_src_fp)), width));
    }
    if (fmt == InstFormat::R4) {
        uint32_t rs3_val = (ctx.ir_org >> 27) & 0x1F;
        std::string rs3_name = kFpRegNames.at(rs3_val);
        explain_rows.push_back(format_to_width(std::format("  rs3    : {} = {}", rs3_name, read_reg_str(static_cast<RegId>(rs3_val), true)), width));
    }
    if (fmt == InstFormat::R || fmt == InstFormat::I || fmt == InstFormat::S || fmt == InstFormat::B || fmt == InstFormat::R4) {
        explain_rows.push_back(format_to_width(std::format("  funct3 : 0x{:X} ({:03b})", std::to_underlying(ctx.funct3), std::to_underlying(ctx.funct3)), width));
    }
    if (fmt == InstFormat::R || fmt == InstFormat::R4) {
        explain_rows.push_back(format_to_width(std::format("  funct7 : 0x{:02X} ({:07b})", (ctx.ir_org >> 25) & 0x7F, (ctx.ir_org >> 25) & 0x7F), width));
    }
    if (fmt == InstFormat::I || fmt == InstFormat::S || fmt == InstFormat::B || fmt == InstFormat::U || fmt == InstFormat::J) {
        explain_rows.push_back(format_to_width(std::format("  imm    : {} (0x{:X})", ctx.imm, static_cast<uint32_t>(ctx.imm)), width));
    }
    if (fmt == InstFormat::B || fmt == InstFormat::J) {
        explain_rows.push_back(format_to_width(std::format("  target : 0x{:0{}x}", st.pc + ctx.imm, simrv::xlen::kXLenHexDigits), width));
    }
    explain_rows.push_back(format_to_width("", width));

    bool const is_csr = (ctx.op_id >= CSRRW && ctx.op_id <= CSRRCI);
    if (is_csr) {
        uint32_t csr_addr = ctx.imm & 0xFFF;
        std::string csr_nm = simrv::util::csr_name(csr_addr);
        explain_rows.push_back(format_to_width(std::format("  CSR    : {} (addr 0x{:03X})", csr_nm, csr_addr), width));
        explain_rows.push_back(format_to_width("", width));
    }

    explain_rows.push_back(section_line("Instruction Description"));
    
    auto wrap_text = [](const std::string& text, int max_len) -> std::vector<std::string> {
        std::vector<std::string> lines;
        std::string current_line;
        std::string word;
        for (char c : text) {
            if (c == ' ') {
                if (!word.empty()) {
                    if (current_line.length() + word.length() + 1 > static_cast<std::size_t>(max_len)) {
                        lines.push_back(current_line);
                        current_line = word;
                    } else {
                        if (!current_line.empty()) {
                            current_line += " ";
                        }
                        current_line += word;
                    }
                    word.clear();
                }
            } else {
                word += c;
            }
        }
        if (!word.empty()) {
            if (current_line.length() + word.length() + 1 > static_cast<std::size_t>(max_len)) {
                lines.push_back(current_line);
                current_line = word;
            } else {
                if (!current_line.empty()) {
                    current_line += " ";
                }
                current_line += word;
            }
        }
        if (!current_line.empty()) {
            lines.push_back(current_line);
        }
        return lines;
    };

    std::string isa_ext = std::string(simrv::util::get_isa_extension_name(ctx.op_id));
    std::string full_desc = std::format("[ISA: {}] {}", isa_ext, behavior_desc);
    auto wrapped = wrap_text(full_desc, width - 4);
    for (const auto& line : wrapped) {
        explain_rows.push_back(format_to_width("  " + line, width));
    }
    explain_rows.push_back(section_line("End Explainer"));

    return explain_rows;
}

void RegisterPane::scroll(int lines) {
    int total_logical_rows = 0;
    if (page_ == TuiRegPage::EXPLAIN) {
        int w = last_width_ > 0 ? last_width_ : 60;
        total_logical_rows = static_cast<int>(get_explain_rows(w).size());
    } else {
        bool const is_reg_page = (page_ == TuiRegPage::GPR || page_ == TuiRegPage::FPR || page_ == TuiRegPage::VEC);
        bool const single_column = is_reg_page && ([&]() {
            int w = last_width_ > 0 ? last_width_ : 60;
            if (page_ == TuiRegPage::GPR) {
                return (simrv::xlen::kIsXLen64 && w < 58) || (!simrv::xlen::kIsXLen64 && w < 42);
            }
            return w < 58;
        }());
        int base_rows = machine_.s_cycle_accurate ? 43 : 35;
        if (single_column) {
            base_rows += 16;
        }
        int debug_rows = machine_.s_debug_mode ? 4 : 0;
        total_logical_rows = base_rows + debug_rows;
    }
    int max_scroll = std::max(0, total_logical_rows - visible_rows_);
    scroll_offset_ += lines;
    if (scroll_offset_ > max_scroll) {
        scroll_offset_ = max_scroll;
    }
    if (scroll_offset_ < 0) {
        scroll_offset_ = 0;
    }
}

}  // namespace simrv::tui
