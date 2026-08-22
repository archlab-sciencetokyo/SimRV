#include "simrv/tui/modals/SystemConfigModal.hpp"

#include <algorithm>
#include <array>
#include <format>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/pipeline/PipelineConfig.hpp"
#include "simrv/tui/TuiTheme.hpp"

namespace simrv::tui::modals {

namespace {

constexpr int kSettingCount = 8;

void assign_numeric(SysConfigDraft& draft, int cursor, uint32_t value) {
    switch (cursor) {
        case 1:
            draft.mul_latency = std::clamp(value, 1u, 20u);
            break;
        case 2:
            draft.div_latency = std::clamp(value, 1u, 100u);
            break;
        case 3:
            draft.fp_alu_latency = std::clamp(value, 1u, 20u);
            break;
        case 4:
            draft.fp_div_latency = std::clamp(value, 1u, 100u);
            break;
        case 5:
            draft.csr_flush_penalty = std::clamp(value, 0u, 20u);
            break;
        case 6:
            draft.fence_flush_penalty = std::clamp(value, 0u, 20u);
            break;
        default:
            break;
    }
}

}  // namespace

void SystemConfigModal::open(SysConfigDraft& draft, int& cursor,
                             const simrv::core::Machine& machine) {
    cursor = 0;
    draft.cycle_accurate = machine.runtime_profile.is_cycle_mode();
    const auto& cfg = machine.cpu.pipeline_sim.config;
    draft.pipeline_type = static_cast<uint8_t>(cfg.pipeline_type);
    draft.mul_latency = cfg.mul_latency;
    draft.div_latency = cfg.div_latency;
    draft.fp_alu_latency = cfg.fp_alu_latency;
    draft.fp_div_latency = cfg.fp_div_latency;
    draft.csr_flush_penalty = cfg.csr_flush_penalty;
    draft.fence_flush_penalty = cfg.fence_flush_penalty;
    draft.enable_forwarding = cfg.enable_forwarding;
}

void SystemConfigModal::move_cursor(const SysConfigDraft& draft, int& cursor, int delta) {
    if (!draft.cycle_accurate) {
        cursor = 0;
        return;
    }
    cursor = (cursor + delta + kSettingCount) % kSettingCount;
}

void SystemConfigModal::adjust_setting(SysConfigDraft& draft, int index, int dir) {
    if (!draft.cycle_accurate) return;
    if (index == 0) {
        draft.pipeline_type = static_cast<uint8_t>(draft.pipeline_type == 0 ? 1 : 0);
        return;
    }
    if (index == 7) {
        draft.enable_forwarding = !draft.enable_forwarding;
        return;
    }
    const std::array<uint32_t*, 6> values = {&draft.mul_latency,       &draft.div_latency,
                                             &draft.fp_alu_latency,    &draft.fp_div_latency,
                                             &draft.csr_flush_penalty, &draft.fence_flush_penalty};
    const auto current = *values.at(static_cast<size_t>(index - 1));
    const auto adjusted = static_cast<uint32_t>(std::max(0, static_cast<int>(current) + dir));
    assign_numeric(draft, index, adjusted);
}

void SystemConfigModal::push_digit(SysConfigDraft& draft, int cursor, std::string& input,
                                   char digit) {
    if (!draft.cycle_accurate || cursor < 1 || cursor > 6 || digit < '0' || digit > '9' ||
        input.size() >= 7) {
        return;
    }
    input.push_back(digit);
    try {
        assign_numeric(draft, cursor, static_cast<uint32_t>(std::stoul(input)));
    } catch (...) {
    }
}

void SystemConfigModal::pop_digit(SysConfigDraft& draft, int cursor, std::string& input) {
    if (!draft.cycle_accurate || input.empty()) return;
    input.pop_back();
    if (input.empty()) return;
    try {
        assign_numeric(draft, cursor, static_cast<uint32_t>(std::stoul(input)));
    } catch (...) {
    }
}

void SystemConfigModal::toggle_setting(SysConfigDraft& draft, int index) {
    adjust_setting(draft, index, 1);
}

auto SystemConfigModal::submit(const SysConfigDraft& draft, simrv::core::Machine& machine) -> bool {
    if (!draft.cycle_accurate) return true;
    auto apply = [&](simrv::pipeline::CpuConfig& cfg) {
        cfg.pipeline_type = static_cast<simrv::pipeline::PipelineType>(draft.pipeline_type);
        cfg.mul_latency = draft.mul_latency;
        cfg.div_latency = draft.div_latency;
        cfg.fp_alu_latency = draft.fp_alu_latency;
        cfg.fp_div_latency = draft.fp_div_latency;
        cfg.csr_flush_penalty = draft.csr_flush_penalty;
        cfg.fence_flush_penalty = draft.fence_flush_penalty;
        cfg.enable_forwarding = draft.enable_forwarding;
    };
    machine.s_pipeline_type = static_cast<simrv::pipeline::PipelineType>(draft.pipeline_type);
    apply(machine.cpu.pipeline_sim.config);
    for (size_t hart = 0; hart < machine.num_harts(); ++hart) {
        apply(machine.hart(hart).pipeline_sim.config);
    }
    return true;
}

void SystemConfigModal::render(std::vector<std::string>& content_rows,
                               const std::function<void(const std::string&)>& add_row,
                               const SysConfigDraft& draft, int cursor, const std::string& input) {
    (void)content_rows;
    if (!draft.cycle_accurate) {
        add_row(
            std::format("{}Disabled in IA Mode. Launch with --ca to configure cycle timing.\033[0m",
                        kThemeMuted));
        return;
    }
    add_row(std::format("{}Use arrows or digits to edit; Enter applies to every hart.\033[0m",
                        kThemeMuted));
    add_row("");
    struct Item {
        const char* name;
        std::string value;
    };
    auto cycles = [&](uint32_t value, int index) {
        return (cursor == index && !input.empty())
                   ? std::format("\033[1;36m{} cycles\033[0m \033[90m(input: {})\033[0m", value,
                                 input)
                   : std::format("\033[1;36m{} cycles\033[0m", value);
    };
    const auto pipeline = static_cast<simrv::pipeline::PipelineType>(draft.pipeline_type);
    const auto settings = std::to_array<Item>({
        {"Pipeline", std::string(simrv::pipeline::pipeline_type_name(pipeline))},
        {"Integer multiply latency", cycles(draft.mul_latency, 1)},
        {"Integer divide latency", cycles(draft.div_latency, 2)},
        {"FP ALU latency", cycles(draft.fp_alu_latency, 3)},
        {"FP divide/sqrt latency", cycles(draft.fp_div_latency, 4)},
        {"CSR serialization latency", cycles(draft.csr_flush_penalty, 5)},
        {"Fence serialization latency", cycles(draft.fence_flush_penalty, 6)},
        {"Integer forwarding", draft.enable_forwarding ? "Enabled" : "Disabled"},
    });
    add_row(std::format("{}\033[1;36m── Authoritative CA Policy ──\033[0m", kThemeText));
    for (size_t i = 0; i < settings.size(); ++i) {
        const bool selected = static_cast<int>(i) == cursor;
        add_row(std::format("{}{}{:<29}\033[0m : {}", selected ? "> " : "  ",
                            selected ? "\033[1;37m" : kThemeText, settings[i].name,
                            settings[i].value));
    }
}

}  // namespace simrv::tui::modals
