// SPDX-License-Identifier: MIT
#include "simrv/v3/Command.hpp"

#include <filesystem>
#include <fstream>
#include <print>
#include <string>
#include <vector>

#include "simrv/v3/Simulator.hpp"

namespace simrv::v3 {
namespace {
void write_json_string(std::ostream& out, const std::string& value) {
    out << '"';
    for (const char c : value) {
        if (c == '"' || c == '\\') out << '\\';
        if (c == '\n') out << "\\n";
        else out << c;
    }
    out << '"';
}
auto execute(const std::filesystem::path& manifest_path, Interaction interaction) -> int {
    auto manifest = parse_manifest(manifest_path);
    if (!manifest) { std::println(stderr, "manifest error: {}", manifest.error().message); return 2; }
    manifest->interaction = interaction;
    auto simulator = Simulator::create(*manifest);
    if (!simulator) { std::println(stderr, "plugin error: {}", simulator.error().message); return 2; }
    std::vector<Event> events;
    const std::filesystem::path output = manifest->output_directory.empty() ? "simrv-run" : manifest->output_directory;
    std::error_code error;
    std::filesystem::create_directories(output, error);
    if (error) { std::println(stderr, "output error: {}", error.message()); return 2; }
    { std::ofstream resolved(output / "manifest.resolved.toml"); resolved << render_manifest(*manifest); }
    [[maybe_unused]] const auto subscription =
        simulator->subscribe([&events](const Event& event) { events.push_back(event); });
    auto status = simulator->run();
    if (!status) { std::println(stderr, "run error: {}", status.error().message); return 1; }
    {
        std::ofstream event_file(output / "events.jsonl");
        for (const auto& event : events) event_file << "{\"kind\":" << static_cast<unsigned>(event.kind)
                                                     << ",\"instruction_count\":" << event.instruction_count
                                                     << ",\"exit_status\":" << event.exit_status << "}\n";
    }
    {
        std::ofstream result(output / "result.json");
        const auto snapshot = simulator->snapshot();
        result << "{\"schema_version\":1,\"exit_status\":" << *status
               << ",\"instruction_count\":" << snapshot.instruction_count << ",\"plugins\":[";
        for (size_t index = 0; index < manifest->plugins.size(); ++index) {
            if (index != 0) result << ',';
            result << "{\"path\":"; write_json_string(result, manifest->plugins[index].path.string());
            result << ",\"sha256\":"; write_json_string(result, manifest->plugins[index].sha256); result << '}';
        }
        result << "]}\n";
    }
    return *status;
}
}  // namespace

auto try_run_command(std::span<char* const> args) -> std::optional<int> {
    if (args.size() < 2) return std::nullopt;
    const std::string command(args[1]);
    if (command != "run" && command != "tui" && command != "validate" && command != "inspect") return std::nullopt;
    if (args.size() != 3) { std::println(stderr, "usage: {} {} <manifest.toml>", args[0], command); return 2; }
    auto manifest = parse_manifest(args[2]);
    if (!manifest) { std::println(stderr, "manifest error: {}", manifest.error().message); return 2; }
    if (command == "validate") { std::println("manifest is valid"); return 0; }
    if (command == "inspect") { std::print("{}", render_manifest(*manifest)); return 0; }
    return execute(args[2], command == "tui" ? Interaction::Tui : Interaction::Headless);
}
}  // namespace simrv::v3
