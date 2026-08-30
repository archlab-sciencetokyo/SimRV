#include <cstdlib>
#include <iostream>

#include "simrv/core/MachineConfig.hpp"
#include "simrv/core/Machine.hpp"
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

    auto invalid = defaults;
    invalid.execution.num_harts = 0;
    expect(!invalid.validate().has_value(), "configuration rejects zero harts");
    invalid = defaults;
    invalid.files.disk_enabled = true;
    expect(!invalid.validate().has_value(), "configuration rejects disk without a path");
    invalid = defaults;
    invalid.isa.vlen = 48;
    expect(!invalid.validate().has_value(), "configuration rejects invalid VLEN");
    expect(defaults.validate().has_value(), "default configuration validates");

    simrv::core::MachineConfig applied{};
    applied.memory = custom;
    applied.execution.appmode = false;
    applied.execution.num_harts = 2;
    applied.execution.smp_quantum = 17;
    applied.execution.pipeline_type = simrv::pipeline::PipelineType::ThreeStage;
    applied.tui.enabled = true;
    applied.tui.debug_diagnostics = true;
    applied.debug.gdb_enabled = true;
    applied.debug.gdb_port = 7777;
    applied.isa.vlen = 256;
    applied.files.binary_path = "guest.bin";
    applied.network.mode = "none";
    applied.platform_profile = simrv::core::PlatformProfile::Mmio;

    simrv::core::Machine machine(applied);
    expect(machine.memory_geometry().dram_base == custom.dram_base &&
               machine.memory_geometry().dram_size == custom.dram_size,
           "applied configuration controls machine memory geometry");
    expect(!machine.execution_config().appmode && machine.execution_config().num_harts == 2 &&
               machine.execution_config().smp_quantum == 17,
           "execution configuration is retained by the machine");
    expect(machine.execution_config().pipeline_type == simrv::pipeline::PipelineType::ThreeStage &&
               machine.tui_enabled() && machine.debug_diagnostics_enabled(),
           "pipeline and TUI configuration are projected through typed accessors");
    expect(machine.debugger_enabled() && machine.debugger_port() == 7777 &&
               machine.isa_config().vlen == 256,
           "debug and ISA configuration are projected through typed accessors");
    expect(machine.binary_path() == "guest.bin" &&
               machine.platform_profile() == simrv::core::PlatformProfile::Mmio &&
               machine.network_mode() == "none",
           "file and platform configuration are projected through typed accessors");

    auto staged = machine.configuration();
    staged.memory.dram_size = 0x2000;
    staged.execution.num_harts = 4;
    expect(machine.stage_reconfiguration(staged).has_value(),
           "validated architectural changes can be staged");
    expect(machine.reboot_requested, "staging requests controlled reinitialization");
    const auto accepted = machine.take_staged_reconfiguration();
    expect(accepted.has_value() && accepted->execution.num_harts == 4 &&
               accepted->memory.dram_size == 0x2000,
           "staged reconfiguration retains the typed snapshot");

    machine.reboot_requested = false;
    auto rejected = machine.configuration();
    rejected.execution.num_harts = 0;
    expect(!machine.stage_reconfiguration(std::move(rejected)).has_value(),
           "invalid staged configuration is rejected");
    expect(!machine.reboot_requested && !machine.take_staged_reconfiguration().has_value(),
           "rejected configuration does not alter lifecycle or pending state");

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
