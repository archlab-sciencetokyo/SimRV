/**
 * @file SystemConfigModal.cpp
 * @brief Implementation of System Configuration modal dialog for CA and SMP parameters.
 */
#include "simrv/tui/modals/SystemConfigModal.hpp"

#include <algorithm>
#include <array>
#include <format>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/pipeline/PipelineSim.hpp"
#include "simrv/tui/TuiTheme.hpp"

namespace simrv::tui::modals {

void SystemConfigModal::open(SysConfigDraft& draft, int& cursor,
                             const simrv::core::Machine& machine) {
    cursor = 0;
    draft.cycle_accurate = machine.s_cycle_accurate;
    const auto& cfg = machine.cpu.pipeline_sim.config;
    draft.preset = 0;  // Default: Rocket
    draft.icache_miss_penalty = cfg.icache_miss_penalty;
    draft.dcache_miss_penalty = cfg.dcache_miss_penalty;
    draft.tlb_miss_penalty = cfg.tlb_miss_penalty;
    draft.mul_latency = cfg.mul_latency;
    draft.div_latency = cfg.div_latency;
    draft.fp_alu_latency = cfg.fp_alu_latency;
    draft.fp_div_latency = cfg.fp_div_latency;
    draft.csr_flush_penalty = cfg.csr_flush_penalty;
    draft.fence_flush_penalty = cfg.fence_flush_penalty;
    draft.branch_mispredict_penalty = cfg.branch_mispredict_penalty;
    draft.enable_forwarding = cfg.enable_forwarding;
    draft.enable_ex_forwarding = cfg.enable_ex_forwarding;
    draft.enable_mem_forwarding = cfg.enable_mem_forwarding;
    draft.bp_type = static_cast<uint8_t>(cfg.bp_type);
    draft.btb_entries = cfg.btb_entries;
}

void SystemConfigModal::move_cursor(const SysConfigDraft& draft, int& cursor, int delta) {
    if (!draft.cycle_accurate) {
        cursor = 0;
        return;
    }
    constexpr int total = 16;
    cursor = (cursor + delta + total) % total;
}

void SystemConfigModal::adjust_setting(SysConfigDraft& draft, int index, int dir) {
    if (!draft.cycle_accurate) return;

    switch (index) {
        case 0: {  // Preset
            int p = (static_cast<int>(draft.preset) + dir + 3) % 3;
            draft.preset = static_cast<uint8_t>(p);
            simrv::pipeline::CpuConfig tmp_cfg;
            tmp_cfg.apply_preset(static_cast<simrv::pipeline::CpuPreset>(p));
            draft.icache_miss_penalty = tmp_cfg.icache_miss_penalty;
            draft.dcache_miss_penalty = tmp_cfg.dcache_miss_penalty;
            draft.tlb_miss_penalty = tmp_cfg.tlb_miss_penalty;
            draft.mul_latency = tmp_cfg.mul_latency;
            draft.div_latency = tmp_cfg.div_latency;
            draft.fp_alu_latency = tmp_cfg.fp_alu_latency;
            draft.fp_div_latency = tmp_cfg.fp_div_latency;
            draft.csr_flush_penalty = tmp_cfg.csr_flush_penalty;
            draft.fence_flush_penalty = tmp_cfg.fence_flush_penalty;
            draft.branch_mispredict_penalty = tmp_cfg.branch_mispredict_penalty;
            draft.enable_forwarding = tmp_cfg.enable_forwarding;
            draft.enable_ex_forwarding = tmp_cfg.enable_ex_forwarding;
            draft.enable_mem_forwarding = tmp_cfg.enable_mem_forwarding;
            draft.bp_type = static_cast<uint8_t>(tmp_cfg.bp_type);
            draft.btb_entries = tmp_cfg.btb_entries;
            break;
        }
        case 1: {  // I-Cache miss penalty
            int v = static_cast<int>(draft.icache_miss_penalty) + dir;
            draft.icache_miss_penalty = std::clamp(v, 1, 100);
            break;
        }
        case 2: {  // D-Cache miss penalty
            int v = static_cast<int>(draft.dcache_miss_penalty) + dir;
            draft.dcache_miss_penalty = std::clamp(v, 1, 100);
            break;
        }
        case 3: {  // TLB miss penalty
            int v = static_cast<int>(draft.tlb_miss_penalty) + dir * 5;
            draft.tlb_miss_penalty = std::clamp(v, 1, 200);
            break;
        }
        case 4: {  // Mul latency
            int v = static_cast<int>(draft.mul_latency) + dir;
            draft.mul_latency = std::clamp(v, 1, 20);
            break;
        }
        case 5: {  // Div latency
            int v = static_cast<int>(draft.div_latency) + dir;
            draft.div_latency = std::clamp(v, 1, 100);
            break;
        }
        case 6: {  // FP ALU latency
            int v = static_cast<int>(draft.fp_alu_latency) + dir;
            draft.fp_alu_latency = std::clamp(v, 1, 20);
            break;
        }
        case 7: {  // FP Div/Sqrt latency
            int v = static_cast<int>(draft.fp_div_latency) + dir;
            draft.fp_div_latency = std::clamp(v, 1, 100);
            break;
        }
        case 8: {  // CSR flush penalty
            int v = static_cast<int>(draft.csr_flush_penalty) + dir;
            draft.csr_flush_penalty = std::clamp(v, 0, 20);
            break;
        }
        case 9: {  // FENCE flush penalty
            int v = static_cast<int>(draft.fence_flush_penalty) + dir;
            draft.fence_flush_penalty = std::clamp(v, 0, 20);
            break;
        }
        case 10: {  // Branch mispredict penalty
            int v = static_cast<int>(draft.branch_mispredict_penalty) + dir;
            draft.branch_mispredict_penalty = std::clamp(v, 1, 20);
            break;
        }
        case 11:
            draft.enable_forwarding = !draft.enable_forwarding;
            break;
        case 12:
            draft.enable_ex_forwarding = !draft.enable_ex_forwarding;
            break;
        case 13:
            draft.enable_mem_forwarding = !draft.enable_mem_forwarding;
            break;
        case 14: {  // Predictor type
            int t = (static_cast<int>(draft.bp_type) + dir + 5) % 5;
            draft.bp_type = static_cast<uint8_t>(t);
            break;
        }
        case 15: {  // BTB entries
            static constexpr std::array<uint32_t, 6> kBtbOpts = {32, 64, 128, 256, 512, 1024};
            auto it = std::find(kBtbOpts.begin(), kBtbOpts.end(), draft.btb_entries);
            int idx = (it != kBtbOpts.end())
                          ? static_cast<int>(std::distance(kBtbOpts.begin(), it))
                          : 2;
            idx = (idx + dir + static_cast<int>(kBtbOpts.size())) %
                  static_cast<int>(kBtbOpts.size());
            draft.btb_entries = kBtbOpts[static_cast<std::size_t>(idx)];
            break;
        }
        default:
            break;
    }
}

void SystemConfigModal::push_digit(SysConfigDraft& draft, int cursor, std::string& input,
                                   char digit) {
    if (digit < '0' || digit > '9') return;
    if (input.size() >= 7) return;

    if (!draft.cycle_accurate) return;
    if (cursor < 1 || (cursor > 10 && cursor != 15)) return;

    input.push_back(digit);
    uint32_t val = 0;
    try {
        val = static_cast<uint32_t>(std::stoul(input));
    } catch (...) {
        return;
    }

    switch (cursor) {
        case 1:
            draft.icache_miss_penalty = std::clamp(val, 1u, 100u);
            break;
        case 2:
            draft.dcache_miss_penalty = std::clamp(val, 1u, 100u);
            break;
        case 3:
            draft.tlb_miss_penalty = std::clamp(val, 1u, 200u);
            break;
        case 4:
            draft.mul_latency = std::clamp(val, 1u, 20u);
            break;
        case 5:
            draft.div_latency = std::clamp(val, 1u, 100u);
            break;
        case 6:
            draft.fp_alu_latency = std::clamp(val, 1u, 20u);
            break;
        case 7:
            draft.fp_div_latency = std::clamp(val, 1u, 100u);
            break;
        case 8:
            draft.csr_flush_penalty = std::clamp(val, 0u, 20u);
            break;
        case 9:
            draft.fence_flush_penalty = std::clamp(val, 0u, 20u);
            break;
        case 10:
            draft.branch_mispredict_penalty = std::clamp(val, 1u, 20u);
            break;
        case 15:
            draft.btb_entries = std::clamp(val, 1u, 4096u);
            break;
        default:
            break;
    }
}

void SystemConfigModal::pop_digit(SysConfigDraft& draft, int cursor, std::string& input) {
    if (input.empty()) return;
    input.pop_back();
    if (input.empty()) return;
    uint32_t val = 0;
    try {
        val = static_cast<uint32_t>(std::stoul(input));
    } catch (...) {
        return;
    }

    if (!draft.cycle_accurate) return;

    switch (cursor) {
        case 1:
            draft.icache_miss_penalty = std::clamp(val, 1u, 100u);
            break;
        case 2:
            draft.dcache_miss_penalty = std::clamp(val, 1u, 100u);
            break;
        case 3:
            draft.tlb_miss_penalty = std::clamp(val, 1u, 200u);
            break;
        case 4:
            draft.mul_latency = std::clamp(val, 1u, 20u);
            break;
        case 5:
            draft.div_latency = std::clamp(val, 1u, 100u);
            break;
        case 6:
            draft.fp_alu_latency = std::clamp(val, 1u, 20u);
            break;
        case 7:
            draft.fp_div_latency = std::clamp(val, 1u, 100u);
            break;
        case 8:
            draft.csr_flush_penalty = std::clamp(val, 0u, 20u);
            break;
        case 9:
            draft.fence_flush_penalty = std::clamp(val, 0u, 20u);
            break;
        case 10:
            draft.branch_mispredict_penalty = std::clamp(val, 1u, 20u);
            break;
        case 15:
            draft.btb_entries = std::clamp(val, 1u, 4096u);
            break;
        default:
            break;
    }
}

void SystemConfigModal::toggle_setting(SysConfigDraft& draft, int index) {
    adjust_setting(draft, index, 1);
}

auto SystemConfigModal::submit(const SysConfigDraft& draft, simrv::core::Machine& machine) -> bool {
    if (draft.cycle_accurate) {
        auto apply_to_cfg = [&](simrv::pipeline::CpuConfig& cfg) {
            cfg.icache_miss_penalty = draft.icache_miss_penalty;
            cfg.dcache_miss_penalty = draft.dcache_miss_penalty;
            cfg.tlb_miss_penalty = draft.tlb_miss_penalty;
            cfg.mul_latency = draft.mul_latency;
            cfg.div_latency = draft.div_latency;
            cfg.fp_alu_latency = draft.fp_alu_latency;
            cfg.fp_div_latency = draft.fp_div_latency;
            cfg.csr_flush_penalty = draft.csr_flush_penalty;
            cfg.fence_flush_penalty = draft.fence_flush_penalty;
            cfg.branch_mispredict_penalty = draft.branch_mispredict_penalty;
            cfg.enable_forwarding = draft.enable_forwarding;
            cfg.enable_ex_forwarding = draft.enable_ex_forwarding;
            cfg.enable_mem_forwarding = draft.enable_mem_forwarding;
            cfg.bp_type = static_cast<simrv::pipeline::BranchPredictorType>(draft.bp_type);
            cfg.btb_entries = draft.btb_entries;
        };

        apply_to_cfg(machine.cpu.pipeline_sim.config);
        for (size_t h = 0; h < machine.num_harts(); ++h) {
            apply_to_cfg(machine.hart(h).pipeline_sim.config);
        }
    }
    return true;
}

void SystemConfigModal::render(std::vector<std::string>& content_rows,
                               const std::function<void(const std::string&)>& add_row_cb,
                               const SysConfigDraft& draft, int cursor, const std::string& input) {
    (void)content_rows;

    struct ItemInfo {
        const char* name;
        std::string val;
    };

    if (draft.cycle_accurate) {
        add_row_cb(std::format(
            "{}Use \033[1m[↑/↓]\033[0m to navigate, \033[1m[←/→/Space/0-9]\033[0m to "
            "modify values, \033[1m[Enter]\033[0m to apply:\033[0m",
            kThemeMuted));
        add_row_cb("");

        static constexpr std::array<const char*, 3> kPresetNames = {
            "Rocket (5-Stage Standard)", "Embedded (3-Stage Minimal)", "Fast (Low-Overhead Timing)"};
        std::string preset_str = (draft.preset < 3) ? kPresetNames[draft.preset] : "Custom";

        static constexpr std::array<const char*, 5> kBpNames = {
            "Static Not-Taken", "Static Taken", "1-Bit Bimodal", "2-Bit Bimodal", "Gshare"};
        std::string bp_str = (draft.bp_type < 5) ? kBpNames[draft.bp_type] : "Unknown";

        std::string icache_val =
            (cursor == 1 && !input.empty())
                ? std::format("\033[1;36m{} cycles\033[0m \033[90m(input: {})\033[0m",
                              draft.icache_miss_penalty, input)
                : std::format("\033[1;36m{} cycles\033[0m", draft.icache_miss_penalty);
        std::string dcache_val =
            (cursor == 2 && !input.empty())
                ? std::format("\033[1;36m{} cycles\033[0m \033[90m(input: {})\033[0m",
                              draft.dcache_miss_penalty, input)
                : std::format("\033[1;36m{} cycles\033[0m", draft.dcache_miss_penalty);
        std::string tlb_val =
            (cursor == 3 && !input.empty())
                ? std::format("\033[1;36m{} cycles\033[0m \033[90m(input: {})\033[0m",
                              draft.tlb_miss_penalty, input)
                : std::format("\033[1;36m{} cycles\033[0m", draft.tlb_miss_penalty);
        std::string mul_val =
            (cursor == 4 && !input.empty())
                ? std::format("\033[1;33m{} cycles\033[0m \033[90m(input: {})\033[0m",
                              draft.mul_latency, input)
                : std::format("\033[1;33m{} cycles\033[0m", draft.mul_latency);
        std::string div_val =
            (cursor == 5 && !input.empty())
                ? std::format("\033[1;33m{} cycles\033[0m \033[90m(input: {})\033[0m",
                              draft.div_latency, input)
                : std::format("\033[1;33m{} cycles\033[0m", draft.div_latency);
        std::string fp_alu_val =
            (cursor == 6 && !input.empty())
                ? std::format("\033[1;35m{} cycles\033[0m \033[90m(input: {})\033[0m",
                              draft.fp_alu_latency, input)
                : std::format("\033[1;35m{} cycles\033[0m", draft.fp_alu_latency);
        std::string fp_div_val =
            (cursor == 7 && !input.empty())
                ? std::format("\033[1;35m{} cycles\033[0m \033[90m(input: {})\033[0m",
                              draft.fp_div_latency, input)
                : std::format("\033[1;35m{} cycles\033[0m", draft.fp_div_latency);
        std::string csr_val =
            (cursor == 8 && !input.empty())
                ? std::format("\033[1;32m{} cycles\033[0m \033[90m(input: {})\033[0m",
                              draft.csr_flush_penalty, input)
                : std::format("\033[1;32m{} cycles\033[0m", draft.csr_flush_penalty);
        std::string fence_val =
            (cursor == 9 && !input.empty())
                ? std::format("\033[1;32m{} cycles\033[0m \033[90m(input: {})\033[0m",
                              draft.fence_flush_penalty, input)
                : std::format("\033[1;32m{} cycles\033[0m", draft.fence_flush_penalty);
        std::string branch_val =
            (cursor == 10 && !input.empty())
                ? std::format("\033[1;31m{} cycles\033[0m \033[90m(input: {})\033[0m",
                              draft.branch_mispredict_penalty, input)
                : std::format("\033[1;31m{} cycles\033[0m", draft.branch_mispredict_penalty);
        std::string btb_val =
            (cursor == 15 && !input.empty())
                ? std::format("\033[1;36m{} entries\033[0m \033[90m(input: {})\033[0m",
                              draft.btb_entries, input)
                : std::format("\033[1;36m{} entries\033[0m", draft.btb_entries);

        const auto settings = std::to_array<ItemInfo>({
            {"Microarchitecture Preset", std::format("\033[1;32m{}\033[0m", preset_str)},
            {"I-Cache Miss Penalty", icache_val},
            {"D-Cache Miss Penalty", dcache_val},
            {"TLB Miss Penalty", tlb_val},
            {"Integer Mul Latency", mul_val},
            {"Integer Div Latency", div_val},
            {"FPU ALU Latency (FADD/FMUL)", fp_alu_val},
            {"FPU Div/Sqrt Latency (FDIV)", fp_div_val},
            {"CSR Pipeline Flush Penalty", csr_val},
            {"FENCE.I Flush Penalty", fence_val},
            {"Branch Mispredict Penalty", branch_val},
            {"Data Forwarding",
             draft.enable_forwarding ? "\033[1;32mEnabled\033[0m"
                                     : "\033[1;31mDisabled\033[0m"},
            {"EX Stage Forwarding",
             draft.enable_ex_forwarding ? "\033[1;32mEnabled\033[0m"
                                        : "\033[1;31mDisabled\033[0m"},
            {"MEM Stage Forwarding",
             draft.enable_mem_forwarding ? "\033[1;32mEnabled\033[0m"
                                         : "\033[1;31mDisabled\033[0m"},
            {"Branch Predictor Type", std::format("\033[1;35m{}\033[0m", bp_str)},
            {"BTB Entry Count", btb_val},
        });

        add_row_cb(std::format(
            "{}\033[1;36m── Pipeline & Microarchitecture (Cycle-Accurate Mode) ──\033[0m",
            kThemeText));
        for (std::size_t i = 0; i < settings.size(); ++i) {
            bool is_sel = (static_cast<int>(i) == cursor);
            std::string prefix = is_sel ? std::format("{}>\033[0m ", kThemeMint) : "  ";
            std::string name_str = std::format("{}{:<29}\033[0m",
                                               is_sel ? "\033[1;37m" : kThemeText, settings[i].name);
            add_row_cb(std::format("{}{} : {}", prefix, name_str, settings[i].val));
        }
    } else {
        add_row_cb(std::format("{}Cycle-accurate pipeline simulation is disabled.\033[0m",
                               kThemeMuted));
        add_row_cb(std::format(
            "{}Launch with {}--cycle-accurate\033[0m{} (-C) or toggle Simulation Mode "
            "in Settings {}[,]\033[0m{} to configure pipeline latencies.\033[0m",
            kThemeMuted, kThemeVal, kThemeMuted, kThemeVal, kThemeMuted));
        add_row_cb("");
        add_row_cb(std::format(
            "{}\033[1;90m── Pipeline & Microarchitecture (Disabled in IA Mode) ──\033[0m",
            kThemeMuted));
        add_row_cb(std::format("  \033[90m{:<29} : Disabled (IA Mode)\033[0m", "I/D Cache Latencies"));
        add_row_cb(std::format("  \033[90m{:<29} : Disabled (IA Mode)\033[0m", "FPU & Integer ALUs"));
        add_row_cb(std::format("  \033[90m{:<29} : Disabled (IA Mode)\033[0m", "Branch Predictor & BTB"));
        add_row_cb(std::format("  \033[90m{:<29} : Disabled (IA Mode)\033[0m", "Pipeline Forwarding"));
    }

    add_row_cb("");
    add_row_cb(
        std::format("{}Press \033[1m[Enter]\033[0m to apply settings, \033[1m[Esc]\033[0m "
                    "or \033[1m[q]\033[0m to cancel\033[0m",
                    kThemeMuted));
}

}  // namespace simrv::tui::modals
