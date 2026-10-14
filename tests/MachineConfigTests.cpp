#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/CpuConfigParser.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/MachineConfig.hpp"
#include "simrv/core/Telemetry.hpp"
#include "simrv/util/CliParser.hpp"
#include "simrv/util/FormatUtil.hpp"

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
    expect(simrv::util::color_enabled(true, "xterm-256color", false, false),
           "color is enabled on capable terminals by default");
    expect(!simrv::util::color_enabled(true, "xterm-256color", true, false),
           "NO_COLOR disables terminal color");
    expect(!simrv::util::color_enabled(true, "dumb", false, false),
           "TERM=dumb disables terminal color");
    expect(simrv::util::color_enabled(false, "dumb", true, true),
           "FORCE_COLOR explicitly overrides automatic color suppression");
    {
        const auto check_isa = [](std::string isa) {
            std::array<std::string, 5> args_str = {"SimRV", "--isa", std::move(isa), "-m",
                                                   "guest.elf"};
            std::array<char*, 5> args{};
            for (size_t i = 0; i < args_str.size(); ++i) args[i] = args_str[i].data();
            return simrv::util::parse_command_line(args);
        };
        const auto xlen = std::to_string(simrv::xlen::kXLenBits);
        expect(check_isa("rv" + xlen + "g_zicntr").has_value(),
               "CLI accepts a canonical G ISA string with Zicntr");
        const auto g = check_isa("rv" + xlen + "g");
        expect(g.has_value() && g->options.isa_preset == simrv::isa::IsaPreset::G,
               "CLI accepts the standard G shorthand as distinct from GC");
        expect(check_isa("rv" + xlen + "g_zicntr2p0").has_value(),
               "CLI accepts standard multi-letter extensions after G");
        expect(check_isa("rv" + xlen + "gc_zicntr2p0_zicsr2p0_zifencei2p0").has_value(),
               "CLI accepts standard versioned multi-letter extensions");
        expect(check_isa("rv" + xlen + "gcZicntr2p0").has_value(),
               "CLI accepts the canonical glued first multi-letter extension spelling");
        expect(check_isa("rv" + xlen + "gc_zicsr_zifencei_zicntr").has_value(),
               "CLI accepts explicit extensions implied by G");
        expect(
            check_isa("rv" + xlen + "gczb").has_value() &&
                check_isa("rv" + xlen + "gczb")->options.isa_preset == simrv::isa::IsaPreset::GC &&
                check_isa("rv" + xlen + "gczb")->options.has_zb_extensions,
            "CLI accepts GC with bitmanip");
        expect(check_isa("rv" + xlen + "gcv").has_value() &&
                   check_isa("rv" + xlen + "gcv")->options.isa_preset == simrv::isa::IsaPreset::GCV,
               "CLI accepts canonical GCV preset");
        expect(check_isa("rv" + xlen + "gczbv").has_value() &&
                   check_isa("rv" + xlen + "gczbv")->options.isa_preset ==
                       simrv::isa::IsaPreset::GCV &&
                   check_isa("rv" + xlen + "gczbv")->options.has_zb_extensions,
               "CLI accepts GCV with bitmanip");
        expect(check_isa("rv" + xlen + "gc_zba_zbb_zbc_zbs").has_value() &&
                   check_isa("rv" + xlen + "gc_zba_zbb_zbc_zbs")->options.isa_preset ==
                       simrv::isa::IsaPreset::GC &&
                   check_isa("rv" + xlen + "gc_zba_zbb_zbc_zbs")->options.has_zb_extensions,
               "CLI accepts ratified bitmanip sub-extensions and enables GC with bitmanip");
        expect(
            check_isa("rv" + xlen + "gc_zb").has_value() &&
                check_isa("rv" + xlen + "gc_zb")->options.isa_preset == simrv::isa::IsaPreset::GC &&
                check_isa("rv" + xlen + "gc_zb")->options.has_zb_extensions,
            "CLI accepts ratified zb shorthand and enables GC with bitmanip");
        expect(check_isa("rv" + xlen + "gcv_zba_zbb_zbc_zbs").has_value() &&
                   check_isa("rv" + xlen + "gcv_zba_zbb_zbc_zbs")->options.isa_preset ==
                       simrv::isa::IsaPreset::GCV &&
                   check_isa("rv" + xlen + "gcv_zba_zbb_zbc_zbs")->options.has_zb_extensions,
               "CLI accepts ratified bitmanip with vector base and enables GCV with bitmanip");
        expect(check_isa("rv" + xlen + "gc_zk").has_value(),
               "CLI accepts ratified scalar crypto zk shorthand");
        expect(!check_isa("rv" + xlen + "gcbv").has_value(),
               "CLI rejects legacy draft shorthand rv{}gcbv");
        expect(!check_isa("gcbv").has_value(), "CLI rejects legacy draft shorthand gcbv");
        expect(!check_isa("rv" + xlen + "gc_zfoo").has_value(),
               "CLI rejects unsupported multi-letter ISA extensions");
        expect(!check_isa("rv" + xlen + "gc_xsimrvtrace").has_value(),
               "CLI rejects the retired experimental Xsimrvtrace extension");
        expect(!check_isa("rv" + xlen + "gc_zicntr2p1").has_value(),
               "CLI rejects unsupported extension versions");
        expect(!check_isa("rv" + xlen + "gc_zicntr_zicntr").has_value(),
               "CLI rejects duplicate extension names");
    }
    expect(simrv::log::parse_format("text") == simrv::log::Format::Text,
           "text diagnostic format is accepted");
    expect(simrv::log::parse_format("JSON") == simrv::log::Format::Json,
           "JSON diagnostic format is case-insensitive");
    expect(simrv::log::parse_format("json-pretty") == simrv::log::Format::JsonPretty,
           "pretty JSON diagnostic format is accepted");
    expect(!simrv::log::parse_format("yaml").has_value(),
           "unsupported diagnostic format is rejected");
    {
        std::array<std::string, 7> args_str = {"SimRV",   "--log-format", "json",     "--log-file",
                                               "run.log", "-m",           "guest.elf"};
        std::array<char*, 7> args{};
        for (size_t i = 0; i < args_str.size(); ++i) args[i] = args_str[i].data();
        const auto parsed = simrv::util::parse_command_line(args);
        expect(parsed.has_value() && parsed->options.log_format == simrv::log::Format::Json &&
                   parsed->options.fn_log == "run.log",
               "CLI parses diagnostic log format and destination");
    }
    const simrv::core::MachineConfig defaults{};
    {
        simrv::core::SoCConfig soc{};
        std::istringstream profile{
            "[smp]\n"
            "hart_count = 4\n"
            "mode = cycle-oriented\n"
            "synchronization_quantum = 23\n"
            "[smp.cluster.0]\n"
            "harts = 0, 1\n"
            "l2_enabled = true\n"
            "l2_capacity_bytes = 65536\n"
            "l2_associativity = 4\n"
            "[smp.cluster.1]\n"
            "harts = 2, 3\n"
            "[smp.l3]\n"
            "enabled = false\n"};
        expect(simrv::core::parse_soc_config_stream(profile, soc) && soc.smp.hart_count == 4 &&
                   soc.smp.mode == simrv::core::SmpMode::CycleOriented &&
                   soc.smp.synchronization_quantum == 23 && soc.smp.clusters.size() == 2 &&
                   soc.smp.clusters[0].l2.enabled && !soc.smp.l3.enabled &&
                   soc.smp.validate().has_value(),
               "SoC profile parses and validates SMP clusters and shared cache topology");
        soc.smp.clusters[1].harts.pop_back();
        expect(!soc.smp.validate(), "SMP profile rejects incomplete cluster membership");
    }
    {
        std::vector<std::string> args_str = {
            "SimRV",          "--config",      "virt-mmio", "--harts", "4",        "--smp-mode",
            "cycle-oriented", "--smp-quantum", "11",        "-m",      "guest.bin"};
        std::vector<char*> args;
        for (auto& arg : args_str) args.push_back(arg.data());
        const auto parsed =
            simrv::util::parse_command_line(std::span<char* const>{args.data(), args.size()});
        if (parsed) {
            const auto cfg = parsed->options.to_machine_config();
            expect(cfg.execution.num_harts == 4 &&
                       cfg.execution.smp_mode == simrv::core::SmpMode::CycleOriented &&
                       cfg.execution.smp_quantum == 11 && cfg.soc.smp.hart_count == 4,
                   "explicit SMP CLI settings override profile defaults");
        } else {
            expect(false, "SMP CLI overrides parse");
        }
        args_str = {"SimRV", "--smp-multithreaded", "-m", "guest.bin"};
        args.clear();
        for (auto& arg : args_str) args.push_back(arg.data());
        const auto legacy =
            simrv::util::parse_command_line(std::span<char* const>{args.data(), args.size()});
        expect(!legacy, "removed multithreaded compatibility spelling is rejected");
    }
    expect(defaults.memory.contains(defaults.memory.dram_base), "DRAM base is contained");
    expect(defaults.memory.contains(defaults.memory.dram_base, 4), "DRAM range is contained");
    expect(!defaults.memory.contains(defaults.memory.dram_base - 1),
           "address below DRAM is rejected");
    expect(!defaults.memory.contains(defaults.memory.dram_base + defaults.memory.dram_size, 1),
           "address at DRAM end is rejected");

    {
        namespace fs = std::filesystem;
        const auto output =
            fs::temp_directory_path() /
            ("simrv-buildroot-output-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directories(output / "images");
        std::ofstream(output / "images/opensbi-linux-payload.elf").put('f');
        std::ofstream(output / "images/devicetree.dtb").put('d');
        std::ofstream(output / "images/rootfs.ext4").put('r');
        std::ofstream(output / "images/simrv-manifest.json") << "{\"dram_size_mb\": 512}\n";
        std::array<std::string, 3> args_str = {"SimRV", "--buildroot-output", output.string()};
        std::array<char*, 3> args{};
        for (size_t i = 0; i < args_str.size(); ++i) args[i] = args_str[i].data();
        const auto parsed = simrv::util::parse_command_line(args);
        expect(parsed.has_value() &&
                   parsed->options.fn_memimg.ends_with("opensbi-linux-payload.elf") &&
                   parsed->options.fn_dvtree.ends_with("devicetree.dtb") &&
                   parsed->options.fn_dskimg.ends_with("rootfs.ext4") && !parsed->options.appmode &&
                   parsed->options.use_disk && parsed->options.dram_size == 512ULL * 1024 * 1024,
               "Buildroot output option resolves the matching SimRV firmware, DTB, and rootfs");
        if (parsed) {
            const auto config = parsed->options.to_machine_config();
            expect(config.files.binary_path == parsed->options.fn_memimg &&
                       config.files.dvtree_path == parsed->options.fn_dvtree &&
                       config.files.disk_path == parsed->options.fn_dskimg &&
                       config.files.disk_enabled && config.memory.dram_size == 512ULL * 1024 * 1024,
                   "Buildroot output selection reaches the Linux machine configuration");
        }
        std::error_code ignored;
        fs::remove_all(output, ignored);
    }

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
    auto secondary_for_irq = std::make_unique<simrv::core::CPU>();
    secondary_for_irq->state().mhartid = 1;
    machine.add_hart_for_testing(std::move(secondary_for_irq));
    machine.set_hart_irq(simrv::xlen::HartId{1}, simrv::core::InterruptType::Software,
                         simrv::xlen::PrivilegeLevel::Machine, true);
    expect((machine.hart(1).state().mip & simrv::core::MipBit::Msip) == 0,
           "cross-hart interrupt remains queued until the target hart boundary");
    machine.hart(1).apply_pending_interrupt_lines(machine);
    expect((machine.hart(1).state().mip & simrv::core::MipBit::Msip) != 0,
           "target hart applies queued interrupt on its execution boundary");
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

    {
        simrv::core::Machine observed_machine;
        std::vector<simrv::core::MachineEvent> observations;
        const auto observer_id = observed_machine.add_event_observer(
            [&observations](const simrv::core::MachineEvent& observed) {
                observations.push_back(observed);
            });
        auto& clint = observed_machine.primary_hart().clint_mmio;
        clint.mtime.store(10, std::memory_order_relaxed);
        clint.mtimecmp.store(20, std::memory_order_relaxed);
        observed_machine.primary_hart().evaluate_timer_interrupt();
        clint.mtimecmp.store(0, std::memory_order_relaxed);
        observed_machine.primary_hart().evaluate_timer_interrupt();
        clint.mtimecmp.store(20, std::memory_order_relaxed);
        observed_machine.primary_hart().evaluate_timer_interrupt();
        observed_machine.set_hart_irq(simrv::xlen::HartId{0}, simrv::core::InterruptType::Software,
                                      simrv::xlen::PrivilegeLevel::Machine, true);
        observed_machine.primary_hart().apply_pending_interrupt_lines(observed_machine);
        observed_machine.set_hart_irq(simrv::xlen::HartId{0}, simrv::core::InterruptType::Software,
                                      simrv::xlen::PrivilegeLevel::Machine, false);
        observed_machine.primary_hart().apply_pending_interrupt_lines(observed_machine);
        observed_machine.set_platform_irq(4, true);
        observed_machine.set_platform_irq(4, false);
        (void)observed_machine.dma_engine().schedule_transfer(64, 10, [] {}, "test-device");
        auto dma_config = observed_machine.dma_engine().config();
        dma_config.enabled = true;
        observed_machine.dma_engine().set_config(dma_config);
        const auto completion_cycle =
            observed_machine.dma_engine().schedule_transfer(128, 20, [] {}, "test-device");
        observed_machine.dma_engine().advance_cycle(completion_cycle - 1);
        const auto event_count_before_completion = observations.size();
        observed_machine.dma_engine().advance_cycle(completion_cycle);
        const bool completed_at_scheduled_cycle =
            observations.size() == event_count_before_completion + 1 &&
            observations.back().kind == simrv::core::MachineEventKind::DmaCompleted &&
            observations.back().cycle == completion_cycle;
        (void)observed_machine.dma_engine().schedule_transfer(
            64, completion_cycle + 1, [] {}, "test-device");
        observed_machine.dma_engine().clear();
        expect(observations.size() == 12,
               "machine event observer receives interrupt and DMA lifecycle transitions");
        expect(observations.size() >= 12 &&
                   observations[0].kind == simrv::core::MachineEventKind::InterruptAsserted &&
                   observations[0].component == "aclint_mtimer" &&
                   observations[1].kind == simrv::core::MachineEventKind::InterruptDeasserted &&
                   observations[1].component == "aclint_mtimer",
               "machine event observer receives timer interrupt transitions");
        expect(observations.size() >= 12 &&
                   observations[2].kind == simrv::core::MachineEventKind::InterruptAsserted &&
                   observations[3].kind == simrv::core::MachineEventKind::InterruptDeasserted &&
                   observations[4].kind == simrv::core::MachineEventKind::InterruptAsserted &&
                   observations[5].kind == simrv::core::MachineEventKind::InterruptDeasserted,
               "machine event observer receives software and platform IRQ edges");
        expect(observations.size() >= 12 &&
                   observations[6].kind == simrv::core::MachineEventKind::DmaStarted &&
                   observations[7].kind == simrv::core::MachineEventKind::DmaCompleted &&
                   observations[8].kind == simrv::core::MachineEventKind::DmaStarted &&
                   observations[9].kind == simrv::core::MachineEventKind::DmaCompleted &&
                   observations[10].kind == simrv::core::MachineEventKind::DmaStarted &&
                   observations[11].kind == simrv::core::MachineEventKind::DmaCancelled &&
                   completed_at_scheduled_cycle,
               "DMA observations report start, completion, and cancellation");
        observed_machine.remove_event_observer(observer_id);
    }

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
        std::array<std::string, 5> trace_args_str = {"SimRV", "--arch-trace", "trace.jsonl", "-m",
                                                     "guest.bin"};
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
        std::array<std::string, 7> args_str = {
            "SimRV", "--arch-trace", "trace.jsonl", "--trace-register-writes",
            "-m",    "guest.bin",    "--cli"};
        std::array<char*, 7> args{};
        for (size_t i = 0; i < args.size(); ++i) args[i] = args_str[i].data();
        const auto parsed = simrv::util::parse_command_line(args);
        expect(parsed.has_value() && parsed->options.trace_register_writes,
               "register-write tracing parses when architectural tracing is enabled");
        if (parsed)
            expect(parsed->options.to_machine_config().debug.trace_register_writes,
                   "register-write trace option projects into MachineConfig");
        std::array<std::string, 5> invalid_str = {"SimRV", "--trace-register-writes", "--cli", "-m",
                                                  "guest.bin"};
        std::array<char*, 5> invalid{};
        for (size_t i = 0; i < invalid.size(); ++i) invalid[i] = invalid_str[i].data();
        expect(!simrv::util::parse_command_line(invalid),
               "register-write tracing requires an architectural trace output");
    }
    {
        const auto expect_cycle_trace_mode = [](std::vector<std::string> args,
                                                const char* message) {
            std::vector<char*> argv;
            argv.reserve(args.size());
            for (auto& arg : args) argv.push_back(arg.data());
            const auto parsed =
                simrv::util::parse_command_line(std::span<char* const>{argv.data(), argv.size()});
            expect(parsed.has_value() &&
                       parsed->options.execution_mode ==
                           simrv::util::RequestedExecutionMode::CycleAccurate &&
                       simrv::util::resolve_runtime_profile(parsed->options).engine ==
                           simrv::core::ExecutionEngine::CycleFast,
                   message);
        };
        expect_cycle_trace_mode({"SimRV", "--mode", "cycle-accurate", "--arch-trace",
                                 "retire.jsonl", "-m", "guest.bin"},
                                "--arch-trace preserves a cycle-accurate mode specified first");
        expect_cycle_trace_mode({"SimRV", "--arch-trace", "retire.jsonl", "--mode",
                                 "cycle-accurate", "-m", "guest.bin"},
                                "--arch-trace preserves a cycle-accurate mode specified after it");
    }
    {
        std::array<std::string, 4> trace_flag_str = {"SimRV", "--trace", "-m", "guest.bin"};
        std::array<char*, 4> trace_flag = {trace_flag_str[0].data(), trace_flag_str[1].data(),
                                           trace_flag_str[2].data(), trace_flag_str[3].data()};
        auto parsed = simrv::util::parse_command_line(trace_flag);
        expect(parsed.has_value() && parsed->options.trace_enabled &&
                   parsed->options.trace_begin == 0 &&
                   parsed->options.trace_end == std::numeric_limits<simrv::xlen::Counter>::max(),
               "--trace enables the complete aligned instruction trace");
    }
    {
        std::array<std::string, 6> trace_dir_str = {"SimRV",     "--trace-dir", "run-artifacts",
                                                    "--instmix", "-m",          "guest.bin"};
        std::array<char*, 6> trace_dir_args = {trace_dir_str[0].data(), trace_dir_str[1].data(),
                                               trace_dir_str[2].data(), trace_dir_str[3].data(),
                                               trace_dir_str[4].data(), trace_dir_str[5].data()};
        auto parsed = simrv::util::parse_command_line(trace_dir_args);
        expect(parsed.has_value() && parsed->options.trace_dir == "run-artifacts" &&
                   parsed->options.use_mix,
               "--trace-dir selects the generated artifact root");
        if (parsed) {
            const auto cfg = parsed->options.to_machine_config();
            expect(cfg.debug.trace_dir == "run-artifacts",
                   "trace artifact root is projected into MachineConfig");
        }
    }
    {
        std::array<std::string, 21> trace_filter_str = {"SimRV",
                                                        "--arch-trace",
                                                        "retire.jsonl",
                                                        "--trace-events",
                                                        "call, return",
                                                        "--trace-function",
                                                        "main",
                                                        "--trace-device",
                                                        "uart0",
                                                        "--trace-hart",
                                                        "1",
                                                        "--trace-pc",
                                                        "0x80000000-0x80001000",
                                                        "--trace-after-cycle",
                                                        "100",
                                                        "--trace-before-cycle",
                                                        "200",
                                                        "-j",
                                                        "2",
                                                        "-m",
                                                        "guest.bin"};
        std::array<char*, 21> trace_filter_args{};
        for (size_t i = 0; i < trace_filter_str.size(); ++i)
            trace_filter_args[i] = trace_filter_str[i].data();
        const auto parsed = simrv::util::parse_command_line(trace_filter_args);
        expect(parsed.has_value() && parsed->options.trace_events == "call, return" &&
                   parsed->options.trace_function == "main" &&
                   parsed->options.trace_device == "uart0" && parsed->options.trace_hart == 1 &&
                   parsed->options.trace_after_cycle == 100 &&
                   parsed->options.trace_before_cycle == 200 &&
                   parsed->options.trace_pc_start == 0x80000000 &&
                   parsed->options.trace_pc_end == 0x80001000,
               "trace event, hart, PC, and inclusive cycle filters parse");
        if (parsed) {
            const auto cfg = parsed->options.to_machine_config();
            expect(cfg.debug.trace_events == "call, return" && cfg.debug.trace_function == "main" &&
                       cfg.debug.trace_device == "uart0" && cfg.debug.trace_hart == 1 &&
                       cfg.debug.trace_pc_start == 0x80000000 &&
                       cfg.debug.trace_pc_end == 0x80001000 && cfg.debug.trace_after_cycle == 100 &&
                       cfg.debug.trace_before_cycle == 200,
                   "trace filters project into machine configuration");
        }
    }
    {
        std::array<std::string, 5> trace_pc_period_str = {"SimRV", "--trace-pc-period", "8", "-m",
                                                          "guest.bin"};
        std::array<char*, 5> trace_pc_period_args{};
        for (size_t i = 0; i < trace_pc_period_str.size(); ++i)
            trace_pc_period_args[i] = trace_pc_period_str[i].data();
        const auto parsed = simrv::util::parse_command_line(trace_pc_period_args);
        expect(parsed.has_value() && parsed->options.strace == 8,
               "--trace-pc-period retains numeric PC sampling");
    }
    {
        std::array<std::string, 5> legacy_pc_trace_str = {"SimRV", "--trace-pc", "8", "-m",
                                                          "guest.bin"};
        std::array<char*, 5> legacy_pc_trace_args{};
        for (size_t i = 0; i < legacy_pc_trace_str.size(); ++i)
            legacy_pc_trace_args[i] = legacy_pc_trace_str[i].data();
        const auto parsed = simrv::util::parse_command_line(legacy_pc_trace_args);
        expect(!parsed.has_value() && parsed.error().find("--trace-pc-period") != std::string::npos,
               "numeric --trace-pc is rejected with the sampling replacement");
    }
    {
        std::array<std::string, 5> config_str = {"SimRV", "--config", "rvcomp", "-m", "guest.bin"};
        std::array<char*, 5> config_args{};
        for (size_t i = 0; i < config_str.size(); ++i) config_args[i] = config_str[i].data();
        const auto parsed = simrv::util::parse_command_line(config_args);
        expect(parsed.has_value() && parsed->options.config_target == "rvcomp",
               "canonical --config option selects platform preset");
    }
    {
        std::array<std::string, 5> retired_soc_str = {"SimRV", "--soc", "rvcomp", "-m",
                                                      "guest.bin"};
        std::array<char*, 5> retired_soc_args{};
        for (size_t i = 0; i < retired_soc_str.size(); ++i)
            retired_soc_args[i] = retired_soc_str[i].data();
        const auto parsed = simrv::util::parse_command_line(retired_soc_args);
        expect(!parsed.has_value() &&
                   parsed.error().find("unknown option '--soc'") != std::string::npos,
               "retired --soc option is rejected");
    }
    {
        std::array<std::string, 5> retired_cpu_str = {"SimRV", "--cpu-config", "model.cfg", "-m",
                                                      "guest.bin"};
        std::array<char*, 5> retired_cpu_args{};
        for (size_t i = 0; i < retired_cpu_str.size(); ++i)
            retired_cpu_args[i] = retired_cpu_str[i].data();
        const auto parsed = simrv::util::parse_command_line(retired_cpu_args);
        expect(!parsed.has_value() &&
                   parsed.error().find("unknown option '--cpu-config'") != std::string::npos,
               "retired --cpu-config option is rejected");
    }
    {
        std::array<std::string, 4> legacy_platform_str = {"SimRV", "--platform", "rvcomp", "-m"};
        std::array<char*, 4> legacy_platform_args{};
        for (size_t i = 0; i < legacy_platform_str.size(); ++i)
            legacy_platform_args[i] = legacy_platform_str[i].data();
        const auto parsed = simrv::util::parse_command_line(legacy_platform_args);
        expect(!parsed.has_value() &&
                   parsed.error().find("unknown option '--platform'") != std::string::npos,
               "removed --platform alias is rejected");
    }
    {
        simrv::core::MachineConfig cfg{};
        cfg.memory.dram_base = 0x10000000ULL;
        cfg.memory.dram_size = 0x10000ULL;
        expect(!cfg.validate(), "MachineConfig rejects DRAM overlapping UART MMIO");

        cfg.memory.dram_base =
            static_cast<simrv::xlen::Address>(~static_cast<simrv::xlen::Address>(0) - 0x1000ULL);
        cfg.memory.dram_size = 0x2000ULL;
        expect(!cfg.validate(), "MachineConfig rejects DRAM range overflow");
    }
    {
        std::array<std::string, 8> args_str = {
            "SimRV", "--checkpoint-every", "10k",  "--checkpoint-dir", "snapshots",
            "-m",    "guest.bin",          "--cli"};
        std::array<char*, 8> args{};
        for (size_t i = 0; i < args_str.size(); ++i) args[i] = args_str[i].data();
        const auto parsed = simrv::util::parse_command_line(args);
        expect(parsed.has_value() && parsed->options.checkpoint_every == 10'000 &&
                   parsed->options.checkpoint_dir == "snapshots",
               "periodic checkpoint interval and directory parse");
        if (parsed) {
            const auto cfg = parsed->options.to_machine_config();
            expect(cfg.debug.checkpoint_every == 10'000 && cfg.debug.checkpoint_dir == "snapshots",
                   "periodic checkpoint settings project into MachineConfig");
        }
        auto invalid_config = simrv::core::MachineConfig{};
        invalid_config.debug.checkpoint_every = 1;
        expect(!invalid_config.validate(), "periodic checkpoints require an output directory");
    }
    {
        const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto checkpoint_dir = std::filesystem::temp_directory_path() /
                                    ("simrv-periodic-checkpoint-" + std::to_string(suffix));
        simrv::core::MachineConfig periodic_config{};
        periodic_config.memory.dram_base = 0x80000000;
        periodic_config.memory.dram_size = 16ULL * 1024 * 1024;
        periodic_config.execution.start_pc = periodic_config.memory.dram_base;
        periodic_config.execution.fincnt = 4;
        periodic_config.debug.checkpoint_every = 2;
        periodic_config.debug.checkpoint_dir = checkpoint_dir.string();
        std::vector<simrv::xlen::Byte> periodic_ram(periodic_config.memory.dram_size,
                                                    simrv::xlen::Byte{0});
        periodic_ram[0] = simrv::xlen::Byte{0x6f};  // jal x0, 0
        simrv::core::Machine periodic_machine(periodic_config);
        periodic_machine.set_ram_for_testing(periodic_ram.data(), periodic_ram.size());
        periodic_machine.primary_hart().state().pc = periodic_config.memory.dram_base;
        periodic_machine.run();

        size_t sidecars = 0;
        bool sidecar_is_explicit = false;
        std::error_code cleanup_error;
        for (const auto& entry : std::filesystem::directory_iterator(checkpoint_dir)) {
            if (entry.path().extension() == ".json") {
                ++sidecars;
                std::ifstream metadata(entry.path());
                std::string record;
                std::getline(metadata, record);
                sidecar_is_explicit |=
                    record.find("\"device_state_included\":false") != std::string::npos &&
                    record.find("\"replay_complete\":false") != std::string::npos;
            }
            std::filesystem::remove(entry.path(), cleanup_error);
        }
        std::filesystem::remove(checkpoint_dir, cleanup_error);
        expect(sidecars >= 2, "periodic checkpoint schedule emits snapshots during execution");
        expect(sidecar_is_explicit,
               "periodic checkpoint metadata declares the snapshot replay limitations");
    }

    // Architectural checkpoint round-trip and rejection coverage.  Keep the image small so this
    // remains a fast native gate while exercising vector/CSR state and RAM contents.
    {
        const auto checkpoint =
            std::filesystem::temp_directory_path() / "simrv-checkpoint-test.bin";
        const auto malformed = std::filesystem::temp_directory_path() / "simrv-checkpoint-bad.bin";
        simrv::core::MachineConfig checkpoint_config{};
        checkpoint_config.memory.dram_base = 0x80000000;
        checkpoint_config.memory.dram_size = 4096;
        checkpoint_config.isa.vlen = 256;
        std::vector<simrv::xlen::Byte> source_ram(4096, simrv::xlen::Byte{0});
        std::vector<simrv::xlen::Byte> restored_ram(4096, simrv::xlen::Byte{0});
        simrv::core::Machine source(checkpoint_config);
        source.set_ram_for_testing(source_ram.data(), source_ram.size());
        auto& source_state = source.primary_hart().state();
        source_state.pc = 0x80000100;
        source_state.regs.vlen = 256;
        source_state.regs.write(simrv::xlen::RegId::T2, 0x12345678);
        source_state.regs.write_fp(simrv::xlen::to_fp_reg_id(static_cast<simrv::xlen::RegId>(5)),
                                   0x7ff8000000001234);
        source_state.regs
            .read_vector(simrv::xlen::to_vec_reg_id(static_cast<simrv::xlen::RegId>(7)))
            .u8[0] = 0xA5;
        source_state.vtype = 0x23;
        source_state.vl = 17;
        source_state.mstatus = 0x1800;
        source_state.mepc = 0x80000200;
        source_state.pmpaddr[0] = 0x1234;
        source.primary_hart().e_icount = 77;
        source.primary_hart().e_ccount = 91;
        source.primary_hart().clint_mmio.mcycle = 105;
        source_ram[123] = simrv::xlen::Byte{0xA5};
        expect(source.save_checkpoint(checkpoint.string()).has_value(),
               "checkpoint saves architectural state");

        simrv::core::Machine restored(checkpoint_config);
        restored.set_ram_for_testing(restored_ram.data(), restored_ram.size());
        expect(restored.load_checkpoint(checkpoint.string()).has_value(),
               "checkpoint restores architectural state");
        const auto& restored_state = restored.primary_hart().state();
        expect(restored_state.pc == source_state.pc &&
                   restored_state.regs.read(simrv::xlen::RegId::T2) == 0x12345678,
               "checkpoint restores PC and integer registers");
        expect(restored_state.vtype == source_state.vtype && restored_state.vl == source_state.vl &&
                   restored_state.mepc == source_state.mepc && restored_state.pmpaddr[0] == 0x1234,
               "checkpoint restores vector, CSR, and PMP state");
        expect(
            restored_state.regs.read_fp(simrv::xlen::to_fp_reg_id(
                static_cast<simrv::xlen::RegId>(5))) == 0x7ff8000000001234 &&
                restored_state.regs
                        .read_vector(simrv::xlen::to_vec_reg_id(static_cast<simrv::xlen::RegId>(7)))
                        .u8[0] == 0xA5,
            "checkpoint restores FP and vector register-file bits");
        expect(restored.primary_hart().e_icount == 77 && restored.primary_hart().e_ccount == 91 &&
                   restored.primary_hart().clint_mmio.mcycle == 105 &&
                   restored_ram[123] == simrv::xlen::Byte{0xA5},
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

    {
        using simrv::core::HostExitCode;
        using simrv::core::to_exit_code;
        expect(to_exit_code(HostExitCode::Success) == 0, "HostExitCode::Success is 0");
        expect(to_exit_code(HostExitCode::GeneralError) == 1, "HostExitCode::GeneralError is 1");
        expect(to_exit_code(HostExitCode::UsageError) == 2, "HostExitCode::UsageError is 2");
        expect(to_exit_code(HostExitCode::ConfigurationError) == 3,
               "HostExitCode::ConfigurationError is 3");
        expect(to_exit_code(HostExitCode::InitializationError) == 4,
               "HostExitCode::InitializationError is 4");
        expect(to_exit_code(HostExitCode::ExecutionError) == 5,
               "HostExitCode::ExecutionError is 5");
        expect(to_exit_code(HostExitCode::CheckpointError) == 6,
               "HostExitCode::CheckpointError is 6");
        expect(to_exit_code(HostExitCode::IpcError) == 7, "HostExitCode::IpcError is 7");
    }

    {
        simrv::core::MachineConfig cfg{};
        cfg.tui.enabled = true;
        simrv::core::Machine machine(cfg);
        expect(machine.initialize().has_value(), "default Machine initializes successfully");
        expect(simrv::isa::misa_has_extension(machine.primary_hart().state().misa,
                                              simrv::isa::IsaExtension::B),
               "default Machine initial misa enables bitmanip extension (B)");
    }

    {
        std::array<std::string, 3> args_str = {"SimRV", "-m", "guest.elf"};
        std::array<char*, 3> args{};
        for (size_t i = 0; i < args_str.size(); ++i) args[i] = args_str[i].data();
        auto parsed = simrv::util::parse_command_line(args);
        expect(parsed.has_value(), "default CLI args parse");
        if (parsed) {
            auto cfg = parsed->options.to_machine_config();
            expect(simrv::isa::misa_has_extension(cfg.isa.isa_preset, simrv::isa::IsaExtension::B),
                   "default CLI MachineConfig enables bitmanip extension (B)");
            cfg.tui.enabled = true;
            simrv::core::Machine machine(cfg);
            expect(machine.initialize().has_value(),
                   "CLI-configured Machine initializes successfully");
            expect(simrv::isa::misa_has_extension(machine.primary_hart().state().misa,
                                                  simrv::isa::IsaExtension::B),
                   "CLI-configured Machine initial misa enables bitmanip extension (B)");
        }
    }

    {
        const auto parse_cli = [](std::vector<std::string> args_str) {
            std::vector<char*> args;
            for (auto& s : args_str) args.push_back(s.data());
            return simrv::util::parse_command_line(args);
        };

        auto validate_cfg = parse_cli({"SimRV", "--validate-config", "configs/models/rvcomp.cfg"});
        expect(validate_cfg.has_value(), "CLI accepts --validate-config");
        if (validate_cfg) {
            expect(validate_cfg->action == simrv::util::CliAction::ValidateCpuModel,
                   "--validate-config sets CliAction::ValidateCpuModel");
            expect(validate_cfg->options.dump_cpu_model_preset == "configs/models/rvcomp.cfg",
                   "--validate-config sets target configuration file");
        }

        auto validate_cpu_cfg =
            parse_cli({"SimRV", "--validate-cpu-config", "configs/models/rvcomp.cfg"});
        expect(validate_cpu_cfg.has_value(), "CLI accepts --validate-cpu-config");

        expect(!parse_cli({"SimRV", "--check-cpu-model", "configs/models/rvcomp.cfg"}).has_value(),
               "CLI rejects retired alias --check-cpu-model");
        expect(!parse_cli({"SimRV", "--verify-cpu-model", "configs/models/rvcomp.cfg"}).has_value(),
               "CLI rejects retired alias --verify-cpu-model");
        expect(!parse_cli({"SimRV", "--export-soc-manifest", "virt-pcie"}).has_value(),
               "CLI rejects retired alias --export-soc-manifest");
        expect(!parse_cli({"SimRV", "--export-cpu-model", "tiny"}).has_value(),
               "CLI rejects retired alias --export-cpu-model");

        auto validate_soc_preset = parse_cli({"SimRV", "--validate-config", "virt-pcie"});
        expect(validate_soc_preset.has_value() &&
                   validate_soc_preset->action == simrv::util::CliAction::ValidateCpuModel &&
                   validate_soc_preset->options.dump_cpu_model_preset == "virt-pcie",
               "CLI accepts --validate-config with built-in SoC preset name");

        auto validate_cpu_preset = parse_cli({"SimRV", "--validate-config", "balanced"});
        expect(validate_cpu_preset.has_value() &&
                   validate_cpu_preset->action == simrv::util::CliAction::ValidateCpuModel &&
                   validate_cpu_preset->options.dump_cpu_model_preset == "balanced",
               "CLI accepts --validate-config with built-in CPU preset name");
    }

    {
        // CPU config parser ISA preset validation and Zb expansion
        expect(!simrv::core::detail::parse_isa_preset_string("gcbv").has_value(),
               "CpuConfigParser detail::parse_isa_preset_string rejects legacy draft gcbv");
        expect(!simrv::core::detail::parse_isa_preset_string("rv64gcbv").has_value(),
               "CpuConfigParser detail::parse_isa_preset_string rejects legacy draft rv64gcbv");
        expect(simrv::core::detail::parse_isa_preset_string("gczbv") == simrv::isa::IsaPreset::GCV,
               "CpuConfigParser detail::parse_isa_preset_string accepts ratified gczbv");

        std::istringstream stream(
            "[cpu]\nname = \"test_core\"\nisa_preset = \"gczbv\"\n"
            "[device.clint]\nname = \"clint0\"\nbase = 0x2000000\nsize = 0x10000\n"
            "[device.plic]\nname = \"plic0\"\nbase = 0xc000000\nsize = 0x400000\n"
            "[smp]\nhart_count = 2\n");
        simrv::pipeline::CpuModelConfig cfg{};
        expect(simrv::core::parse_cpu_config_stream(stream, cfg),
               "parse_cpu_config_stream successfully parses config with gczbv and device/smp "
               "sections");
        expect(cfg.isa_preset == simrv::isa::IsaPreset::GCV, "gczbv config maps to IsaPreset::GCV");
        expect(std::ranges::find(cfg.isa_extensions, "zba") != cfg.isa_extensions.end(),
               "gczbv config automatically includes zba");
        expect(std::ranges::find(cfg.isa_extensions, "zbb") != cfg.isa_extensions.end(),
               "gczbv config automatically includes zbb");
        expect(std::ranges::find(cfg.isa_extensions, "zbc") != cfg.isa_extensions.end(),
               "gczbv config automatically includes zbc");
        expect(std::ranges::find(cfg.isa_extensions, "zbs") != cfg.isa_extensions.end(),
               "gczbv config automatically includes zbs");
    }

    {
        // CLI --log-stderr option parsing and Logger control
        std::array<std::string, 4> log_stderr_str = {"SimRV", "--log-stderr", "-m", "guest.bin"};
        std::array<char*, 4> log_stderr_args = {log_stderr_str[0].data(), log_stderr_str[1].data(),
                                                log_stderr_str[2].data(), log_stderr_str[3].data()};
        auto parsed = simrv::util::parse_command_line(log_stderr_args);
        expect(parsed.has_value() && parsed->options.log_stderr,
               "--log-stderr is parsed into runtime options");

        simrv::log::set_log_to_stderr(true);
        expect(simrv::log::get_log_to_stderr(), "simrv::log::get_log_to_stderr() reflects true");
        simrv::log::set_log_to_stderr(false);
        expect(!simrv::log::get_log_to_stderr(), "simrv::log::get_log_to_stderr() reflects false");
    }

    {
        // Terminal mode restoration idempotency and state safety
        expect(!simrv::util::global_termios_active().load(),
               "global_termios_active is initially false");
        simrv::util::global_termios_active().store(true);
        simrv::util::restore_terminal_mode();
        expect(!simrv::util::global_termios_active().load(),
               "restore_terminal_mode atomically resets active flag");
    }

    {
        // Stdin platform config parsing with unified [cpu] and [soc] sections
        std::istringstream combined_stream(
            "[cpu]\nname = \"piped_core\"\nisa_preset = \"gczbv\"\n"
            "[soc]\nname = \"piped_soc\"\ndram_base = 0x80000000\ndram_size = 0x10000000\n");
        simrv::pipeline::CpuModelConfig cpu_cfg{};
        simrv::core::SoCConfig soc_cfg{};
        expect(simrv::core::parse_cpu_config_stream(combined_stream, cpu_cfg),
               "parse_cpu_config_stream parses piped stream");
        combined_stream.clear();
        combined_stream.seekg(0);
        expect(simrv::core::parse_soc_config_stream(combined_stream, soc_cfg),
               "parse_soc_config_stream parses piped stream");
        expect(cpu_cfg.name == "piped_core", "stream populated CPU name");
        expect(soc_cfg.name == "piped_soc", "stream populated SoC name");
    }

    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
