// SPDX-License-Identifier: MIT
#include "simrv/v3/RunManifest.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace simrv::v3 {
namespace {
auto trim(std::string value) -> std::string {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char c) { return std::isspace(c); }).base();
    return first >= last ? "" : std::string(first, last);
}
auto unquote(std::string value) -> Result<std::string> {
    value = trim(std::move(value));
    if (value.size() < 2 || value.front() != '\"' || value.back() != '\"') {
        return std::unexpected(Error{"expected a quoted TOML string"});
    }
    return value.substr(1, value.size() - 2);
}
auto bool_value(const std::string& value) -> Result<bool> {
    if (value == "true") return true;
    if (value == "false") return false;
    return std::unexpected(Error{"expected true or false"});
}
}  // namespace

auto validate_manifest(const RunManifest& manifest) -> Result<void> {
    if (manifest.schema_version != RunManifest::kSchemaVersion) return std::unexpected(Error{"unsupported manifest schema_version"});
    if (manifest.program.empty()) return std::unexpected(Error{"guest.program is required"});
    if (manifest.harts == 0) return std::unexpected(Error{"machine.harts must be greater than zero"});
    for (const auto& plugin : manifest.plugins) {
        if (plugin.path.empty() || plugin.sha256.size() != 64 || !std::all_of(plugin.sha256.begin(), plugin.sha256.end(), [](unsigned char c) { return std::isxdigit(c); }))
            return std::unexpected(Error{"each plugin requires a path and a 64-character sha256"});
    }
    return {};
}

auto parse_manifest(const std::filesystem::path& path) -> Result<RunManifest> {
    std::ifstream input(path);
    if (!input) return std::unexpected(Error{"cannot open manifest: " + path.string()});
    RunManifest manifest;
    std::string section;
    std::string line;
    unsigned line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        line = trim(line.substr(0, line.find('#')));
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') { section = line.substr(1, line.size() - 2); continue; }
        const auto equal = line.find('=');
        if (equal == std::string::npos) return std::unexpected(Error{"invalid TOML at line " + std::to_string(line_number)});
        const auto key = trim(line.substr(0, equal));
        const auto value = trim(line.substr(equal + 1));
        auto string = [&]() { return unquote(value); };
        if (section.empty() && key == "schema_version") { manifest.schema_version = static_cast<unsigned>(std::stoul(value)); }
        else if (section == "guest" && key == "program") { auto v = string(); if (!v) return std::unexpected(v.error()); manifest.program = *v; }
        else if (section == "guest" && key == "disk") { auto v = string(); if (!v) return std::unexpected(v.error()); manifest.disk = *v; }
        else if (section == "guest" && key == "device_tree") { auto v = string(); if (!v) return std::unexpected(v.error()); manifest.device_tree = *v; }
        else if (section == "run" && key == "output_directory") { auto v = string(); if (!v) return std::unexpected(v.error()); manifest.output_directory = *v; }
        else if (section == "run" && key == "tui") { auto v = bool_value(value); if (!v) return std::unexpected(v.error()); manifest.interaction = *v ? Interaction::Tui : Interaction::Headless; }
        else if (section == "machine" && key == "baremetal") { auto v = bool_value(value); if (!v) return std::unexpected(v.error()); manifest.baremetal = *v; }
        else if (section == "machine" && key == "cycle_accurate") { auto v = bool_value(value); if (!v) return std::unexpected(v.error()); manifest.cycle_accurate = *v; }
        else if (section == "machine" && key == "harts") manifest.harts = static_cast<unsigned>(std::stoul(value));
        else if (section == "machine" && key == "vlen") manifest.vlen = static_cast<unsigned>(std::stoul(value));
        else if (section == "machine" && key == "misa") { auto v = string(); if (!v) return std::unexpected(v.error()); manifest.misa = *v; }
        else return std::unexpected(Error{"unknown key " + section + "." + key + " at line " + std::to_string(line_number)});
    }
    return validate_manifest(manifest).transform([&] { return manifest; });
}

auto render_manifest(const RunManifest& manifest) -> std::string {
    std::ostringstream out;
    out << "schema_version = " << manifest.schema_version << "\n\n[guest]\nprogram = \"" << manifest.program.string() << "\"\n";
    if (!manifest.disk.empty()) out << "disk = \"" << manifest.disk.string() << "\"\n";
    out << "\n[machine]\nbaremetal = " << (manifest.baremetal ? "true" : "false") << "\ncycle_accurate = " << (manifest.cycle_accurate ? "true" : "false") << "\nharts = " << manifest.harts << "\nvlen = " << manifest.vlen << "\nmisa = \"" << manifest.misa << "\"\n";
    out << "\n[run]\ntui = " << (manifest.interaction == Interaction::Tui ? "true" : "false") << "\n";
    if (!manifest.output_directory.empty()) out << "output_directory = \"" << manifest.output_directory.string() << "\"\n";
    return out.str();
}
}  // namespace simrv::v3
