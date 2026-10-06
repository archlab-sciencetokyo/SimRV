/**
 * @file FormatUtil.hpp
 * @brief High-performance human-readable formatting utilities.
 */
#pragma once

#include <termios.h>
#include <unistd.h>

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <string>
#include <string_view>

namespace simrv::util {

/**
 * @brief Format a large integer with thousands separators.
 * @param val The value to format.
 * @return String representation with comma separators.
 */
inline auto format_with_commas(uint64_t val) -> std::string {
    std::string s = std::to_string(val);
    int n = static_cast<int>(s.length()) - 3;
    while (n > 0) {
        s.insert(static_cast<std::size_t>(n), ",");
        n -= 3;
    }
    return s;
}

/**
 * @brief Format a large integer into a human-readable scaled string (e.g. 1.23M, 4.56B, 800.00K).
 * @param val The value to format.
 * @return Scaled string representation.
 */
inline auto format_scaled(uint64_t val) -> std::string {
    if (val >= 1000000000ULL) {
        return std::format("{:.2f}B", static_cast<double>(val) / 1000000000.0);
    }
    if (val >= 1000000ULL) {
        return std::format("{:.2f}M", static_cast<double>(val) / 1000000.0);
    }
    if (val >= 1000ULL) {
        return std::format("{:.2f}K", static_cast<double>(val) / 1000.0);
    }
    return std::to_string(val);
}

/**
 * @brief Check if a file descriptor is associated with a terminal.
 * @param fd The file descriptor.
 * @return True if fd is a TTY.
 */
inline auto is_terminal(int fd) -> bool { return ::isatty(fd) != 0; }

/** Decide whether CLI presentation should emit ANSI color under standard environment policy. */
inline auto color_enabled(bool terminal, std::string_view term, bool no_color,
                          bool force_color) noexcept -> bool {
    if (force_color) return true;
    if (no_color || term == "dumb") return false;
    return terminal;
}

inline auto terminal_color_enabled(int fd) noexcept -> bool {
    const char* term_value = std::getenv("TERM");
    const char* no_color_value = std::getenv("NO_COLOR");
    const char* force_color_value = std::getenv("FORCE_COLOR");
    const auto has_value = [](const char* value) { return value != nullptr && value[0] != '\0'; };
    const bool force_disabled =
        force_color_value != nullptr && std::string_view(force_color_value) == "0";
    return color_enabled(is_terminal(fd), term_value == nullptr ? "" : term_value,
                         has_value(no_color_value) || force_disabled,
                         has_value(force_color_value) && !force_disabled);
}

inline auto terminal_path_label(const std::filesystem::path& path, int fd) -> std::string {
    auto label = path.string();
    const char* term_value = std::getenv("TERM");
    const bool links =
        is_terminal(fd) && (term_value == nullptr || std::string_view(term_value) != "dumb");
    if (!links) return label;

    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    if (error) return label;
    constexpr char kHex[] = "0123456789ABCDEF";
    std::string uri = "file://";
    for (const unsigned char byte : absolute.string()) {
        const bool unreserved = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                                (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' ||
                                byte == '.' || byte == '~' || byte == '/';
        if (unreserved) {
            uri += static_cast<char>(byte);
        } else {
            uri += '%';
            uri += kHex[byte >> 4];
            uri += kHex[byte & 0x0f];
        }
    }
    for (auto& ch : label) {
        if (static_cast<unsigned char>(ch) < 0x20 || ch == 0x7f) ch = '?';
    }
    return std::format("\033]8;;{}\033\\{}\033]8;;\033\\", uri, label);
}

/**
 * @brief Check whether this process owns the foreground interactive terminal.
 *
 * A descriptor may refer to a tty without this process being its foreground owner. Batch and
 * redirected CLI runs must not try to change terminal attributes.
 */
inline auto owns_interactive_terminal(int input_fd = STDIN_FILENO, int output_fd = STDOUT_FILENO)
    -> bool {
    if (!is_terminal(input_fd) || !is_terminal(output_fd)) return false;
    const auto foreground_group = ::tcgetpgrp(input_fd);
    return foreground_group >= 0 && foreground_group == ::getpgrp();
}

inline auto interactive_tui_available() noexcept -> bool {
    const char* term_value = std::getenv("TERM");
    return owns_interactive_terminal() &&
           (term_value == nullptr || std::string_view(term_value) != "dumb");
}

namespace ansi {
constexpr std::string_view kReset = "\033[0m";
constexpr std::string_view kBold = "\033[1m";

// Foreground Colors
constexpr std::string_view kBlack = "\033[30m";
constexpr std::string_view kRed = "\033[31m";
constexpr std::string_view kGreen = "\033[32m";
constexpr std::string_view kYellow = "\033[33m";
constexpr std::string_view kBlue = "\033[34m";
constexpr std::string_view kMagenta = "\033[35m";
constexpr std::string_view kCyan = "\033[36m";
constexpr std::string_view kWhite = "\033[37m";

// Bright Foreground Colors
constexpr std::string_view kBrightBlack = "\033[90m";
constexpr std::string_view kBrightRed = "\033[91m";
constexpr std::string_view kBrightGreen = "\033[92m";
constexpr std::string_view kBrightYellow = "\033[93m";
constexpr std::string_view kBrightBlue = "\033[94m";
constexpr std::string_view kBrightMagenta = "\033[95m";
constexpr std::string_view kBrightCyan = "\033[96m";
constexpr std::string_view kBrightWhite = "\033[97m";

// Background colors
constexpr std::string_view kBgBlack = "\033[40m";
constexpr std::string_view kBgRed = "\033[41m";
constexpr std::string_view kBgGreen = "\033[42m";
constexpr std::string_view kBgYellow = "\033[43m";
constexpr std::string_view kBgBlue = "\033[44m";
constexpr std::string_view kBgMagenta = "\033[45m";
constexpr std::string_view kBgCyan = "\033[46m";
constexpr std::string_view kBgWhite = "\033[47m";

// Combined styles used in SimRV TUI
constexpr std::string_view kBgYellowFgBlack = "\033[1;43;30m";
constexpr std::string_view kBgGreenFgBlack = "\033[1;42;30m";
constexpr std::string_view kBgRedFgBlack = "\033[1;30;41m";
constexpr std::string_view kBgYellowFgBlackBlink = "\033[1;5;30;43m";

constexpr std::string_view kBoldFgBrightBlue = "\033[1;94m";
constexpr std::string_view kBoldFgBrightWhite = "\033[1;97m";
constexpr std::string_view kBoldFgBrightGreen = "\033[1;92m";
constexpr std::string_view kBoldFgCyan = "\033[1;36m";
constexpr std::string_view kBoldFgRed = "\033[1;31m";
constexpr std::string_view kBoldFgBrightBlack = "\033[1;90m";
constexpr std::string_view kBoldFgBrightYellow = "\033[1;93m";

// Terminal control sequences
constexpr std::string_view kClearScreen = "\033[2J";
constexpr std::string_view kCursorHome = "\033[H";
constexpr std::string_view kHideCursor = "\033[?25l";
constexpr std::string_view kShowCursor = "\033[?25h";
constexpr std::string_view kEnterAltScreen = "\033[?1049h";
constexpr std::string_view kLeaveAltScreen = "\033[?1049l";
}  // namespace ansi

class TerminalModeGuard {
   public:
    ~TerminalModeGuard() {
        if (active_) {
            tcsetattr(0, TCSANOW, &saved_);
        }
    }

    auto enable_raw_mode() -> bool {
        struct termios tty{};
        if (tcgetattr(0, &tty) != 0) {
            return false;
        }
        saved_ = tty;

        tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
        tty.c_oflag |= OPOST;
        tty.c_lflag &= ~(ECHO | ECHONL | ICANON | IEXTEN);
        tty.c_cflag &= ~(CSIZE | PARENB);
        tty.c_cflag |= CS8;
        tty.c_cc[VMIN] = 1;
        tty.c_cc[VTIME] = 0;
        if (tcsetattr(0, TCSANOW, &tty) != 0) {
            return false;
        }

        active_ = true;
        return true;
    }

   private:
    bool active_ = false;
    struct termios saved_{};
};

}  // namespace simrv::util
