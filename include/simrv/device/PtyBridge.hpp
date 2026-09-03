/**
 * @file PtyBridge.hpp
 * @brief POSIX Pseudo-Terminal (PTY) bridge for native guest terminal I/O.
 *
 * Opens a master/slave PTY pair via openpty(). The simulator owns the master
 * endpoint and external terminal programs open the slave path (/dev/pts/N).
 */
#pragma once

#include <fcntl.h>
#include <pty.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <string>

#include "simrv/debug/GdbStub.hpp"

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
        int m_fd = -1;
        int s_fd = -1;
        if (::openpty(&m_fd, &s_fd, name_buf, nullptr, nullptr) != 0) {
            return false;
        }
        master_fd_ = debug::UniqueFd(m_fd);
        slave_fd_ = debug::UniqueFd(s_fd);
        slave_path_ = name_buf;

        // UART MMIO must never block waiting for an external terminal to drain output.
        int flags = ::fcntl(master_fd_.get(), F_GETFL, 0);
        if (flags >= 0) {
            ::fcntl(master_fd_.get(), F_SETFL, flags | O_NONBLOCK);
        }

        // Configure the terminal endpoint as a transparent byte stream. Terminal
        // programs may subsequently apply their own preferred slave settings.
        struct termios t{};
        if (::tcgetattr(slave_fd_.get(), &t) == 0) {
            ::cfmakeraw(&t);
            ::tcsetattr(slave_fd_.get(), TCSANOW, &t);
        }

        is_open_ = true;
        return true;
    }

    void close() {
        is_open_ = false;
        master_fd_.reset();
        slave_fd_.reset();
    }

    [[nodiscard]] auto is_open() const -> bool { return is_open_; }
    [[nodiscard]] auto master_fd() const -> int { return master_fd_.get(); }
    [[nodiscard]] auto slave_path() const -> const std::string& { return slave_path_; }

    /// Write guest UART TX output. It becomes readable by the external slave endpoint.
    auto write_byte_to_master(uint8_t byte) -> ssize_t {
        if (!master_fd_) return -1;
        return ::write(master_fd_.get(), &byte, 1);
    }

    /// Write a guest UART TX buffer to the external slave endpoint.
    auto write_to_master(const uint8_t* data, std::size_t len) -> ssize_t {
        if (!master_fd_ || len == 0) return 0;
        return ::write(master_fd_.get(), data, len);
    }

    /// Read external terminal input written through the slave endpoint.
    auto read_from_master(uint8_t* buf, std::size_t max_len) -> ssize_t {
        if (!master_fd_ || max_len == 0) return 0;
        ssize_t n = ::read(master_fd_.get(), buf, max_len);
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        return n;
    }

   private:
    debug::UniqueFd master_fd_;
    debug::UniqueFd slave_fd_;
    std::string slave_path_;
    bool is_open_{false};
};

}  // namespace simrv::device
