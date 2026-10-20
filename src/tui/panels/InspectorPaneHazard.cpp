/**
 * @file InspectorPaneHazard.cpp
 * @brief Implements Pipeline Hazard & Forwarding Unit Inspector for TUI Left Pane.
 */
#include <format>
#include <string>

#include "InspectorPaneCycleMode.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/pipeline/PipelineSim.hpp"
#include "simrv/pipeline/Scoreboard.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/panels/InspectorPane.hpp"
#include "simrv/util/FormatUtil.hpp"

namespace simrv::tui {

auto InspectorPane::render_hazard_stats(const simrv::core::CPU& cpu, int logical_row, int col_width,
                                        int right_width) -> std::string {
    int const width = col_width + right_width;

    if (!machine_.runtime_profile.is_cycle_mode()) {
        if (logical_row == 0) {
            return section_line("Pipeline Hazard & Forwarding Unit Inspector", width);
        }
        if (auto row = detail::inactive_cycle_mode_row(logical_row, width)) return *row;
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

        auto f_pc = static_cast<uint32_t>(f_reg.pc & 0xFFFFFFFFULL);
        auto e_pc = static_cast<uint32_t>(e_reg.pc & 0xFFFFFFFFULL);
        auto w_pc = static_cast<uint32_t>(w_reg.pc & 0xFFFFFFFFULL);

        auto e_rs1 = static_cast<uint8_t>(e_reg.rs1);
        auto e_rs2 = static_cast<uint8_t>(e_reg.rs2);
        auto e_rd = static_cast<uint8_t>(e_reg.rd);
        auto w_rd = static_cast<uint8_t>(w_reg.rd);

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

    auto f_pc = static_cast<uint32_t>(f_reg.pc & 0xFFFFFFFFULL);
    auto d_pc = static_cast<uint32_t>(d_reg.pc & 0xFFFFFFFFULL);
    auto e_pc = static_cast<uint32_t>(e_reg.pc & 0xFFFFFFFFULL);
    auto m_pc = static_cast<uint32_t>(m_reg.pc & 0xFFFFFFFFULL);
    auto w_pc = static_cast<uint32_t>(w_reg.pc & 0xFFFFFFFFULL);

    auto d_rs1 = static_cast<uint8_t>(d_reg.rs1);
    auto d_rs2 = static_cast<uint8_t>(d_reg.rs2);
    auto d_rd = static_cast<uint8_t>(d_reg.rd);
    auto e_rs1 = static_cast<uint8_t>(e_reg.rs1);
    auto e_rs2 = static_cast<uint8_t>(e_reg.rs2);
    auto m_rd = static_cast<uint8_t>(m_reg.rd);
    auto w_rd = static_cast<uint8_t>(w_reg.rd);

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
        std::string rs1_source = (e_rs1 == 0)                     ? "x0"
                                 : (e_rs1 == m_rd && m_reg.valid) ? "\033[1;36mMEM→EX\033[0m"
                                 : (e_rs1 == w_rd && w_reg.valid) ? "\033[1;32mWB→EX\033[0m"
                                                                  : "\033[38;5;244mRegFile\033[0m";
        std::string rs2_source = (e_rs2 == 0)                     ? "x0"
                                 : (e_rs2 == m_rd && m_reg.valid) ? "\033[1;36mMEM→EX\033[0m"
                                 : (e_rs2 == w_rd && w_reg.valid) ? "\033[1;32mWB→EX\033[0m"
                                                                  : "\033[38;5;244mRegFile\033[0m";
        return format_to_width(
            std::format("  {}EX  Stage:\033[0m {}PC {:08x}\033[0m │ {}rs1:\033[0m{} │ "
                        "{}rs2:\033[0m{}",
                        kThemeText, kThemeVal, e_pc, kThemeText, rs1_source, kThemeText,
                        rs2_source),
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

    // --- Live Multi-Bank Scoreboard & Active Dependency Section (rows 10+) ---
    if (logical_row == 10) {
        return section_line("Live Multi-Bank Scoreboard (In-Flight Reservations)", width);
    }

    struct ActiveReservation {
        const char* bank_name;
        std::string reg_name;
        const char* stage_name;
        simrv::xlen::LatencyCycles latency;
        bool can_forward;
    };
    std::vector<ActiveReservation> active_entries;
    const auto& sb = projected_scoreboard_;

    const auto stage_to_string = [](simrv::pipeline::PipelineStage s) -> const char* {
        switch (s) {
            case simrv::pipeline::PipelineStage::Fetch:
                return "IF ";
            case simrv::pipeline::PipelineStage::Decode:
                return "ID ";
            case simrv::pipeline::PipelineStage::Execute:
                return "EX ";
            case simrv::pipeline::PipelineStage::Memory:
                return "MEM";
            case simrv::pipeline::PipelineStage::Writeback:
                return "WB ";
            case simrv::pipeline::PipelineStage::Commit:
                return "CM ";
        }
        return "UNK";
    };

    // Check Integer bank
    for (size_t r = 1; r < 32; ++r) {
        auto reg = static_cast<simrv::xlen::RegId>(r);
        if (auto entry = sb.get_entry_data(simrv::pipeline::operation::RegBank::Integer, reg)) {
            active_entries.push_back({
                .bank_name = "INT",
                .reg_name = std::format("x{:<2}/{}", r, kRegNames.at(r)),
                .stage_name = stage_to_string(entry->stage),
                .latency = entry->latency,
                .can_forward = entry->can_forward,
            });
        }
    }
    // Check Float bank
    for (size_t r = 0; r < 32; ++r) {
        auto reg = static_cast<simrv::xlen::RegId>(r);
        if (auto entry = sb.get_entry_data(simrv::pipeline::operation::RegBank::Float, reg)) {
            active_entries.push_back({
                .bank_name = "FP ",
                .reg_name = std::format("f{:<2}/{}", r, kFpRegNames.at(r)),
                .stage_name = stage_to_string(entry->stage),
                .latency = entry->latency,
                .can_forward = entry->can_forward,
            });
        }
    }

    if (active_entries.empty()) {
        if (logical_row == 11) {
            return format_to_width(
                std::format("  {}No in-flight reservations (all register writes committed)\033[0m",
                            kThemeMuted),
                width);
        }
    } else {
        auto entry_idx = static_cast<size_t>(logical_row - 11);
        if (entry_idx < active_entries.size() && entry_idx < 6) {
            const auto& item = active_entries[entry_idx];
            std::string fwd_str = item.can_forward ? "\033[38;5;120m[CAN FWD]\033[0m"
                                                   : "\033[38;5;203m[STALL  ]\033[0m";
            return format_to_width(
                std::format("  {}[{}]\033[0m {}{:<10}\033[0m │ {}Stage: {}{}\033[0m │ "
                            "{}Lat:\033[0m {}{:<2}c\033[0m │ {}",
                            kThemeText, item.bank_name, kThemeVal, item.reg_name, kThemeText,
                            kThemeSky, item.stage_name, kThemeText, kThemePeach, item.latency,
                            fwd_str),
                width);
        }
    }

    if (logical_row == 17) {
        return section_line("Active RAW Hazard Interlock & Dependency Status", width);
    }
    if (logical_row == 18) {
        if (cpu.ca_pipeline.data_hazard_stall) {
            return format_to_width(
                std::format(
                    "  {}Status:\033[0m \033[38;5;203m[RAW STALL]\033[0m Pipeline interlock "
                    "active in Decode stage",
                    kThemeText),
                width);
        }
        return format_to_width(
            std::format("  {}Status:\033[0m \033[38;5;120m[FLOWING]\033[0m No data hazard stalls "
                        "this cycle",
                        kThemeText),
            width);
    }
    if (logical_row == 19) {
        uint32_t int_mask = sb.int_busy_mask();
        uint32_t fp_mask = sb.fp_busy_mask();
        uint32_t fwd_mask = sb.int_forwardable_mask();
        return format_to_width(
            std::format("  {}Scoreboard Bitmasks:\033[0m INT:{}0x{:08x}\033[0m │ "
                        "FP:{}0x{:08x}\033[0m │ Fwd:{}0x{:08x}\033[0m",
                        kThemeText, kThemeVal, int_mask, kThemeText, fp_mask, kThemeMint, fwd_mask),
            width);
    }
    if (logical_row == 20) {
        return section_line("Branch Predictor 2-Bit Saturation & Direction Confidence", width);
    }
    if (logical_row == 21) {
        uint8_t counter = cpu.branch_predictor.get_counter_for_pc(f_reg.pc);
        std::string gauge;
        switch (counter) {
            case 0:
                gauge = "\033[1;31m[░░░░] Strongly Not-Taken (0/3)\033[0m";
                break;
            case 1:
                gauge = "\033[33m[▓░░░] Weakly Not-Taken   (1/3)\033[0m";
                break;
            case 2:
                gauge = "\033[36m[▓▓▓░] Weakly Taken       (2/3)\033[0m";
                break;
            case 3:
            default:
                gauge = "\033[1;32m[▓▓▓▓] Strongly Taken     (3/3)\033[0m";
                break;
        }
        return format_to_width(
            std::format("  {}PC {:08x}:\033[0m {}  {}GHR: 0x{:03x}\033[0m", kThemeText, f_pc, gauge,
                        kThemeMuted, cpu.branch_predictor.ghr()),
            width);
    }
    if (logical_row == 22) {
        auto dist = cpu.branch_predictor.bht_distribution();
        return format_to_width(
            std::format("  {}PHT Distribution:\033[0m SNT:{:<5} WNT:{:<5} WT:{:<5} ST:{:<5}",
                        kThemeText, dist[0], dist[1], dist[2], dist[3]),
            width);
    }

    return format_to_width("", width);
}

}  // namespace simrv::tui
