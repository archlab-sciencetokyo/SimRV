#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: simrv-linux-builder-post-build <Buildroot target directory>\n";
        return 2;
    }
    const std::filesystem::path inittab = std::filesystem::path{argv[1]} / "etc/inittab";
    std::ifstream input{inittab};
    if (!input) {
        std::cerr << "Buildroot inittab not found: " << inittab << '\n';
        return 1;
    }
    const std::string content{std::istreambuf_iterator<char>{input},
                              std::istreambuf_iterator<char>{}};
    if (content.find("tty1::respawn:") == std::string::npos) {
        std::ofstream output{inittab, std::ios::app};
        if (!content.empty() && content.back() != '\n') output << '\n';
        output << "tty1::respawn:/sbin/getty -L 38400 tty1 vt100\n";
        if (!output) {
            std::cerr << "Could not update Buildroot inittab: " << inittab << '\n';
            return 1;
        }
    }

    if (const auto* overlay = std::getenv("SIMRV_LINUX_CUSTOM_OVERLAY");
        overlay != nullptr && *overlay != '\0') {
        const std::filesystem::path overlayPath{overlay};
        if (!std::filesystem::is_directory(overlayPath)) {
            std::cerr << "Custom rootfs overlay is not a directory: " << overlayPath << '\n';
            return 1;
        }
        std::error_code error;
        std::filesystem::copy(overlayPath, argv[1],
                              std::filesystem::copy_options::recursive |
                                  std::filesystem::copy_options::overwrite_existing |
                                  std::filesystem::copy_options::copy_symlinks,
                              error);
        if (error) {
            std::cerr << "Could not apply custom rootfs overlay: " << error.message() << '\n';
            return 1;
        }
    }
    return 0;
}
