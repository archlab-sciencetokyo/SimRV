#include <cstdint>
#include <string_view>

#include "simrv/v3/Plugin.hpp"
#include "simrv/v3/RunManifest.hpp"
#include "simrv/v3/Simulator.hpp"

namespace {
class ConsumerPlugin final : public simrv::v3::Plugin {
   public:
    [[nodiscard]] auto api_version() const noexcept -> uint32_t override {
        return simrv::v3::kPluginApiVersion;
    }
    [[nodiscard]] auto name() const noexcept -> std::string_view override { return "consumer"; }
};
}  // namespace

int main() {
    simrv::v3::RunManifest manifest;
    ConsumerPlugin plugin;
    return manifest.schema_version == simrv::v3::RunManifest::kSchemaVersion &&
                   plugin.api_version() == simrv::v3::kPluginApiVersion
               ? 0
               : 1;
}
