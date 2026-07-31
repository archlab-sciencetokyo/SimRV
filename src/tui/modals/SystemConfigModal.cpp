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

void SystemConfigModal::push_digit(SysConfigDraft& draft, int cursor, std::string& input, char digit) {
    if (digit < '0' || digit > '9') return;
    if (cursor != 0 && cursor != 1 && cursor != 2 && cursor != 3 && cursor != 4 && cursor != 5 && cursor != 10) return;
    if (input.size() >= 5) return;
    input.push_back(digit);
    uint32_t val = 0;
    try {
        val = static_cast<uint32_t>(std::stoul(input));
    } catch (...) {
        return;
    }
    switch (cursor) {
        case 0: draft.icache_miss_penalty = std::clamp(val, 1u, 100u); break;
        case 1: draft.dcache_miss_penalty = std::clamp(val, 1u, 100u); break;
        case 2: draft.tlb_miss_penalty = std::clamp(val, 1u, 200u); break;
        case 3: draft.mul_latency = std::clamp(val, 1u, 20u); break;
        case 4: draft.div_latency = std::clamp(val, 1u, 100u); break;
        case 5: draft.branch_mispredict_penalty = std::clamp(val, 1u, 20u); break;
        case 10: draft.btb_entries = std::clamp(val, 1u, 4096u); break;
        default: break;
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
    switch (cursor) {
        case 0: draft.icache_miss_penalty = std::clamp(val, 1u, 100u); break;
        case 1: draft.dcache_miss_penalty = std::clamp(val, 1u, 100u); break;
        case 2: draft.tlb_miss_penalty = std::clamp(val, 1u, 200u); break;
        case 3: draft.mul_latency = std::clamp(val, 1u, 20u); break;
        case 4: draft.div_latency = std::clamp(val, 1u, 100u); break;
        case 5: draft.branch_mispredict_penalty = std::clamp(val, 1u, 20u); break;
        case 10: draft.btb_entries = std::clamp(val, 1u, 4096u); break;
        default: break;
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
                               const SysConfigDraft& draft, int cursor, const std::string& input) {
    (void)content_rows;
    add_row_cb(
        std::format("{}Use \033[1m[↑/↓]\033[0m to navigate, \033[1m[←/→/Space/0-9]\033[0m to "
                    "modify count, \033[1m[Enter]\033[0m to apply:\033[0m",
                    kThemeMuted));
    add_row_cb("");

    static constexpr std::array<const char*, 5> kBpNames = {
        "Static Not-Taken", "Static Taken", "1-Bit Bimodal", "2-Bit Bimodal", "Gshare"
    };
    std::string bp_str = (draft.bp_type < 5) ? kBpNames[draft.bp_type] : "Unknown";

    struct ItemInfo {
        const char* name;
        std::string val;
    };

    std::string icache_val = (cursor == 0 && !input.empty())
                                 ? std::format("\033[1;36m{} cycles\033[0m \033[90m(input: {})\033[0m", draft.icache_miss_penalty, input)
                                 : std::format("\033[1;36m{} cycles\033[0m", draft.icache_miss_penalty);
    std::string dcache_val = (cursor == 1 && !input.empty())
                                 ? std::format("\033[1;36m{} cycles\033[0m \033[90m(input: {})\033[0m", draft.dcache_miss_penalty, input)
                                 : std::format("\033[1;36m{} cycles\033[0m", draft.dcache_miss_penalty);
    std::string tlb_val = (cursor == 2 && !input.empty())
                               ? std::format("\033[1;36m{} cycles\033[0m \033[90m(input: {})\033[0m", draft.tlb_miss_penalty, input)
                               : std::format("\033[1;36m{} cycles\033[0m", draft.tlb_miss_penalty);
    std::string mul_val = (cursor == 3 && !input.empty())
                               ? std::format("\033[1;33m{} cycles\033[0m \033[90m(input: {})\033[0m", draft.mul_latency, input)
                               : std::format("\033[1;33m{} cycles\033[0m", draft.mul_latency);
    std::string div_val = (cursor == 4 && !input.empty())
                               ? std::format("\033[1;33m{} cycles\033[0m \033[90m(input: {})\033[0m", draft.div_latency, input)
                               : std::format("\033[1;33m{} cycles\033[0m", draft.div_latency);
    std::string branch_val = (cursor == 5 && !input.empty())
                                  ? std::format("\033[1;31m{} cycles\033[0m \033[90m(input: {})\033[0m", draft.branch_mispredict_penalty, input)
                                  : std::format("\033[1;31m{} cycles\033[0m", draft.branch_mispredict_penalty);
    std::string btb_val = (cursor == 10 && !input.empty())
                              ? std::format("\033[1;36m{} entries\033[0m \033[90m(input: {})\033[0m", draft.btb_entries, input)
                              : std::format("\033[1;36m{} entries\033[0m", draft.btb_entries);

    const auto settings = std::to_array<ItemInfo>({
        {"I-Cache Miss Penalty", icache_val},
        {"D-Cache Miss Penalty", dcache_val},
        {"TLB Miss Penalty", tlb_val},
        {"Multiplier Latency", mul_val},
        {"Divider Latency", div_val},
        {"Branch Mispredict Penalty", branch_val},
        {"Full Operand Forwarding", draft.enable_forwarding ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
        {"EX-Stage Forwarding", draft.enable_ex_forwarding ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
        {"MEM-Stage Forwarding", draft.enable_mem_forwarding ? "\033[1;32m[ON]\033[0m" : "\033[90m[OFF]\033[0m"},
        {"Branch Predictor Strategy", std::format("\033[1;35m[{}]\033[0m", bp_str)},
        {"BTB Table Entries", btb_val},
    });

    for (std::size_t i = 0; i < settings.size(); ++i) {
        bool is_sel = (static_cast<int>(i) == cursor);
        std::string prefix = is_sel ? std::format("{}>\033[0m ", kThemeMint) : "  ";
        std::string name_str = std::format(
            "{}{:<29}\033[0m", is_sel ? "\033[1;37m" : kThemeText, settings[i].name);
        add_row_cb(std::format("{}{} : {}", prefix, name_str, settings[i].val));
    }
    add_row_cb("");
    add_row_cb(
        std::format("{}Press \033[1m[Enter]\033[0m to apply settings, \033[1m[Esc]\033[0m "
                    "or \033[1m[q]\033[0m to cancel\033[0m",
                    kThemeMuted));
}

}  // namespace simrv::tui::modals
