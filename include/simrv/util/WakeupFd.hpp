/**
 * @file WakeupFd.hpp
 * @brief Portable pollable wake channel backed by a nonblocking pipe.
 */
#pragma once

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <utility>

#include "simrv/util/UniqueFd.hpp"

namespace simrv::util {

class WakeupFd {
   public:
    [[nodiscard]] auto open() noexcept -> bool {
        if (read_fd_) return true;

        int descriptors[2]{};
        if (::pipe(descriptors) != 0) return false;
        UniqueFd read_end(descriptors[0]);
        UniqueFd write_end(descriptors[1]);
        for (const int descriptor : descriptors) {
            const int status_flags = ::fcntl(descriptor, F_GETFL, 0);
            const int descriptor_flags = ::fcntl(descriptor, F_GETFD, 0);
            if (status_flags < 0 || descriptor_flags < 0 ||
                ::fcntl(descriptor, F_SETFL, status_flags | O_NONBLOCK) < 0 ||
                ::fcntl(descriptor, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0) {
                return false;
            }
        }
        read_fd_ = std::move(read_end);
        write_fd_ = std::move(write_end);
        return true;
    }

    [[nodiscard]] auto fd() const noexcept -> int { return read_fd_.get(); }
    [[nodiscard]] explicit operator bool() const noexcept { return static_cast<bool>(read_fd_); }

    void signal() const noexcept {
        constexpr char byte = 1;
        const auto ignored = ::write(write_fd_.get(), &byte, sizeof(byte));
        static_cast<void>(ignored);
    }

    void drain() const noexcept {
        char bytes[128];
        while (::read(read_fd_.get(), bytes, sizeof(bytes)) > 0) {
        }
    }

   private:
    UniqueFd read_fd_;
    UniqueFd write_fd_;
};

}  // namespace simrv::util
