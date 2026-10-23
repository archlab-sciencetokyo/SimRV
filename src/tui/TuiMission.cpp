/** @file TuiMission.cpp */
#include "simrv/tui/TuiMission.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <fstream>

namespace simrv::tui {
namespace {

auto trim(std::string_view value) -> std::string_view {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.remove_suffix(1);
    return value;
}

auto unquote(std::string_view value) -> std::optional<std::string> {
    value = trim(value);
    if (value.size() < 2 || value.front() != '"' || value.back() != '"') return std::nullopt;
    value.remove_prefix(1);
    value.remove_suffix(1);
    std::string decoded;
    decoded.reserve(value.size());
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] != '\\') {
            decoded.push_back(value[i]);
            continue;
        }
        if (++i >= value.size()) return std::nullopt;
        switch (value[i]) {
            case '"': decoded.push_back('"'); break;
            case '\\': decoded.push_back('\\'); break;
            case 'n': decoded.push_back('\n'); break;
            case 'r': decoded.push_back('\r'); break;
            case 't': decoded.push_back('\t'); break;
            case 'b': decoded.push_back('\b'); break;
            case 'f': decoded.push_back('\f'); break;
            default: return std::nullopt;
        }
    }
    return decoded;
}

auto page_for(std::string_view value) -> std::optional<TuiRegPage> {
    if (value == "registers") return TuiRegPage::GPR;
    if (value == "stack") return TuiRegPage::STACK;
    if (value == "trace") return TuiRegPage::TRACE;
    if (value == "explain") return TuiRegPage::EXPLAIN;
    if (value == "pipeline") return TuiRegPage::PIPELINE;
    if (value == "cache") return TuiRegPage::CACHE;
    if (value == "tlb") return TuiRegPage::TLB;
    if (value == "hazard") return TuiRegPage::HAZARD;
    if (value == "branch-predictor") return TuiRegPage::BPRED;
    if (value == "bus") return TuiRegPage::BUS;
    if (value == "console") return TuiRegPage::CONSOLE;
    if (value == "display") return TuiRegPage::DISPLAY;
    if (value == "live") return TuiRegPage::LIVE;
    return std::nullopt;
}

auto basename(std::string_view path) -> std::string_view {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

}  // namespace

auto MissionProgress::load(std::string_view mission_path) -> bool {
    mission_path_ = mission_path;
    expected_program_.clear();
    description_.clear();
    goal_.clear();
    detail_.clear();
    steps_.clear();
    checkpoints_.clear();
    std::ifstream input(mission_path_);
    if (!input) {
        detail_ = std::format("Cannot open mission file: {}", mission_path_);
        return false;
    }

    enum class Section { None, Mission, Step, Checkpoint };
    Section section = Section::None;
    MissionStep step{};
    bool step_open = false;
    bool step_has_view = false;
    MissionCheckpoint checkpoint{};
    bool checkpoint_open = false;
    bool saw_version = false;
    size_t line_number = 0;
    auto fail = [&](std::string_view reason) {
        detail_ = std::format("Mission file line {}: {}", line_number, reason);
        return false;
    };
    auto finish_step = [&]() -> bool {
        if (!step_open) return true;
        if (step.id.empty() || step.guidance.title.empty() || step.guidance.prompt.empty() ||
            step.guidance.why.empty() || (!step_has_view && step.guidance.panels.empty())) {
            detail_ = "Each [step] requires label, title, prompt, why, and either view or panels.";
            return false;
        }
        steps_.push_back(std::move(step));
        step = {};
        step_open = false;
        step_has_view = false;
        return true;
    };
    auto finish_checkpoint = [&]() -> bool {
        if (!checkpoint_open) return true;
        if (checkpoint.symbol.empty() || checkpoint.after_step == 0) {
            detail_ = "Each [checkpoint] requires label and after_step.";
            return false;
        }
        checkpoints_.push_back(std::move(checkpoint));
        checkpoint = {};
        checkpoint_open = false;
        return true;
    };

    std::string line;
    while (std::getline(input, line)) {
        ++line_number;
        const auto text = trim(line);
        if (text.empty() || text.starts_with('#')) continue;
        if (text == "[mission]" || text == "[step]" || text == "[checkpoint]") {
            if (!finish_step() || !finish_checkpoint()) return false;
            section = text == "[mission]" ? Section::Mission
                      : text == "[step]" ? Section::Step
                                          : Section::Checkpoint;
            step_open = section == Section::Step;
            checkpoint_open = section == Section::Checkpoint;
            step_has_view = false;
            continue;
        }
        const auto equals = text.find('=');
        if (equals == std::string_view::npos) return fail("expected key = \"value\"");
        const auto key = trim(text.substr(0, equals));
        auto raw_value = trim(text.substr(equals + 1));
        std::optional<std::string> value = unquote(raw_value);
        if (!value && section == Section::Checkpoint && key == "after_step")
            value = std::string(raw_value);
        if (!value) return fail("values must be quoted strings (except numeric after_step)");
        if (section == Section::Mission) {
            if (key == "version") {
                if (*value != "1") return fail("unsupported version (expected \"1\")");
                saw_version = true;
            } else if (key == "program") {
                expected_program_ = *value;
            } else if (key == "description") {
                description_ = *value;
            } else if (key == "goal") {
                goal_ = *value;
            } else if (key != "id") {
                return fail("unknown [mission] key");
            }
        } else if (section == Section::Step) {
            if (key == "label")
                step.id = *value;
            else if (key == "id")
                step.id = *value;
            else if (key == "title")
                step.guidance.title = *value;
            else if (key == "prompt")
                step.guidance.prompt = *value;
            else if (key == "why")
                step.guidance.why = *value;
            else if (key == "view") {
                const auto page = page_for(*value);
                if (!page) return fail("unknown step view");
                step.guidance.destination = *page;
                step_has_view = true;
            } else if (key == "panels") {
                std::string_view list = *value;
                while (!list.empty()) {
                    const auto comma = list.find(',');
                    const auto name = trim(list.substr(0, comma));
                    const auto page = page_for(name);
                    if (!page) return fail("unknown panel in step panels list");
                    if (std::ranges::find(step.guidance.panels, *page) !=
                        step.guidance.panels.end()) {
                        return fail("step panels must be unique");
                    }
                    step.guidance.panels.push_back(*page);
                    if (comma == std::string_view::npos) break;
                    list.remove_prefix(comma + 1);
                }
                if (step.guidance.panels.empty() || step.guidance.panels.size() > 4)
                    return fail("panels must list between one and four views");
            } else if (key == "glossary") {
                try {
                    step.guidance.glossary_topic = std::stoi(*value);
                } catch (...) {
                    return fail("glossary must be an integer encoded as a string");
                }
            } else {
                return fail("unknown [step] key");
            }
        } else if (section == Section::Checkpoint) {
            if (key == "label")
                checkpoint.symbol = *value;
            else if (key == "message")
                checkpoint.message = *value;
            else if (key == "after_step") {
                if (value->empty() || !std::ranges::all_of(*value, [](char ch) {
                        return std::isdigit(static_cast<unsigned char>(ch)) != 0;
                    })) {
                    return fail("after_step must be a positive step number");
                }
                try {
                    checkpoint.after_step = std::stoul(*value);
                } catch (...) {
                    return fail("after_step must be a positive step number");
                }
            } else {
                return fail("unknown [checkpoint] key");
            }
        } else {
            return fail("key is outside a section");
        }
    }
    if (!finish_step() || !finish_checkpoint()) return false;
    if (!saw_version || expected_program_.empty() || steps_.empty()) {
        detail_ = "[mission] requires version and program, followed by at least one [step].";
        return false;
    }
    for (size_t i = 0; i < checkpoints_.size(); ++i) {
        const auto& item = checkpoints_[i];
        if (item.after_step > steps_.size()) {
            detail_ = "A checkpoint after_step must refer to an existing [step].";
            return false;
        }
        for (size_t j = 0; j < i; ++j) {
            if (checkpoints_[j].symbol == item.symbol) {
                detail_ = "Checkpoint labels must be unique.";
                return false;
            }
            if (checkpoints_[j].after_step == item.after_step) {
                detail_ = "Only one checkpoint may follow a step.";
                return false;
            }
        }
    }
    return true;
}

void MissionProgress::configure(std::string_view mission_path, std::string_view binary_path) {
    step_.store(0, std::memory_order_relaxed);
    checkpoint_reached_.store(false, std::memory_order_relaxed);
    revision_.fetch_add(1, std::memory_order_relaxed);
    if (mission_path.empty()) {
        mission_path_.clear();
        expected_program_.clear();
        description_.clear();
        goal_.clear();
        detail_.clear();
        steps_.clear();
        checkpoints_.clear();
        status_.store(MissionStatus::Inactive, std::memory_order_relaxed);
        return;
    }
    if (!load(mission_path)) {
        status_.store(MissionStatus::LoadError, std::memory_order_relaxed);
        return;
    }
    if (basename(binary_path) != expected_program_) {
        detail_ = std::format("Load {} for this mission.", expected_program_);
        status_.store(MissionStatus::ProgramMismatch, std::memory_order_relaxed);
        return;
    }
    status_.store(MissionStatus::Active, std::memory_order_relaxed);
}

void MissionProgress::restart() {
    if (!steps_.empty() && status() != MissionStatus::LoadError &&
        status() != MissionStatus::ProgramMismatch) {
        step_.store(0, std::memory_order_relaxed);
        checkpoint_reached_.store(false, std::memory_order_relaxed);
        revision_.fetch_add(1, std::memory_order_relaxed);
        status_.store(MissionStatus::Active, std::memory_order_relaxed);
    }
}

void MissionProgress::observe_symbol(std::string_view exact_symbol) {
    if (!enabled() || exact_symbol != checkpoint_symbol()) return;
    if (!checkpoint_reached_.exchange(true, std::memory_order_relaxed))
        revision_.fetch_add(1, std::memory_order_relaxed);
}

auto MissionProgress::advance_step() noexcept -> bool {
    if (!can_advance_step()) return false;
    const auto next_step = step() + 1;
    checkpoint_reached_.store(false, std::memory_order_relaxed);
    step_.store(next_step, std::memory_order_relaxed);
    revision_.fetch_add(1, std::memory_order_relaxed);
    if (next_step >= steps_.size()) {
        status_.store(MissionStatus::Complete, std::memory_order_relaxed);
    }
    return true;
}

auto MissionProgress::can_advance_step() const noexcept -> bool {
    if (!enabled()) return false;
    if (checkpoint_symbol().empty()) return true;
    return checkpoint_reached();
}

auto MissionProgress::checkpoint_symbol() const noexcept -> std::string_view {
    const auto current_step = step();
    if (!enabled() || current_step >= steps_.size()) return {};
    const size_t after_step = current_step + 1;
    for (const auto& checkpoint : checkpoints_)
        if (checkpoint.after_step == after_step) return checkpoint.symbol;
    return {};
}

auto MissionProgress::checkpoint_message() const noexcept -> std::string_view {
    const size_t after_step = step() + 1;
    for (const auto& checkpoint : checkpoints_)
        if (checkpoint.after_step == after_step) return checkpoint.message;
    return {};
}

auto MissionProgress::guidance() const -> std::optional<MissionGuidance> {
    if (status() == MissionStatus::LoadError) {
        return MissionGuidance{.title = "Mission could not load",
                               .prompt = detail_,
                               .why = "Fix the lesson file, then restart.",
                               .destination = TuiRegPage::GPR,
                               .panels = {},
                               .glossary_topic = 0};
    }
    if (status() == MissionStatus::ProgramMismatch) {
        return MissionGuidance{.title = "Mission needs its example",
                               .prompt = detail_,
                               .why = "Named checkpoints make the observation sequence repeatable.",
                               .destination = TuiRegPage::GPR,
                               .panels = {},
                               .glossary_topic = 0};
    }
    if (status() == MissionStatus::Complete) {
        return MissionGuidance{
            .title = "Mission complete",
            .prompt = "Review Trace, registers, and stack before restarting.",
            .why = "You connected branch, loop, call, and return behavior to architectural state.",
            .destination = TuiRegPage::TRACE,
            .panels = {},
            .glossary_topic = 0};
    }
    const auto current_step = step();
    if (!enabled() || current_step >= steps_.size()) return std::nullopt;
    auto guidance = steps_[current_step].guidance;
    guidance.title = std::format("{}/{} · {}", current_step + 1, steps_.size(), guidance.title);
    return guidance;
}

}  // namespace simrv::tui
