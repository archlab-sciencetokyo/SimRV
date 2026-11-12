#include <string_view>

#include "simrv/core/BuildInfo.hpp"
#include "simrv/core/MachineConfig.hpp"
#include "simrv/device/ExternalSerialDevice.h"
#include "simrv/device/SerialDevice.hpp"

static_assert(SIMRV_EXTERNAL_SERIAL_API_VERSION == 1);

int main() {
    simrv::core::MachineConfig config{};
    const auto valid = config.validate();
    return (!std::string_view(simrv::buildinfo::kVersion).empty() && config.execution.appmode &&
            valid)
               ? 0
               : 1;
}
