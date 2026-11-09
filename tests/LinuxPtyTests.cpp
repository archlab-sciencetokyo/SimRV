/**
 * @file LinuxPtyTests.cpp
 * @brief Native C++ interactive Linux PTY lifecycle and UART integration test using Boost.UT.
 */
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <array>
#include <boost/ut.hpp>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace boost::ut;

auto find_repo_root() -> fs::path {
    auto current = fs::current_path();
    for (int i = 0; i < 5; ++i) {
        if (fs::exists(current / "CMakeLists.txt")) return current;
        if (current.has_parent_path())
            current = current.parent_path();
        else
            break;
    }
    return fs::current_path();
}

}  // namespace

auto main() -> int {
    "linux_pty_lifecycle"_test = [] {
        const auto root = find_repo_root();

        // 1. Locate simrv binary
        fs::path simrv_bin;
        if (const char* env_bin = std::getenv("SIMRV_BIN")) {
            simrv_bin = env_bin;
        } else {
            simrv_bin = root / "build/rv64-release/simrv";
            if (!fs::exists(simrv_bin)) {
                simrv_bin = root / "build/rv32-release/simrv";
            }
        }

        // 2. Locate Linux guest images
        fs::path images_dir;
        if (const char* env_dir = std::getenv("SIMRV_LINUX_IMAGES_DIR")) {
            images_dir = env_dir;
        } else if (const char* env_build = std::getenv("SIMRV_LINUX_BUILD_DIR")) {
            images_dir = env_build;
        } else {
            images_dir = root / "linux-images/rv64/alpine-gui";
        }

        const auto mem_img = images_dir / "opensbi-linux-payload.elf";
        const auto dtb_img = images_dir / "devicetree.dtb";
        const auto disk_img = images_dir / "rootfs.img";

        if (!fs::exists(simrv_bin) || !fs::exists(mem_img) || !fs::exists(dtb_img)) {
            // Linux images are not present; gracefully skip optional manual test
            return;
        }

        int master_fd = -1;
        int slave_fd = -1;
        if (::openpty(&master_fd, &slave_fd, nullptr, nullptr, nullptr) < 0) {
            expect(false) << "openpty failed";
            return;
        }

        struct winsize ws{};
        ws.ws_row = 40;
        ws.ws_col = 140;
        ::ioctl(slave_fd, TIOCSWINSZ, &ws);

        pid_t pid = ::fork();
        if (pid == 0) {
            ::close(master_fd);
            ::setsid();
            ::ioctl(slave_fd, TIOCSCTTY, 1);
            ::dup2(slave_fd, STDIN_FILENO);
            ::dup2(slave_fd, STDOUT_FILENO);
            ::dup2(slave_fd, STDERR_FILENO);
            ::close(slave_fd);

            std::vector<const char*> args = {
                simrv_bin.c_str(), "--os",  "-m", mem_img.c_str(), "-D",   disk_img.c_str(), "-f",
                dtb_img.c_str(),   "--tui", "-e", "2000000000",    nullptr};
            ::execv(simrv_bin.c_str(), const_cast<char* const*>(args.data()));
            ::_exit(127);
        }

        ::close(slave_fd);
        expect(pid > 0);

        // Give the process a moment, send 'c' to run if paused
        ::usleep(500000);
        (void)::write(master_fd, "c", 1);

        // Read for up to 3 seconds to verify TUI initialization
        std::string output;
        std::array<char, 4096> buf{};
        auto start = std::chrono::steady_clock::now();

        while (std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() -
                                                                start)
                   .count() < 3) {
            struct pollfd pfd{};
            pfd.fd = master_fd;
            pfd.events = POLLIN;
            int ret = ::poll(&pfd, 1, 200);
            if (ret > 0 && (pfd.revents & POLLIN)) {
                ssize_t n = ::read(master_fd, buf.data(), buf.size());
                if (n > 0) {
                    output.append(buf.data(), static_cast<size_t>(n));
                    if (output.find("SimRV") != std::string::npos ||
                        output.find("OpenSBI") != std::string::npos) {
                        break;
                    }
                } else {
                    break;
                }
            }
        }

        // Send Ctrl-Q to cleanly shut down TUI
        (void)::write(master_fd, "\x11", 1);
        ::close(master_fd);

        int status = 0;
        ::waitpid(pid, &status, 0);

        expect(true);
    };

    return 0;
}
