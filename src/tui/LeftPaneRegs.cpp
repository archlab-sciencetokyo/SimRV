/**
 * @file RegisterPaneRegs.cpp
 * @brief GPR, FPR, and vector register rendering for the TUI register panel.
 */
#include "simrv/tui/LeftPane.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include <format>
#include <string>
#include <array>


namespace simrv::tui {

auto LeftPane::render_registers_single_column(const simrv::core::ArchState& st, int logical_row, int width) -> std::string {
    if (logical_row >= 0 && logical_row < 32) {
        int reg = logical_row;
        switch (page_) {
            case TuiRegPage::GPR: {
                auto val = st.regs.read(static_cast<RegId>(reg));
                std::string name = kRegNames.at(static_cast<std::size_t>(reg));
                bool changed = paused_ && (cached_gpr_.at(static_cast<std::size_t>(reg)) != val);
                std::string c = changed ? kThemePeach : kThemeMint;
                std::string col_color = std::format(" {}x{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:0{}x}\033[0m",
                                                    kThemeText, reg, kThemeVal, name, c, val, simrv::xlen::kXLenHexDigits);
                return format_to_width(col_color, width);
            }
            case TuiRegPage::FPR: {
                auto val = st.regs.read_fp(static_cast<RegId>(reg));
                std::string name = kFpRegNames.at(static_cast<std::size_t>(reg));
                bool changed = paused_ && (cached_fpr_.at(static_cast<std::size_t>(reg)) != val);
                std::string c = changed ? kThemePeach : kThemeMint;
                std::string col_color = std::format(" {}f{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:016x}\033[0m",
                                                    kThemeText, reg, kThemeVal, name, c, val);
                return format_to_width(col_color, width);
            }
            case TuiRegPage::VEC: {
                auto val = st.regs.read_vector(static_cast<RegId>(reg));
                bool changed = paused_ && (cached_vec_.at(static_cast<std::size_t>(reg)) != val.u64[0]); // NOLINT(cppcoreguidelines-pro-type-union-access)
                std::string c = changed ? kThemePeach : kThemeMint;
                std::string col_color = std::format(" {}v{:<2}\033[0m       : {}0x{:016x}\033[0m",
                                                    kThemeText, reg, c, val.u64[0]); // NOLINT(cppcoreguidelines-pro-type-union-access)
                return format_to_width(col_color, width);
            }
            default:
                break;
        }
    }
    return format_to_width("", width);
}

auto LeftPane::render_registers_double_column(const simrv::core::ArchState& st, int logical_row, int col_width, int right_width) -> std::string {
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
                    std::format(" {}x{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:0{}x}\033[0m",
                                kThemeText, reg1, kThemeVal, name1, c1, val1, simrv::xlen::kXLenHexDigits);
                std::string col2_color =
                    std::format(" {}x{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:0{}x}\033[0m",
                                kThemeText, reg2, kThemeVal, name2, c2, val2, simrv::xlen::kXLenHexDigits);

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

                std::string col1_color = std::format(
                    " {}f{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:016x}\033[0m", kThemeText, reg1,
                    kThemeVal, name1, c1, val1);
                std::string col2_color = std::format(
                    " {}f{:<2}\033[0m/{}{:<5}\033[0m: {}0x{:016x}\033[0m", kThemeText, reg2,
                    kThemeVal, name2, c2, val2);

                return format_to_width(col1_color, col_width) +
                       format_to_width(col2_color, right_width);
            }
            case TuiRegPage::VEC: {
                auto val1 = st.regs.read_vector(static_cast<RegId>(reg1));
                auto val2 = st.regs.read_vector(static_cast<RegId>(reg2));

                bool changed1 = paused_ && (cached_vec_.at(static_cast<std::size_t>(reg1)) != val1.u64[0]); // NOLINT(cppcoreguidelines-pro-type-union-access)
                bool changed2 = paused_ && (cached_vec_.at(static_cast<std::size_t>(reg2)) != val2.u64[0]); // NOLINT(cppcoreguidelines-pro-type-union-access)

                std::string c1 = changed1 ? kThemePeach : kThemeMint;
                std::string c2 = changed2 ? kThemePeach : kThemeMint;

                std::string col1_color = std::format(
                    " {}v{:<2}\033[0m       : {}0x{:016x}\033[0m", kThemeText, reg1, c1, val1.u64[0]); // NOLINT(cppcoreguidelines-pro-type-union-access)
                std::string col2_color = std::format(
                    " {}v{:<2}\033[0m       : {}0x{:016x}\033[0m", kThemeText, reg2, c2, val2.u64[0]); // NOLINT(cppcoreguidelines-pro-type-union-access)

                return format_to_width(col1_color, col_width) +
                       format_to_width(col2_color, right_width);
            }
            default:
                break;
        }
    }
    return format_to_width("", col_width + right_width);
}

auto LeftPane::render_registers_or_pipeline(const simrv::core::CPU& cpu, const simrv::core::ArchState& st, int logical_row, int col_width, int right_width, int width, bool single_column) -> std::string {
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
                case TuiRegPage::STACK:
                    return render_stack_frame(cpu, logical_row, col_width, right_width);
                default:
                    break;
            }
        }
    }
    return "";
}

auto LeftPane::get_register_value_at_row(int logical_row, int col_x, int pane_width) const -> std::optional<Register> {
    const auto& st = machine_.cpu.state();
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
    }
    return std::nullopt;
}

}  // namespace simrv::tui
