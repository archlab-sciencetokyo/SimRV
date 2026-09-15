#pragma once

#include <unistd.h>

#include <charconv>
#include <chrono>
#include <cstdlib>
#include <format>
#include <string_view>

namespace simrv::util {
// Optional inherited, nonblocking pipe used by benchmark drivers. Never emitted to the TUI.
inline void benchmark_event(std::string_view name, uint64_t retired = 0) {
    const char* value = std::getenv("SIMRV_EVENT_FD");
    if (!value) return;
    int fd = -1;
    const std::string_view number(value);
    auto parsed = std::from_chars(number.data(), number.data() + number.size(), fd);
    if (parsed.ec != std::errc{} || fd < 0) return;
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count();
    const auto record = std::format("{} {} {}\n", name, ns, retired);
    (void)::write(fd, record.data(), record.size());
}
}  // namespace simrv::util
