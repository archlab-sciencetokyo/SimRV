/**
 * @file GaSmoke.cpp
 * @brief Native C++ GA smoke verification utility for SimRV release binaries.
 */
#include <unistd.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <system_error>

namespace {

namespace fs = std::filesystem;

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

void print_help(std::string_view prog) {
    std::println(
        "Usage: {} --simrv <PATH> [--guest <ELF>]\n\n"
        "Run the distribution-independent SimRV GA smoke verification workflow.\n\n"
        "Options:\n"
        "  -h, --help            Show this help message and exit\n"
        "  --simrv <PATH>        Path to SimRV binary (required)\n"
        "  --guest <ELF>         Path to guest baremetal test ELF (optional)\n",
        prog);
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
    std::string_view prog = argc > 0 ? argv[0] : "simrv-smoke";
    fs::path simrv_path;
    fs::path guest_path;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help(prog);
            return 0;
        } else if (arg == "--simrv" && i + 1 < argc) {
            simrv_path = argv[++i];
        } else if (arg.starts_with("--simrv=")) {
            simrv_path = arg.substr(8);
        } else if (arg == "--guest" && i + 1 < argc) {
            guest_path = argv[++i];
        } else if (arg.starts_with("--guest=")) {
            guest_path = arg.substr(8);
        } else {
            std::println(std::cerr, "Unknown argument: {}", arg);
            print_help(prog);
            return 1;
        }
    }

    if (simrv_path.empty()) {
        std::println(std::cerr, "error: --simrv is required");
        print_help(prog);
        return 1;
    }

    std::error_code ec;
    simrv_path = fs::canonical(simrv_path, ec);
    if (ec || !fs::is_regular_file(simrv_path)) {
        std::println(std::cerr, "error: simrv binary does not exist: {}", simrv_path.string());
        return 1;
    }

    // 1. Check version
    auto [v_code, v_out] = run_cmd(simrv_path.string() + " --version");
    if (v_code != 0) {
        std::println(std::cerr, "error: --version failed:\n{}", v_out);
        return 1;
    }

    // 2. Check capabilities / ISA info
    auto [c_code, c_out] = run_cmd(simrv_path.string() + " --isa-info");
    if (c_code != 0) {
        std::println(std::cerr, "error: --isa-info failed:\n{}", c_out);
        return 1;
    }
    if (c_out.find("RV32GCBV") == std::string::npos ||
        c_out.find("RV64GCBV") == std::string::npos) {
        std::println(std::cerr, "error: capability report does not advertise both public profiles");
        return 1;
    }

    // 3. Check doctor
    auto [d_code, d_out] = run_cmd(simrv_path.string() + " --doctor");
    if (d_code != 0) {
        std::println(std::cerr, "error: --doctor failed:\n{}", d_out);
        return 1;
    }

    // 4. Dump and verify SoC manifest
    const fs::path temp_dir =
        fs::temp_directory_path() / ("simrv-ga-smoke-" + std::to_string(::getpid()));
    fs::create_directories(temp_dir, ec);
    const fs::path manifest_path = temp_dir / "soc.json";

    auto [m_code, m_out] =
        run_cmd(simrv_path.string() + " --dump-soc-manifest virt-pcie " + manifest_path.string());
    if (m_code != 0) {
        std::println(std::cerr, "error: failed to dump SoC manifest:\n{}", m_out);
        fs::remove_all(temp_dir, ec);
        return 1;
    }

    std::ifstream stream(manifest_path);
    std::string manifest_content((std::istreambuf_iterator<char>(stream)),
                                 std::istreambuf_iterator<char>());
    fs::remove_all(temp_dir, ec);

    if (manifest_content.find("\"schema_version\": 1") == std::string::npos &&
        manifest_content.find("\"schema_version\":1") == std::string::npos) {
        std::println(std::cerr, "error: unexpected SoC manifest schema version");
        return 1;
    }
    if (manifest_content.find("\"manifest_version\": \"3.0\"") == std::string::npos &&
        manifest_content.find("\"manifest_version\":\"3.0\"") == std::string::npos) {
        std::println(std::cerr, "error: SoC manifest is missing the 3.0 compatibility contract");
        return 1;
    }

    // 5. Run optional guest ELF
    if (!guest_path.empty()) {
        auto [g_code, g_out] = run_cmd(simrv_path.string() + " --cli --baremetal -m " +
                                       guest_path.string() + " --steps 100000");
        if (g_code != 0) {
            std::println(std::cerr, "error: guest execution failed:\n{}", g_out);
            return 1;
        }
    }

    std::println("ga-smoke: version, capability, environment, and SoC manifest checks passed");
    if (!guest_path.empty()) {
        std::println("ga-smoke: guest execution passed for {}", guest_path.string());
    }
    return 0;
}
