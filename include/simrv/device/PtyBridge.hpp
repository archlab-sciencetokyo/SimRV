/**
 * @file PtyBridge.hpp
 * @brief POSIX Pseudo-Terminal (PTY) bridge for native guest terminal I/O.
 *
 * Opens a master/slave PTY pair via openpty(). The master fd is used for all
 * I/O between the simulator and the guest UART/console. The slave path
 * (/dev/pts/N) can be opened by external tools (picocom, screen, etc.).
 */
#pragma once

#include <fcntl.h>
#include <pty.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

namespace simrv::device {

class PtyBridge {
   public:
    PtyBridge() = default;
    ~PtyBridge() { close(); }

    PtyBridge(const PtyBridge&) = delete;
    PtyBridge& operator=(const PtyBridge&) = delete;

    /// Open the PTY pair. Returns true on success.
    [[nodiscard]] auto open() -> bool {
        char name_buf[64]{};
        if (::openpty(&master_fd_, &slave_fd_, name_buf, nullptr, nullptr) != 0) {
            return false;
        }
        slave_path_ = name_buf;

        // Put master in non-blocking mode for polling reads
        int flags = ::fcntl(master_fd_, F_GETFL, 0);
        if (flags >= 0) {
            ::fcntl(master_fd_, F_SETFL, flags | O_NONBLOCK);
        }

        // Keep master in raw mode so no host line-discipline transforms bytes
        struct termios t{};
        if (::tcgetattr(master_fd_, &t) == 0) {
            ::cfmakeraw(&t);
            ::tcsetattr(master_fd_, TCSANOW, &t);
        }

        is_open_ = true;
        return true;
    }

    void close() {
        is_open_ = false;
        if (master_fd_ >= 0) {
            ::close(master_fd_);
            master_fd_ = -1;
        }
        if (slave_fd_ >= 0) {
            ::close(slave_fd_);
            slave_fd_ = -1;
        }
    }

    [[nodiscard]] auto is_open() const -> bool { return is_open_; }
    [[nodiscard]] auto master_fd() const -> int { return master_fd_; }
    [[nodiscard]] auto slave_path() const -> const std::string& { return slave_path_; }

    /// Write a byte from the guest (UART TX output) to the PTY slave.
    /// Data written to the slave appears on the master fd, where render() picks it up.
    auto write_byte_to_slave(uint8_t byte) -> ssize_t {
        if (slave_fd_ < 0) return -1;
        return ::write(slave_fd_, &byte, 1);
    }

    /// Write a buffer from the guest to the PTY slave.
    auto write_to_slave(const uint8_t* data, std::size_t len) -> ssize_t {
        if (slave_fd_ < 0 || len == 0) return 0;
        return ::write(slave_fd_, data, len);
    }

    /// Non-blocking read from master fd.
    /// This is where guest output appears (written by guest to slave → readable on master).
    /// Also the entry point for host keyboard writes (master → slave pipe).
    auto read_from_master(uint8_t* buf, std::size_t max_len) -> ssize_t {
        if (master_fd_ < 0 || max_len == 0) return 0;
        ssize_t n = ::read(master_fd_, buf, max_len);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        return n;
    }

    /// Non-blocking read from slave fd.
    /// Host keyboard data (written to master_fd) appears here after PTY processing.
    auto read_from_slave(uint8_t* buf, std::size_t max_len) -> ssize_t {
        if (slave_fd_ < 0 || max_len == 0) return 0;
        ssize_t n = ::read(slave_fd_, buf, max_len);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        return n;
    }

    [[nodiscard]] auto slave_fd() const -> int { return slave_fd_; }


    /// Check (zero-timeout select) whether master fd has data to read.
    [[nodiscard]] auto has_data() const -> bool {
        if (master_fd_ < 0) return false;
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(master_fd_, &fds);
        struct timeval tv{0, 0};
        return ::select(master_fd_ + 1, &fds, nullptr, nullptr, &tv) > 0;
    }

   private:
    int master_fd_{-1};
    int slave_fd_{-1};
    std::string slave_path_;
    bool is_open_{false};
};

}  // namespace simrv::device
