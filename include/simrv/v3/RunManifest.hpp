// SPDX-License-Identifier: MIT
#pragma once

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace simrv::v3 {

struct Error {
    std::string message;
};

template <typename T>
using Result = std::expected<T, Error>;

enum class Interaction { Headless, Tui };

struct PluginDeclaration {
    std::filesystem::path path;
    std::string sha256;
};

/// Versioned, resolved input to a simulator run.  This is the only configuration type exposed by
/// the v3 SDK; legacy command-line options deliberately remain internal compatibility code.
struct RunManifest {
    static constexpr unsigned kSchemaVersion = 1;

    unsigned schema_version = kSchemaVersion;
    std::filesystem::path program;
    std::filesystem::path disk;
    std::filesystem::path device_tree;
    std::filesystem::path output_directory;
    Interaction interaction = Interaction::Headless;
    bool baremetal = true;
    bool cycle_accurate = false;
    unsigned harts = 1;
    unsigned vlen = 0;
    std::string misa = "gcbv";
    std::vector<PluginDeclaration> plugins;
};

[[nodiscard]] auto parse_manifest(const std::filesystem::path& path) -> Result<RunManifest>;
[[nodiscard]] auto validate_manifest(const RunManifest& manifest) -> Result<void>;
[[nodiscard]] auto render_manifest(const RunManifest& manifest) -> std::string;

}  // namespace simrv::v3
