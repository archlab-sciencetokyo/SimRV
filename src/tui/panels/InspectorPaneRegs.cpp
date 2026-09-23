/**
 * @file InspectorPaneRegs.cpp
 * @brief GPR, FPR, and vector register rendering for the TUI register panel.
 */
#include <algorithm>
#include <array>
#include <format>
#include <string>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/RegisterFile.hpp"
#include "simrv/pipeline/OperationInfo.hpp"
#include "simrv/pipeline/Scoreboard.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/panels/InspectorPane.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::tui {

namespace {

auto get_in_flight_badge(const simrv::pipeline::Scoreboard& sb,
                         simrv::pipeline::operation::RegBank bank, RegId reg) -> std::string {
    auto entry = sb.get_entry_data(bank, reg);
    if (!entry) return "";
    if (entry->can_forward) {
        return " \033[38;5;120m[FWD]\033[0m";
    }
    const char* stage_str = "EX";
    switch (entry->stage) {
        case simrv::pipeline::PipelineStage::Fetch:
            stage_str = "IF";
            break;
        case simrv::pipeline::PipelineStage::Decode:
            stage_str = "ID";
            break;
        case simrv::pipeline::PipelineStage::Execute:
            stage_str = "EX";
            break;
        case simrv::pipeline::PipelineStage::Memory:
            stage_str = "MEM";
            break;
        case simrv::pipeline::PipelineStage::Writeback:
            stage_str = "WB";
            break;
        case simrv::pipeline::PipelineStage::Commit:
            stage_str = "CM";
            break;
    }
    if (entry->latency > 0) {
        return std::format(" \033[38;5;203m[{} {}c]\033[0m", stage_str, entry->latency);
    }
    return std::format(" \033[38;5;203m[{}]\033[0m", stage_str);
}

auto format_vec_value(const simrv::core::VectorRegister& val, unsigned vlen, int avail_w)
    -> std::string {
    unsigned num_words = vlen / 64;
    if (num_words == 0) num_words = 1;
    if (num_words > kVlenMaxBytes / 8) num_words = kVlenMaxBytes / 8;

    std::string full_hex = "0x";
    for (int w = static_cast<int>(num_words) - 1; w >= 0; --w) {
        full_hex += std::format("{:016x}", val.u64[static_cast<std::size_t>(w)]);
        if (w > 0) full_hex += "_";
    }

    if (static_cast<int>(full_hex.length()) <= avail_w) {
        return full_hex;
    }

    if (num_words > 1) {
        std::string hi = std::format("{:016x}", val.u64[static_cast<std::size_t>(num_words - 1)]);
        std::string lo = std::format("{:016x}", val.u64[0]);
        std::string abbrev = std::format("0x{}..{}", hi.substr(0, 8), lo.substr(8));
        if (static_cast<int>(abbrev.length()) <= avail_w) {
            return abbrev;
        }
    }
    return full_hex.substr(0, static_cast<std::size_t>(std::max(4, avail_w)));
}

auto vec_reg_changed(bool paused, const simrv::core::VectorRegister& cached,
                     const simrv::core::VectorRegister& val, unsigned vlen) -> bool {
    if (!paused) return false;
    unsigned num_words = vlen / 64;
    if (num_words == 0) num_words = 1;
    if (num_words > kVlenMaxBytes / 8) num_words = kVlenMaxBytes / 8;
    for (unsigned w = 0; w < num_words; ++w) {
        if (cached.u64[w] != val.u64[w]) {
            return true;
        }
    }
    return false;
}

}  // namespace

auto InspectorPane::render_registers_single_column(const simrv::core::ArchState& st,
                                                   int logical_row, int width) -> std::string {
    if (logical_row >= 0 && logical_row < 32) {
        int reg = logical_row;
        const auto& sb = projected_scoreboard_;
        switch (page_) {
            case TuiRegPage::GPR: {
                auto val = st.regs.read(static_cast<RegId>(reg));
                std::string name = kRegNames.at(static_cast<std::size_t>(reg));
                bool changed = paused_ && (cached_gpr_.at(static_cast<std::size_t>(reg)) != val);
                std::string c = changed ? kThemePeach : kThemeMint;
                std::string badge = get_in_flight_badge(
                    sb, simrv::pipeline::operation::RegBank::Integer, static_cast<RegId>(reg));
                std::string col_color =
                    std::format(" {}x{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:0{}x}\033[0m{}", kThemeText,
                                reg, kThemeVal, name, c, val, simrv::xlen::kXLenHexDigits, badge);
                return format_to_width(col_color, width);
            }
            case TuiRegPage::FPR: {
                auto val = st.regs.read_fp(static_cast<RegId>(reg));
                std::string name = kFpRegNames.at(static_cast<std::size_t>(reg));
                bool changed = paused_ && (cached_fpr_.at(static_cast<std::size_t>(reg)) != val);
                std::string c = changed ? kThemePeach : kThemeMint;
                std::string badge = get_in_flight_badge(
                    sb, simrv::pipeline::operation::RegBank::Float, static_cast<RegId>(reg));
                std::string col_color =
                    std::format(" {}f{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:016x}\033[0m{}", kThemeText,
                                reg, kThemeVal, name, c, val, badge);
                return format_to_width(col_color, width);
            }
            case TuiRegPage::VEC: {
                auto val = st.regs.read_vector(static_cast<RegId>(reg));
                bool changed = vec_reg_changed(
                    paused_, cached_vec_.at(static_cast<std::size_t>(reg)), val, st.regs.vlen);
                std::string c = changed ? kThemePeach : kThemeMint;
                std::string val_str = format_vec_value(val, st.regs.vlen, std::max(10, width - 6));
                std::string col_color =
                    std::format(" {}v{:<2}\033[0m: {}{}\033[0m", kThemeText, reg, c, val_str);
                return format_to_width(col_color, width);
            }
            default:
                break;
        }
    }
    return format_to_width("", width);
}

auto InspectorPane::render_registers_double_column(const simrv::core::ArchState& st,
                                                   int logical_row, int col_width, int right_width)
    -> std::string {
    if (logical_row >= 0 && logical_row < 16) {
        int reg1 = logical_row;
        int reg2 = logical_row + 16;

        const auto& sb = projected_scoreboard_;
        switch (page_) {
            case TuiRegPage::GPR: {
                auto val1 = st.regs.read(static_cast<RegId>(reg1));
                auto val2 = st.regs.read(static_cast<RegId>(reg2));

                std::string name1 = kRegNames.at(static_cast<std::size_t>(reg1));
                std::string name2 = kRegNames.at(static_cast<std::size_t>(reg2));

                bool changed = paused_ && (cached_gpr_.at(static_cast<std::size_t>(reg1)) != val1);
                std::string c1 = changed ? kThemePeach : kThemeMint;
                bool changed2 = paused_ && (cached_gpr_.at(static_cast<std::size_t>(reg2)) != val2);
                std::string c2 = changed2 ? kThemePeach : kThemeMint;

                std::string badge1 =
                    (col_width >= 36)
                        ? get_in_flight_badge(sb, simrv::pipeline::operation::RegBank::Integer,
                                              static_cast<RegId>(reg1))
                        : "";
                std::string badge2 =
                    (right_width >= 36)
                        ? get_in_flight_badge(sb, simrv::pipeline::operation::RegBank::Integer,
                                              static_cast<RegId>(reg2))
                        : "";

                std::string col1_color = std::format(
                    " {}x{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:0{}x}\033[0m{}", kThemeText, reg1,
                    kThemeVal, name1, c1, val1, simrv::xlen::kXLenHexDigits, badge1);
                std::string col2_color = std::format(
                    " {}x{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:0{}x}\033[0m{}", kThemeText, reg2,
                    kThemeVal, name2, c2, val2, simrv::xlen::kXLenHexDigits, badge2);

                return format_to_width(col1_color, col_width) +
                       format_to_width(col2_color, right_width);
            }
            case TuiRegPage::FPR: {
                auto val1 = st.regs.read_fp(static_cast<RegId>(reg1));
                auto val2 = st.regs.read_fp(static_cast<RegId>(reg2));

                std::string name1 = kFpRegNames.at(static_cast<std::size_t>(reg1));
                std::string name2 = kFpRegNames.at(static_cast<std::size_t>(reg2));

                bool changed = paused_ && (cached_fpr_.at(static_cast<std::size_t>(reg1)) != val1);
                std::string c1 = changed ? kThemePeach : kThemeMint;
                bool changed2 = paused_ && (cached_fpr_.at(static_cast<std::size_t>(reg2)) != val2);
                std::string c2 = changed2 ? kThemePeach : kThemeMint;

                std::string badge1 =
                    (col_width >= 36)
                        ? get_in_flight_badge(sb, simrv::pipeline::operation::RegBank::Float,
                                              static_cast<RegId>(reg1))
                        : "";
                std::string badge2 =
                    (right_width >= 36)
                        ? get_in_flight_badge(sb, simrv::pipeline::operation::RegBank::Float,
                                              static_cast<RegId>(reg2))
                        : "";

                std::string col1_color =
                    std::format(" {}f{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:016x}\033[0m{}", kThemeText,
                                reg1, kThemeVal, name1, c1, val1, badge1);
                std::string col2_color =
                    std::format(" {}f{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:016x}\033[0m{}", kThemeText,
                                reg2, kThemeVal, name2, c2, val2, badge2);

                return format_to_width(col1_color, col_width) +
                       format_to_width(col2_color, right_width);
            }
            case TuiRegPage::VEC: {
                auto val1 = st.regs.read_vector(static_cast<RegId>(reg1));
                auto val2 = st.regs.read_vector(static_cast<RegId>(reg2));

                bool changed1 = vec_reg_changed(
                    paused_, cached_vec_.at(static_cast<std::size_t>(reg1)), val1, st.regs.vlen);
                bool changed2 = vec_reg_changed(
                    paused_, cached_vec_.at(static_cast<std::size_t>(reg2)), val2, st.regs.vlen);

                std::string c1 = changed1 ? kThemePeach : kThemeMint;
                std::string c2 = changed2 ? kThemePeach : kThemeMint;

                std::string val1_str =
                    format_vec_value(val1, st.regs.vlen, std::max(10, col_width - 8));
                std::string val2_str =
                    format_vec_value(val2, st.regs.vlen, std::max(10, right_width - 8));

                std::string col1_color =
                    std::format(" {}v{:<2}\033[0m: {}{}\033[0m", kThemeText, reg1, c1, val1_str);
                std::string col2_color =
                    std::format(" {}v{:<2}\033[0m: {}{}\033[0m", kThemeText, reg2, c2, val2_str);

                return format_to_width(col1_color, col_width) +
                       format_to_width(col2_color, right_width);
            }
            default:
                break;
        }
    }
    return format_to_width("", col_width + right_width);
}

auto InspectorPane::render_registers_or_pipeline(const simrv::core::CPU& cpu,
                                                 const simrv::core::ArchState& st, int logical_row,
                                                 int col_width, int right_width, int width,
                                                 bool single_column) -> std::string {
    if (single_column) {
        if (logical_row >= 0 && logical_row < 32) {
            return render_registers_single_column(st, logical_row, width);
        }
    } else {
        if (logical_row >= 0 && logical_row < 16) {
            switch (page_) {
                case TuiRegPage::GPR:
                case TuiRegPage::FPR:
                case TuiRegPage::VEC:
                    return render_registers_double_column(st, logical_row, col_width, right_width);
                case TuiRegPage::PIPELINE:
                    return render_pipeline_stages(cpu, logical_row, col_width, right_width);
                case TuiRegPage::CACHE:
                    return render_cache_stats(cpu, logical_row, col_width, right_width);
                case TuiRegPage::TLB:
                    return render_tlb_stats(cpu, logical_row, col_width, right_width);
                case TuiRegPage::BPRED:
                    return render_bp_stats(cpu, logical_row, col_width, right_width);
                case TuiRegPage::HAZARD:
                    return render_hazard_stats(cpu, logical_row, col_width, right_width);
                case TuiRegPage::BUS:
                    return render_io_stats(cpu, logical_row, col_width, right_width);
                case TuiRegPage::STACK:
                    return render_stack_frame(cpu, logical_row, col_width, right_width);
                default:
                    break;
            }
        } else if (page_ == TuiRegPage::VEC && logical_row >= 16 && logical_row < 21) {
            if (logical_row == 16) {
                return section_line("Vector Control & Status", width);
            }
            ::VtypeView const vt{st.vtype, static_cast<uint8_t>(st.regs.xlen)};
            if (logical_row == 17) {
                std::string const sew_str = vt.vill() ? "ill" : std::format("e{}", vt.sew_bits());
                std::string const lmul_str = vt.vill() ? "ill" : [&]() {
                    switch (vt.vlmul()) {
                        case ::Vlmul::LMUL_1:
                            return "m1";
                        case ::Vlmul::LMUL_2:
                            return "m2";
                        case ::Vlmul::LMUL_4:
                            return "m4";
                        case ::Vlmul::LMUL_8:
                            return "m8";
                        case ::Vlmul::LMUL_F2:
                            return "mf2";
                        case ::Vlmul::LMUL_F4:
                            return "mf4";
                        case ::Vlmul::LMUL_F8:
                            return "mf8";
                        default:
                            return "res";
                    }
                }();
                std::string const vtype_summary =
                    vt.vill() ? "vill"
                              : std::format("0x{:x} ({}, {})", st.vtype, sew_str, lmul_str);
                return render_pair("vl", std::format("{}", st.vl), kThemeMint, "vtype",
                                   vtype_summary, vt.vill() ? kThemePeach : kThemeSky, col_width,
                                   right_width, 6);
            }
            if (logical_row == 18) {
                return render_pair("vstart", std::format("{}", st.vstart), kThemeMint, "vlenb",
                                   std::format("{} ({}b)", st.regs.vlen_bytes(), st.regs.vlen),
                                   kThemeSky, col_width, right_width, 6);
            }
            if (logical_row == 19) {
                std::string const vxrm_str = [&]() {
                    switch (st.vxrm & 3) {
                        case 0:
                            return "0 (rnu)";
                        case 1:
                            return "1 (rne)";
                        case 2:
                            return "2 (rdn)";
                        case 3:
                            return "3 (rod)";
                        default:
                            return "0";
                    }
                }();
                return render_pair(
                    "vxrm", vxrm_str, kThemeMint, "vxsat", std::format("{}", st.vxsat),
                    (st.vxsat != 0) ? kThemePeach : kThemeSky, col_width, right_width, 6);
            }
            if (logical_row == 20) {
                std::string const vta_str = (vt.vta() == ::Vta::Agnostic) ? "ta" : "tu";
                std::string const vma_str = (vt.vma() == ::Vma::Agnostic) ? "ma" : "mu";
                return render_pair("vta/vma", std::format("{}/{}", vta_str, vma_str), kThemeMint,
                                   "vill", vt.vill() ? "1 (illegal)" : "0 (ok)",
                                   vt.vill() ? kThemePeach : kThemeSky, col_width, right_width, 6);
            }
        }
    }
    return "";
}

auto InspectorPane::get_register_value_at_row(int logical_row, int col_x, int pane_width) const
    -> std::optional<Register> {
    const auto& st = current_cpu().state();
    bool single_col = is_single_column(pane_width);
    int reg = -1;
    if (single_col) {
        if (logical_row >= 0 && logical_row < 32) reg = logical_row;
    } else {
        if (logical_row >= 0 && logical_row < 16) {
            int const total_cols = content_total_columns(pane_width);
            int const render_width = std::max(pane_width, total_cols);
            int const col_width = render_width / 2;
            const auto& sv = current_scroll_view();
            int content_x = col_x;
            if (sv.offset_x() > 0 || sv.can_scroll_right()) {
                content_x = col_x + sv.offset_x() - 1;
            }
            reg = (content_x < col_width) ? logical_row : (logical_row + 16);
        }
    }

    if (reg >= 0 && reg < 32) {
        if (page_ == TuiRegPage::GPR) {
            return st.regs.read(static_cast<RegId>(reg));
        }
        if (page_ == TuiRegPage::FPR) {
            return static_cast<Register>(st.regs.read_fp(static_cast<RegId>(reg)));
        }
        if (page_ == TuiRegPage::VEC) {
            return static_cast<Register>(st.regs.read_vector(static_cast<RegId>(reg)).u64[0]);
        }
    }
    return std::nullopt;
}

}  // namespace simrv::tui
