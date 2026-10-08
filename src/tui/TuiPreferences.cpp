/** @file TuiPreferences.cpp */
#include "simrv/tui/TuiPreferences.hpp"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <string>
#include <string_view>

namespace simrv::tui {
namespace {

template <typename T>
auto parse_integer(std::string_view text, T& value) -> bool {
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

auto parse_slots(std::string_view text, std::vector<WorkbenchSlot>& slots) -> bool {
    std::vector<WorkbenchSlot> parsed;
    while (!text.empty()) {
        const auto separator = text.find(';');
        const auto item = text.substr(0, separator);
        const auto colon = item.find(':');
        unsigned page = 0;
        int scroll = 0;
        if (colon == std::string_view::npos || !parse_integer(item.substr(0, colon), page) ||
            !parse_integer(item.substr(colon + 1), scroll) ||
            page > static_cast<unsigned>(TuiRegPage::DISPLAY) || scroll < 0 || parsed.size() >= 4) {
            return false;
        }
        parsed.push_back({static_cast<TuiRegPage>(page), scroll});
        if (separator == std::string_view::npos) break;
        text.remove_prefix(separator + 1);
    }
    if (parsed.empty()) return false;
    slots = std::move(parsed);
    return true;
}

}  // namespace

auto TuiPreferences::default_path() -> std::filesystem::path {
    if (const char* override_path = std::getenv("SIMRV_TUI_PREFERENCES");
        override_path != nullptr) {
        if (std::string_view(override_path) == "off") return {};
        if (*override_path != '\0') return std::filesystem::path(override_path);
    }
    if (const char* config_home = std::getenv("XDG_CONFIG_HOME");
        config_home != nullptr && *config_home != '\0') {
        return std::filesystem::path(config_home) / "simrv" / "tui.conf";
    }
    if (const char* home = std::getenv("HOME"); home != nullptr && *home != '\0') {
        return std::filesystem::path(home) / ".config" / "simrv" / "tui.conf";
    }
    return {};
}

auto TuiPreferences::load(const std::filesystem::path& path) -> bool {
    std::ifstream input(path);
    if (!input) return false;
    std::string line;
    unsigned version = 0;
    while (std::getline(input, line)) {
        const auto equal = line.find('=');
        if (equal == std::string::npos) continue;
        const std::string_view key(line.data(), equal);
        const std::string_view value(line.data() + equal + 1, line.size() - equal - 1);
        unsigned parsed = 0;
        if (key == "version" && parse_integer(value, version)) {
            continue;
        } else if (key == "layout" && parse_integer(value, parsed) && parsed <= 4) {
            layout = static_cast<TuiLayout>(parsed);
        } else if (key == "slots") {
            (void)parse_slots(value, slots);
        } else if (key == "focused" && parse_integer(value, parsed)) {
            focused_slot = parsed;
        } else if (key == "inspector_width" && parse_integer(value, inspector_width)) {
            if (inspector_width < -1) inspector_width = -1;
        } else if (key == "column_widths") {
            auto rest = value;
            auto candidate = column_widths;
            bool valid = true;
            for (auto& width : candidate) {
                const auto comma = rest.find(',');
                const auto token = rest.substr(0, comma);
                if (!parse_integer(token, width) || width < -1) valid = false;
                if (&width != &candidate.back()) {
                    if (comma == std::string_view::npos) valid = false;
                    else rest.remove_prefix(comma + 1);
                } else if (comma != std::string_view::npos) {
                    valid = false;
                }
            }
            if (valid) column_widths = candidate;
        } else if (key == "theme" && parse_integer(value, parsed) && parsed <= 2) {
            theme = static_cast<TuiThemeStyle>(parsed);
        } else if (key == "high_contrast" && parse_integer(value, parsed) && parsed <= 1) {
            high_contrast = parsed != 0;
        } else if (key == "class_mode" && parse_integer(value, parsed) && parsed <= 1) {
            class_mode = parsed != 0;
        } else if (key == "student_guide" && parse_integer(value, parsed) && parsed <= 1) {
            student_guide = parsed != 0;
        } else if (key == "target_fps" && parse_integer(value, parsed) && parsed >= 1 &&
                   parsed <= 120) {
            target_fps = parsed;
        } else if (key == "mouse_sensitivity") {
            try {
                size_t consumed = 0;
                const auto parsed_value = std::stod(std::string(value), &consumed);
                if (consumed == value.size() && std::isfinite(parsed_value) &&
                    parsed_value > 0.0) {
                    mouse_sensitivity = parsed_value;
                }
            } catch (...) {
            }
        }
    }
    if (focused_slot >= slots.size()) focused_slot = 0;
    return input.eof() && version == 1;
}

auto TuiPreferences::save(const std::filesystem::path& path) const -> bool {
    if (path.empty()) return false;
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error) return false;
    const auto temporary_path = path.string() + ".tmp";
    std::ofstream output(temporary_path, std::ios::trunc);
    if (!output) return false;
    output << "version=1\n"
           << "layout=" << static_cast<unsigned>(layout) << '\n'
           << "slots=";
    for (size_t index = 0; index < slots.size(); ++index) {
        if (index != 0) output << ';';
        output << static_cast<unsigned>(slots[index].page) << ':' << slots[index].scroll_offset;
    }
    output << "\nfocused=" << focused_slot << "\ninspector_width=" << inspector_width
           << "\ncolumn_widths=";
    for (size_t index = 0; index < column_widths.size(); ++index) {
        if (index != 0) output << ',';
        output << column_widths[index];
    }
    output << "\ntheme=" << static_cast<unsigned>(theme)
           << "\nhigh_contrast=" << static_cast<unsigned>(high_contrast)
           << "\nclass_mode=" << static_cast<unsigned>(class_mode) << "\ntarget_fps="
           << target_fps << "\nstudent_guide=" << static_cast<unsigned>(student_guide)
           << "\nmouse_sensitivity=" << mouse_sensitivity << '\n';
    output.close();
    if (!output) return false;
    std::filesystem::rename(temporary_path, path, error);
    if (error) {
        std::filesystem::remove(temporary_path);
        return false;
    }
    return true;
}

}  // namespace simrv::tui
