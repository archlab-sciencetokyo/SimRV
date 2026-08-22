/**
 * @file LeftPaneHazard.cpp
 * @brief Implements Pipeline Hazard & Forwarding Unit Inspector for TUI Left Pane.
 */
#include <format>
#include <string>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/pipeline/PipelineSim.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/panels/LeftPane.hpp"
#include "simrv/util/FormatUtil.hpp"

namespace simrv::tui {

auto LeftPane::render_hazard_stats(const simrv::core::CPU& cpu, int logical_row, int col_width,
                                   int right_width) -> std::string {
    int const width = col_width + right_width;

    if (!machine_.runtime_profile.is_cycle_mode()) {
        if (logical_row == 0) {
            return section_line("Pipeline Hazard & Forwarding Unit Inspector", width);
        }
        if (logical_row == 4) {
            std::string text = "\033[1;31m[!] CYCLE-ACCURATE MODE INACTIVE\033[0m";
            int text_w = get_display_width(text);
            int spaces = std::max(1, (width - text_w) / 2);
            return format_to_width(std::string(static_cast<std::size_t>(spaces), ' ') + text,
                                   width);
        }
        if (logical_row == 6) {
            std::string text =
                (width < 45) ? "Enable Cycle-Accurate mode [,]" : "Enable cycle mode [,] or --ca";
            int text_w = get_display_width(text);
            int spaces = std::max(1, (width - text_w) / 2);
            return format_to_width(
                std::string(static_cast<std::size_t>(spaces), ' ') + kThemeMuted + text + "\033[0m",
                width);
        }
        if (logical_row == 14) {
            return section_line("Status: Hazard Detection Disabled (Functional)", width);
        }
        return format_to_width("", width);
    }

    auto const state = cpu.pipeline_sim.save_state();

    uint64_t total_cycles = cpu.pipeline_sim.cycle_count();
    uint64_t total_stalls = cpu.pipeline_sim.stall_cycles();
    uint64_t data_hazards = cpu.pipeline_sim.data_hazard_stalls();
    uint64_t control_bubbles = cpu.pipeline_sim.control_hazard_bubbles();
    uint64_t structural_stalls = cpu.pipeline_sim.structural_stalls();

    double stall_pct = (total_cycles == 0) ? 0.0
                                           : 100.0 * static_cast<double>(total_stalls) /
                                                 static_cast<double>(total_cycles);

    if (logical_row == 0) {
        return section_line("Pipeline Hazard & Forwarding Unit Inspector", width);
    }
    if (logical_row == 1) {
        if (width < 50) {
            return format_to_width(
                std::format("  {}Stalls:\033[0m {}{}\033[0m ({:3.0f}%) {}RAW:\033[0m {}{}\033[0m",
                            kThemeText, kThemeCoral, simrv::util::format_scaled(total_stalls),
                            stall_pct, kThemeText, kThemePeach,
                            simrv::util::format_scaled(data_hazards)),
                width);
        }
        return format_to_width(
            std::format("  {}Stalls:\033[0m {}{} ({:4.1f}%)\033[0m │ {}RAW:\033[0m {}{}\033[0m │ "
                        "{}Ctrl:\033[0m {}{}\033[0m",
                        kThemeText, kThemeCoral, simrv::util::format_with_commas(total_stalls),
                        stall_pct, kThemeText, kThemePeach,
                        simrv::util::format_with_commas(data_hazards), kThemeText, kThemeCoral,
                        simrv::util::format_with_commas(control_bubbles)),
            width);
    }
    if (logical_row == 2) {
        if (width < 50) {
            return format_to_width(
                std::format(
                    "  {}IC:\033[0m {}{}\033[0m {}DC:\033[0m {}{}\033[0m {}TLB:\033[0m {}{}\033[0m",
                    kThemeText, kThemeMint,
                    simrv::util::format_scaled(cpu.pipeline_sim.icache_stalls()), kThemeText,
                    kThemePeach, simrv::util::format_scaled(cpu.pipeline_sim.dcache_stalls()),
                    kThemeText, kThemeSky,
                    simrv::util::format_scaled(cpu.pipeline_sim.tlb_stalls())),
                width);
        }
        return format_to_width(
            std::format("  {}Struct:\033[0m {}{}\033[0m │ {}IC:\033[0m {}{}\033[0m │ {}DC:\033[0m "
                        "{}{}\033[0m │ {}TLB:\033[0m {}{}\033[0m",
                        kThemeText, kThemeSky, simrv::util::format_with_commas(structural_stalls),
                        kThemeText, kThemeMint,
                        simrv::util::format_with_commas(cpu.pipeline_sim.icache_stalls()),
                        kThemeText, kThemePeach,
                        simrv::util::format_with_commas(cpu.pipeline_sim.dcache_stalls()),
                        kThemeText, kThemeSky,
                        simrv::util::format_with_commas(cpu.pipeline_sim.tlb_stalls())),
            width);
    }
    const auto ptype = cpu.pipeline_sim.config.pipeline_type;

    if (ptype == simrv::pipeline::PipelineType::ThreeStage) {
        if (logical_row == 3) {
            return section_line("3-Stage Direct Forwarding & Bypass State", width);
        }
        auto f_reg = state.f_reg;
        auto e_reg = state.e_reg;
        auto w_reg = state.w_reg;

        uint32_t f_pc = static_cast<uint32_t>(f_reg.pc & 0xFFFFFFFFULL);
        uint32_t e_pc = static_cast<uint32_t>(e_reg.pc & 0xFFFFFFFFULL);
        uint32_t w_pc = static_cast<uint32_t>(w_reg.pc & 0xFFFFFFFFULL);

        uint8_t e_rs1 = static_cast<uint8_t>(e_reg.rs1);
        uint8_t e_rs2 = static_cast<uint8_t>(e_reg.rs2);
        uint8_t e_rd = static_cast<uint8_t>(e_reg.rd);
        uint8_t w_rd = static_cast<uint8_t>(w_reg.rd);

        if (logical_row == 4) {
            return format_to_width(
                std::format("  {}IF    Stage:\033[0m {}PC {:08x}\033[0m │ {}Valid: {}\033[0m",
                            kThemeText, kThemeVal, f_pc, kThemeText,
                            (f_reg.valid ? "\033[38;5;120mYES\033[0m" : "\033[38;5;244mNO\033[0m")),
                width);
        }
        if (logical_row == 5) {
            return format_to_width(
                std::format("  {}ID/EX Stage:\033[0m {}PC {:08x}\033[0m │ {}rs1:\033[0mx{:<2} "
                            "{}rs2:\033[0mx{:<2} {}rd:\033[0mx{:<2}",
                            kThemeText, kThemeVal, e_pc, kThemeText, e_rs1, kThemeText, e_rs2,
                            kThemeText, e_rd),
                width);
        }
        if (logical_row == 6) {
            bool fwd_rs1 = (e_reg.valid && e_rs1 != 0 && e_rs1 == w_rd);
            bool fwd_rs2 = (e_reg.valid && e_rs2 != 0 && e_rs2 == w_rd);
            return format_to_width(
                std::format("  {}Forwarding:\033[0m  {}WB→EX rs1:\033[0m{}\033[0m │ "
                            "{}WB→EX rs2:\033[0m{}\033[0m",
                            kThemeText, kThemeText,
                            (fwd_rs1 ? "\033[38;5;120mYES\033[0m" : "\033[38;5;244mNO\033[0m"),
                            kThemeText,
                            (fwd_rs2 ? "\033[38;5;120mYES\033[0m" : "\033[38;5;244mNO\033[0m")),
                width);
        }
        if (logical_row == 7) {
            return format_to_width(
                std::format("  {}MEM/WB Stage:\033[0m {}PC {:08x}\033[0m │ {}rd:\033[0mx{:<2} │ "
                            "{}Bypass:\033[0m {}\033[0m",
                            kThemeText, kThemeVal, w_pc, kThemeText, w_rd, kThemeText,
                            (w_reg.valid && w_rd != 0 ? "\033[38;5;120mYES\033[0m"
                                                      : "\033[38;5;244mNO\033[0m")),
                width);
        }
        if (logical_row == 8) {
            bool load_use = e_reg.valid && w_reg.valid && w_reg.is_load &&
                            (w_rd != 0 && (w_rd == e_rs1 || w_rd == e_rs2));
            return format_to_width(
                std::format("  {}Load-Use Stall:\033[0m {}", kThemeText,
                            (load_use ? "\033[38;5;203mDETECTED (1-cycle bubble)\033[0m"
                                      : "\033[38;5;120mCLEAR (no hazard)\033[0m")),
                width);
        }
        if (logical_row == 9) {
            return section_line("WB→EX Forwarding + 1-Cycle Load-Use Stall Detection", width);
        }
        return format_to_width("", width);
    }

    // 5-Stage Rocket
    if (logical_row == 3) {
        return section_line("Data Bypass & Forwarding Unit State", width);
    }

    auto f_reg = state.f_reg;
    auto d_reg = state.d_reg;
    auto e_reg = state.e_reg;
    auto m_reg = state.m_reg;
    auto w_reg = state.w_reg;

    uint32_t f_pc = static_cast<uint32_t>(f_reg.pc & 0xFFFFFFFFULL);
    uint32_t d_pc = static_cast<uint32_t>(d_reg.pc & 0xFFFFFFFFULL);
    uint32_t e_pc = static_cast<uint32_t>(e_reg.pc & 0xFFFFFFFFULL);
    uint32_t m_pc = static_cast<uint32_t>(m_reg.pc & 0xFFFFFFFFULL);
    uint32_t w_pc = static_cast<uint32_t>(w_reg.pc & 0xFFFFFFFFULL);

    uint8_t d_rs1 = static_cast<uint8_t>(d_reg.rs1);
    uint8_t d_rs2 = static_cast<uint8_t>(d_reg.rs2);
    uint8_t d_rd = static_cast<uint8_t>(d_reg.rd);
    uint8_t e_rs1 = static_cast<uint8_t>(e_reg.rs1);
    uint8_t e_rs2 = static_cast<uint8_t>(e_reg.rs2);
    uint8_t m_rd = static_cast<uint8_t>(m_reg.rd);
    uint8_t w_rd = static_cast<uint8_t>(w_reg.rd);

    if (logical_row == 4) {
        return format_to_width(
            std::format("  {}IF  Stage:\033[0m {}PC {:08x}\033[0m │ {}Valid: {}\033[0m", kThemeText,
                        kThemeVal, f_pc, kThemeText,
                        (f_reg.valid ? "\033[38;5;120mYES\033[0m" : "\033[38;5;244mNO\033[0m")),
            width);
    }
    if (logical_row == 5) {
        return format_to_width(
            std::format("  {}ID  Stage:\033[0m {}PC {:08x}\033[0m │ {}rs1:\033[0mx{:<2} "
                        "{}rs2:\033[0mx{:<2} {}rd:\033[0mx{:<2}",
                        kThemeText, kThemeVal, d_pc, kThemeText, d_rs1, kThemeText, d_rs2,
                        kThemeText, d_rd),
            width);
    }
    if (logical_row == 6) {
        bool fwd_rs1 = (e_reg.valid && e_rs1 != 0 && (e_rs1 == m_rd || e_rs1 == w_rd));
        bool fwd_rs2 = (e_reg.valid && e_rs2 != 0 && (e_rs2 == m_rd || e_rs2 == w_rd));
        return format_to_width(
            std::format("  {}EX  Stage:\033[0m {}PC {:08x}\033[0m │ {}Fwd rs1:\033[0m{}\033[0m "
                        "{}rs2:\033[0m{}\033[0m",
                        kThemeText, kThemeVal, e_pc, kThemeText,
                        (fwd_rs1 ? "\033[38;5;120mYES\033[0m" : "\033[38;5;244mNO\033[0m"),
                        kThemeText,
                        (fwd_rs2 ? "\033[38;5;120mYES\033[0m" : "\033[38;5;244mNO\033[0m")),
            width);
    }
    if (logical_row == 7) {
        return format_to_width(
            std::format("  {}MEM Stage:\033[0m {}PC {:08x}\033[0m │ {}rd:\033[0mx{:<2} │ "
                        "{}Bypass:\033[0m {}\033[0m",
                        kThemeText, kThemeVal, m_pc, kThemeText, m_rd, kThemeText,
                        (m_reg.valid && m_rd != 0 ? "\033[38;5;120mYES\033[0m"
                                                  : "\033[38;5;244mNO\033[0m")),
            width);
    }
    if (logical_row == 8) {
        return format_to_width(
            std::format("  {}WB  Stage:\033[0m {}PC {:08x}\033[0m │ {}rd:\033[0mx{:<2} │ "
                        "{}Bypass:\033[0m {}\033[0m",
                        kThemeText, kThemeVal, w_pc, kThemeText, w_rd, kThemeText,
                        (w_reg.valid && w_rd != 0 ? "\033[38;5;120mYES\033[0m"
                                                  : "\033[38;5;244mNO\033[0m")),
            width);
    }
    if (logical_row == 9) {
        return section_line("EX/MEM & MEM/WB Bypassing + 1-Cycle Load-Use Bubble", width);
    }

    return format_to_width("", width);
}

}  // namespace simrv::tui
