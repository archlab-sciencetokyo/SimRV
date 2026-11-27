#include <charconv>
#include <print>
#include <string_view>

#include "simrv/util/MissionTools.hpp"
#include "simrv/util/Scaffolding.hpp"
#include "simrv/util/ShellCompletion.hpp"
#include "simrv/util/UefiDiskGenerator.hpp"

namespace {
void usage() {
    std::println(
        stderr,
        "Usage: simrv-tools <command> [arguments]\n\n"
        "Commands:\n"
        "  scaffold cpu [PRESET] [FILE]\n"
        "  scaffold device NAME [DIR|FILE]\n"
        "  scaffold device --from-dtb <board.dtb> [DIR]\n"
        "  scaffold cfu NAME [DIR|FILE]\n"
        "  mission check <file.mission> | create <file.mission>\n"
        "  disk create <output.img> --kernel <BOOTRISCV64.EFI> [--rootfs FILE] [--size-mb MiB]\n"
        "  completion [bash|zsh|fish]");
}

auto scaffold(int argc, char** argv) -> int {
    if (argc < 3) {
        usage();
        return 2;
    }
    const std::string_view kind(argv[2]);
    if (kind == "cpu") {
        const auto preset = argc > 3 ? std::string_view(argv[3]) : std::string_view("balanced");
        const auto output = argc > 4 ? std::string_view(argv[4]) : std::string_view("-");
        if (argc > 5) {
            usage();
            return 2;
        }
        return simrv::util::scaffold_cpu_model(preset, output) ? 0 : 1;
    }
    if (kind == "device") {
        if (argc >= 4 && std::string_view(argv[3]) == "--from-dtb") {
            if (argc < 5 || argc > 6) {
                usage();
                return 2;
            }
            const auto output = argc == 6 ? std::string_view(argv[5]) : std::string_view{};
            return simrv::util::scaffold_device_plugin_from_dtb(argv[4], output) ? 0 : 1;
        }
        if (argc < 4 || argc > 5) {
            usage();
            return 2;
        }
        const auto output = argc == 5 ? std::string_view(argv[4]) : std::string_view{};
        return simrv::util::scaffold_device_plugin(argv[3], output) ? 0 : 1;
    }
    if (kind == "cfu") {
        if (argc < 4 || argc > 5) {
            usage();
            return 2;
        }
        const auto output = argc == 5 ? std::string_view(argv[4]) : std::string_view{};
        return simrv::util::scaffold_cfu_plugin(argv[3], output) ? 0 : 1;
    }
    usage();
    return 2;
}

auto disk(int argc, char** argv) -> int {
    if (argc < 5 || std::string_view(argv[2]) != "create") {
        usage();
        return 2;
    }
    simrv::util::UefiDiskConfig config{};
    config.output_image = argv[3];
    for (int i = 4; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if ((arg == "--kernel" || arg == "--rootfs" || arg == "--size-mb") && i + 1 >= argc) {
            std::println(stderr, "{} requires a value", arg);
            return 2;
        }
        if (arg == "--kernel")
            config.kernel_payload = argv[++i];
        else if (arg == "--rootfs")
            config.rootfs_payload = argv[++i];
        else if (arg == "--size-mb") {
            const std::string_view value(argv[++i]);
            const auto [end, error] =
                std::from_chars(value.data(), value.data() + value.size(), config.esp_size_mb);
            if (error != std::errc{} || end != value.data() + value.size() ||
                config.esp_size_mb == 0) {
                std::println(stderr, "Invalid ESP size: {}", value);
                return 2;
            }
        } else {
            std::println(stderr, "Unknown option: {}", arg);
            return 2;
        }
    }
    return simrv::util::generate_uefi_disk(config) ? 0 : 1;
}
}  // namespace

int main(int argc, char** argv) {
    if (argc == 2 && (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h")) {
        usage();
        return 0;
    }
    if (argc < 2) {
        usage();
        return 2;
    }
    const std::string_view command(argv[1]);
    if (command == "scaffold") return scaffold(argc, argv);
    if (command == "mission") {
        if (argc != 4) {
            usage();
            return 2;
        }
        const std::string_view action(argv[2]);
        if (action == "check") return simrv::util::check_mission_file(argv[3], true) ? 0 : 1;
        if (action == "create") return simrv::util::create_mission_scaffold(argv[3]) ? 0 : 1;
    }
    if (command == "disk") return disk(argc, argv);
    if (command == "completion") {
        const auto shell = argc > 2 ? std::string_view(argv[2]) : std::string_view("bash");
        if (argc > 3 || (shell != "bash" && shell != "zsh" && shell != "fish")) {
            usage();
            return 2;
        }
        std::print("{}", simrv::util::generate_shell_completion(shell));
        return 0;
    }
    usage();
    return 2;
}
