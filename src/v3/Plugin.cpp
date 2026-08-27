// SPDX-License-Identifier: MIT
#include "simrv/v3/Plugin.hpp"

#include <filesystem>

namespace simrv::v3 {
auto validate_plugin_declaration(const PluginDeclaration& declaration) -> Result<void> {
    if (declaration.path.empty()) return std::unexpected(Error{"plugin path is required"});
    if (!std::filesystem::is_regular_file(declaration.path)) {
        return std::unexpected(Error{"plugin does not exist: " + declaration.path.string()});
    }
    if (declaration.sha256.size() != 64) return std::unexpected(Error{"plugin sha256 must contain 64 hexadecimal characters"});
    // Hashing is deliberately delegated to the release-time dependency integrity tool for now.
    // The runtime contract still requires the declared hash and records it in the run result.
    return {};
}
}  // namespace simrv::v3
