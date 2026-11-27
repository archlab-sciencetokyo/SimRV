/**
 * @file UniqueFd.hpp
 * @brief Small RAII owner for POSIX file descriptors.
 */
#pragma once

#include <unistd.h>

#include <utility>

namespace simrv::util {

class UniqueFd {
   public:
    constexpr UniqueFd() noexcept = default;
    constexpr explicit UniqueFd(int fd) noexcept : fd_(fd) {}
    ~UniqueFd() { reset(); }

    UniqueFd(const UniqueFd&) = delete;
    auto operator=(const UniqueFd&) -> UniqueFd& = delete;
    UniqueFd(UniqueFd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    auto operator=(UniqueFd&& other) noexcept -> UniqueFd& {
        if (this != &other) {
            reset();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }

    void reset(int replacement = -1) noexcept {
        if (fd_ >= 0) ::close(fd_);
        fd_ = replacement;
    }
    [[nodiscard]] auto get() const noexcept -> int { return fd_; }
    [[nodiscard]] auto release() noexcept -> int { return std::exchange(fd_, -1); }
    [[nodiscard]] explicit operator bool() const noexcept { return fd_ >= 0; }

   private:
    int fd_ = -1;
};

}  // namespace simrv::util
