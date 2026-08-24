/**
 * @file LeftPaneRegs.cpp
 * @brief GPR, FPR, and vector register rendering for the TUI register panel.
 */
#include <algorithm>
#include <array>
#include <format>
#include <string>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/RegisterFile.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/panels/LeftPane.hpp"

namespace simrv::tui {

namespace {

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

auto LeftPane::render_registers_single_column(const simrv::core::ArchState& st, int logical_row,
                                              int width) -> std::string {
    if (logical_row >= 0 && logical_row < 32) {
        int reg = logical_row;
        switch (page_) {
            case TuiRegPage::GPR: {
                auto val = st.regs.read(static_cast<RegId>(reg));
                std::string name = kRegNames.at(static_cast<std::size_t>(reg));
                bool changed = paused_ && (cached_gpr_.at(static_cast<std::size_t>(reg)) != val);
                std::string c = changed ? kThemePeach : kThemeMint;
                std::string col_color =
                    std::format(" {}x{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:0{}x}\033[0m", kThemeText,
                                reg, kThemeVal, name, c, val, simrv::xlen::kXLenHexDigits);
                return format_to_width(col_color, width);
            }
            case TuiRegPage::FPR: {
                auto val = st.regs.read_fp(static_cast<RegId>(reg));
                std::string name = kFpRegNames.at(static_cast<std::size_t>(reg));
                bool changed = paused_ && (cached_fpr_.at(static_cast<std::size_t>(reg)) != val);
                std::string c = changed ? kThemePeach : kThemeMint;
                std::string col_color =
                    std::format(" {}f{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:016x}\033[0m", kThemeText,
                                reg, kThemeVal, name, c, val);
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

auto LeftPane::render_registers_double_column(const simrv::core::ArchState& st, int logical_row,
                                              int col_width, int right_width) -> std::string {
    if (logical_row >= 0 && logical_row < 16) {
        int reg1 = logical_row;
        int reg2 = logical_row + 16;

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

                std::string col1_color =
                    std::format(" {}x{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:0{}x}\033[0m", kThemeText,
                                reg1, kThemeVal, name1, c1, val1, simrv::xlen::kXLenHexDigits);
                std::string col2_color =
                    std::format(" {}x{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:0{}x}\033[0m", kThemeText,
                                reg2, kThemeVal, name2, c2, val2, simrv::xlen::kXLenHexDigits);

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

                std::string col1_color =
                    std::format(" {}f{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:016x}\033[0m", kThemeText,
                                reg1, kThemeVal, name1, c1, val1);
                std::string col2_color =
                    std::format(" {}f{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:016x}\033[0m", kThemeText,
                                reg2, kThemeVal, name2, c2, val2);

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

auto LeftPane::render_registers_or_pipeline(const simrv::core::CPU& cpu,
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
        }
    }
    return "";
}

auto LeftPane::get_register_value_at_row(int logical_row, int col_x, int pane_width) const
    -> std::optional<Register> {
    const auto& st = current_cpu().state();
    bool single_col = is_single_column(pane_width);
    int reg = -1;
    if (single_col) {
        if (logical_row >= 0 && logical_row < 32) reg = logical_row;
    } else {
        if (logical_row >= 0 && logical_row < 16) {
            reg = (col_x < pane_width / 2) ? logical_row : (logical_row + 16);
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
