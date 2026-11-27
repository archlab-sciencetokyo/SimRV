/**
 * @file CheckLinuxArtifacts.cpp
 * @brief Native C++ validation utility for SimRV Linux image artifacts and manifests.
 */
#include <array>
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

#include "simrv/util/Sha256.hpp"

namespace {

namespace fs = std::filesystem;

void fail(std::string_view message) {
    std::println(std::cerr, "check-linux-artifacts: error: {}", message);
    std::exit(1);
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

auto read_file_string(const fs::path& path) -> std::string {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) return {};
    return std::string((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

void print_help(std::string_view prog) {
    std::println(
        "Usage: {} <IMAGES_DIR>\n\n"
        "Validate a published SimRV Linux profile bundle and its reproducibility manifest.\n",
        prog);
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
    std::string_view prog = argc > 0 ? argv[0] : "simrv-check-artifacts";
    fs::path images_dir;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help(prog);
            return 0;
        } else if (!arg.starts_with("-")) {
            images_dir = arg;
        }
    }

    if (images_dir.empty()) {
        fail("missing IMAGES_DIR argument");
    }

    if (!fs::is_directory(images_dir)) {
        fail("directory not found: " + images_dir.string());
    }

    const auto manifest_path = images_dir / "manifest.json";
    if (!fs::is_regular_file(manifest_path)) {
        fail("missing " + manifest_path.string());
    }

    const auto manifest_text = read_file_string(manifest_path);

    const std::vector<std::string> required = {
        "opensbi-linux-payload.elf", "opensbi-dynamic.bin", "Image", "devicetree.dtb", "rootfs.img",
    };

    for (const auto& req : required) {
        const auto file_path = images_dir / req;
        if (!fs::is_regular_file(file_path)) {
            fail("missing required artifact: " + req);
        }
    }

    // Verify SHA-256 for all artifacts declared in manifest.json
    const std::regex art_re(
        R"re("(opensbi-[^"]+|Image|devicetree\.dtb|rootfs\.(?:img|ext4))":\s*"([0-9a-fA-F]{64})")re");
    auto art_begin = std::sregex_iterator(manifest_text.begin(), manifest_text.end(), art_re);
    auto art_end = std::sregex_iterator();
    int verified_count = 0;

    for (auto it = art_begin; it != art_end; ++it) {
        const std::string name = (*it)[1].str();
        const std::string expected_hash = (*it)[2].str();
        const auto file_path = images_dir / name;
        if (!fs::is_regular_file(file_path)) {
            fail("manifest lists missing artifact: " + name);
        }
        auto computed = simrv::util::sha256_file(file_path);
        if (!computed) {
            fail("failed to compute hash for " + name);
        }
        if (*computed != expected_hash) {
            fail("SHA-256 mismatch for " + name + ": " + *computed + " != " + expected_hash);
        }
        ++verified_count;
    }

    if (verified_count == 0) {
        fail("manifest contains no verified artifacts in artifacts table");
    }

    // Verify bootargs
    for (const auto& token :
         {"console=ttyS0,115200", "root=LABEL=", "rootfstype=ext4", "rootwait", "rw"}) {
        if (manifest_text.find(token) == std::string::npos) {
            fail(std::string("bootargs missing required token: ") + token);
        }
    }

    // Check debug rdinit
    if (manifest_text.find("rdinit=/init") != std::string::npos &&
        manifest_text.find("\"profile\": \"debug\"") == std::string::npos &&
        manifest_text.find("\"profile\":\"debug\"") == std::string::npos) {
        fail("only explicit debug profile may declare rdinit=/init");
    }

    // Optional tune2fs check
    auto [which_t, _] = run_cmd("which tune2fs");
    if (which_t == 0) {
        auto [t_code, t_out] = run_cmd("tune2fs -l " + (images_dir / "rootfs.img").string());
        if (t_code == 0) {
            if (t_out.find("has_journal") == std::string::npos) {
                fail("rootfs.img does not have an ext4 journal");
            }
        }
    }

    std::println("check-linux-artifacts: all checks passed for {}", images_dir.string());
    return 0;
}
