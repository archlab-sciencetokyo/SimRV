// SPDX-License-Identifier: MIT
#include <cassert>
#include <filesystem>
#include <fstream>

#include "simrv/v3/RunManifest.hpp"
#include "simrv/v3/Simulator.hpp"

int main() {
    namespace fs = std::filesystem;
    const fs::path path = fs::temp_directory_path() / "simrv-v3-manifest.toml";
    {
        std::ofstream file(path);
        file << R"(schema_version = 1

[guest]
program = "guest.bin"

[machine]
baremetal = true
cycle_accurate = false
harts = 2
vlen = 256
misa = "gcbv"

[run]
tui = false
output_directory = "out"
)";
    }
    const auto manifest = simrv::v3::parse_manifest(path);
    assert(manifest);
    assert(manifest->harts == 2);
    assert(manifest->vlen == 256);
    assert(manifest->interaction == simrv::v3::Interaction::Headless);
    assert(simrv::v3::render_manifest(*manifest).find("schema_version = 1") != std::string::npos);
    assert(simrv::v3::Simulator::create(*manifest));
    auto invalid = *manifest;
    invalid.schema_version = 2;
    assert(!simrv::v3::validate_manifest(invalid));
    fs::remove(path);
}
