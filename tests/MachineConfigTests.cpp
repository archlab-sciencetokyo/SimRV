#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "simrv/core/Machine.hpp"
#include "simrv/core/MachineConfig.hpp"
#include "simrv/core/Telemetry.hpp"
#include "simrv/util/CliParser.hpp"

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
    expect(!defaults.memory.contains(defaults.memory.dram_base - 1),
           "address below DRAM is rejected");
    expect(!defaults.memory.contains(defaults.memory.dram_base + defaults.memory.dram_size, 1),
           "address at DRAM end is rejected");

    const simrv::core::MemoryGeometry custom{.dram_base = 0x40000000, .dram_size = 0x1000};
    expect(custom.contains(0x40000FFC, 4), "custom geometry contains final word");
    expect(!custom.contains(0x40000FFC, 8), "custom geometry rejects overflow");

    auto invalid = defaults;
    invalid.execution.num_harts = 0;
    expect(!invalid.validate().has_value(), "configuration rejects zero harts");
    invalid = defaults;
    invalid.execution.num_harts = simrv::core::ExecutionConfig::kMaxHarts + 1;
    expect(!invalid.validate().has_value(), "configuration rejects harts beyond platform capacity");
    invalid = defaults;
    invalid.execution.num_harts = simrv::core::ExecutionConfig::kMaxHarts;
    expect(invalid.validate().has_value(), "configuration accepts platform hart capacity");
    invalid = defaults;
    invalid.files.disk_enabled = true;
    expect(!invalid.validate().has_value(), "configuration rejects disk without a path");
    invalid = defaults;
    invalid.isa.vlen = 48;
    expect(!invalid.validate().has_value(), "configuration rejects invalid VLEN");
    invalid = defaults;
    invalid.isa.vlen = 64;
    expect(!invalid.validate().has_value(), "full V rejects VLEN below 128 bits");
    invalid = defaults;
    invalid.isa.vlen = 192;
    expect(!invalid.validate().has_value(), "configuration rejects non-power-of-two VLEN");
    invalid = defaults;
    invalid.isa.vlen = 128;
    expect(invalid.validate().has_value(), "configuration accepts minimum full-V VLEN");
    invalid = defaults;
    invalid.tui.enabled = true;
    invalid.debug.gdb_enabled = true;
    expect(!invalid.validate().has_value(), "configuration rejects TUI with GDB");
    invalid = defaults;
    invalid.debug.gdb_enabled = true;
    invalid.debug.lockstep_enabled = true;
    expect(!invalid.validate().has_value(), "configuration rejects GDB with lockstep");
    expect(defaults.validate().has_value(), "default configuration validates");

    simrv::core::MachineConfig applied{};
    applied.memory = custom;
    applied.execution.appmode = false;
    applied.execution.num_harts = 2;
    applied.execution.smp_quantum = 17;
    applied.execution.pipeline_type = simrv::pipeline::PipelineType::ThreeStage;
    applied.tui.enabled = false;
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
               !machine.tui_enabled(),
           "pipeline and CLI configuration are projected through typed accessors");
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

    // C++23 explicit object parameter (deducing this) builder chaining test
    auto fluent = simrv::core::MachineConfig{}
                      .with_harts(3)
                      .with_dram_base(0x80000000)
                      .with_dram_size(0x40000000)
                      .with_appmode(false)
                      .with_smp_quantum(42)
                      .with_pipeline(simrv::pipeline::PipelineType::ThreeStage)
                      .with_misa_xlen(64)
                      .with_vlen(128)
                      .with_binary("kernel.elf")
                      .with_platform_profile(simrv::core::PlatformProfile::Mmio);

    expect(fluent.execution.num_harts == 3, "builder sets num_harts on rvalue");
    expect(fluent.memory.dram_base == 0x80000000 && fluent.memory.dram_size == 0x40000000,
           "builder sets memory geometry on rvalue");
    expect(!fluent.execution.appmode && fluent.execution.smp_quantum == 42,
           "builder sets execution config on rvalue");
    expect(fluent.execution.pipeline_type == simrv::pipeline::PipelineType::ThreeStage,
           "builder sets pipeline type on rvalue");
    expect(fluent.isa.misa_xlen == 64 && fluent.isa.vlen == 128,
           "builder sets ISA config on rvalue");
    expect(fluent.files.binary_path == "kernel.elf", "builder sets binary path on rvalue");
    expect(fluent.platform_profile == simrv::core::PlatformProfile::Mmio,
           "builder sets platform profile on rvalue");

    // Lvalue chaining test
    fluent.with_harts(5).with_smp_quantum(99).with_realtime_pacing(true);
    expect(fluent.execution.num_harts == 5 && fluent.execution.smp_quantum == 99 &&
               fluent.execution.realtime_pacing,
           "builder chains on lvalue including realtime pacing");

    // Real-time pacing machine accessors and pacing control
    expect(!machine.is_realtime_pacing_enabled(),
           "machine starts with default realtime pacing false");
    machine.set_realtime_pacing_enabled(true);
    expect(machine.is_realtime_pacing_enabled(), "set_realtime_pacing_enabled enables pacing");
    machine.pace_realtime();
    machine.set_realtime_pacing_enabled(false);
    expect(!machine.is_realtime_pacing_enabled(), "set_realtime_pacing_enabled disables pacing");

    // Real-time pacing CLI option parsing tests
    {
        std::array<std::string, 4> rt_args_str = {"SimRV", "-R", "-m", "guest.bin"};
        std::array<char*, 4> rt_args = {rt_args_str[0].data(), rt_args_str[1].data(),
                                        rt_args_str[2].data(), rt_args_str[3].data()};
        auto parsed = simrv::util::parse_command_line(rt_args);
        expect(parsed.has_value() && parsed->options.realtime_pacing,
               "-R flag enables realtime pacing option");
        if (parsed) {
            auto cfg = parsed->options.to_machine_config();
            expect(cfg.execution.realtime_pacing, "-R enables realtime pacing in MachineConfig");
        }
    }
    {
        std::array<std::string, 4> rt_long_str = {"SimRV", "--realtime", "-m", "guest.bin"};
        std::array<char*, 4> rt_long = {rt_long_str[0].data(), rt_long_str[1].data(),
                                        rt_long_str[2].data(), rt_long_str[3].data()};
        auto parsed = simrv::util::parse_command_line(rt_long);
        expect(parsed.has_value() && parsed->options.realtime_pacing,
               "--realtime flag enables realtime pacing option");
    }
    {
        std::array<std::string, 5> no_rt_str = {"SimRV", "--tui", "--no-realtime", "-m",
                                                "guest.bin"};
        std::array<char*, 5> no_rt = {no_rt_str[0].data(), no_rt_str[1].data(), no_rt_str[2].data(),
                                      no_rt_str[3].data(), no_rt_str[4].data()};
        auto parsed = simrv::util::parse_command_line(no_rt);
        expect(parsed.has_value() && !parsed->options.realtime_pacing,
               "--no-realtime flag disables realtime pacing option");
        if (parsed) {
            auto cfg = parsed->options.to_machine_config();
            expect(!cfg.execution.realtime_pacing,
                   "--no-realtime overrides TUI default in MachineConfig");
        }
    }
    {
        std::array<std::string, 5> trace_args_str = {"SimRV", "--arch-trace", "trace.jsonl",
                                                     "-m", "guest.bin"};
        std::array<char*, 5> trace_args = {trace_args_str[0].data(), trace_args_str[1].data(),
                                           trace_args_str[2].data(), trace_args_str[3].data(),
                                           trace_args_str[4].data()};
        auto parsed = simrv::util::parse_command_line(trace_args);
        expect(parsed.has_value() && parsed->options.fn_archtrace == "trace.jsonl" &&
                   parsed->options.execution_mode == simrv::util::RequestedExecutionMode::Detailed,
               "--arch-trace selects detailed execution and retains its output path");
        if (parsed) {
            const auto cfg = parsed->options.to_machine_config();
            expect(cfg.debug.architecture_trace_path == "trace.jsonl",
                   "architecture trace path is projected into MachineConfig");
        }
    }
    {
        std::array<std::string, 4> trace_flag_str = {"SimRV", "--trace", "-m", "guest.bin"};
        std::array<char*, 4> trace_flag = {trace_flag_str[0].data(), trace_flag_str[1].data(),
                                           trace_flag_str[2].data(), trace_flag_str[3].data()};
        auto parsed = simrv::util::parse_command_line(trace_flag);
        expect(parsed.has_value() && parsed->options.trace_enabled &&
                   parsed->options.trace_begin == 0 &&
                   parsed->options.trace_end == std::numeric_limits<Counter>::max(),
               "--trace enables the complete aligned instruction trace");
    }

    // Architectural checkpoint round-trip and rejection coverage.  Keep the image small so this
    // remains a fast native gate while exercising vector/CSR state and RAM contents.
    {
        const auto checkpoint =
            std::filesystem::temp_directory_path() / "simrv-checkpoint-test.bin";
        const auto malformed =
            std::filesystem::temp_directory_path() / "simrv-checkpoint-bad.bin";
        simrv::core::MachineConfig checkpoint_config{};
        checkpoint_config.memory.dram_base = 0x80000000;
        checkpoint_config.memory.dram_size = 4096;
        checkpoint_config.isa.vlen = 256;
        std::vector<Byte> source_ram(4096, Byte{0});
        std::vector<Byte> restored_ram(4096, Byte{0});
        simrv::core::Machine source(checkpoint_config);
        source.set_ram_for_testing(source_ram.data(), source_ram.size());
        auto& source_state = source.primary_hart().state();
        source_state.pc = 0x80000100;
        source_state.regs.vlen = 256;
        source_state.regs.write(simrv::RegId::T2, 0x12345678);
        source_state.vtype = 0x23;
        source_state.vl = 17;
        source_state.mstatus = 0x1800;
        source_state.mepc = 0x80000200;
        source_state.pmpaddr[0] = 0x1234;
        source.primary_hart().e_icount = 77;
        source.primary_hart().e_ccount = 91;
        source.primary_hart().clint_mmio.mcycle = 105;
        source_ram[123] = Byte{0xA5};
        expect(source.save_checkpoint(checkpoint.string()).has_value(),
               "checkpoint saves architectural state");

        simrv::core::Machine restored(checkpoint_config);
        restored.set_ram_for_testing(restored_ram.data(), restored_ram.size());
        expect(restored.load_checkpoint(checkpoint.string()).has_value(),
               "checkpoint restores architectural state");
        const auto& restored_state = restored.primary_hart().state();
        expect(restored_state.pc == source_state.pc &&
                   restored_state.regs.read(simrv::RegId::T2) == 0x12345678,
               "checkpoint restores PC and integer registers");
        expect(restored_state.vtype == source_state.vtype && restored_state.vl == source_state.vl &&
                   restored_state.mepc == source_state.mepc && restored_state.pmpaddr[0] == 0x1234,
               "checkpoint restores vector, CSR, and PMP state");
        expect(restored.primary_hart().e_icount == 77 && restored.primary_hart().e_ccount == 91 &&
                   restored.primary_hart().clint_mmio.mcycle == 105 &&
                   restored_ram[123] == Byte{0xA5},
               "checkpoint restores counters and physical RAM");

        {
            std::ofstream bad(malformed, std::ios::binary | std::ios::trunc);
            bad << "SIMRVCP";
        }
        expect(!restored.load_checkpoint(malformed.string()).has_value(),
               "truncated checkpoint is rejected");
        auto mismatched_config = checkpoint_config;
        mismatched_config.isa.vlen = 128;
        simrv::core::Machine mismatched(mismatched_config);
        mismatched.set_ram_for_testing(restored_ram.data(), restored_ram.size());
        expect(!mismatched.load_checkpoint(checkpoint.string()).has_value(),
               "checkpoint with mismatched VLEN is rejected");
        std::error_code cleanup_error;
        std::filesystem::remove(checkpoint, cleanup_error);
        std::filesystem::remove(malformed, cleanup_error);
    }

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
