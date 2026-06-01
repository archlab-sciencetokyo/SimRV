#include "simrv/tui/TuiComponents.hpp"

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
#include "simrv/tui/Ansi.hpp"
#include "simrv/xlen/Helpers.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::tui {

namespace {

void handle_sigwinch(int sig) {
    (void)sig;
    g_resized = 1;
}

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
    for (std::size_t i = 0; i < s.length(); ++i) {
        if (s[i] == '\033') {
            in_esc = true;
        } else if (in_esc) {
            if (s[i] == 'm') {
                in_esc = false;
            }
        } else {
            unsigned char c = static_cast<unsigned char>(s[i]);
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
    bool skipping = false;

    for (std::size_t i = 0; i < colored_str.length(); ++i) {
        if (colored_str[i] == '\033') {
            in_esc = true;
            result += colored_str[i];
        } else if (in_esc) {
            result += colored_str[i];
            if (colored_str[i] == 'm') {
                in_esc = false;
            }
        } else {
            unsigned char c = static_cast<unsigned char>(colored_str[i]);
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
                result += colored_str[i];
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

auto format_with_commas(uint64_t val) -> std::string {
    std::string s = std::to_string(val);
    int n = static_cast<int>(s.length()) - 3;
    while (n > 0) {
        s.insert(static_cast<std::size_t>(n), ",");
        n -= 3;
    }
    return s;
}

auto make_progress_bar(double ratio, int width, const std::string& color_code) -> std::string {
    int filled = static_cast<int>(ratio * width);
    if (filled < 0) filled = 0;
    if (filled > width) filled = width;
    std::string bar;
    bar += color_code;
    for (int i = 0; i < filled; ++i) {
        bar += "█";
    }
    bar += "\033[90m";  // Dark gray
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
auto wrap_line(const std::string& line, int max_width) -> std::vector<std::string> {
    std::vector<std::string> chunks;
    if (line.empty()) {
        chunks.push_back("");
        return chunks;
    }
    if (max_width <= 0) {
        chunks.push_back(line);
        return chunks;
    }
    std::size_t pos = 0;
    while (pos < line.length()) {
        std::size_t len = std::min(static_cast<std::size_t>(max_width), line.length() - pos);
        chunks.push_back(line.substr(pos, len));
        pos += len;
    }
    return chunks;
}

}  // namespace

StatusBar::StatusBar(simrv::core::Machine& machine) : machine_(machine) {}

auto StatusBar::render_row(int row_idx, int width) -> std::string {
    if (row_idx == 0) {
        // Header
        std::string binary_name = "application";
        auto last_slash = binary_name.find_last_of("/\\");
        if (last_slash != std::string::npos) {
            binary_name = binary_name.substr(last_slash + 1);
        }
        std::string status_badge =
            status_override_.empty()
                ? (paused_ ? "\033[43;30m PAUSED \033[0m" : "\033[42;30m RUNNING \033[0m")
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
        std::string left_text = std::format(" SimRV [{}] | Status: ", binary_name);
        std::string left_render =
            left_text + status_badge + std::format(" | Page: \033[1;36m{}\033[0m", page_badge);
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
            right_text = std::format("Cycles: {} | Insns: {}",
                                     format_with_commas(machine_.cpu.clint_mmio.mtime),
                                     format_with_commas(machine_.cpu.e_icount));
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
            screen += "\033[1;94m╔" + make_repeated_string("═", left_width_) + "╤" +
                      make_repeated_string("═", right_width_) + "╗\033[0m\n";
            screen += "\033[1;94m║\033[0m" + left_render + "\033[1;94m│\033[0m" + right_render +
                      "\033[1;94m║\033[0m\n";
            screen += "\033[1;94m╠" + make_repeated_string("═", left_width_) + "╪" +
                      make_repeated_string("═", right_width_) + "╣\033[0m\n";
        } else if (layout_ == TuiLayout::FullConsole) {
            screen += "\033[1;94m╔" + make_repeated_string("═", width - 2) + "╗\033[0m\n";
            screen += "\033[1;94m║\033[0m" + right_render + "\033[1;94m║\033[0m\n";
            screen += "\033[1;94m╠" + make_repeated_string("═", width - 2) + "╣\033[0m\n";
        } else {
            screen += "\033[1;94m╔" + make_repeated_string("═", width - 2) + "╗\033[0m\n";
            screen += "\033[1;94m║\033[0m" + left_render + "\033[1;94m║\033[0m\n";
            screen += "\033[1;94m╠" + make_repeated_string("═", width - 2) + "╣\033[0m\n";
        }
        return screen;
    } else if (row_idx == 1) {
        // Footer
        std::string footer_text;
        if (paused_) {
            footer_text =
                " [s] Step | [c] Continue | [q] Quit | [r] Cycle Regs | [Tab] Cycle Layout ";
        } else {
            footer_text = " [Ctrl-P] Pause | [Ctrl-Q] Quit | [Tab] Cycle Layout ";
        }
        int footer_len = get_display_width(footer_text);
        int pad_foot = (width - 2) - footer_len;
        std::string footer_render = footer_text;
        if (pad_foot > 0) {
            footer_render += std::string(static_cast<std::size_t>(pad_foot), ' ');
        } else {
            footer_render = format_to_width(footer_render, width - 2);
        }
        std::string screen = "\033[1;94m║\033[0m" + footer_render + "\033[1;94m║\033[0m\n";
        screen += "\033[1;94m╚" + make_repeated_string("═", width - 2) + "╝\033[0m";
        return screen;
    }
    return "";
}

void StatusBar::update_kips(uint64_t current_kips) {
    kips_history_.push_back(current_kips);
    if (kips_history_.size() > 60) {
        kips_history_.erase(kips_history_.begin());
    }
}

auto ConsolePane::render_row(int row_idx, int width) -> std::string {
    if (static_cast<std::size_t>(row_idx) >= lines_.size()) return format_to_width("", width);
    std::string line = lines_[static_cast<std::size_t>(row_idx)];
    return format_to_width(line, width);
}

auto RegisterPane::render_row(int row_idx, int width) -> std::string {
    auto get_row_uncached = [&]() -> std::string {
        auto& cpu = machine_.cpu;
        auto& st = cpu.state();
        int col_width = width / 2;
        int right_width = width - col_width;

        auto make_field = [](const std::string& label, const std::string& value,
                             const char* value_color = "\033[36m") -> std::string {
            return std::format(" \033[97m{:<8}\033[0m: {}{}\033[0m", label, value_color, value);
        };

        auto render_pair = [&](const std::string& l1, const std::string& v1, const char* c1,
                               const std::string& l2, const std::string& v2,
                               const char* c2) -> std::string {
            return format_to_width(make_field(l1, v1, c1), col_width) +
                   format_to_width(make_field(l2, v2, c2), right_width);
        };

        auto section_line = [&](const std::string& title) -> std::string {
            std::string full = " " + title + " ";
            std::string dashes =
                make_repeated_string("─", std::max(0, width - static_cast<int>(full.length())));
            return "\033[1;90m" + format_to_width(full + dashes, width) + "\033[0m";
        };

        if (row_idx >= 0 && row_idx < 16) {
            int reg1 = row_idx;
            int reg2 = row_idx + 16;

            if (page_ == TuiRegPage::GPR) {
                auto val1 = st.regs.read(static_cast<RegId>(reg1));
                auto val2 = st.regs.read(static_cast<RegId>(reg2));

                std::string name1 = kRegNames[static_cast<std::size_t>(reg1)];
                std::string name2 = kRegNames[static_cast<std::size_t>(reg2)];

                bool changed = paused_ && (cached_gpr_[reg1] != val1);
                std::string c1 = changed ? "\033[93m" : "\033[92m";
                bool changed2 = paused_ && (cached_gpr_[reg2] != val2);
                std::string c2 = changed2 ? "\033[93m" : "\033[92m";

                std::string col1_color =
                    std::format(" \033[97mx{:<2}\033[0m/\033[36m{:<5}\033[0m: {}0x{:0{}x}\033[0m",
                                reg1, name1, c1, val1, simrv::xlen::kXLenHexDigits);
                std::string col2_color =
                    std::format(" \033[97mx{:<2}\033[0m/\033[36m{:<5}\033[0m: {}0x{:0{}x}\033[0m",
                                reg2, name2, c2, val2, simrv::xlen::kXLenHexDigits);

                return format_to_width(col1_color, col_width) +
                       format_to_width(col2_color, width - col_width);
            } else if (page_ == TuiRegPage::FPR) {
                auto val1 = st.regs.read_fp(static_cast<RegId>(reg1));
                auto val2 = st.regs.read_fp(static_cast<RegId>(reg2));

                std::string name1 = kFpRegNames[static_cast<std::size_t>(reg1)];
                std::string name2 = kFpRegNames[static_cast<std::size_t>(reg2)];

                std::string col1_color = std::format(
                    " \033[97mf{:<2}\033[0m/\033[36m{:<5}\033[0m: \033[92m0x{:016x}\033[0m", reg1,
                    name1, val1);
                std::string col2_color = std::format(
                    " \033[97mf{:<2}\033[0m/\033[36m{:<5}\033[0m: \033[92m0x{:016x}\033[0m", reg2,
                    name2, val2);

                return format_to_width(col1_color, col_width) +
                       format_to_width(col2_color, width - col_width);
            } else if (page_ == TuiRegPage::VEC) {
                // VEC page
                std::string col1_color = std::format(
                    " \033[97mv{:<2}\033[0m       : \033[90m0x0000000000000000\033[0m", reg1);
                std::string col2_color = std::format(
                    " \033[97mv{:<2}\033[0m       : \033[90m0x0000000000000000\033[0m", reg2);

                return format_to_width(col1_color, col_width) +
                       format_to_width(col2_color, width - col_width);
            } else {
                // PIPELINE page
                auto& ctx = cpu.pipeline_context;
                auto exc_text =
                    ctx.pending_exception.has_value()
                        ? std::to_string(std::to_underlying(ctx.pending_exception.value()))
                        : std::string("none");

                switch (row_idx) {
                    case 0:
                        return section_line("── IF/CVT");
                    case 1:
                        return render_pair(
                            "cpc", std::format("0x{:0{}x}", ctx.cpc, simrv::xlen::kXLenHexDigits),
                            "\033[92m", "ir_org", std::format("0x{:08x}", ctx.ir_org), "\033[36m");
                    case 2:
                        return render_pair("ir", std::format("0x{:08x}", ctx.ir), "\033[36m",
                                           "cinsn", std::format("0x{:08x}", ctx.cinsn), "\033[36m");
                    case 3:
                        return section_line("── ID");
                    case 4:
                        return render_pair(
                            "opcode", std::to_string(std::to_underlying(ctx.opcode)), "\033[36m",
                            "funct3", std::to_string(std::to_underlying(ctx.funct3)), "\033[36m");
                    case 5:
                        return render_pair(
                            "rd/rs1",
                            std::format("{}/{}", std::to_underlying(ctx.rd),
                                        std::to_underlying(ctx.rs1)),
                            "\033[36m", "rs2/f7",
                            std::format("{}/0x{:x}", std::to_underlying(ctx.rs2), ctx.funct7),
                            "\033[36m");
                    case 6:
                        return render_pair(
                            "imm", std::format("0x{:0{}x}", ctx.imm, simrv::xlen::kXLenHexDigits),
                            "\033[36m", "funct12", std::format("0x{:x}", ctx.funct12), "\033[36m");
                    case 7:
                        return section_line("── OF/EX");
                    case 8:
                        return render_pair(
                            "rrs1", std::format("0x{:0{}x}", ctx.rrs1, simrv::xlen::kXLenHexDigits),
                            "\033[92m", "rrs2",
                            std::format("0x{:0{}x}", ctx.rrs2, simrv::xlen::kXLenHexDigits),
                            "\033[92m");
                    case 9:
                        return render_pair(
                            "jmp_pc",
                            std::format("0x{:0{}x}", ctx.jmp_pc, simrv::xlen::kXLenHexDigits),
                            "\033[92m", "taken", ctx.tkn ? "yes" : "no", "\033[36m");
                    case 10:
                        return render_pair(
                            "wb_data",
                            std::format("0x{:0{}x}", ctx.wb_data, simrv::xlen::kXLenHexDigits),
                            "\033[92m", "wb_csr",
                            std::format("0x{:0{}x}", ctx.wb_data_csr, simrv::xlen::kXLenHexDigits),
                            "\033[36m");
                    case 11:
                        return section_line("── MEM/FP");
                    case 12:
                        return render_pair(
                            "mem_addr",
                            std::format("0x{:0{}x}", ctx.mem_addr, simrv::xlen::kXLenHexDigits),
                            "\033[92m", "mem_w",
                            std::format("0x{:0{}x}", ctx.mem_wdata, simrv::xlen::kXLenHexDigits),
                            "\033[92m");
                    case 13:
                        return render_pair(
                            "mem_r",
                            std::format("0x{:0{}x}", ctx.mem_rdata, simrv::xlen::kXLenHexDigits),
                            "\033[92m", "fp_wb", std::format("0x{:016x}", ctx.fp_wb_data),
                            "\033[36m");
                    case 14:
                        return render_pair("fp_wb_en", ctx.fp_wb_enable ? "on" : "off", "\033[36m",
                                           "int<-fp", ctx.int_wb_from_fp ? "on" : "off",
                                           "\033[36m");
                    case 15:
                        return section_line("── TRAP/TLB");
                    default:
                        return format_to_width(" \033[90mPipeline page\033[0m", width);
                }
            }
        }

        if (page_ == TuiRegPage::PIPELINE) {
            auto& ctx = cpu.pipeline_context;
            auto exc_text = ctx.pending_exception.has_value()
                                ? std::to_string(std::to_underlying(ctx.pending_exception.value()))
                                : std::string("none");

            switch (row_idx) {
                case 16:
                    return render_pair(
                        "exc", exc_text, "\033[93m", "tval",
                        std::format("0x{:0{}x}", ctx.pending_tval, simrv::xlen::kXLenHexDigits),
                        "\033[36m");
                case 17:
                    return render_pair(
                        "padr1", std::format("0x{:0{}x}", ctx.padr1, simrv::xlen::kXLenHexDigits),
                        "\033[36m", "padr2",
                        std::format("0x{:0{}x}", ctx.padr2, simrv::xlen::kXLenHexDigits),
                        "\033[36m");
                case 18:
                    return render_pair(
                        "rcsr", std::format("0x{:0{}x}", ctx.rcsr, simrv::xlen::kXLenHexDigits),
                        "\033[36m", "funct5", std::to_string(std::to_underlying(ctx.funct5)),
                        "\033[36m");
                case 19:
                    return section_line("── End Pipeline Snapshot");
                default:
                    return format_to_width("", width);
            }
        }

        if (row_idx == 16) {
            std::string title = " ── CSRs & Privilege State ";
            std::string dashes =
                make_repeated_string("─", std::max(0, width - static_cast<int>(title.length())));
            return "\033[1;90m" + format_to_width(title + dashes, width) + "\033[0m";
        }

        if (row_idx == 17) {
            std::string priv_str = (st.priv == PrivilegeLevel::Machine)      ? "Machine"
                                   : (st.priv == PrivilegeLevel::Supervisor) ? "Supervisor"
                                                                             : "User";
            return render_pair("pc", std::format("0x{:0{}x}", st.pc, simrv::xlen::kXLenHexDigits),
                               "\033[92m", "priv", priv_str, "\033[35m");
        }

        if (row_idx == 18) {
            std::string misa_str = simrv::xlen::resolve_misa_string(st.misa);

            return render_pair("mstatus",
                               std::format("0x{:0{}x}", st.mstatus, simrv::xlen::kXLenHexDigits),
                               "\033[36m", "misa", misa_str, "\033[36m");
        }

        if (row_idx == 19) {
            return render_pair(
                "mie", std::format("0x{:0{}x}", st.mie, simrv::xlen::kXLenHexDigits), "\033[36m",
                "mip", std::format("0x{:0{}x}", st.mip, simrv::xlen::kXLenHexDigits), "\033[36m");
        }

        if (row_idx == 20) {
            return render_pair(
                "mtvec", std::format("0x{:0{}x}", st.mtvec, simrv::xlen::kXLenHexDigits),
                "\033[36m", "mepc", std::format("0x{:0{}x}", st.mepc, simrv::xlen::kXLenHexDigits),
                "\033[36m");
        }

        if (row_idx == 21) {
            return render_pair(
                "stvec", std::format("0x{:0{}x}", st.stvec, simrv::xlen::kXLenHexDigits),
                "\033[36m", "sepc", std::format("0x{:0{}x}", st.sepc, simrv::xlen::kXLenHexDigits),
                "\033[36m");
        }

        if (row_idx == 22) {
            return render_pair(
                "mtval", std::format("0x{:0{}x}", st.mtval, simrv::xlen::kXLenHexDigits),
                "\033[36m", "satp", std::format("0x{:0{}x}", st.satp, simrv::xlen::kXLenHexDigits),
                "\033[36m");
        }

        if (row_idx == 23) {
            return render_pair(
                "scause", std::format("0x{:0{}x}", st.scause, simrv::xlen::kXLenHexDigits),
                "\033[36m", "stval",
                std::format("0x{:0{}x}", st.stval, simrv::xlen::kXLenHexDigits), "\033[36m");
        }

        if (row_idx == 24) {
            return render_pair(
                "medeleg", std::format("0x{:0{}x}", st.medeleg, simrv::xlen::kXLenHexDigits),
                "\033[36m", "mideleg",
                std::format("0x{:0{}x}", st.mideleg, simrv::xlen::kXLenHexDigits), "\033[36m");
        }

        return "";
    };

    if (row_idx >= 0 && row_idx <= 24) {
        std::string res = get_row_uncached();
        cached_left_rows_[static_cast<std::size_t>(row_idx)] = res;
        return res;
    }

    auto& cpu = machine_.cpu;

    if (row_idx == 25) {
        std::string title = " ── L1 Cache & Performance ";
        std::string dashes =
            make_repeated_string("─", std::max(0, width - static_cast<int>(title.length())));
        return "\033[1;90m" + format_to_width(title + dashes, width) + "\033[0m";
    }

    if (row_idx == 26) {
        uint64_t i_hits = cpu.icache.hit_count();
        uint64_t i_misses = cpu.icache.miss_count();
        uint64_t i_total = i_hits + i_misses;
        double i_ratio =
            (i_total == 0) ? 0.0 : static_cast<double>(i_hits) / static_cast<double>(i_total);

        // Dynamic progress bar calculation
        std::string suffix = std::format(" {:.1f}% (H:{} M:{})", i_ratio * 100.0,
                                         format_compact(i_hits), format_compact(i_misses));
        std::string prefix = " L1-I Cache : [";
        int bar_width =
            width - static_cast<int>(prefix.length()) - static_cast<int>(suffix.length()) - 1;
        if (bar_width < 5) bar_width = 5;

        std::string bar = make_progress_bar(i_ratio, bar_width, "\033[96m");  // Cyan bar
        std::string color = std::format(
            "\033[97mL1-I Cache\033[0m : [{}]\033[96m{:.1f}%\033[0m \033[90m(H:{} M:{})\033[0m",
            bar, i_ratio * 100.0, format_compact(i_hits), format_compact(i_misses));

        return format_to_width(color, width);
    }

    if (row_idx == 27) {
        uint64_t d_hits = cpu.dcache.hit_count();
        uint64_t d_misses = cpu.dcache.miss_count();
        uint64_t d_total = d_hits + d_misses;
        double d_ratio =
            (d_total == 0) ? 0.0 : static_cast<double>(d_hits) / static_cast<double>(d_total);

        // Dynamic progress bar calculation
        std::string suffix = std::format(" {:.1f}% (H:{} M:{})", d_ratio * 100.0,
                                         format_compact(d_hits), format_compact(d_misses));
        std::string prefix = " L1-D Cache : [";
        int bar_width =
            width - static_cast<int>(prefix.length()) - static_cast<int>(suffix.length()) - 1;
        if (bar_width < 5) bar_width = 5;

        std::string bar = make_progress_bar(d_ratio, bar_width, "\033[95m");  // Magenta bar
        std::string color = std::format(
            "\033[97mL1-D Cache\033[0m : [{}]\033[95m{:.1f}%\033[0m \033[90m(H:{} M:{})\033[0m",
            bar, d_ratio * 100.0, format_compact(d_hits), format_compact(d_misses));

        return format_to_width(color, width);
    }

    if (row_idx == 28) {
        uint64_t max_val = 1;
        for (auto val : kips_history_) {
            if (val > max_val) max_val = val;
        }

        std::string suffix =
            std::format(" {} Max:{}", format_with_commas(kips_), format_with_commas(max_val));
        std::string prefix = " Speed (KIPS): [";
        int spark_width =
            width - static_cast<int>(prefix.length()) - static_cast<int>(suffix.length()) - 1;
        if (spark_width < 5) spark_width = 5;

        std::string spark = get_sparkline_string(spark_width);
        std::string color = std::format(
            "\033[97mSpeed (KIPS)\033[0m: [\033[92m{}\033[0m]\033[92m {}\033[0m "
            "\033[90mMax:{}\033[0m",
            spark, format_with_commas(kips_), format_with_commas(max_val));

        return format_to_width(color, width);
    }

    if (row_idx == 29) {
        uint64_t alu_count = 0;
        uint64_t mem_count = 0;
        uint64_t ctrl_count = 0;
        uint64_t sys_count = 0;

        for (int i = 0; i < OperationIdCount; ++i) {
            uint64_t count = cpu.e_instmix[i];
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
            " \033[97mInst Mix\033[0m   : \033[92mALU:{:.1f}%\033[0m \033[96mMEM:{:.1f}%\033[0m "
            "\033[93mCTRL:{:.1f}%\033[0m \033[95mSYS:{:.1f}%\033[0m",
            alu_p, mem_p, ctrl_p, sys_p);

        return format_to_width(color, width);
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

    const char* blocks[8] = {" ", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
    int start_hist = (history_size > width) ? (history_size - width) : 0;
    for (int i = start_hist; i < history_size; ++i) {
        double ratio = static_cast<double>(kips_history_[static_cast<std::size_t>(i)]) /
                       static_cast<double>(max_val);
        int block_idx = static_cast<int>(ratio * 7.0);
        if (block_idx < 0) block_idx = 0;
        if (block_idx > 7) block_idx = 7;
        s += blocks[block_idx];
    }
    return s;
}

void RegisterPane::update_cache() {
    auto& st = machine_.cpu.state();
    for (int i = 0; i < 32; ++i) {
        cached_gpr_[i] = st.regs.read(static_cast<RegId>(i));
        cached_fpr_[i] = st.regs.read_fp(static_cast<RegId>(i));
    }
}

}  // namespace simrv::tui
