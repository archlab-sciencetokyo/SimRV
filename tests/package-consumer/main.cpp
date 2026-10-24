#include <string_view>

#include "simrv/core/BuildInfo.hpp"
#include "simrv/core/MachineConfig.hpp"

int main() {
    simrv::core::MachineConfig config{};
    const auto valid = config.validate();
    return (!std::string_view(simrv::buildinfo::kVersion).empty() && config.execution.appmode &&
            valid)
               ? 0
               : 1;
}
