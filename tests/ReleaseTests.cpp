/**
 * @file ReleaseTests.cpp
 * @brief Native Boost.UT regression tests for release metadata, packaging, schemas, and catalogs.
 */
#include <boost/ut.hpp>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string>

namespace {

namespace fs = std::filesystem;
using namespace boost::ut;

auto read_file_string(const fs::path& p) -> std::string {
    std::ifstream stream(p, std::ios::binary);
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

auto get_source_version(const fs::path& root) -> std::string {
    const auto content = read_file_string(root / "CMakeLists.txt");
    std::smatch match;
    const std::regex re("set\\(SIMRV_VERSION \"([^\"]+)\"\\)");
    if (std::regex_search(content, match, re) && match.size() > 1) {
        return match.str(1);
    }
    return {};
}

auto artifact_contains_version(const std::string& artifact, const std::string& version) -> bool {
    if (artifact.find(version) != std::string::npos) return true;
    const auto dash = version.find('-');
    if (dash != std::string::npos) {
        const auto base = version.substr(0, dash);
        const auto prerelease = version.substr(dash + 1);
        const auto deb_candidate = base + "~" + prerelease;
        const auto rpm_candidate = base + "-0." + prerelease;
        if (artifact.find(deb_candidate) != std::string::npos) return true;
        if (artifact.find(rpm_candidate) != std::string::npos) return true;
    }
    return false;
}

}  // namespace

int main() {
    const auto root = find_project_root();
    const auto source_ver = get_source_version(root);

    "release_metadata_and_manifest"_test = [&] {
        expect(!source_ver.empty());

        const auto manifest_path = root / "release/release-manifest.json";
        expect(fs::exists(manifest_path));
        const auto manifest = read_file_string(manifest_path);

        expect(manifest.find(R"("schema_version": 2)") != std::string::npos ||
               manifest.find(R"("schema_version":2)") != std::string::npos);

        expect(manifest.find(std::string("\"version\": \"") + source_ver + "\"") !=
               std::string::npos);

        // Check required artifacts
        expect(manifest.find(std::string("SimRV-linux-x86_64-v") + source_ver + ".tar.gz") !=
               std::string::npos);
        expect(manifest.find(std::string("SimRV-rpm-packages-v") + source_ver + ".tar.gz") !=
               std::string::npos);
        expect(manifest.find(std::string("SimRV-deb-packages-v") + source_ver + ".tar.gz") !=
               std::string::npos);

        // Package support
        expect(manifest.find("Ubuntu 22.04") != std::string::npos);
        expect(manifest.find("Rocky Linux 9") != std::string::npos);
        expect(manifest.find("Fedora 44") != std::string::npos);
        expect(manifest.find("Ubuntu 24.04") != std::string::npos);
        expect(manifest.find("Debian sid") != std::string::npos);

        // Schema existence
        expect(fs::exists(root / "release/schemas/release-manifest.schema.json"));
        expect(fs::exists(root / "release/schemas/evidence.schema.json"));
        expect(fs::exists(root / "schemas/inspection-report.schema.json"));
    };

    "citation_cff_matches_source_version"_test = [&] {
        const auto citation = read_file_string(root / "CITATION.cff");
        expect(!citation.empty());
        expect(citation.find(std::string("version: ") + source_ver) != std::string::npos ||
               citation.find(std::string("version: '") + source_ver + "'") != std::string::npos ||
               citation.find(std::string("version: \"") + source_ver + "\"") != std::string::npos);

        std::smatch match;
        const std::regex date_re(R"(date-released:\s+\S+)");
        expect(std::regex_search(citation, match, date_re));
    };

    "packaging_and_zero_python_dependencies"_test = [&] {
        const auto cmake_packaging = read_file_string(root / "cmake/SimRVPackaging.cmake");
        expect(!cmake_packaging.empty());
        expect(cmake_packaging.find("CPACK_COMPONENTS_ALL Runtime Tools Benchmark Development") !=
               std::string::npos);
        expect(cmake_packaging.find("SimRVBuildMetadata.json") != std::string::npos);
        expect(
            cmake_packaging.find(R"(set(CPACK_DEBIAN_DEVELOPMENT_PACKAGE_NAME "simrv-devel"))") !=
            std::string::npos);
        expect(cmake_packaging.find(R"(set(CPACK_RPM_DEVELOPMENT_PACKAGE_NAME "simrv-devel"))") !=
               std::string::npos);
        // Packaging must have zero python runtime dependencies
        expect(cmake_packaging.find(", python3") == std::string::npos);
    };

    "workflow_contracts"_test = [&] {
        const auto release_wf = read_file_string(root / ".github/workflows/release-binaries.yml");
        expect(!release_wf.empty());
        expect(release_wf.find("publish-release:") != std::string::npos);
        expect(release_wf.find("SimRV-rpm-packages-v${VERSION}.tar.gz") != std::string::npos);
        expect(release_wf.find("SimRV-deb-packages-v${VERSION}.tar.gz") != std::string::npos);
        expect(release_wf.find(".sha256") == std::string::npos);
        expect(release_wf.find("reproducibility-framework:") == std::string::npos);
        expect(release_wf.find("documentation:") == std::string::npos);
        expect(release_wf.find("simrv-devel") != std::string::npos);

        const auto docs_wf = read_file_string(root / ".github/workflows/docs.yml");
        expect(!docs_wf.empty());
        expect(docs_wf.find("zensical build --strict") != std::string::npos);
        expect(docs_wf.find("uv sync") == std::string::npos);
        expect(docs_wf.find("uv.lock") == std::string::npos);
        expect(docs_wf.find("pyproject.toml") == std::string::npos);
    };

    "cmake_presets_native_release"_test = [&] {
        const auto presets = read_file_string(root / "CMakePresets.json");
        expect(!presets.empty());
        expect(presets.find("rv64-native-release") != std::string::npos);
        expect(presets.find(R"("CMAKE_C_COMPILER": "clang")") != std::string::npos);
        expect(presets.find(R"("CMAKE_CXX_COMPILER": "clang++")") != std::string::npos);
        expect(presets.find(R"("SIMRV_NATIVE_HOST_OPTIMIZATIONS": "ON")") != std::string::npos);
    };

    "inspection_report_schema_privacy"_test = [&] {
        const auto schema = read_file_string(root / "schemas/inspection-report.schema.json");
        expect(!schema.empty());
        expect(schema.find(R"("const": 1)") != std::string::npos ||
               schema.find(R"("const":1)") != std::string::npos);
        expect(schema.find("student_id") == std::string::npos);
        expect(schema.find("grade") == std::string::npos);
    };

    "semver_artifact_encoding"_test = [&] {
        const std::string version = "3.0.0-rc.19";
        expect(artifact_contains_version("SimRV-v3.0.0-rc.19.tar.gz", version));
        expect(artifact_contains_version("simrv_3.0.0~rc.19_amd64.deb", version));
        expect(artifact_contains_version("simrv-3.0.0-0.rc.19.x86_64.rpm", version));
        expect(!artifact_contains_version("simrv-3.0.0-0.beta.3.x86_64.rpm", version));
    };

    "guest_capabilities_dts_templates"_test = [&] {
        const auto header =
            read_file_string(root / "include/simrv/core/GuestCapabilityCatalog.hpp");
        const auto hpm_def = read_file_string(root / "include/simrv/core/HpmEventCatalog.def");
        const auto dts32 = read_file_string(root / "scripts/templates/virt-rv32.dts");
        const auto dts64 = read_file_string(root / "scripts/templates/virt-rv64.dts");

        expect(!header.empty());
        expect(!hpm_def.empty());
        expect(!dts32.empty());
        expect(!dts64.empty());

        expect(dts32.find("riscv,isa-extensions =") != std::string::npos);
        expect(dts64.find("riscv,isa-extensions =") != std::string::npos);
        expect(dts64.find("cbom-block-size") != std::string::npos);
        expect(dts32.find("cbom-block-size") != std::string::npos);

        // HPM PMU properties synchronized with HpmEventCatalog.def
        expect(dts32.find("riscv,raw-event-to-mhpmcounters =") != std::string::npos);
        expect(dts64.find("riscv,raw-event-to-mhpmcounters =") != std::string::npos);
        expect(dts32.find("riscv,event-to-mhpmevent =") != std::string::npos);
        expect(dts64.find("riscv,event-to-mhpmevent =") != std::string::npos);
    };

    return 0;
}
