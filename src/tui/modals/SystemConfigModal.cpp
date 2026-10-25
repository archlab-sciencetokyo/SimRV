#include "simrv/tui/modals/SystemConfigModal.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <utility>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/pipeline/CpuModel.hpp"
#include "simrv/pipeline/PipelineConfig.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/modals/ModalComponents.hpp"

namespace simrv::tui::modals {

namespace {

[[nodiscard]] constexpr auto to_pipeline_type(uint8_t raw) noexcept
    -> simrv::pipeline::PipelineType {
    if (raw == static_cast<uint8_t>(simrv::pipeline::PipelineType::ThreeStage)) {
        return simrv::pipeline::PipelineType::ThreeStage;
    }
    return simrv::pipeline::PipelineType::FiveStage;
}

[[nodiscard]] constexpr auto to_cpu_model_preset(uint8_t raw) noexcept
    -> simrv::pipeline::CpuModelPreset {
    switch (raw) {
        case static_cast<uint8_t>(simrv::pipeline::CpuModelPreset::Tiny):
            return simrv::pipeline::CpuModelPreset::Tiny;
        case static_cast<uint8_t>(simrv::pipeline::CpuModelPreset::Balanced):
            return simrv::pipeline::CpuModelPreset::Balanced;
        case static_cast<uint8_t>(simrv::pipeline::CpuModelPreset::Performance):
            return simrv::pipeline::CpuModelPreset::Performance;
        case static_cast<uint8_t>(simrv::pipeline::CpuModelPreset::Custom):
        default:
            return simrv::pipeline::CpuModelPreset::Custom;
    }
}

[[nodiscard]] constexpr auto to_branch_predictor_type(uint8_t raw) noexcept
    -> simrv::pipeline::BranchPredictorType {
    switch (raw) {
        case static_cast<uint8_t>(simrv::pipeline::BranchPredictorType::Static):
            return simrv::pipeline::BranchPredictorType::Static;
        case static_cast<uint8_t>(simrv::pipeline::BranchPredictorType::Bimodal):
            return simrv::pipeline::BranchPredictorType::Bimodal;
        case static_cast<uint8_t>(simrv::pipeline::BranchPredictorType::GShare):
            return simrv::pipeline::BranchPredictorType::GShare;
        case static_cast<uint8_t>(simrv::pipeline::BranchPredictorType::Tournament):
            return simrv::pipeline::BranchPredictorType::Tournament;
        case static_cast<uint8_t>(simrv::pipeline::BranchPredictorType::Disabled):
            return simrv::pipeline::BranchPredictorType::Disabled;
        default:
            return simrv::pipeline::BranchPredictorType::Static;
    }
}

constexpr int kSettingCount = 15;

void assign_pipeline(SysConfigDraft& draft, const simrv::pipeline::CpuModelConfig& model) {
    const auto& cfg = model.pipeline;
    draft.pipeline_type = static_cast<uint8_t>(cfg.pipeline_type);
    draft.mul_latency = cfg.mul_latency;
    draft.div_latency = cfg.div_latency;
    draft.fp_alu_latency = cfg.fp_alu_latency;
    draft.fp_div_latency = cfg.fp_div_latency;
    draft.csr_flush_penalty = cfg.csr_flush_penalty;
    draft.fence_flush_penalty = cfg.fence_flush_penalty;
    draft.enable_forwarding = cfg.enable_forwarding;
    draft.bpred_type = static_cast<uint8_t>(cfg.branch_predictor.type);
    draft.bht_entries = cfg.branch_predictor.bht_entries;
    draft.btb_entries = cfg.branch_predictor.btb_entries;
    draft.ras_entries = cfg.branch_predictor.ras_entries;
}

void assign_numeric(SysConfigDraft& draft, int cursor, uint32_t value) {
    switch (cursor) {
        case 1:
            draft.mul_latency = std::clamp(value, 0u, 20u);
            break;
        case 2:
            draft.div_latency = std::clamp(value, 0u, 100u);
            break;
        case 3:
            draft.fp_alu_latency = std::clamp(value, 0u, 20u);
            break;
        case 4:
            draft.fp_div_latency = std::clamp(value, 0u, 100u);
            break;
        case 5:
            draft.csr_flush_penalty = std::clamp(value, 0u, 20u);
            break;
        case 6:
            draft.fence_flush_penalty = std::clamp(value, 0u, 20u);
            break;
        case 9:
            draft.bht_entries = std::clamp(value, 16u, 65536u);
            break;
        case 10:
            draft.btb_entries = std::clamp(value, 16u, 8192u);
            break;
        case 11:
            draft.ras_entries = std::clamp(value, 2u, 256u);
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
    draft.preset = static_cast<uint8_t>(machine.primary_hart().cpu_model_config.preset);
    const auto& cfg = machine.primary_hart().pipeline_sim.config;
    draft.pipeline_type = static_cast<uint8_t>(cfg.pipeline_type);
    draft.mul_latency = cfg.mul_latency;
    draft.div_latency = cfg.div_latency;
    draft.fp_alu_latency = cfg.fp_alu_latency;
    draft.fp_div_latency = cfg.fp_div_latency;
    draft.csr_flush_penalty = cfg.csr_flush_penalty;
    draft.fence_flush_penalty = cfg.fence_flush_penalty;
    draft.enable_forwarding = cfg.enable_forwarding;
    draft.bpred_type = static_cast<uint8_t>(cfg.branch_predictor.type);
    draft.bht_entries = cfg.branch_predictor.bht_entries;
    draft.btb_entries = cfg.branch_predictor.btb_entries;
    draft.ras_entries = cfg.branch_predictor.ras_entries;
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
        const auto current_type = to_pipeline_type(draft.pipeline_type);
        draft.pipeline_type =
            static_cast<uint8_t>(current_type == simrv::pipeline::PipelineType::FiveStage
                                     ? simrv::pipeline::PipelineType::ThreeStage
                                     : simrv::pipeline::PipelineType::FiveStage);
        return;
    }
    if (index == 12) {
        constexpr int kProfiles = 4;
        const auto current_idx = std::to_underlying(to_cpu_model_preset(draft.preset));
        draft.preset = static_cast<uint8_t>((current_idx + dir + kProfiles) % kProfiles);
        const auto preset = to_cpu_model_preset(draft.preset);
        if (preset != simrv::pipeline::CpuModelPreset::Custom) {
            assign_pipeline(draft, simrv::pipeline::make_cpu_model_preset(preset));
        }
        return;
    }
    if (index == 7) {
        draft.enable_forwarding = !draft.enable_forwarding;
        return;
    }
    if (index == 8) {
        constexpr int kPredictorTypes = 5;
        const auto current_idx = std::to_underlying(to_branch_predictor_type(draft.bpred_type));
        draft.bpred_type =
            static_cast<uint8_t>((current_idx + dir + kPredictorTypes) % kPredictorTypes);
        return;
    }
    if (index == 9) {
        if (dir > 0)
            draft.bht_entries = std::min(65536u, draft.bht_entries * 2u);
        else if (dir < 0)
            draft.bht_entries = std::max(16u, draft.bht_entries / 2u);
        return;
    }
    if (index == 10) {
        if (dir > 0)
            draft.btb_entries = std::min(8192u, draft.btb_entries * 2u);
        else if (dir < 0)
            draft.btb_entries = std::max(16u, draft.btb_entries / 2u);
        return;
    }
    if (index == 11) {
        if (dir > 0)
            draft.ras_entries = std::min(256u, draft.ras_entries * 2u);
        else if (dir < 0)
            draft.ras_entries = std::max(2u, draft.ras_entries / 2u);
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
    if (!draft.cycle_accurate || cursor < 1 || (cursor > 6 && cursor < 9) || cursor > 11 ||
        digit < '0' || digit > '9' || input.size() >= 7) {
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
    auto model = machine.primary_hart().cpu_model_config;
    if (!apply_to_model(draft, model)) return false;
    for (size_t hart = 0; hart < machine.num_harts(); ++hart) {
        auto& cpu = machine.hart(hart);
        cpu.cpu_model_config = model;
        cpu.pipeline_sim.config = model.pipeline;
        (void)cpu.icache.configure(model.instruction_cache.capacity_bytes,
                                   model.instruction_cache.associativity);
        (void)cpu.dcache.configure(model.data_cache.capacity_bytes, model.data_cache.associativity);
        cpu.ca_state.reset_instruction();
        cpu.ca_pipeline.reset();
        cpu.branch_predictor.configure(cpu.pipeline_sim.config.branch_predictor);
        cpu.branch_predictor.reset();
    }
    machine.set_pipeline_type(to_pipeline_type(draft.pipeline_type));
    return true;
}

auto SystemConfigModal::apply_to_model(const SysConfigDraft& draft,
                                       simrv::pipeline::CpuModelConfig& model) -> bool {
    if (!draft.cycle_accurate) return true;
    const auto selected_preset = to_cpu_model_preset(draft.preset);
    if (selected_preset != simrv::pipeline::CpuModelPreset::Custom) {
        model = simrv::pipeline::make_cpu_model_preset(selected_preset);
    }
    auto& cfg = model.pipeline;
    {
        cfg.pipeline_type = to_pipeline_type(draft.pipeline_type);
        cfg.mul_latency = draft.mul_latency;
        cfg.div_latency = draft.div_latency;
        cfg.fp_alu_latency = draft.fp_alu_latency;
        cfg.fp_div_latency = draft.fp_div_latency;
        cfg.csr_flush_penalty = draft.csr_flush_penalty;
        cfg.fence_flush_penalty = draft.fence_flush_penalty;
        cfg.enable_forwarding = draft.enable_forwarding;
        cfg.branch_predictor.type = to_branch_predictor_type(draft.bpred_type);
        cfg.branch_predictor.bht_entries = draft.bht_entries;
        cfg.branch_predictor.btb_entries = draft.btb_entries;
        cfg.branch_predictor.ras_entries = draft.ras_entries;
    }
    // Explicit field editing retains the named preset only while it remains an exact preset.
    model.preset = selected_preset;
    return static_cast<bool>(model.validate());
}

void SystemConfigModal::render(std::vector<std::string>& content_rows,
                               const std::function<void(const std::string&)>& add_row,
                               const SysConfigDraft& draft, int cursor, const std::string& input) {
    (void)content_rows;
    if (!draft.cycle_accurate) {
        add_row(
            std::format("{}Disabled in IA Mode. Launch with --mode cycle-accurate to configure "
                        "cycle timing.\033[0m",
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
    auto entries = [&](uint32_t value, int index) {
        return (cursor == index && !input.empty())
                   ? std::format("\033[1;36m{} entries\033[0m \033[90m(input: {})\033[0m", value,
                                 input)
                   : std::format("\033[1;36m{} entries\033[0m", value);
    };
    const auto pipeline = to_pipeline_type(draft.pipeline_type);
    const auto bp_type = to_branch_predictor_type(draft.bpred_type);
    const auto settings = std::to_array<Item>({
        {.name = "Pipeline", .value = std::string(simrv::pipeline::pipeline_type_name(pipeline))},
        {.name = "Integer multiply stall cycles", .value = cycles(draft.mul_latency, 1)},
        {.name = "Integer divide stall cycles", .value = cycles(draft.div_latency, 2)},
        {.name = "FP ALU stall cycles", .value = cycles(draft.fp_alu_latency, 3)},
        {.name = "FP divide/sqrt stall cycles", .value = cycles(draft.fp_div_latency, 4)},
        {.name = "CSR serialization latency", .value = cycles(draft.csr_flush_penalty, 5)},
        {.name = "Fence serialization latency", .value = cycles(draft.fence_flush_penalty, 6)},
        {.name = "Integer forwarding", .value = draft.enable_forwarding ? "Enabled" : "Disabled"},
        {.name = "Branch predictor", .value = std::string(simrv::pipeline::to_string(bp_type))},
        {.name = "BHT capacity", .value = entries(draft.bht_entries, 9)},
        {.name = "BTB capacity", .value = entries(draft.btb_entries, 10)},
        {.name = "RAS capacity", .value = entries(draft.ras_entries, 11)},
        {.name = "FPGA core preset",
         .value = std::string(
             simrv::pipeline::cpu_model_preset_name(to_cpu_model_preset(draft.preset)))},
        {.name = "Save configuration", .value = "\033[1;32m[S] Export to .cfg...\033[0m"},
        {.name = "Load configuration", .value = "\033[1;36m[L] Import from .cfg...\033[0m"},
    });
    add_row(build_section_divider("Authoritative CA Policy", kThemeMint));
    for (size_t i = 0; i < settings.size(); ++i) {
        const bool selected = static_cast<int>(i) == cursor;
        add_row(build_menu_item_row(settings[i].name, settings[i].value, selected, 29));
    }
    const auto preset = to_cpu_model_preset(draft.preset);
    if (preset != simrv::pipeline::CpuModelPreset::Custom) {
        const auto model = simrv::pipeline::make_cpu_model_preset(preset);
        add_row(std::format("{}BRAM L1: I {} KiB/{}-way · D {} KiB/{}-way · {} B lines\033[0m",
                            kThemeMuted, model.instruction_cache.capacity_bytes / 1024,
                            model.instruction_cache.associativity,
                            model.data_cache.capacity_bytes / 1024, model.data_cache.associativity,
                            model.instruction_cache.line_bytes));
    }
}

}  // namespace simrv::tui::modals
