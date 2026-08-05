/**
 * @file LeftPaneStats.cpp
 * @brief Performance statistics and pipeline counters pane rendering.
 */
#include <algorithm>
#include <array>
#include <format>
#include <string>
#include <vector>

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/debug/GdbStub.hpp"
#include "simrv/debug/SpikeLockstep.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/panels/LeftPane.hpp"
#include "simrv/util/FormatUtil.hpp"
#include "simrv/xlen/Helpers.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::tui {

using simrv::isa::Opcode;
using simrv::isa::OperationId;
using enum simrv::isa::OperationId;
using simrv::isa::InstFormat;

namespace {

enum class InstCategory : uint8_t { ALU, MEM, CTRL, SYS, VEC };

auto get_inst_category(int i) -> InstCategory {
    static constexpr std::array<InstCategory, 512> category_lut =
        []() -> std::array<InstCategory, 512> {
        std::array<InstCategory, 512> lut{};
        lut.fill(InstCategory::ALU);

        lut[OperationId::JAL] = InstCategory::CTRL;
        lut[OperationId::JALR] = InstCategory::CTRL;
        for (int op = OperationId::BEQ; op <= OperationId::BGEU; ++op) {
            lut[op] = InstCategory::CTRL;
        }

        for (int op = OperationId::LB; op <= OperationId::SD; ++op) {
            lut[op] = InstCategory::MEM;
        }
        lut[OperationId::LR_W] = InstCategory::MEM;
        lut[OperationId::SC_W] = InstCategory::MEM;
        for (int op = OperationId::AMOSWAP_W; op <= OperationId::AMOMAXU_W; ++op) {
            lut[op] = InstCategory::MEM;
        }

        lut[OperationId::ECALL] = InstCategory::SYS;
        lut[OperationId::EBREAK] = InstCategory::SYS;
        lut[OperationId::URET] = InstCategory::SYS;
        lut[OperationId::SRET] = InstCategory::SYS;
        lut[OperationId::MRET] = InstCategory::SYS;
        lut[OperationId::WFI] = InstCategory::SYS;
        lut[OperationId::SFENCE_VMA] = InstCategory::SYS;

        for (int op = OperationId::VSETVLI; op <= OperationId::VWSLL_VI; ++op) {
            lut[op] = InstCategory::VEC;
        }

        return lut;
    }();
    if (i >= 0 && i < 512) {
        return category_lut[i];
    }
    return InstCategory::ALU;
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

auto make_progress_bar(double ratio, int width, const std::string& color_code) -> std::string {
    int filled = static_cast<int>(ratio * width);
    if (filled < 0) filled = 0;
    if (filled > width) filled = width;
    std::string bar;
    bar += color_code;
    for (int i = 0; i < filled; ++i) {
        bar += "█";
    }
    bar += kThemeMuted;
    for (int i = filled; i < width; ++i) {
        bar += "░";
    }
    bar += "\033[0m";
    return bar;
}

}  // namespace

auto LeftPane::render_machine_performance_stats(const simrv::core::CPU& cpu, int adj_logical_row,
                                                int width) -> std::string {
    if (adj_logical_row >= 25 && adj_logical_row <= 30) {
        return render_machine_performance_stats_core(cpu, adj_logical_row, width);
    }
    return render_machine_performance_stats_sys(cpu, adj_logical_row, width);
}

auto LeftPane::render_machine_performance_stats_core(const simrv::core::CPU& cpu,
                                                     int adj_logical_row, int width)
    -> std::string {
    if (adj_logical_row == 25) {
        return section_line("Performance & Machine Stats", width);
    }
    if (adj_logical_row == 26) {
        std::string insns = std::format("  Executed Insns : {}{}\033[0m", kThemeMint,
                                        simrv::util::format_with_commas(cpu.e_icount));
        return format_to_width(insns, width);
    }
    if (adj_logical_row == 27) {
        double rtc_seconds = 0.0;
        if (cpu.machine_) {
            auto now = std::chrono::steady_clock::now();
            rtc_seconds = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(
                                                  now - cpu.machine_->s_start_time)
                                                  .count()) /
                          1000000.0;
        } else {
            rtc_seconds = static_cast<double>(cpu.clint_mmio.mtime) / 10000000.0;
        }
        std::string time =
            std::format("  Simulated Time : {}{:.6f} s\033[0m {}(mtime: 0x{:x})\033[0m", kThemeMint,
                        rtc_seconds, kThemeMuted, cpu.clint_mmio.mtime);
        return format_to_width(time, width);
    }
    if (adj_logical_row == 28) {
        std::string time =
            std::format("  Active Runtime : {}{:.6f} s\033[0m", kThemeMint, active_runtime_);
        return format_to_width(time, width);
    }
    if (adj_logical_row == 29) {
        std::string mode =
            std::format("  Simulation Mode: {}Functional (High-Perf)\033[0m", kThemeVal);
        return format_to_width(mode, width);
    }
    if (adj_logical_row == 30) {
        std::string extensions = simrv::xlen::resolve_misa_string(cpu.state().misa);
        std::string isa = std::format("  ISA Extensions : {}{}\033[0m", kThemeVal, extensions);
        return format_to_width(isa, width);
    }
    return format_to_width("", width);
}

auto LeftPane::render_machine_performance_stats_sys([[maybe_unused]] const simrv::core::CPU& cpu,
                                                    int adj_logical_row, int width) -> std::string {
    if (adj_logical_row == 31) {
        std::string mem_name = machine_.s_fn_memimg;
        auto pos = mem_name.find_last_of("/\\");
        if (pos != std::string::npos) mem_name = mem_name.substr(pos + 1);
        if (mem_name.empty()) mem_name = "None";
        std::string img = std::format("  Memory Image   : {}{}\033[0m", kThemeSky, mem_name);
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
        std::string dsk = std::format("  Disk Image     : {}{}\033[0m", kThemeSky, dsk_name);
        return format_to_width(dsk, width);
    }
    if (adj_logical_row == 33) {
        std::string suffix = std::format("] {:.1f} KIPS", static_cast<double>(kips_));
        std::string prefix = "  Current Speed  : [";
        int spark_width =
            width - static_cast<int>(prefix.length()) - static_cast<int>(suffix.length());
        if (spark_width < 5) spark_width = 5;

        std::string spark = get_sparkline_string(spark_width);
        std::string color =
            std::format("  {}Current Speed\033[0m  : [{}{}\033[0m] {}{:.1f} KIPS\033[0m",
                        kThemeText, kThemeMint, spark, kThemeMint, static_cast<double>(kips_));

        return format_to_width(color, width);
    }
    if (adj_logical_row == 34) {
        uint64_t max_val = max_kips_;
        for (auto val : kips_history_) {
            if (val > max_val) max_val = val;
        }
        double top_kips = static_cast<double>(max_val);
        double avg_kips = (active_runtime_ > 0.0)
                              ? (static_cast<double>(cpu.e_icount) / 1000.0 / active_runtime_)
                              : 0.0;
        std::string top_str = (top_kips >= 1000.0) ? std::format("{:.2f} MIPS", top_kips / 1000.0)
                                                   : std::format("{:.1f} KIPS", top_kips);
        std::string avg_str = (avg_kips >= 1000.0) ? std::format("{:.2f} MIPS", avg_kips / 1000.0)
                                                   : std::format("{:.1f} KIPS", avg_kips);

        std::string color =
            std::format("  {}Overall Speed\033[0m  : {}Top: {}\033[0m  │  {}Avg: {}\033[0m",
                        kThemeText, kThemeMint, top_str, kThemeMint, avg_str);

        return format_to_width(color, width);
    }
    return format_to_width("", width);
}

auto LeftPane::render_cycle_accurate_stats(const simrv::core::CPU& cpu, int adj_logical_row,
                                           int width) -> std::string {
    if (adj_logical_row >= 25 && adj_logical_row <= 29) {
        return render_cycle_accurate_core_stats(cpu, adj_logical_row, width);
    }
    if (adj_logical_row >= 30 && adj_logical_row <= 32) {
        return render_cycle_accurate_hazard_stats(cpu, adj_logical_row, width);
    }
    if (adj_logical_row >= 33 && adj_logical_row <= 34) {
        return render_cycle_accurate_mix_stats(cpu, adj_logical_row, width);
    }
    return render_cycle_accurate_hw_info(cpu, adj_logical_row, width);
}

auto LeftPane::render_cycle_accurate_core_stats(const simrv::core::CPU& cpu, int adj_logical_row,
                                                int width) -> std::string {
    if (adj_logical_row == 25) {
        return section_line("Statistics & Performance", width);
    }

    if (adj_logical_row == 26) {
        uint64_t i_hits = cpu.icache.hit_count();
        uint64_t i_misses = cpu.icache.miss_count();
        uint64_t i_total = i_hits + i_misses;
        double i_ratio =
            (i_total == 0) ? 0.0 : static_cast<double>(i_hits) / static_cast<double>(i_total);

        // In high-performance mode the fetch stage bypasses the ICache entirely;
        // report this explicitly instead of showing misleading 0% stats.
        const bool hp_mode = cpu.machine_ && cpu.machine_->s_high_performance;
        if (hp_mode && i_total == 0) {
            std::string color =
                std::format("  {}L1-I Cache\033[0m     : {}bypassed (high-perf mode)\033[0m",
                            kThemeText, kThemeMuted);
            return format_to_width(color, width);
        }

        std::string suffix = std::format(" {:5.1f}% (H:{} M:{})", i_ratio * 100.0,
                                         format_compact(i_hits), format_compact(i_misses));
        std::string prefix = "  L1-I Cache     : [";
        int bar_width =
            width - static_cast<int>(prefix.length()) - static_cast<int>(suffix.length()) - 1;
        if (bar_width < 5) bar_width = 5;

        std::string bar = make_progress_bar(i_ratio, bar_width, kThemeSky);
        std::string color =
            std::format("  {}L1-I Cache\033[0m     : [{}] {}{:5.1f}%\033[0m {}(H:{} M:{})\033[0m",
                        kThemeText, bar, kThemeSky, i_ratio * 100.0, kThemeMuted,
                        format_compact(i_hits), format_compact(i_misses));

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

        std::string bar = make_progress_bar(d_ratio, bar_width, kThemePink);
        std::string color =
            std::format("  {}L1-D Cache\033[0m     : [{}] {}{:5.1f}%\033[0m {}(H:{} M:{})\033[0m",
                        kThemeText, bar, kThemePink, d_ratio * 100.0, kThemeMuted,
                        format_compact(d_hits), format_compact(d_misses));

        return format_to_width(color, width);
    }

    uint64_t cycles = cpu.clint_mmio.mcycle;
    uint64_t icount = cpu.e_icount;
    double ipc = (cycles == 0) ? 0.0 : static_cast<double>(icount) / static_cast<double>(cycles);
    double cpi = (icount == 0) ? 0.0 : static_cast<double>(cycles) / static_cast<double>(icount);

    if (adj_logical_row == 28) {
        std::string color =
            std::format("  {}IPC / CPI\033[0m      : {}{:.2f} IPC\033[0m  /  {}{:.2f} CPI\033[0m",
                        kThemeText, kThemeMint, ipc, kThemePeach, cpi);
        return format_to_width(color, width);
    }

    uint64_t stalls = cpu.pipeline_sim.stall_cycles();
    uint64_t bubbles = cpu.pipeline_sim.bubble_cycles();
    uint64_t total_stalls_bubbles = stalls + bubbles;
    double stall_pct = (cycles == 0) ? 0.0
                                     : (static_cast<double>(total_stalls_bubbles) * 100.0) /
                                           static_cast<double>(cycles);

    if (adj_logical_row == 29) {
        std::string color = std::format(
            "  {}Stall Ratio\033[0m    : {}{:5.1f}%\033[0m (Stall:{} clk, Bubble:{} clk)",
            kThemeText, kThemeCoral, stall_pct, format_compact(stalls), format_compact(bubbles));
        return format_to_width(color, width);
    }

    return format_to_width("", width);
}

auto LeftPane::render_cycle_accurate_hazard_stats(const simrv::core::CPU& cpu, int adj_logical_row,
                                                  int width) -> std::string {
    uint64_t cycles = cpu.clint_mmio.mcycle;
    uint64_t data_stalls = cpu.pipeline_sim.data_hazard_stalls();
    uint64_t ctrl_bubbles = cpu.pipeline_sim.control_hazard_bubbles();
    uint64_t ic_stalls = cpu.pipeline_sim.icache_stalls();
    uint64_t dc_stalls = cpu.pipeline_sim.dcache_stalls();
    uint64_t cache_stalls = ic_stalls + dc_stalls;

    if (adj_logical_row == 30) {
        double data_pct = (cycles == 0) ? 0.0
                                        : (static_cast<double>(data_stalls) * 100.0) /
                                              static_cast<double>(cycles);
        std::string color = std::format(
            "    {}- Data RAW\033[0m   : {}{:>10}\033[0m clk {}({:5.1f}%)\033[0m", kThemeMuted,
            kThemePeach, simrv::util::format_with_commas(data_stalls), kThemeMuted, data_pct);
        return format_to_width(color, width);
    }

    if (adj_logical_row == 31) {
        double ctrl_pct = (cycles == 0) ? 0.0
                                        : (static_cast<double>(ctrl_bubbles) * 100.0) /
                                              static_cast<double>(cycles);
        std::string color = std::format(
            "    {}- Control\033[0m    : {}{:>10}\033[0m clk {}({:5.1f}%)\033[0m", kThemeMuted,
            kThemePeach, simrv::util::format_with_commas(ctrl_bubbles), kThemeMuted, ctrl_pct);
        return format_to_width(color, width);
    }

    if (adj_logical_row == 32) {
        double cache_pct = (cycles == 0) ? 0.0
                                         : (static_cast<double>(cache_stalls) * 100.0) /
                                               static_cast<double>(cycles);
        std::string color = std::format(
            "    {}- Cache\033[0m      : {}{:>10}\033[0m clk {}({:5.1f}%)\033[0m", kThemeMuted,
            kThemePeach, simrv::util::format_with_commas(cache_stalls), kThemeMuted, cache_pct);
        return format_to_width(color, width);
    }

    return format_to_width("", width);
}

auto LeftPane::render_cycle_accurate_mix_stats(const simrv::core::CPU& cpu, int adj_logical_row,
                                               int width) -> std::string {
    if (adj_logical_row == 33) {
        uint64_t max_val = 1;
        for (auto val : kips_history_) {
            if (val > max_val) max_val = val;
        }

        std::string suffix = std::format("] {} Max:{}", simrv::util::format_with_commas(kips_),
                                         simrv::util::format_with_commas(max_val));
        std::string prefix = "  Speed (KIPS)   : [";
        int spark_width =
            width - static_cast<int>(prefix.length()) - static_cast<int>(suffix.length());
        if (spark_width < 5) spark_width = 5;

        std::string spark = get_sparkline_string(spark_width);
        std::string color = std::format(
            "  {}Speed (KIPS)\033[0m   : [{}{}\033[0m] {}{}\033[0m {}{}Max:{}\033[0m", kThemeText,
            kThemeMint, spark, kThemeMint, simrv::util::format_with_commas(kips_), kThemeMuted,
            kThemeMuted, simrv::util::format_with_commas(max_val));

        return format_to_width(color, width);
    }

    if (adj_logical_row == 34) {
        uint64_t alu_count = 0;
        uint64_t mem_count = 0;
        uint64_t ctrl_count = 0;
        uint64_t sys_count = 0;
        uint64_t vec_count = 0;

        for (int i = 0; i < OperationIdCount; ++i) {
            uint64_t count = cpu.e_instmix.at(static_cast<std::size_t>(i));
            InstCategory cat = get_inst_category(i);
            if (cat == InstCategory::CTRL) {
                ctrl_count += count;
            } else if (cat == InstCategory::MEM) {
                mem_count += count;
            } else if (cat == InstCategory::SYS) {
                sys_count += count;
            } else if (cat == InstCategory::VEC) {
                vec_count += count;
            } else {
                alu_count += count;
            }
        }

        uint64_t total = alu_count + mem_count + ctrl_count + sys_count + vec_count;
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
        double vec_p = (total == 0)
                           ? 0.0
                           : static_cast<double>(vec_count * 100ULL) / static_cast<double>(total);

        // Build the inst mix line, suppressing zero-percentage categories
        std::string color = std::format("  {}Inst Mix\033[0m       :", kThemeText);
        auto append_cat = [&](const char* label, const char* col, double pct) -> void {
            if (pct < 0.05) return;  // suppress 0.0%
            color += std::format(" {}{}: {:.1f}%\033[0m", col, label, pct);
        };
        append_cat("ALU", kThemeMint, alu_p);
        append_cat("MEM", kThemeSky, mem_p);
        append_cat("CTRL", kThemePeach, ctrl_p);
        append_cat("SYS", kThemePink, sys_p);
        append_cat("VEC", kThemeCoral, vec_p);

        return format_to_width(color, width);
    }

    return format_to_width("", width);
}

auto LeftPane::render_cycle_accurate_hw_info(const simrv::core::CPU& cpu, int adj_logical_row,
                                             int width) -> std::string {
    if (adj_logical_row == 35) {
        return section_line("Machine & Hardware Info", width);
    }

    if (adj_logical_row == 36) {
        double sim_time_seconds = static_cast<double>(cpu.clint_mmio.mtime) / 10000000.0;
        std::string time =
            std::format("  Simulated Time : {}{:.6f} s\033[0m {}(0x{:x})\033[0m", kThemeMint,
                        sim_time_seconds, kThemeMuted, cpu.clint_mmio.mtime);
        return format_to_width(time, width);
    }

    if (adj_logical_row == 37) {
        std::string time =
            std::format("  Active Runtime : {}{:.6f} s\033[0m", kThemeMint, active_runtime_);
        return format_to_width(time, width);
    }

    if (adj_logical_row == 38) {
        std::string fwd_status = cpu.pipeline_sim.config.enable_forwarding ? "ON" : "OFF";
        std::string mode =
            std::format("  Simulation Mode: {}Cycle-Accurate (CA, forwarding: {})\033[0m",
                        kThemeVal, fwd_status);
        return format_to_width(mode, width);
    }

    if (adj_logical_row == 39) {
        std::string extensions = simrv::xlen::resolve_misa_string(cpu.state().misa);
        std::string isa = std::format("  ISA Extensions : {}{}\033[0m", kThemeVal, extensions);
        return format_to_width(isa, width);
    }

    if (adj_logical_row == 40) {
        std::string mem_name = machine_.s_fn_memimg;
        auto pos = mem_name.find_last_of("/\\");
        if (pos != std::string::npos) mem_name = mem_name.substr(pos + 1);
        if (mem_name.empty()) mem_name = "None";
        std::string img = std::format("  Memory Image   : {}{}\033[0m", kThemeSky, mem_name);
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
        std::string dsk = std::format("  Disk Image     : {}{}\033[0m", kThemeSky, dsk_name);
        return format_to_width(dsk, width);
    }

    if (adj_logical_row == 42) {
        return format_to_width("", width);
    }

    return format_to_width("", width);
}

auto LeftPane::render_debug_state(int debug_row, int width) -> std::string {
    auto const& cpu = machine_.cpu;
    auto const& st = cpu.state();
    int col_width = width / 2;
    int right_width = width - col_width;

    if (debug_row == 0) {
        return section_line("Debug Diagnostics", width);
    }
    if (debug_row == 1) {
        std::string sym = machine_.symbols.lookup(st.pc);
        if (sym.empty()) {
            sym = "none";
        } else {
            sym = "<" + sym + ">";
        }
        return format_to_width(
            std::format(" {}{:<8}\033[0m: {}{}\033[0m", kThemeText, "symbol", kThemePeach, sym),
            width);
    }
    if (debug_row == 2) {
        std::string gdb_status = "disabled";
        if (machine_.gdb_stub) {
            gdb_status = machine_.gdb_stub->is_connected() ? "connected" : "listening";
        }
        std::string lockstep_status = machine_.spike_lockstep ? "active" : "disabled";
        return render_pair("gdb_stub", gdb_status, machine_.gdb_stub ? kThemeMint : kThemeMuted,
                           "lockstep", lockstep_status,
                           machine_.spike_lockstep ? kThemeMint : kThemeMuted, col_width,
                           right_width, 8);
    }
    if (debug_row == 3) {
        std::string tohost_str = std::format("0x{:x}", machine_.tohost);
        std::string traplog_status = machine_.s_traplog_mode ? "active" : "disabled";
        return render_pair("tohost", tohost_str, machine_.tohost != 0 ? kThemePeach : kThemeVal,
                           "traplog", traplog_status,
                           machine_.s_traplog_mode ? kThemeMint : kThemeMuted, col_width,
                           right_width, 8);
    }
    return format_to_width("", width);
}

auto LeftPane::render_perf_or_debug(const simrv::core::CPU& cpu, int logical_row, int width,
                                    bool single_column) -> std::string {
    int const total_logical_rows = get_total_rows(width);
    int const adj_base_rows = total_logical_rows - (machine_.s_debug_mode ? 4 : 0);

    if (machine_.s_debug_mode && logical_row >= adj_base_rows && logical_row < total_logical_rows) {
        return render_debug_state(logical_row - adj_base_rows, width);
    }

    int const adj_logical_row =
        (single_column && logical_row >= 32) ? (logical_row - 16) : logical_row;

    if (!machine_.s_cycle_accurate) {
        return render_machine_performance_stats(cpu, adj_logical_row, width);
    } else {
        return render_cycle_accurate_stats(cpu, adj_logical_row, width);
    }
}

auto LeftPane::get_sparkline_string(int width) -> std::string {
    if (kips_history_.empty()) {
        std::string res(static_cast<std::size_t>(width), ' ');
        return res;
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

}  // namespace simrv::tui
