/** @file TuiMission.cpp */
#include "simrv/tui/TuiMission.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
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
            case '"':
                decoded.push_back('"');
                break;
            case '\\':
                decoded.push_back('\\');
                break;
            case 'n':
                decoded.push_back('\n');
                break;
            case 'r':
                decoded.push_back('\r');
                break;
            case 't':
                decoded.push_back('\t');
                break;
            case 'b':
                decoded.push_back('\b');
                break;
            case 'f':
                decoded.push_back('\f');
                break;
            default:
                return std::nullopt;
        }
    }
    return decoded;
}

auto page_for(std::string_view value) -> std::optional<TuiRegPage> {
    return parse_tui_page_slug(value);
}

auto parse_mission_speed(std::string_view value) -> std::optional<uint64_t> {
    uint64_t speed_hz = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), speed_hz);
    if (error != std::errc{} || end != value.data() + value.size() || speed_hz > 1'000'000) {
        return std::nullopt;
    }
    return speed_hz;
}

auto basename(std::string_view path) -> std::string_view {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

}  // namespace

auto MissionProgress::load(std::string_view mission_path) -> bool {
    mission_path_ = mission_path;
    lesson_id_.clear();
    expected_program_.clear();
    description_.clear();
    goal_.clear();
    detail_.clear();
    steps_.clear();
    checkpoints_.clear();
    speed_hz_ = 30;
    {
        std::scoped_lock lock(checkpoint_events_mutex_);
        checkpoint_events_.clear();
    }
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
            detail_ = "Each [step] requires id, title, prompt, why, and either view or panels.";
            return false;
        }
        for (const auto& existing : steps_) {
            if (existing.id == step.id) {
                detail_ = std::format("Duplicate [step] id '{}'.", step.id);
                return false;
            }
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
    std::vector<std::string> section_keys;
    while (std::getline(input, line)) {
        ++line_number;
        const auto text = trim(line);
        if (text.empty() || text.starts_with('#')) continue;
        if (text == "[mission]" || text == "[step]" || text == "[checkpoint]") {
            if (!finish_step() || !finish_checkpoint()) return false;
            section = text == "[mission]" ? Section::Mission
                      : text == "[step]"  ? Section::Step
                                          : Section::Checkpoint;
            step_open = section == Section::Step;
            checkpoint_open = section == Section::Checkpoint;
            step_has_view = false;
            section_keys.clear();
            continue;
        }
        const auto equals = text.find('=');
        if (equals == std::string_view::npos) return fail("expected key = \"value\"");
        const auto key = trim(text.substr(0, equals));
        auto raw_value = trim(text.substr(equals + 1));
        const auto canonical_key = section == Section::Step && key == "label"
                                       ? std::string_view("id")
                                       : key;
        if (std::ranges::find(section_keys, canonical_key) != section_keys.end())
            return fail("duplicate key");
        section_keys.emplace_back(canonical_key);
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
            } else if (key == "id") {
                lesson_id_ = *value;
            } else if (key == "description") {
                description_ = *value;
            } else if (key == "goal") {
                goal_ = *value;
            } else if (key == "speed_hz") {
                const auto speed = parse_mission_speed(*value);
                if (!speed) return fail("speed_hz must be an integer from 0 to 1000000");
                speed_hz_ = *speed;
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
            else if (key == "learn")
                step.guidance.learn = *value;
            else if (key == "hint")
                step.guidance.hint = *value;
            else if (key == "why")
                step.guidance.why = *value;
            else if (key == "speed_hz") {
                step.speed_hz = parse_mission_speed(*value);
                if (!step.speed_hz) return fail("speed_hz must be an integer from 0 to 1000000");
            } else if (key == "view") {
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
                int topic = -1;
                const auto [end, error] =
                    std::from_chars(value->data(), value->data() + value->size(), topic);
                if (error != std::errc{} || end != value->data() + value->size() || topic < 0 ||
                    topic > 5) {
                    return fail("glossary must be an integer from 0 to 5 encoded as a string");
                }
                step.guidance.glossary_topic = topic;
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
    if (!saw_version || lesson_id_.empty() || expected_program_.empty() || steps_.empty()) {
        detail_ = "[mission] requires version, id, and program, followed by at least one [step].";
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
        lesson_id_.clear();
        description_.clear();
        goal_.clear();
        detail_.clear();
        speed_hz_ = 30;
        steps_.clear();
        checkpoints_.clear();
        {
            std::scoped_lock lock(checkpoint_events_mutex_);
            checkpoint_events_.clear();
        }
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
        {
            std::scoped_lock lock(checkpoint_events_mutex_);
            checkpoint_events_.clear();
        }
        revision_.fetch_add(1, std::memory_order_relaxed);
        status_.store(MissionStatus::Active, std::memory_order_relaxed);
    }
}

void MissionProgress::observe_symbol(std::string_view exact_symbol) {
    if (!enabled()) return;
    const auto checkpoint = checkpoint_progress();
    if (!checkpoint || checkpoint->reached || exact_symbol != checkpoint->symbol) return;
    {
        std::scoped_lock lock(checkpoint_events_mutex_);
        if (std::ranges::find(checkpoint_events_, exact_symbol) != checkpoint_events_.end()) return;
        checkpoint_events_.emplace_back(exact_symbol);
    }
    checkpoint_reached_.store(true, std::memory_order_relaxed);
    revision_.fetch_add(1, std::memory_order_relaxed);
}

auto MissionProgress::reached_checkpoint_events() const -> std::vector<std::string> {
    std::scoped_lock lock(checkpoint_events_mutex_);
    return checkpoint_events_;
}

auto MissionProgress::advance_step() -> bool {
    if (!can_advance_step()) return false;
    const auto next_step = step() + 1;
    step_.store(next_step, std::memory_order_relaxed);
    if (next_step >= steps_.size()) {
        checkpoint_reached_.store(false, std::memory_order_relaxed);
        status_.store(MissionStatus::Complete, std::memory_order_relaxed);
    } else {
        const auto next_symbol = checkpoint_symbol();
        bool already_reached = false;
        if (!next_symbol.empty()) {
            std::scoped_lock lock(checkpoint_events_mutex_);
            already_reached =
                std::ranges::find(checkpoint_events_, next_symbol) != checkpoint_events_.end();
        }
        checkpoint_reached_.store(already_reached, std::memory_order_relaxed);
    }
    revision_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

auto MissionProgress::can_advance_step() const noexcept -> bool {
    if (!enabled()) return false;
    const size_t boundary = step() + 1;
    for (const auto& checkpoint : checkpoints_) {
        if (checkpoint.after_step != boundary) continue;
        std::scoped_lock lock(checkpoint_events_mutex_);
        return std::ranges::find(checkpoint_events_, checkpoint.symbol) != checkpoint_events_.end();
    }
    return true;
}

auto MissionProgress::previous_step() -> bool {
    if (!can_previous_step()) return false;
    const auto prev_step = step() - 1;
    status_.store(MissionStatus::Active, std::memory_order_relaxed);
    step_.store(prev_step, std::memory_order_relaxed);
    const auto prev_symbol = checkpoint_symbol();
    bool already_reached = false;
    if (!prev_symbol.empty()) {
        std::scoped_lock lock(checkpoint_events_mutex_);
        already_reached =
            std::ranges::find(checkpoint_events_, prev_symbol) != checkpoint_events_.end();
    }
    checkpoint_reached_.store(already_reached, std::memory_order_relaxed);
    revision_.fetch_add(1, std::memory_order_relaxed);
    return true;
}

auto MissionProgress::can_previous_step() const noexcept -> bool {
    return (enabled() || completed()) && step() > 0 && !steps_.empty();
}

auto MissionProgress::checkpoint_symbol() const noexcept -> std::string_view {
    if ((!enabled() && !completed()) || steps_.empty()) return {};
    const size_t next_boundary = step() + 1;
    const MissionCheckpoint* next = nullptr;
    for (const auto& checkpoint : checkpoints_)
        if (checkpoint.after_step >= next_boundary &&
            (next == nullptr || checkpoint.after_step < next->after_step))
            next = &checkpoint;
    return next ? std::string_view(next->symbol) : std::string_view{};
}

auto MissionProgress::checkpoint_message() const noexcept -> std::string_view {
    if ((!enabled() && !completed()) || steps_.empty()) return {};
    const size_t next_boundary = step() + 1;
    const MissionCheckpoint* next = nullptr;
    for (const auto& checkpoint : checkpoints_)
        if (checkpoint.after_step >= next_boundary &&
            (next == nullptr || checkpoint.after_step < next->after_step))
            next = &checkpoint;
    return next ? std::string_view(next->message) : std::string_view{};
}

auto MissionProgress::checkpoint_progress() const -> std::optional<MissionCheckpointProgress> {
    if ((!enabled() && !completed()) || steps_.empty()) return std::nullopt;
    const size_t next_boundary = step() + 1;
    std::scoped_lock lock(checkpoint_events_mutex_);
    const MissionCheckpoint* next = nullptr;
    for (const auto& checkpoint : checkpoints_) {
        if (checkpoint.after_step >= next_boundary &&
            (next == nullptr || checkpoint.after_step < next->after_step))
            next = &checkpoint;
    }
    if (next == nullptr) return std::nullopt;
    const bool reached =
        std::ranges::find(checkpoint_events_, next->symbol) != checkpoint_events_.end();
    return MissionCheckpointProgress{next->symbol, next->message, next->after_step, reached};
}

auto MissionProgress::checkpoints_progress() const -> std::vector<MissionCheckpointProgress> {
    std::scoped_lock lock(checkpoint_events_mutex_);
    std::vector<MissionCheckpointProgress> progress;
    progress.reserve(checkpoints_.size());
    for (const auto& checkpoint : checkpoints_) {
        const bool reached =
            std::ranges::find(checkpoint_events_, checkpoint.symbol) != checkpoint_events_.end();
        progress.push_back({checkpoint.symbol, checkpoint.message, checkpoint.after_step, reached});
    }
    std::ranges::sort(progress, {}, &MissionCheckpointProgress::after_step);
    return progress;
}

auto MissionProgress::reached_checkpoint_count() const -> size_t {
    std::scoped_lock lock(checkpoint_events_mutex_);
    return checkpoint_events_.size();
}

auto MissionProgress::guidance() const -> std::optional<MissionGuidance> {
    if (status() == MissionStatus::LoadError) {
        return MissionGuidance{.title = "Mission could not load",
                               .learn = {},
                               .prompt = detail_,
                               .hint = {},
                               .why = "Fix the lesson file, then restart.",
                               .destination = TuiRegPage::GPR,
                               .panels = {},
                               .glossary_topic = 0};
    }
    if (status() == MissionStatus::ProgramMismatch) {
        return MissionGuidance{.title = "Mission needs its example",
                               .learn = {},
                               .prompt = detail_,
                               .hint = {},
                               .why = "Named checkpoints make the observation sequence repeatable.",
                               .destination = TuiRegPage::GPR,
                               .panels = {},
                               .glossary_topic = 0};
    }
    if (status() == MissionStatus::Complete) {
        return MissionGuidance{
            .title = "Mission complete",
            .learn = {},
            .prompt =
                "Review the result and restored stack. Restart the lesson or exit when ready.",
            .hint = {},
            .why = "You connected branch, loop, call, and return behavior to architectural state.",
            .destination = TuiRegPage::TRACE,
            .panels = {TuiRegPage::TRACE, TuiRegPage::GPR, TuiRegPage::STACK},
            .glossary_topic = 0};
    }
    const auto current_step = step();
    if (!enabled() || current_step >= steps_.size()) return std::nullopt;
    return steps_[current_step].guidance;
}

auto MissionProgress::speed_hz() const noexcept -> uint64_t {
    const size_t current_step = step();
    if (current_step < steps_.size() && steps_[current_step].speed_hz)
        return *steps_[current_step].speed_hz;
    return speed_hz_;
}

}  // namespace simrv::tui
