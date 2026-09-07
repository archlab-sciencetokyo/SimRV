/** @file TuiMission.cpp */
#include "simrv/tui/TuiMission.hpp"

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
    return std::string(value.substr(1, value.size() - 2));
}

auto page_for(std::string_view value) -> std::optional<TuiRegPage> {
    if (value == "registers") return TuiRegPage::GPR;
    if (value == "stack") return TuiRegPage::STACK;
    if (value == "trace") return TuiRegPage::TRACE;
    if (value == "explain") return TuiRegPage::EXPLAIN;
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
    detail_.clear();
    steps_.clear();
    std::ifstream input(mission_path_);
    if (!input) {
        detail_ = std::format("Cannot open mission file: {}", mission_path_);
        return false;
    }

    enum class Section { None, Mission, Step };
    Section section = Section::None;
    MissionStep step{};
    bool step_open = false;
    bool step_has_view = false;
    bool saw_version = false;
    size_t line_number = 0;
    auto fail = [&](std::string_view reason) {
        detail_ = std::format("Mission file line {}: {}", line_number, reason);
        return false;
    };
    auto finish_step = [&]() -> bool {
        if (!step_open) return true;
        if (step.symbol.empty() || step.guidance.title.empty() || step.guidance.prompt.empty() ||
            step.guidance.why.empty() || !step_has_view) {
            detail_ = "Each [step] requires label, title, prompt, why, and view.";
            return false;
        }
        steps_.push_back(std::move(step));
        step = {};
        step_open = false;
        step_has_view = false;
        return true;
    };

    std::string line;
    while (std::getline(input, line)) {
        ++line_number;
        const auto text = trim(line);
        if (text.empty() || text.starts_with('#')) continue;
        if (text == "[mission]" || text == "[step]") {
            if (!finish_step()) return false;
            section = text == "[mission]" ? Section::Mission : Section::Step;
            step_open = section == Section::Step;
            step_has_view = false;
            continue;
        }
        const auto equals = text.find('=');
        if (equals == std::string_view::npos) return fail("expected key = \"value\"");
        const auto key = trim(text.substr(0, equals));
        const auto value = unquote(text.substr(equals + 1));
        if (!value) return fail("values must be quoted strings");
        if (section == Section::Mission) {
            if (key == "version") {
                if (*value != "1") return fail("unsupported version (expected \"1\")");
                saw_version = true;
            } else if (key == "program") {
                expected_program_ = *value;
            } else if (key != "id") {
                return fail("unknown [mission] key");
            }
        } else if (section == Section::Step) {
            if (key == "label")
                step.symbol = *value;
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
            } else if (key == "glossary") {
                try {
                    step.guidance.glossary_topic = std::stoi(*value);
                } catch (...) {
                    return fail("glossary must be an integer encoded as a string");
                }
            } else {
                return fail("unknown [step] key");
            }
        } else {
            return fail("key is outside a section");
        }
    }
    if (!finish_step()) return false;
    if (!saw_version || expected_program_.empty() || steps_.empty()) {
        detail_ = "[mission] requires version and program, followed by at least one [step].";
        return false;
    }
    return true;
}

void MissionProgress::configure(std::string_view mission_path, std::string_view binary_path) {
    step_.store(0, std::memory_order_relaxed);
    if (mission_path.empty()) {
        mission_path_.clear();
        expected_program_.clear();
        detail_.clear();
        steps_.clear();
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
        status_.store(MissionStatus::Active, std::memory_order_relaxed);
    }
}

void MissionProgress::observe_symbol(std::string_view exact_symbol) {
    const auto current_step = step();
    if (!enabled() || current_step >= steps_.size() || exact_symbol != steps_[current_step].symbol)
        return;
    const auto next_step = current_step + 1;
    step_.store(next_step, std::memory_order_relaxed);
    if (next_step == steps_.size())
        status_.store(MissionStatus::Complete, std::memory_order_relaxed);
}

auto MissionProgress::guidance() const -> std::optional<MissionGuidance> {
    if (status() == MissionStatus::LoadError) {
        return MissionGuidance{"Mission could not load", detail_,
                               "Fix the lesson file, then restart.", TuiRegPage::GPR, 0};
    }
    if (status() == MissionStatus::ProgramMismatch) {
        return MissionGuidance{"Mission needs its example", detail_,
                               "Named checkpoints make the observation sequence repeatable.",
                               TuiRegPage::GPR, 0};
    }
    if (status() == MissionStatus::Complete) {
        return MissionGuidance{
            "Mission complete", "Review Trace, registers, and stack before restarting.",
            "You connected branch, loop, call, and return behavior to architectural state.",
            TuiRegPage::TRACE, 0};
    }
    const auto current_step = step();
    if (!enabled() || current_step >= steps_.size()) return std::nullopt;
    auto guidance = steps_[current_step].guidance;
    guidance.title = std::format("{}/{} · {}", current_step + 1, steps_.size(), guidance.title);
    return guidance;
}

}  // namespace simrv::tui
