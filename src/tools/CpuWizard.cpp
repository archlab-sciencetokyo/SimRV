/**
 * @file CpuWizard.cpp
 * @brief Native C++ wizard for scaffolding and creating custom SimRV CPU models.
 */
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <print>
#include <span>
#include <string>
#include <string_view>

#include "simrv/util/Scaffolding.hpp"

namespace {

void print_help(std::string_view prog) {
    std::println(
        "Usage: {} [OPTIONS]\n\n"
        "Interactive or automated CLI wizard for scaffolding SimRV configurations (.cfg).\n\n"
        "Options:\n"
        "  -h, --help                Show this help message and exit\n"
        "  --name <NAME>             Configuration name (default: custom_riscv)\n"
        "  --template <PRESET>       Base preset: balanced, tiny, performance, rvcomp (default: "
        "balanced)\n"
        "  --preset <PRESET>         Alias for --template\n"
        "  -o, --output <PATH>       Output config file path (default: configs/models/<NAME>.cfg)\n"
        "  --non-interactive         Generate immediately without prompting for adjustments\n",
        prog);
}

}  // namespace

auto main(int argc, char* argv[]) -> int {
    std::string_view prog = argc > 0 ? argv[0] : "simrv-cpu-wizard";
    std::string name = "custom_riscv";
    std::string preset = "balanced";
    std::string output_path;
    bool non_interactive = false;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "-h" || arg == "--help") {
            print_help(prog);
            return 0;
        } else if (arg == "--name" && i + 1 < argc) {
            name = argv[++i];
        } else if ((arg == "--template" || arg == "--preset") && i + 1 < argc) {
            preset = argv[++i];
        } else if ((arg == "-o" || arg == "--output") && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "--non-interactive") {
            non_interactive = true;
        } else if (arg.starts_with("--name=")) {
            name = arg.substr(7);
        } else if (arg.starts_with("--template=")) {
            preset = arg.substr(11);
        } else if (arg.starts_with("--preset=")) {
            preset = arg.substr(9);
        } else if (arg.starts_with("--output=")) {
            output_path = arg.substr(9);
        } else {
            std::println(std::cerr, "Unknown option: {}", arg);
            print_help(prog);
            return 1;
        }
    }

    if (output_path.empty()) {
        output_path = "configs/models/" + name + ".cfg";
    }

    if (!non_interactive && isatty(fileno(stdin))) {
        std::println("SimRV Configuration Wizard");
        std::println("  Target name    : {}", name);
        std::println("  Base preset    : {}", preset);
        std::println("  Output file    : {}", output_path);
    }

    if (!simrv::util::scaffold_cpu_model(preset, output_path)) {
        std::println(std::cerr, "Failed to scaffold configuration for preset '{}'", preset);
        return 1;
    }

    std::println("Successfully generated configuration at '{}'", output_path);
    return 0;
}
