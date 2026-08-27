#include <string_view>

#include "simrv/core/BuildInfo.hpp"
#include "simrv/core/MachineConfig.hpp"

int main() {
    simrv::core::MachineConfig config{};
    return (!std::string_view(simrv::buildinfo::kVersion).empty() && config.execution.appmode) ? 0
                                                                                               : 1;
}
