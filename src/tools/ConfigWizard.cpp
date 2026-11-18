/**
 * @file ConfigWizard.cpp
 * @brief Interactive CPU and DTB platform configuration wizard.
 */
#include <unistd.h>

#include <algorithm>
#include <bit>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <print>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/core/CpuConfigParser.hpp"
#include "simrv/core/MachineConfig.hpp"
#include "simrv/core/SoCConfig.hpp"
#include "simrv/pipeline/CpuModel.hpp"
#include "simrv/util/FdtPatcher.hpp"
#include "simrv/util/Scaffolding.hpp"

namespace {

struct Options {
    std::string name = "custom_riscv";
    std::string preset = "balanced";
    std::string output;
    std::string dtb;
    std::string validate;
    bool non_interactive = false;
};

void help(std::string_view program) {
    std::println(
        "Usage: {} [OPTIONS]\n\n"
        "Create and inspect CPU/SoC configs, or import a DTB.\n\n"
        "  --from-dtb <PATH>       Import a compiled device tree blob\n"
        "  --name <NAME>           Config name (default: custom_riscv)\n"
        "  --template <PRESET>     CPU preset: balanced, tiny, performance\n"
        "  --preset <PRESET>       Alias for --template\n"
        "  -o, --output <PATH>     Output path (default: configs/models/<NAME>.cfg)\n"
        "  --non-interactive       Skip the terminal review UI\n"
        "  --validate <FILE|PRESET> Validate a CPU/SoC config or built-in preset\n"
        "  -h, --help              Show this help\n",
        program);
}

auto parse_args(int argc, char** argv, Options& options) -> bool {
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            help(argc > 0 ? argv[0] : "simrv-config-wizard");
            std::exit(0);
        }
        if (arg == "--non-interactive") {
            options.non_interactive = true;
            continue;
        }
        const auto get_value = [&](std::string_view key) -> std::string_view {
            if (arg == key && i + 1 < argc) return argv[++i];
            if (arg.starts_with(std::string(key) + "=")) return arg.substr(key.size() + 1);
            return {};
        };
        if (auto value = get_value("--validate"); !value.empty())
            options.validate = value;
        else if (auto value = get_value("--from-dtb"); !value.empty())
            options.dtb = value;
        else if (auto value = get_value("--name"); !value.empty())
            options.name = value;
        else if (auto value = get_value("--template"); !value.empty())
            options.preset = value;
        else if (auto value = get_value("--preset"); !value.empty())
            options.preset = value;
        else if (auto value = get_value("--output"); !value.empty())
            options.output = value;
        else if (arg == "-o" && i + 1 < argc)
            options.output = argv[++i];
        else {
            std::println(std::cerr, "Unknown option or missing value: {}", arg);
            return false;
        }
    }
    return true;
}

auto validate_target(std::string_view target) -> int {
    simrv::pipeline::CpuModelConfig cpu{};
    simrv::core::SoCConfig soc{};
    bool has_cpu = false;
    bool has_soc = false;
    const auto resolved = simrv::core::resolve_cpu_model_path(target);
    const std::string path = resolved.value_or(std::string(target));
    if (std::filesystem::exists(path)) {
        has_cpu = simrv::core::parse_cpu_config(std::filesystem::path(path), cpu);
        has_soc = simrv::core::load_soc_config(path, soc);
    } else if (auto preset_soc = simrv::core::SoCConfig::preset(target)) {
        soc = *preset_soc;
        has_soc = true;
    } else if (auto preset = simrv::pipeline::parse_cpu_model_preset(target)) {
        cpu = simrv::pipeline::make_cpu_model_preset(*preset);
        has_cpu = true;
    } else {
        std::println(stderr, "Config path or preset not found: {}", target);
        return 1;
    }
    if (!has_cpu && !has_soc) {
        std::println(stderr, "Could not parse config: {}", target);
        return 1;
    }
    if (has_cpu) {
        if (const auto valid = cpu.validate(); !valid) {
            std::println(stderr, "CPU model is invalid: {}", valid.error());
            return 1;
        }
    }
    if (has_soc) {
        if (const auto valid = soc.validate(); !valid) {
            std::println(stderr, "SoC config is invalid: {}", valid.error());
            return 1;
        }
    }
    std::println("Valid configuration: {}", target);
    return 0;
}

auto prompt(std::string_view label, std::string current) -> std::string {
    std::print("{} [{}]: ", label, current.empty() ? "enter value" : current);
    std::string entered;
    if (!std::getline(std::cin, entered) || entered.empty()) return current;
    return entered;
}

auto prompt_u64(std::string_view label, uint64_t current) -> uint64_t {
    const auto value = prompt(label, std::format("{:#x}", current));
    try {
        size_t consumed = 0;
        const auto parsed = std::stoull(value, &consumed, 0);
        return consumed == value.size() ? parsed : current;
    } catch (...) {
        std::println("  Invalid number; keeping {}.", current);
        return current;
    }
}

auto prompt_u32(std::string_view label, uint32_t current) -> uint32_t {
    const auto value = prompt_u64(label, current);
    if (value > std::numeric_limits<uint32_t>::max()) {
        std::println("  Value is too large; keeping {}.", current);
        return current;
    }
    return static_cast<uint32_t>(value);
}

auto prompt_yes_no(std::string_view label, bool current) -> bool {
    const auto answer = prompt(label, current ? "Y/n" : "y/N");
    if (answer == "y" || answer == "Y" || answer == "yes" || answer == "YES") return true;
    if (answer == "n" || answer == "N" || answer == "no" || answer == "NO") return false;
    return current;
}

auto sanitize(std::string text) -> std::string {
    for (auto& ch : text) {
        if (!std::isalnum(static_cast<unsigned char>(ch)) && ch != '-' && ch != '_') ch = '-';
    }
    while (!text.empty() && text.front() == '-') text.erase(text.begin());
    while (!text.empty() && text.back() == '-') text.pop_back();
    return text.empty() ? "device" : text;
}

auto kind_for(std::string_view compatible) -> std::optional<simrv::core::SoCDeviceKind> {
    using simrv::core::SoCDeviceKind;
    if (compatible == "ns16550a" || compatible == "ns16550" ||
        compatible.find("uart8250") != std::string_view::npos)
        return SoCDeviceKind::Uart;
    if (compatible == "google,goldfish-rtc") return SoCDeviceKind::Rtc;
    if (compatible == "sifive,test1" || compatible == "sifive,test0") return SoCDeviceKind::Power;
    if (compatible == "sifive,plic-1.0.0" || compatible == "riscv,plic0")
        return SoCDeviceKind::Plic;
    if (compatible == "riscv,clint0" || compatible == "sifive,clint0") return SoCDeviceKind::Clint;
    return std::nullopt;
}

auto read_platform(const std::string& path)
    -> std::expected<simrv::util::FdtPlatformDescription, std::string> {
    std::ifstream input(path, std::ios::binary);
    if (!input) return std::unexpected("cannot open DTB: " + path);
    std::vector<uint8_t> blob{std::istreambuf_iterator<char>(input),
                              std::istreambuf_iterator<char>()};
    return simrv::util::read_fdt_platform(blob);
}

void review_platform(simrv::util::FdtPlatformDescription& platform, Options& options) {
    std::println("\nDTB import review (press Enter to keep each value)");
    options.name = prompt("Config name", options.name);
    options.preset = prompt("CPU preset (balanced/tiny/performance)", options.preset);
    std::println("  DTB CPU ISA: {} (informational; select a compatible CPU preset separately)",
                 platform.cpu_isa.empty() ? "not specified" : platform.cpu_isa);
    platform.dram_base = simrv::xlen::PhysAddr{prompt_u64("RAM base", platform.dram_base.raw())};
    platform.dram_size = prompt_u64("RAM size", platform.dram_size);
    platform.hart_count = prompt_u32("Hart count", platform.hart_count);
    platform.cpu_clock_hz = prompt_u64("CPU clock (Hz)", platform.cpu_clock_hz);
    platform.timebase_hz = prompt_u64("Timebase frequency (Hz)", platform.timebase_hz);
    platform.bootargs = prompt("Bootargs", platform.bootargs);

    for (auto& device : platform.devices) {
        const auto mapped = kind_for(device.compatible);
        const auto kind_label = mapped ? std::string(simrv::core::soc_device_kind_name(*mapped))
                                       : "custom MMIO (no emulation implied)";
        std::println("  {}: {} at {:#x}+{:#x}, irq {}", device.name, kind_label, device.base.raw(),
                     device.size, device.irq);
        device.included = prompt_yes_no("    Include this device?", true);
        if (device.included) {
            device.base = simrv::xlen::PhysAddr{prompt_u64("    MMIO base", device.base.raw())};
            device.size = prompt_u64("    MMIO size", device.size);
            device.irq = prompt_u32("    IRQ", device.irq);
        }
    }
}

void serialize_platform(const simrv::pipeline::CpuModelConfig& cpu, const Options& options,
                        const simrv::util::FdtPlatformDescription& imported, std::ostream& output) {
    simrv::core::serialize_cpu_config(cpu, output, options.name,
                                      "CPU and platform configuration imported from a DTB");
    output << "\n[soc]\nname = " << std::quoted(options.name)
           << "\ncpu_model = " << std::quoted(options.preset)
           << "\ndevice_policy = \"explicit\"\nenable_pcie = false\nenable_mmio = false\n"
           << "cpu_clock_hz = " << imported.cpu_clock_hz << "\n"
           << "timebase_hz = " << imported.timebase_hz << "\n\n"
           << "[memory]\ndram_base = " << std::format("{:#x}", imported.dram_base.raw())
           << "\ndram_size = " << imported.dram_size << "\n\n"
           << "[boot]\nbootargs = " << std::quoted(imported.bootargs) << "\n\n"
           << "[smp]\nhart_count = " << imported.hart_count << "\n";

    for (const auto& source : imported.devices) {
        if (!source.included) continue;
        auto kind = kind_for(source.compatible).value_or(simrv::core::SoCDeviceKind::CustomMmio);
        const bool duplicate = std::ranges::any_of(imported.devices, [&](const auto& candidate) {
            return &candidate != &source && kind_for(candidate.compatible) == kind;
        });
        if (kind != simrv::core::SoCDeviceKind::CustomMmio && duplicate)
            kind = simrv::core::SoCDeviceKind::CustomMmio;
        const auto instance = sanitize(source.name);
        output << "\n[device." << simrv::core::soc_device_kind_name(kind);
        if (kind == simrv::core::SoCDeviceKind::CustomMmio) output << '.' << instance;
        output << "]\nname = " << std::quoted(instance)
               << "\ncompatible = " << std::quoted(source.compatible)
               << "\nbase = " << std::format("{:#x}", source.base.raw())
               << "\nsize = " << source.size << "\nirq = " << source.irq << "\n";
        if (source.clock_hz != 0) output << "clock_hz = " << source.clock_hz << "\n";
    }
}

auto cpu_only(Options options, bool interactive) -> int {
    if (interactive) {
        std::println("\nCPU model setup");
        options.name = prompt("Config name", options.name);
        options.preset = prompt("CPU preset (balanced/tiny/performance)", options.preset);
    }
    if (options.output.empty()) options.output = "configs/models/" + options.name + ".cfg";
    if (interactive) {
        std::println("\nReview: CPU preset '{}' -> {}", options.preset, options.output);
        if (!prompt_yes_no("Write this config?", true)) return 0;
    }
    return simrv::util::scaffold_cpu_model(options.preset, options.output) ? 0 : 1;
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
    Options options{};
    if (!parse_args(argc, argv, options)) {
        help(argc > 0 ? argv[0] : "simrv-config-wizard");
        return 1;
    }
    if (!options.validate.empty()) return validate_target(options.validate);
    const bool interactive =
        !options.non_interactive && isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    if (interactive) {
        std::println(
            "SimRV Configuration Wizard\n  1) Create a CPU model config\n  2) Import a DTB into a "
            "combined CPU/SoC config");
        if (options.dtb.empty()) {
            const auto choice = prompt("Choose mode (1/2)", "1");
            if (choice == "2") options.dtb = prompt("DTB path", "path/to/board.dtb");
        }
    }
    if (options.dtb.empty()) return cpu_only(std::move(options), interactive);

    auto imported = read_platform(options.dtb);
    if (!imported) {
        std::println(std::cerr, "DTB import failed: {}", imported.error());
        return 1;
    }
    for (const auto& warning : imported->warnings) std::println("Warning: {}", warning);
    if (imported->dram_size == 0) {
        std::println(std::cerr,
                     "The DTB has no usable RAM range; cannot create a platform config.");
        return 1;
    }
    if (interactive) review_platform(*imported, options);
    if (options.output.empty()) options.output = "configs/models/" + options.name + ".cfg";

    const auto preset = simrv::pipeline::parse_cpu_model_preset(options.preset);
    if (!preset) {
        std::println(std::cerr, "Unknown CPU preset '{}'.", options.preset);
        return 1;
    }
    const auto cpu = simrv::pipeline::make_cpu_model_preset(*preset);
    if (imported->hart_count == 0 ||
        imported->hart_count > simrv::core::ExecutionConfig::kMaxHarts) {
        std::println(std::cerr, "Imported hart count is outside SimRV's supported range.");
        return 1;
    }
    if (imported->dram_size == 0 ||
        imported->dram_size > std::numeric_limits<simrv::xlen::Address>::max() ||
        !std::has_single_bit(imported->dram_size) ||
        imported->dram_base.raw() >
            std::numeric_limits<simrv::xlen::Address>::max() - imported->dram_size) {
        std::println(std::cerr,
                     "Imported RAM geometry is invalid for SimRV; edit it in the review.");
        return 1;
    }
    if (interactive) {
        const auto included_devices = static_cast<size_t>(std::ranges::count_if(
            imported->devices, [](const auto& device) { return device.included; }));
        std::println("\nReview: CPU preset '{}' + {} devices -> {}", options.preset,
                     included_devices, options.output);
        if (!prompt_yes_no("Write this combined config?", true)) return 0;
    }

    std::error_code error;
    const std::filesystem::path output_path(options.output);
    if (output_path.has_parent_path())
        std::filesystem::create_directories(output_path.parent_path(), error);
    std::ofstream output(output_path);
    if (!output) {
        std::println(std::cerr, "Cannot write config: {}", options.output);
        return 1;
    }
    serialize_platform(cpu, options, *imported, output);
    if (!output) {
        std::println(std::cerr, "Failed while writing config: {}", options.output);
        return 1;
    }
    std::println("Wrote combined config to {}", options.output);
    return 0;
}
