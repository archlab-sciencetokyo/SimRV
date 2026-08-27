#include <cstdlib>
#include <iostream>

#include "simrv/core/MachineConfig.hpp"
#include "simrv/core/Telemetry.hpp"

namespace {

auto failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

}  // namespace

auto main() -> int {
    const simrv::core::MachineConfig defaults{};
    expect(defaults.memory.contains(defaults.memory.dram_base), "DRAM base is contained");
    expect(defaults.memory.contains(defaults.memory.dram_base, 4), "DRAM range is contained");
    expect(!defaults.memory.contains(defaults.memory.dram_base - 1), "address below DRAM is rejected");
    expect(!defaults.memory.contains(defaults.memory.dram_base + defaults.memory.dram_size, 1),
           "address at DRAM end is rejected");

    const simrv::core::MemoryGeometry custom{.dram_base = 0x40000000, .dram_size = 0x1000};
    expect(custom.contains(0x40000FFC, 4), "custom geometry contains final word");
    expect(!custom.contains(0x40000FFC, 8), "custom geometry rejects overflow");

    const simrv::core::LifecycleEvent event{
        .kind = simrv::core::LifecycleEventKind::ExitRequested,
        .instruction_count = 42,
        .exit_status = 7,
        .stop_reason = 3,
    };
    expect(event.kind == simrv::core::LifecycleEventKind::ExitRequested,
           "lifecycle telemetry preserves its event kind");
    expect(event.instruction_count == 42 && event.exit_status == 7,
           "lifecycle telemetry preserves value fields");

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
