#include "simrv/tui/TuiComponents.hpp"
#include "simrv/util/FormatUtil.hpp"


#include <sys/ioctl.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <print>
#include <string>

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/xlen/Helpers.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::tui {

namespace {

auto make_repeated_string(const std::string& pattern, int count) -> std::string {
    std::string s;
    for (int i = 0; i < count; ++i) {
        s += pattern;
    }
    return s;
}

auto get_display_width(const std::string& s) -> int {
    int len = 0;
    bool in_esc = false;
    bool in_csi = false;
    for (std::size_t i = 0; i < s.length(); ++i) {
        if (s.at(i) == '\033') {
            in_esc = true;
            in_csi = false;
        } else if (in_esc) {
            if (s.at(i) == '[') {
                in_csi = true;
            } else if (in_csi) {
                if (s.at(i) >= 0x40 && s.at(i) <= 0x7E) {
                    in_esc = false;
                    in_csi = false;
                }
            } else {
                if (s.at(i) == '(' || s.at(i) == ')') {
                    // keep in_esc
                } else {
                    in_esc = false;
                }
            }
        } else {
            const auto c = static_cast<unsigned char>(s.at(i));
            if (c < 0x80 || c >= 0xC0) {
                len++;
            }
        }
    }
    return len;
}

auto format_to_width(const std::string& colored_str, int target_width) -> std::string {
    int current_width = 0;
    std::string result;
    bool in_esc = false;
    bool in_csi = false;
    bool skipping = false;

    for (std::size_t i = 0; i < colored_str.length(); ++i) {
        if (colored_str.at(i) == '\033') {
            in_esc = true;
            in_csi = false;
            result += colored_str.at(i);
        } else if (in_esc) {
            result += colored_str.at(i);
            if (colored_str.at(i) == '[') {
                in_csi = true;
            } else if (in_csi) {
                if (colored_str.at(i) >= 0x40 && colored_str.at(i) <= 0x7E) {
                    in_esc = false;
                    in_csi = false;
                }
            } else {
                if (colored_str.at(i) == '(' || colored_str.at(i) == ')') {
                    // keep in_esc
                } else {
                    in_esc = false;
                }
            }
        } else {
            const auto c = static_cast<unsigned char>(colored_str.at(i));
            bool is_lead = (c < 0x80 || c >= 0xC0);
            if (is_lead) {
                if (current_width >= target_width) {
                    skipping = true;
                } else {
                    skipping = false;
                    current_width++;
                }
            }
            if (!skipping) {
                result += colored_str.at(i);
            }
        }
    }

    if (current_width < target_width) {
        result += std::string(static_cast<std::size_t>(target_width - current_width), ' ');
    }

    return result + "\033[0m";
}

static constexpr std::array<const char*, 32> kRegNames = {
    "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0/fp", "s1", "a0",
    "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3",    "s4", "s5",
    "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5",    "t6"};

static constexpr std::array<const char*, 32> kFpRegNames = {
    "ft0", "ft1", "ft2", "ft3", "ft4",  "ft5",  "ft6", "ft7", "fs0",  "fs1", "fa0",
    "fa1", "fa2", "fa3", "fa4", "fa5",  "fa6",  "fa7", "fs2", "fs3",  "fs4", "fs5",
    "fs6", "fs7", "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"};

using simrv::util::format_with_commas;
using simrv::util::format_scaled;

auto make_progress_bar(double ratio, int width, const std::string& color_code) -> std::string {
    int filled = static_cast<int>(ratio * width);
    if (filled < 0) filled = 0;
    if (filled > width) filled = width;
    std::string bar;
    bar += color_code;
    for (int i = 0; i < filled; ++i) {
        bar += "█";
    }
    bar += kSakuraMuted;  // Muted gray/pink
    for (int i = filled; i < width; ++i) {
        bar += "░";
    }
    bar += "\033[0m";
    return bar;
}

auto format_compact(uint64_t val) -> std::string {
    if (val >= 1000000000ULL) {
        return std::format("{:.1f}G", static_cast<double>(val) / 1000000000.0);
    }
    if (val >= 1000000ULL) {
        return std::format("{:.1f}M", static_cast<double>(val) / 1000000.0);
    }
    if (val >= 1000ULL) {
        return std::format("{:.1f}K", static_cast<double>(val) / 1000.0);
    }
    return std::to_string(val);
}

}  // namespace

StatusBar::StatusBar(simrv::core::Machine& machine) : machine_(machine) {}

auto StatusBar::render_row(int row_idx, int width) -> std::string {
    if (row_idx == 0) {
        // Header
        std::string binary_name = machine_.s_fn_memimg;
        auto last_slash = binary_name.find_last_of("/\\");
        if (last_slash != std::string::npos) {
            binary_name = binary_name.substr(last_slash + 1);
        }
        if (binary_name.empty()) {
            binary_name = "application";
        }
        std::string mode_str = machine_.s_appmode ? "Application" : "OS/RTOS";
        std::string status_badge =
            status_override_.empty()
                ? (paused_ ? "\033[48;5;223m\033[38;5;232m PAUSED \033[0m" : "\033[48;5;121m\033[38;5;232m RUNNING \033[0m")
                : status_override_;
        std::string page_badge;
        if (active_page_ == TuiRegPage::GPR) {
            page_badge = "GPR";
        } else if (active_page_ == TuiRegPage::FPR) {
            page_badge = "FPR";
        } else if (active_page_ == TuiRegPage::VEC) {
            page_badge = "VEC";
        } else {
            page_badge = "PIPE";
        }
        std::string left_text = std::format(" SimRV [{}] ({}) | Status: ", binary_name, mode_str);
        std::string left_render =
            left_text + status_badge + std::format(" | Page: {}{}\033[0m", kSakuraSky, page_badge);
        int left_printed_len = get_display_width(left_render);
        int pad_left = (layout_ == TuiLayout::Split ? left_width_ : width - 2) - left_printed_len;
        if (pad_left > 0) {
            left_render += std::string(static_cast<std::size_t>(pad_left), ' ');
        } else {
            left_render =
                format_to_width(left_render, layout_ == TuiLayout::Split ? left_width_ : width - 2);
        }

        std::string right_text;
        int right_text_display_len = 0;
        std::string color_prefix = "";
        std::string color_suffix = "";
        if (scroll_offset_ > 0) {
            color_prefix = "\033[1;5;30;43m";
            color_suffix = "\033[0m";
            right_text = std::format(
                " ═══ SCROLLBACK (Offset: -{}) [Press 'c'/'Enter' to Live] ═══ ", scroll_offset_);
            right_text_display_len = get_display_width(right_text);
        } else {
            const auto cycles = machine_.cpu.clint_mmio.mcycle;
            const auto icount = machine_.cpu.e_icount;
            const double cpi = icount == 0 ? 0.0 : static_cast<double>(cycles) / static_cast<double>(icount);
            
            // Calculate simulation speed in MIPS/KIPS
            const double mips = static_cast<double>(kips_) / 1000.0;
            
            std::string speed_str;
            if (mips >= 1.0) {
                speed_str = std::format("{:.2f} MIPS", mips);
            } else {
                speed_str = std::format("{:.1f} KIPS", mips * 1000.0);
            }

            int target_width = layout_ == TuiLayout::Split ? right_width_ : width - 2;
            
            if (paused_) {
                if (target_width >= 55) {
                    if (machine_.s_cycle_accurate) {
                        right_text = std::format("Cycles: {} | Insns: {} | CPI: {:.2f}",
                                                 format_with_commas(cycles), format_with_commas(icount), cpi);
                    } else {
                        right_text = std::format("Insns: {}",
                                                 format_with_commas(icount));
                    }
                } else {
                    if (machine_.s_cycle_accurate) {
                        right_text = std::format("C: {} | I: {}",
                                                 format_with_commas(cycles), format_with_commas(icount));
                    } else {
                        right_text = std::format("I: {}",
                                                 format_with_commas(icount));
                    }
                }
            } else {
                if (target_width >= 75) {
                    if (machine_.s_cycle_accurate) {
                        right_text = std::format("Cycles: {} | Insns: {} | CPI: {:.2f} | Speed: {}",
                                                 format_scaled(cycles), format_scaled(icount),
                                                 cpi, speed_str);
                    } else {
                        right_text = std::format("Insns: {} | Speed: {}",
                                                 format_scaled(icount), speed_str);
                    }
                } else if (target_width >= 55) {
                    if (machine_.s_cycle_accurate) {
                        right_text = std::format("Cycles: {} | Insns: {} | Speed: {}",
                                                 format_scaled(cycles), format_scaled(icount), speed_str);
                    } else {
                        right_text = std::format("Insns: {} | Speed: {}",
                                                 format_scaled(icount), speed_str);
                    }
                } else if (target_width >= 40) {
                    if (machine_.s_cycle_accurate) {
                        right_text = std::format("C: {} | I: {} | Speed: {}",
                                                 format_scaled(cycles), format_scaled(icount), speed_str);
                    } else {
                        right_text = std::format("I: {} | Speed: {}",
                                                 format_scaled(icount), speed_str);
                    }
                } else {
                    if (machine_.s_cycle_accurate) {
                        right_text = std::format("C: {} | I: {}",
                                                 format_scaled(cycles), format_scaled(icount));
                    } else {
                        right_text = std::format("I: {}",
                                                 format_scaled(icount));
                    }
                }
            }
            right_text_display_len = get_display_width(right_text);
        }
        int pad_right =
            (layout_ == TuiLayout::Split ? right_width_ : width - 2) - right_text_display_len;
        std::string right_render = right_text;
        if (pad_right > 0) {
            if (scroll_offset_ > 0) {
                right_render = std::string(static_cast<std::size_t>(pad_right / 2), ' ') +
                               right_render +
                               std::string(static_cast<std::size_t>((pad_right + 1) / 2), ' ');
                right_render = color_prefix + right_render + color_suffix;
            } else {
                right_render = std::string(static_cast<std::size_t>(pad_right), ' ') + right_render;
            }
        } else {
            right_render = format_to_width(right_render,
                                           layout_ == TuiLayout::Split ? right_width_ : width - 2);
            if (scroll_offset_ > 0) {
                right_render = color_prefix + right_render + color_suffix;
            }
        }

        std::string screen;
        if (layout_ == TuiLayout::Split) {
            screen += std::string(kSakuraBorder) + "╔" + make_repeated_string("═", left_width_) + "╤" +
                      make_repeated_string("═", right_width_) + "╗\033[0m\n";
            screen += std::string(kSakuraBorder) + "║\033[0m" + left_render + kSakuraBorder + "│\033[0m" + right_render +
                      kSakuraBorder + "║\033[0m\n";
            screen += std::string(kSakuraBorder) + "╠" + make_repeated_string("═", left_width_) + "╪" +
                      make_repeated_string("═", right_width_) + "╣\033[0m\n";
        } else if (layout_ == TuiLayout::FullConsole) {
            screen += std::string(kSakuraBorder) + "╔" + make_repeated_string("═", width - 2) + "╗\033[0m\n";
            screen += std::string(kSakuraBorder) + "║\033[0m" + right_render + kSakuraBorder + "║\033[0m\n";
            screen += std::string(kSakuraBorder) + "╠" + make_repeated_string("═", width - 2) + "╣\033[0m\n";
        } else {
            screen += std::string(kSakuraBorder) + "╔" + make_repeated_string("═", width - 2) + "╗\033[0m\n";
            screen += std::string(kSakuraBorder) + "║\033[0m" + left_render + kSakuraBorder + "║\033[0m\n";
            screen += std::string(kSakuraBorder) + "╠" + make_repeated_string("═", width - 2) + "╣\033[0m\n";
        }
        return screen;
    } else if (row_idx == 1) {
        // Footer
        std::string footer_text;
        if (paused_) {
            footer_text =
                " [s] Step | [c] Continue | [q] Quit | [r] Cycle Regs | [Tab] Cycle Layout | [u/d] Scroll ";
        } else {
            footer_text =
                " [Ctrl-P] Pause | [Ctrl-Q] Quit | [Alt-L] Layout | [Alt-R] Regs | [Alt-U/D] Scroll ";
        }
        int footer_len = get_display_width(footer_text);
        int pad_foot = (width - 2) - footer_len;
        std::string footer_render = footer_text;
        if (pad_foot > 0) {
            footer_render += std::string(static_cast<std::size_t>(pad_foot), ' ');
        } else {
            footer_render = format_to_width(footer_render, width - 2);
        }
        std::string screen = std::string(kSakuraBorder) + "║\033[0m" + footer_render + kSakuraBorder + "║\033[0m\n";
        screen += std::string(kSakuraBorder) + "╚" + make_repeated_string("═", width - 2) + "╝\033[0m";
        return screen;
    }
    return "";
}

void StatusBar::update_kips(uint64_t current_kips) {
    kips_ = current_kips;
    kips_history_.push_back(current_kips);
    if (kips_history_.size() > 60) {
        kips_history_.erase(kips_history_.begin());
    }
}

auto ConsolePane::render_row(int row_idx, int width) -> std::string {
    if (static_cast<std::size_t>(row_idx) >= lines_.size()) return format_to_width("", width);
    return lines_.at(static_cast<std::size_t>(row_idx));
}

auto RegisterPane::render_row(int row_idx, int width) -> std::string {
    int logical_row = row_idx + scroll_offset_;
    int total_logical_rows = machine_.s_cycle_accurate ? 43 : 35;

    if (logical_row >= total_logical_rows) {
        return format_to_width("", width);
    }

    auto section_line = [&](const std::string& title) -> std::string {
        std::string full = " " + title + " ";
        std::string dashes =
            make_repeated_string("─", std::max(0, width - get_display_width(full)));
        return std::format("{}{}\033[0m", kSakuraMuted, format_to_width(full + dashes, width));
    };

    if (!paused_) {
        if (logical_row >= 0 && logical_row <= 24) {
            auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
            constexpr std::array<const char*, 10> spinner = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
            std::string spin = spinner.at((static_cast<std::size_t>(now_ms / 80)) % 10);

            if (logical_row == 10) {
                std::string text = std::format("\033[38;5;218m●\033[0m \033[1;38;5;218mSIMULATOR ACTIVE\033[0m");
                int spaces = std::max(0, (width - 18) / 2);
                std::string line = std::string(spaces, ' ') + text;
                return format_to_width(line, width);
            }
            if (logical_row == 11) {
                std::string text = std::format("[  \033[38;5;121m{}\033[0m  Executing instructions... ]", spin);
                int spaces = std::max(0, (width - 33) / 2);
                std::string line = std::string(spaces, ' ') + text;
                return format_to_width(line, width);
            }
            if (logical_row == 12) {
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

        auto make_field = [](const std::string& label, const std::string& value,
                             const char* value_color = kSakuraVal) -> std::string {
            return std::format(" {}{:<8}\033[0m: {}{}\033[0m", kSakuraText, label, value_color, value);
        };

        auto render_pair = [&](const std::string& l1, const std::string& v1, const char* c1,
                               const std::string& l2, const std::string& v2,
                               const char* c2) -> std::string {
            return format_to_width(make_field(l1, v1, c1), col_width) +
                   format_to_width(make_field(l2, v2, c2), right_width);
        };

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
                // VEC page
                std::string col1_color = std::format(
                    " {}v{:<2}\033[0m       : {}0x0000000000000000\033[0m", kSakuraText, reg1, kSakuraMuted);
                std::string col2_color = std::format(
                    " {}v{:<2}\033[0m       : {}0x0000000000000000\033[0m", kSakuraText, reg2, kSakuraMuted);

                return format_to_width(col1_color, col_width) +
                       format_to_width(col2_color, width - col_width);
            } else {
                // PIPELINE page
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

        if (page_ == TuiRegPage::PIPELINE) {
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

        if (logical_row == 16) {
            return section_line("CSRs & Privilege State");
        }

        if (logical_row == 17) {
            std::string priv_str = (st.priv == PrivilegeLevel::Machine)      ? "Machine"
                                   : (st.priv == PrivilegeLevel::Supervisor) ? "Supervisor"
                                                                             : "User";
            return render_pair("pc", std::format("0x{:0{}x}", st.pc, simrv::xlen::kXLenHexDigits),
                               kSakuraMint, "priv", priv_str, kSakuraPink);
        }

        if (logical_row == 18) {
            std::string misa_str = simrv::xlen::resolve_misa_string(st.misa);

            return render_pair("mstatus",
                               std::format("0x{:0{}x}", st.mstatus, simrv::xlen::kXLenHexDigits),
                               kSakuraVal, "misa", misa_str, kSakuraVal);
        }

        if (logical_row == 19) {
            return render_pair(
                "mie", std::format("0x{:0{}x}", st.mie, simrv::xlen::kXLenHexDigits), kSakuraVal,
                "mip", std::format("0x{:0{}x}", st.mip, simrv::xlen::kXLenHexDigits), kSakuraVal);
        }

        if (logical_row == 20) {
            return render_pair(
                "mtvec", std::format("0x{:0{}x}", st.mtvec, simrv::xlen::kXLenHexDigits),
                kSakuraVal, "mepc", std::format("0x{:0{}x}", st.mepc, simrv::xlen::kXLenHexDigits),
                kSakuraVal);
        }

        if (logical_row == 21) {
            return render_pair(
                "stvec", std::format("0x{:0{}x}", st.stvec, simrv::xlen::kXLenHexDigits),
                kSakuraVal, "sepc", std::format("0x{:0{}x}", st.sepc, simrv::xlen::kXLenHexDigits),
                kSakuraVal);
        }

        if (logical_row == 22) {
            return render_pair(
                "mtval", std::format("0x{:0{}x}", st.mtval, simrv::xlen::kXLenHexDigits),
                kSakuraVal, "satp", std::format("0x{:0{}x}", st.satp, simrv::xlen::kXLenHexDigits),
                kSakuraVal);
        }

        if (logical_row == 23) {
            return render_pair(
                "scause", std::format("0x{:0{}x}", st.scause, simrv::xlen::kXLenHexDigits),
                kSakuraVal, "stval",
                std::format("0x{:0{}x}", st.stval, simrv::xlen::kXLenHexDigits), kSakuraVal);
        }

        if (logical_row == 24) {
            return render_pair(
                "medeleg", std::format("0x{:0{}x}", st.medeleg, simrv::xlen::kXLenHexDigits),
                kSakuraVal, "mideleg",
                std::format("0x{:0{}x}", st.mideleg, simrv::xlen::kXLenHexDigits), kSakuraVal);
        }

        return "";
    };

    if (logical_row >= 0 && logical_row <= 24) {
        std::string res = get_row_uncached();
        cached_left_rows_.at(static_cast<std::size_t>(logical_row)) = res;
        last_width_ = width;
        return res;
    }

    auto& cpu = machine_.cpu;

    if (!machine_.s_cycle_accurate) {
        if (logical_row == 25) {
            return section_line("Performance & Machine Stats");
        }
        if (logical_row == 26) {
            std::string insns = std::format("  Executed Insns : {}{}\033[0m", kSakuraMint, format_with_commas(cpu.e_icount));
            return format_to_width(insns, width);
        }
        if (logical_row == 27) {
            double sim_time_seconds = static_cast<double>(cpu.clint_mmio.mtime) / 10000000.0;
            std::string time = std::format("  Simulated Time : {}{:.6f} s\033[0m {}(0x{:x})\033[0m",
                                           kSakuraMint, sim_time_seconds, kSakuraMuted, cpu.clint_mmio.mtime);
            return format_to_width(time, width);
        }
        if (logical_row == 28) {
            std::string time = std::format("  Active Runtime : {}{:.6f} s\033[0m",
                                           kSakuraMint, active_runtime_);
            return format_to_width(time, width);
        }
        if (logical_row == 29) {
            std::string mode = std::format("  Simulation Mode: {}Functional (High-Perf)\033[0m", kSakuraVal);
            return format_to_width(mode, width);
        }
        if (logical_row == 30) {
            std::string extensions = simrv::xlen::resolve_misa_string(cpu.state().misa);
            std::string isa = std::format("  ISA Extensions : {}{}\033[0m", kSakuraVal, extensions);
            return format_to_width(isa, width);
        }
        if (logical_row == 31) {
            std::string mem_name = machine_.s_fn_memimg;
            auto pos = mem_name.find_last_of("/\\");
            if (pos != std::string::npos) mem_name = mem_name.substr(pos + 1);
            if (mem_name.empty()) mem_name = "None";
            std::string img = std::format("  Memory Image   : {}{}\033[0m", kSakuraSky, mem_name);
            return format_to_width(img, width);
        }
        if (logical_row == 32) {
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
        if (logical_row == 33) {
            uint64_t max_val = 1;
            for (auto val : kips_history_) {
                if (val > max_val) max_val = val;
            }

            std::string suffix =
                std::format("] {} Max:{}", format_with_commas(kips_), format_with_commas(max_val));
            std::string prefix = "  Speed (KIPS)   : [";
            int spark_width =
                width - static_cast<int>(prefix.length()) - static_cast<int>(suffix.length());
            if (spark_width < 5) spark_width = 5;

            std::string spark = get_sparkline_string(spark_width);
            std::string color = std::format(
                "  {}Speed (KIPS)\033[0m   : [{}{}\033[0m] {}{}\033[0m {}{}Max:{}\033[0m",
                kSakuraText, kSakuraMint, spark, kSakuraMint, format_with_commas(kips_), kSakuraMuted, kSakuraMuted, format_with_commas(max_val));

            return format_to_width(color, width);
        }
        if (logical_row == 34) {
            return format_to_width("", width);
        }
        return format_to_width("", width);
    }

    if (logical_row == 25) {
        return section_line("Statistics & Performance");
    }

    if (logical_row == 26) {
        uint64_t i_hits = cpu.icache.hit_count();
        uint64_t i_misses = cpu.icache.miss_count();
        uint64_t i_total = i_hits + i_misses;
        double i_ratio =
            (i_total == 0) ? 0.0 : static_cast<double>(i_hits) / static_cast<double>(i_total);

        std::string suffix = std::format(" {:.1f}% (H:{} M:{})", i_ratio * 100.0,
                                         format_compact(i_hits), format_compact(i_misses));
        std::string prefix = "  L1-I Cache     : [";
        int bar_width =
            width - static_cast<int>(prefix.length()) - static_cast<int>(suffix.length()) - 1;
        if (bar_width < 5) bar_width = 5;

        std::string bar = make_progress_bar(i_ratio, bar_width, kSakuraSky);
        std::string color = std::format(
            "  {}L1-I Cache\033[0m     : [{}] {}{:.1f}%\033[0m {}(H:{} M:{})\033[0m",
            kSakuraText, bar, kSakuraSky, i_ratio * 100.0, kSakuraMuted, format_compact(i_hits), format_compact(i_misses));

        return format_to_width(color, width);
    }

    if (logical_row == 27) {
        uint64_t d_hits = cpu.dcache.hit_count();
        uint64_t d_misses = cpu.dcache.miss_count();
        uint64_t d_total = d_hits + d_misses;
        double d_ratio =
            (d_total == 0) ? 0.0 : static_cast<double>(d_hits) / static_cast<double>(d_total);

        std::string suffix = std::format(" {:.1f}% (H:{} M:{})", d_ratio * 100.0,
                                         format_compact(d_hits), format_compact(d_misses));
        std::string prefix = "  L1-D Cache     : [";
        int bar_width =
            width - static_cast<int>(prefix.length()) - static_cast<int>(suffix.length()) - 1;
        if (bar_width < 5) bar_width = 5;

        std::string bar = make_progress_bar(d_ratio, bar_width, kSakuraPink);
        std::string color = std::format(
            "  {}L1-D Cache\033[0m     : [{}] {}{:.1f}%\033[0m {}(H:{} M:{})\033[0m",
            kSakuraText, bar, kSakuraPink, d_ratio * 100.0, kSakuraMuted, format_compact(d_hits), format_compact(d_misses));

        return format_to_width(color, width);
    }

    uint64_t cycles = cpu.clint_mmio.mcycle;
    uint64_t icount = cpu.e_icount;
    double ipc = (cycles == 0) ? 0.0 : static_cast<double>(icount) / static_cast<double>(cycles);
    double cpi = (icount == 0) ? 0.0 : static_cast<double>(cycles) / static_cast<double>(icount);

    if (logical_row == 28) {
        std::string color = std::format(
            "  {}IPC / CPI\033[0m      : {}{:.2f} IPC\033[0m  /  {}{:.2f} CPI\033[0m",
            kSakuraText, kSakuraMint, ipc, kSakuraPeach, cpi);
        return format_to_width(color, width);
    }

    uint64_t stalls = cpu.pipeline_sim.stall_cycles();
    uint64_t bubbles = cpu.pipeline_sim.bubble_cycles();
    uint64_t total_stalls_bubbles = stalls + bubbles;
    double stall_pct = (cycles == 0) ? 0.0 : (static_cast<double>(total_stalls_bubbles) * 100.0) / static_cast<double>(cycles);

    if (logical_row == 29) {
        std::string color = std::format(
            "  {}Stall Ratio\033[0m    : {}{:.1f}%\033[0m (Stall:{} clk, Bubble:{} clk)",
            kSakuraText, kSakuraCoral, stall_pct, format_compact(stalls), format_compact(bubbles));
        return format_to_width(color, width);
    }

    uint64_t data_stalls = cpu.pipeline_sim.data_hazard_stalls();
    uint64_t ctrl_bubbles = cpu.pipeline_sim.control_hazard_bubbles();
    uint64_t ic_stalls = cpu.pipeline_sim.icache_stalls();
    uint64_t dc_stalls = cpu.pipeline_sim.dcache_stalls();
    uint64_t cache_stalls = ic_stalls + dc_stalls;

    if (logical_row == 30) {
        double data_pct = (cycles == 0) ? 0.0 : (static_cast<double>(data_stalls) * 100.0) / static_cast<double>(cycles);
        std::string color = std::format(
            "    {}- Data RAW\033[0m   : {}{:>10}\033[0m clk {}({:.1f}%)\033[0m",
            kSakuraMuted, kSakuraPeach, format_with_commas(data_stalls), kSakuraMuted, data_pct);
        return format_to_width(color, width);
    }

    if (logical_row == 31) {
        double ctrl_pct = (cycles == 0) ? 0.0 : (static_cast<double>(ctrl_bubbles) * 100.0) / static_cast<double>(cycles);
        std::string color = std::format(
            "    {}- Control\033[0m    : {}{:>10}\033[0m clk {}({:.1f}%)\033[0m",
            kSakuraMuted, kSakuraPeach, format_with_commas(ctrl_bubbles), kSakuraMuted, ctrl_pct);
        return format_to_width(color, width);
    }

    if (logical_row == 32) {
        double cache_pct = (cycles == 0) ? 0.0 : (static_cast<double>(cache_stalls) * 100.0) / static_cast<double>(cycles);
        std::string color = std::format(
            "    {}- Cache\033[0m      : {}{:>10}\033[0m clk {}({:.1f}%)\033[0m",
            kSakuraMuted, kSakuraPeach, format_with_commas(cache_stalls), kSakuraMuted, cache_pct);
        return format_to_width(color, width);
    }

    if (logical_row == 33) {
        uint64_t max_val = 1;
        for (auto val : kips_history_) {
            if (val > max_val) max_val = val;
        }

        std::string suffix =
            std::format("] {} Max:{}", format_with_commas(kips_), format_with_commas(max_val));
        std::string prefix = "  Speed (KIPS)   : [";
        int spark_width =
            width - static_cast<int>(prefix.length()) - static_cast<int>(suffix.length());
        if (spark_width < 5) spark_width = 5;

        std::string spark = get_sparkline_string(spark_width);
        std::string color = std::format(
            "  {}Speed (KIPS)\033[0m   : [{}{}\033[0m] {}{}\033[0m {}{}Max:{}\033[0m",
            kSakuraText, kSakuraMint, spark, kSakuraMint, format_with_commas(kips_), kSakuraMuted, kSakuraMuted, format_with_commas(max_val));

        return format_to_width(color, width);
    }

    if (logical_row == 34) {
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

    if (logical_row == 35) {
        return section_line("Machine & Hardware Info");
    }

    if (logical_row == 36) {
        double sim_time_seconds = static_cast<double>(cpu.clint_mmio.mtime) / 10000000.0;
        std::string time = std::format("  Simulated Time : {}{:.6f} s\033[0m {}(0x{:x})\033[0m",
                                       kSakuraMint, sim_time_seconds, kSakuraMuted, cpu.clint_mmio.mtime);
        return format_to_width(time, width);
    }

    if (logical_row == 37) {
        std::string time = std::format("  Active Runtime : {}{:.6f} s\033[0m",
                                       kSakuraMint, active_runtime_);
        return format_to_width(time, width);
    }

    if (logical_row == 38) {
        std::string mode = std::format("  Simulation Mode: {}Cycle-Accurate (CA)\033[0m", kSakuraVal);
        return format_to_width(mode, width);
    }

    if (logical_row == 39) {
        std::string extensions = simrv::xlen::resolve_misa_string(cpu.state().misa);
        std::string isa = std::format("  ISA Extensions : {}{}\033[0m", kSakuraVal, extensions);
        return format_to_width(isa, width);
    }

    if (logical_row == 40) {
        std::string mem_name = machine_.s_fn_memimg;
        auto pos = mem_name.find_last_of("/\\");
        if (pos != std::string::npos) mem_name = mem_name.substr(pos + 1);
        if (mem_name.empty()) mem_name = "None";
        std::string img = std::format("  Memory Image   : {}{}\033[0m", kSakuraSky, mem_name);
        return format_to_width(img, width);
    }

    if (logical_row == 41) {
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

    if (logical_row == 42) {
        return format_to_width("", width);
    }

    return format_to_width("", width);
}

auto RegisterPane::get_sparkline_string(int width) -> std::string {
    if (kips_history_.empty()) {
        return std::string(static_cast<std::size_t>(width), ' '); // NOLINT(modernize-return-braced-init-list)
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

void RegisterPane::scroll(int lines) {
    int total_logical_rows = machine_.s_cycle_accurate ? 43 : 35;
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
