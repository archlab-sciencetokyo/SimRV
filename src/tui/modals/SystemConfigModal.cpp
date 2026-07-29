/**
 * @file SystemConfigModal.cpp
 * @brief Implementation of System Configuration modal dialog for Cycle-Accurate mode.
 */
#include "simrv/tui/modals/SystemConfigModal.hpp"

#include <array>
#include <format>
#include <algorithm>
#include "simrv/core/Machine.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/pipeline/PipelineSim.hpp"
#include "simrv/tui/TuiTheme.hpp"

namespace simrv::tui::modals {

namespace {

struct SysSettingItem {
    const char* key;
    const char* name;
    std::string val;
};

}  // namespace

void SystemConfigModal::open(SysConfigDraft& draft, int& cursor, const simrv::core::Machine& machine) {
    cursor = 0;
    const auto& cfg = machine.cpu.pipeline_sim.config;
    draft.icache_miss_penalty = cfg.icache_miss_penalty;
    draft.dcache_miss_penalty = cfg.dcache_miss_penalty;
    draft.tlb_miss_penalty = cfg.tlb_miss_penalty;
    draft.mul_latency = cfg.mul_latency;
    draft.div_latency = cfg.div_latency;
    draft.branch_mispredict_penalty = cfg.branch_mispredict_penalty;
    draft.enable_forwarding = cfg.enable_forwarding;
    draft.enable_ex_forwarding = cfg.enable_ex_forwarding;
    draft.enable_mem_forwarding = cfg.enable_mem_forwarding;
    draft.bp_type = static_cast<uint8_t>(cfg.bp_type);
    draft.btb_entries = cfg.btb_entries;
}

void SystemConfigModal::move_cursor(int& cursor, int delta) {
    constexpr int kNumSettings = 11;
    cursor = (cursor + delta + kNumSettings) % kNumSettings;
}

void SystemConfigModal::adjust_setting(SysConfigDraft& draft, int index, int dir) {
    switch (index) {
        case 0: { // I-Cache miss penalty
            int v = static_cast<int>(draft.icache_miss_penalty) + dir;
            draft.icache_miss_penalty = std::clamp(v, 1, 100);
            break;
        }
        case 1: { // D-Cache miss penalty
            int v = static_cast<int>(draft.dcache_miss_penalty) + dir;
            draft.dcache_miss_penalty = std::clamp(v, 1, 100);
            break;
        }
        case 2: { // TLB miss penalty
            int v = static_cast<int>(draft.tlb_miss_penalty) + dir * 5;
            draft.tlb_miss_penalty = std::clamp(v, 1, 200);
            break;
        }
        case 3: { // Mul latency
            int v = static_cast<int>(draft.mul_latency) + dir;
            draft.mul_latency = std::clamp(v, 1, 20);
            break;
        }
        case 4: { // Div latency
            int v = static_cast<int>(draft.div_latency) + dir;
            draft.div_latency = std::clamp(v, 1, 100);
            break;
        }
        case 5: { // Branch mispredict penalty
            int v = static_cast<int>(draft.branch_mispredict_penalty) + dir;
            draft.branch_mispredict_penalty = std::clamp(v, 1, 20);
            break;
        }
        case 6:
            draft.enable_forwarding = !draft.enable_forwarding;
            break;
        case 7:
            draft.enable_ex_forwarding = !draft.enable_ex_forwarding;
            break;
        case 8:
            draft.enable_mem_forwarding = !draft.enable_mem_forwarding;
            break;
        case 9: { // Predictor type
            int t = (static_cast<int>(draft.bp_type) + dir + 5) % 5;
            draft.bp_type = static_cast<uint8_t>(t);
            break;
        }
        case 10: { // BTB entries
            static constexpr std::array<uint32_t, 6> kBtbOpts = {32, 64, 128, 256, 512, 1024};
            auto it = std::find(kBtbOpts.begin(), kBtbOpts.end(), draft.btb_entries);
            int idx = (it != kBtbOpts.end()) ? static_cast<int>(std::distance(kBtbOpts.begin(), it)) : 2;
            idx = (idx + dir + static_cast<int>(kBtbOpts.size())) % static_cast<int>(kBtbOpts.size());
            draft.btb_entries = kBtbOpts[static_cast<std::size_t>(idx)];
            break;
        }
        default:
            break;
    }
}

void SystemConfigModal::toggle_setting(SysConfigDraft& draft, int index) {
    adjust_setting(draft, index, 1);
}

auto SystemConfigModal::submit(const SysConfigDraft& draft, simrv::core::Machine& machine) -> bool {
    auto& cfg = machine.cpu.pipeline_sim.config;
    cfg.icache_miss_penalty = draft.icache_miss_penalty;
    cfg.dcache_miss_penalty = draft.dcache_miss_penalty;
    cfg.tlb_miss_penalty = draft.tlb_miss_penalty;
    cfg.mul_latency = draft.mul_latency;
    cfg.div_latency = draft.div_latency;
    cfg.branch_mispredict_penalty = draft.branch_mispredict_penalty;
    cfg.enable_forwarding = draft.enable_forwarding;
    cfg.enable_ex_forwarding = draft.enable_ex_forwarding;
    cfg.enable_mem_forwarding = draft.enable_mem_forwarding;
    cfg.bp_type = static_cast<simrv::pipeline::BranchPredictorType>(draft.bp_type);
    cfg.btb_entries = draft.btb_entries;
    return true;
}

void SystemConfigModal::render(std::vector<std::string>& content_rows,
                               const std::function<void(const std::string&)>& add_row_cb,
                               const SysConfigDraft& draft, int cursor) {
    (void)content_rows;
    add_row_cb(
        std::format("{}Use \033[1m[↑/↓]\033[0m to navigate, \033[1m[←/→/Space]\033[0m or \033[1m[1-9,a,b]\033[0m to "
                    "modify, \033[1m[Enter]\033[0m to apply:\033[0m",
                    kThemeMuted));
    add_row_cb("");

    static constexpr std::array<const char*, 5> kBpNames = {
        "Static Not-Taken", "Static Taken", "1-Bit Bimodal", "2-Bit Bimodal", "Gshare"
    };
    std::string bp_str = (draft.bp_type < 5) ? kBpNames[draft.bp_type] : "Unknown";

    const auto settings = std::to_array<SysSettingItem>({
        {" 1", "I-Cache Miss Penalty", std::format("\033[1;36m{} cycles\033[0m", draft.icache_miss_penalty)},
        {" 2", "D-Cache Miss Penalty", std::format("\033[1;36m{} cycles\033[0m", draft.dcache_miss_penalty)},
        {" 3", "TLB Miss Penalty", std::format("\033[1;36m{} cycles\033[0m", draft.tlb_miss_penalty)},
        {" 4", "Multiplier Latency", std::format("\033[1;33m{} cycles\033[0m", draft.mul_latency)},
        {" 5", "Divider Latency", std::format("\033[1;33m{} cycles\033[0m", draft.div_latency)},
        {" 6", "Branch Mispredict Penalty", std::format("\033[1;31m{} cycles\033[0m", draft.branch_mispredict_penalty)},
        {" 7", "Full Operand Forwarding", draft.enable_forwarding ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
        {" 8", "EX-Stage Forwarding", draft.enable_ex_forwarding ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
        {" 9", "MEM-Stage Forwarding", draft.enable_mem_forwarding ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
        {" a", "Branch Predictor Strategy", std::format("\033[1;35m[{}]\033[0m", bp_str)},
        {" b", "BTB Table Entries", std::format("\033[1;36m{} entries\033[0m", draft.btb_entries)},
    });

    for (std::size_t i = 0; i < settings.size(); ++i) {
        bool is_sel = (static_cast<int>(i) == cursor);
        std::string prefix = is_sel ? std::format("{}>\033[0m ", kThemeMint) : "  ";
        std::string num_key =
            std::format("{}[{}]\033[0m", is_sel ? kThemeMint : kThemeSky, settings[i].key);
        std::string name_str = std::format(
            "{}{:<27}\033[0m", is_sel ? "\033[1;37m" : kThemeText, settings[i].name);
        add_row_cb(std::format("{}{} {} : {}", prefix, num_key, name_str, settings[i].val));
    }
    add_row_cb("");
    add_row_cb(
        std::format("{}Press \033[1m[Enter]\033[0m to apply settings, \033[1m[Esc]\033[0m "
                    "or \033[1m[q]\033[0m to cancel\033[0m",
                    kThemeMuted));
}

}  // namespace simrv::tui::modals
