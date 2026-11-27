/**
 * @file ReleaseEvidence.cpp
 * @brief Native C++ release qualification evidence collector for SimRV.
 */
#include <sys/utsname.h>

#include <array>
#include <chrono>
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

#include "simrv/core/BuildInfo.hpp"

namespace {

namespace fs = std::filesystem;

auto run_cmd(const std::string& cmd) -> std::pair<int, std::string> {
    std::array<char, 512> buffer{};
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

auto git_rev(const fs::path& repo) -> std::string {
    auto [code, out] = run_cmd("git -C " + repo.string() + " rev-parse HEAD");
    if (code == 0) {
        out.erase(out.find_last_not_of(" \n\r\t") + 1);
        return out;
    }
    return "unavailable";
}

auto escape_json(std::string_view str) -> std::string {
    std::string out;
    out.reserve(str.size() + 16);
    for (char c : str) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\b':
                out += "\\b";
                break;
            case '\f':
                out += "\\f";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += c;
                }
                break;
        }
    }
    return out;
}

struct SuiteResult {
    std::string id;
    std::string label;
    std::string status;
    int registered{0};
    int passed{0};
    int failed{0};
    int skipped{0};
    double duration_seconds{0.0};
    std::string output;
};

auto run_ctest_suite(const fs::path& build_dir, std::string_view id, std::string_view label)
    -> SuiteResult {
    SuiteResult res;
    res.id = id;
    res.label = label;

    auto start = std::chrono::steady_clock::now();

    // Query registered count
    auto [q_code, q_out] =
        run_cmd("ctest --test-dir " + build_dir.string() + " -L " + std::string(label) + " -N");
    std::smatch total_m;
    if (std::regex_search(q_out, total_m, std::regex(R"(Total Tests: (\d+))"))) {
        res.registered = std::stoi(total_m.str(1));
    }

    if (res.registered == 0) {
        res.status = "unavailable";
        return res;
    }

    auto [code, out] = run_cmd("ctest --test-dir " + build_dir.string() +
                               " --output-on-failure -L " + std::string(label));
    auto end = std::chrono::steady_clock::now();
    res.duration_seconds =
        std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() / 1000.0;

    std::smatch sm;
    if (std::regex_search(out, sm, std::regex(R"((\d+) tests? skipped)", std::regex::icase))) {
        res.skipped = std::stoi(sm.str(1));
    }
    if (std::regex_search(out, sm,
                          std::regex(R"((\d+) tests? failed out of)", std::regex::icase))) {
        res.failed = std::stoi(sm.str(1));
    } else {
        res.failed = (code != 0) ? 1 : 0;
    }

    res.passed = std::max(0, res.registered - res.failed - res.skipped);
    res.status = (code == 0 && res.skipped == 0) ? "passed" : "failed";
    if (res.status != "passed") {
        res.output = out.size() > 12000 ? out.substr(out.size() - 12000) : out;
    }
    return res;
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
    fs::path build_dir;
    int arch = 64;
    std::string compiler = "gcc";
    fs::path output_file;
    std::vector<std::string> requested_suites;
    std::vector<std::pair<std::string, std::string>> dependencies;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--build-dir" && i + 1 < argc) {
            build_dir = argv[++i];
        } else if (arg == "--arch" && i + 1 < argc) {
            arch = std::stoi(argv[++i]);
        } else if (arg == "--compiler" && i + 1 < argc) {
            compiler = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            output_file = argv[++i];
        } else if (arg == "--suite" && i + 1 < argc) {
            requested_suites.emplace_back(argv[++i]);
        } else if (arg == "--dependency" && i + 1 < argc) {
            std::string item = argv[++i];
            auto eq = item.find('=');
            if (eq != std::string::npos) {
                dependencies.emplace_back(item.substr(0, eq), item.substr(eq + 1));
            }
        }
    }

    if (build_dir.empty() || output_file.empty()) {
        std::println(std::cerr,
                     "Usage: simrv-evidence --build-dir <DIR> --arch <32|64> --compiler "
                     "<gcc|clang> --output <FILE>");
        return 1;
    }

    fs::path root = build_dir.has_parent_path() ? build_dir.parent_path() : fs::current_path();
    if (!fs::exists(root / "release/release-manifest.json")) {
        root = fs::current_path();
    }

    // Map manifest and gate suite IDs to their CTest labels.
    auto resolve_suite_label = [](std::string_view id) -> std::string_view {
        if (id == "native") return "regress";
        if (id == "isa") return "isa";
        if (id == "rva23-act") return "rva23";
        if (id == "vector") return "vector";
        if (id == "linux-pty") return "linux";
        if (id == "asan-ubsan") return "regress";
        if (id == "tsan") return "thread";
        if (id == "package") return "package";
        if (id == "release-suite") return "gate";
        if (id == "simrv-release-tests") return "release";
        return id;
    };

    std::vector<std::pair<std::string, std::string>> suites;
    if (!requested_suites.empty()) {
        for (const auto& req : requested_suites) {
            suites.emplace_back(req, std::string(resolve_suite_label(req)));
        }
    } else {
        suites = {{"release-suite", "gate"}, {"simrv-release-tests", "release"}};
    }

    std::vector<SuiteResult> results;
    int total_passed = 0;
    int total_failed = 0;
    int total_skipped = 0;
    int total_unavailable = 0;

    for (const auto& [id, label] : suites) {
        auto res = run_ctest_suite(build_dir, id, label);
        total_passed += res.passed;
        total_failed += res.failed;
        total_skipped += res.skipped;
        if (res.status == "unavailable") total_unavailable++;
        results.push_back(std::move(res));
    }

    std::string summary_status =
        (total_failed == 0 && total_unavailable == 0) ? "passed" : "failed";

    std::ofstream out(output_file);
    if (!out) {
        std::println(std::cerr, "Failed to open output file: {}", output_file.string());
        return 1;
    }

    struct utsname uts{};
    const bool has_uname = (::uname(&uts) == 0);
    const std::string_view host_system = has_uname ? std::string_view(uts.sysname) : "Linux";
    const std::string_view host_machine = has_uname ? std::string_view(uts.machine) : "unknown";

    out << "{\n";
    out << "  \"schema_version\": 1,\n";
    out << "  \"simrv\": {\n";
    out << "    \"version\": \"" << escape_json(simrv::buildinfo::kVersion) << "\",\n";
    out << "    \"revision\": \"" << escape_json(git_rev(root)) << "\"\n";
    out << "  },\n";
    out << "  \"host\": {\n";
    out << "    \"system\": \"" << escape_json(host_system) << "\",\n";
    out << "    \"machine\": \"" << escape_json(host_machine) << "\"\n";
    out << "  },\n";
    out << "  \"dependencies\": {";
    if (!dependencies.empty()) {
        out << "\n";
        for (size_t i = 0; i < dependencies.size(); ++i) {
            out << "    \"" << escape_json(dependencies[i].first) << "\": \""
                << escape_json(dependencies[i].second) << "\""
                << (i + 1 < dependencies.size() ? "," : "") << "\n";
        }
        out << "  ";
    }
    out << "},\n";
    out << "  \"configurations\": [\n";
    out << "    {\n";
    out << "      \"arch\": " << arch << ",\n";
    out << "      \"compiler\": \"" << escape_json(compiler) << "\"\n";
    out << "    }\n";
    out << "  ],\n";
    out << "  \"summary\": {\n";
    out << "    \"status\": \"" << summary_status << "\",\n";
    out << "    \"passed\": " << total_passed << ",\n";
    out << "    \"failed\": " << total_failed << ",\n";
    out << "    \"skipped\": " << total_skipped << ",\n";
    out << "    \"unavailable\": " << total_unavailable << "\n";
    out << "  },\n";
    out << "  \"suites\": [\n";
    for (size_t i = 0; i < results.size(); ++i) {
        const auto& r = results[i];
        out << "    {\n";
        out << "      \"id\": \"" << r.id << "\",\n";
        out << "      \"label\": \"" << r.label << "\",\n";
        out << "      \"status\": \"" << r.status << "\",\n";
        out << "      \"registered\": " << r.registered << ",\n";
        out << "      \"passed\": " << r.passed << ",\n";
        out << "      \"failed\": " << r.failed << ",\n";
        out << "      \"skipped\": " << r.skipped << ",\n";
        out << "      \"duration_seconds\": " << r.duration_seconds;
        if (!r.output.empty()) {
            out << ",\n      \"output\": \"" << escape_json(r.output) << "\"\n";
        } else {
            out << "\n";
        }
        out << "    }" << (i + 1 < results.size() ? "," : "") << "\n";
    }
    out << "  ]\n";
    out << "}\n";

    std::println("release-evidence written to {}", output_file.string());
    return 0;
}
