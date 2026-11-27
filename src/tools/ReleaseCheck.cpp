/**
 * @file ReleaseCheck.cpp
 * @brief Native C++ release qualification and metadata verification utility for SimRV.
 */
#include <array>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <print>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;

void fail(std::string_view message) {
    std::println(std::cerr, "release-check: error: {}", message);
    std::exit(1);
}

auto read_file_string(const fs::path& path) -> std::string {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) return {};
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

auto find_project_root() -> fs::path {
    auto current = fs::current_path();
    for (int i = 0; i < 5; ++i) {
        if (fs::exists(current / "CMakeLists.txt") &&
            fs::exists(current / "release/release-manifest.json")) {
            return current;
        }
        if (current.has_parent_path()) {
            current = current.parent_path();
        } else {
            break;
        }
    }
    return fs::current_path();
}

auto source_version(const fs::path& root) -> std::string {
    const auto content = read_file_string(root / "CMakeLists.txt");
    std::smatch match;
    const std::regex re(R"re(set\(SIMRV_VERSION "([^"]+)"\))re");
    if (std::regex_search(content, match, re) && match.size() > 1) {
        return match.str(1);
    }
    fail("cannot read SIMRV_VERSION from CMakeLists.txt");
    return {};
}

auto artifact_contains_version(const std::string& artifact, const std::string& version) -> bool {
    const auto dash = version.find('-');
    if (dash != std::string::npos) {
        if (artifact.find(version) != std::string::npos) return true;
        const auto base = version.substr(0, dash);
        const auto prerelease = version.substr(dash + 1);
        const auto deb_candidate = base + "~" + prerelease;
        const auto rpm_candidate = base + "-0." + prerelease;
        return artifact.find(deb_candidate) != std::string::npos ||
               artifact.find(rpm_candidate) != std::string::npos;
    }
    size_t pos = 0;
    while ((pos = artifact.find(version, pos)) != std::string::npos) {
        const bool left_ok =
            pos == 0 || !std::isdigit(static_cast<unsigned char>(artifact[pos - 1]));
        const size_t end = pos + version.size();
        const bool right_ok = end == artifact.size() ||
                              (artifact[end] != '~' &&
                               !(artifact[end] == '-' && end + 1 < artifact.size() &&
                                 (artifact[end + 1] == '0' ||
                                  std::isalpha(static_cast<unsigned char>(artifact[end + 1])))) &&
                               !std::isdigit(static_cast<unsigned char>(artifact[end])) &&
                               !(artifact[end] == '.' && end + 1 < artifact.size() &&
                                 std::isdigit(static_cast<unsigned char>(artifact[end + 1]))));
        if (left_ok && right_ok) {
            return true;
        }
        pos = end;
    }
    return false;
}

auto run_cmd(const std::string& cmd) -> std::pair<int, std::string> {
    std::array<char, 256> buffer{};
    std::string result;
    FILE* pipe = ::popen((cmd + " 2>&1").c_str(), "r");
    if (!pipe) {
        return {-1, "popen failed"};
    }
    while (::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        result += buffer.data();
    }
    int status = ::pclose(pipe);
    int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return {exit_code, result};
}

void verify_metadata(const fs::path& root, const fs::path& manifest_override) {
    const auto manifest_path =
        manifest_override.empty() ? (root / "release/release-manifest.json") : manifest_override;
    if (!fs::exists(manifest_path)) {
        fail("missing release manifest: " + manifest_path.string());
    }
    const auto manifest = read_file_string(manifest_path);
    if (manifest.find(R"("schema_version": 2)") == std::string::npos &&
        manifest.find(R"("schema_version":2)") == std::string::npos) {
        fail("release manifest schema_version must be 2");
    }

    const auto schema_dir = root / "release/schemas";
    for (const auto& schema : {"release-manifest.schema.json", "evidence.schema.json"}) {
        if (!fs::is_regular_file(schema_dir / schema)) {
            fail(std::string("missing schema: release/schemas/") + schema);
        }
    }

    const auto version = source_version(root);
    const auto citation = read_file_string(root / "CITATION.cff");
    std::smatch cit_match;
    const std::regex cit_re(R"((?:^|\n)version:\s*['"]?([^'"\s\r\n]+))");
    if (!std::regex_search(citation, cit_match, cit_re) || cit_match.str(1) != version) {
        fail("CITATION.cff version must match source version " + version);
    }
    if (!std::regex_search(citation, std::regex(R"(date-released:\s*['"]?[^'"\s]+)"))) {
        fail("CITATION.cff must define date-released");
    }

    const auto expected_tag = "v" + version;
    if (manifest.find(std::string("\"version\": \"") + version + "\"") == std::string::npos) {
        fail("manifest version != source version " + version);
    }
    if (manifest.find(std::string("\"release_tag\": \"") + expected_tag + "\"") ==
        std::string::npos) {
        fail("manifest release_tag must be " + expected_tag);
    }

    const auto changelog = read_file_string(root / "CHANGELOG.md");
    if (changelog.find("## [" + expected_tag + "]") == std::string::npos) {
        fail("CHANGELOG.md has no ## [" + expected_tag + "] heading");
    }

    // Required artifacts check
    for (const auto& art : {"SimRV-linux-x86_64-v" + version + ".tar.gz",
                            "SimRV-linux-aarch64-v" + version + ".tar.gz",
                            "SimRV-rpm-packages-v" + version + ".tar.gz",
                            "SimRV-deb-packages-v" + version + ".tar.gz"}) {
        if (!artifact_contains_version(art, version)) {
            fail("artifact does not match version: " + art);
        }
        if (manifest.find(art) == std::string::npos) {
            fail("manifest missing required artifact: " + art);
        }
    }

    // Check package support
    for (const auto& distro :
         {"Ubuntu 22.04", "Rocky Linux 10", "Fedora 44", "Ubuntu 24.04", "Debian sid"}) {
        if (manifest.find(distro) == std::string::npos) {
            fail(std::string("manifest missing package support distribution: ") + distro);
        }
    }
}

void verify_binary(const fs::path& binary_path, const fs::path& guest_path,
                   const std::string& version) {
    if (!fs::is_regular_file(binary_path)) {
        fail("simrv binary does not exist: " + binary_path.string());
    }

    auto [v_code, v_out] = run_cmd(binary_path.string() + " --version");
    if (v_code != 0) fail("binary --version failed:\n" + v_out);
    if (v_out.find(version) == std::string::npos) {
        fail("binary --version output does not match version " + version + ":\n" + v_out);
    }

    auto [c_code, c_out] = run_cmd(binary_path.string() + " --isa-info");
    if (c_code != 0) fail("binary --isa-info failed:\n" + c_out);

    auto [l_code, l_out] = run_cmd(binary_path.string() + " --license");
    if (l_code != 0) fail("binary --license failed:\n" + l_out);

    if (!guest_path.empty()) {
        auto [g_code, g_out] = run_cmd(binary_path.string() + " --cli --baremetal -m " +
                                       guest_path.string() + " --steps 1000");
        if (g_code != 0) fail("binary guest execution failed:\n" + g_out);
    }
}

void print_help(std::string_view prog) {
    std::println(
        "Usage: {} [OPTIONS]\n\n"
        "Validate SimRV release metadata and optionally verify packaged binaries.\n\n"
        "Options:\n"
        "  -h, --help            Show this help message and exit\n"
        "  --binary <PATH>       Path to simrv binary to verify (can be repeated)\n"
        "  --guest <ELF>         Path to guest baremetal ELF for binary verification\n"
        "  --manifest <PATH>     Path to release-manifest.json (optional)\n"
        "  --root <PATH>         Project root directory (optional)\n",
        prog);
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
    std::string_view prog = argc > 0 ? argv[0] : "simrv-release-check";
    fs::path root_dir;
    fs::path manifest_path;
    fs::path guest_path;
    std::vector<fs::path> binaries;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help(prog);
            return 0;
        } else if (arg == "--binary" && i + 1 < argc) {
            binaries.emplace_back(argv[++i]);
        } else if (arg.starts_with("--binary=")) {
            binaries.emplace_back(arg.substr(9));
        } else if (arg == "--guest" && i + 1 < argc) {
            guest_path = argv[++i];
        } else if (arg.starts_with("--guest=")) {
            guest_path = arg.substr(8);
        } else if (arg == "--manifest" && i + 1 < argc) {
            manifest_path = argv[++i];
        } else if (arg.starts_with("--manifest=")) {
            manifest_path = arg.substr(11);
        } else if (arg == "--root" && i + 1 < argc) {
            root_dir = argv[++i];
        } else if (arg.starts_with("--root=")) {
            root_dir = arg.substr(7);
        }
    }

    if (root_dir.empty()) {
        root_dir = find_project_root();
    }

    verify_metadata(root_dir, manifest_path);

    const auto version = source_version(root_dir);
    for (const auto& binary : binaries) {
        verify_binary(binary, guest_path, version);
    }

    std::println("release-check: metadata and binary checks passed.");
    return 0;
}
