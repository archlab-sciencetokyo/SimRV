/**
 * @file TestHelper.hpp
 * @brief Common test infrastructure, C++23 source_location assertions, and test fixtures.
 */
#pragma once

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>

namespace simrv::test {

inline auto failures() noexcept -> int { return 0; }

inline void check(bool condition, std::string_view expr,
                  std::source_location loc = std::source_location::current()) {
    if (!condition) {
        std::cerr << "Assertion failed: " << expr << " at " << loc.file_name() << ":" << loc.line()
                  << "\n";
        std::abort();
    }
}

template <typename V>
inline auto print_val(const V& val) {
    if constexpr (std::is_enum_v<V>) {
        return static_cast<std::underlying_type_t<V>>(val);
    } else {
        return val;
    }
}

template <typename T, typename U>
inline void check_eq(const T& actual, const U& expected, std::string_view expr_actual,
                     std::string_view expr_expected,
                     std::source_location loc = std::source_location::current()) {
    if (actual != expected) {
        std::cerr << "Assertion failed: " << expr_actual << " == " << expr_expected << "\n"
                  << "  Expected: " << print_val(expected) << "\n"
                  << "  Actual:   " << print_val(actual) << "\n"
                  << "  At:       " << loc.file_name() << ":" << loc.line() << "\n";
        std::abort();
    }
}

template <typename T, typename U>
inline void check_ne(const T& actual, const U& expected, std::string_view expr_actual,
                     std::string_view expr_expected,
                     std::source_location loc = std::source_location::current()) {
    if (actual == expected) {
        std::cerr << "Assertion failed: " << expr_actual << " != " << expr_expected << "\n"
                  << "  Unexpected: " << print_val(actual) << "\n"
                  << "  At:         " << loc.file_name() << ":" << loc.line() << "\n";
        std::abort();
    }
}

/// RAII helper for creating isolated temporary directories with guaranteed cleanup
class ScopedTempDir {
   public:
    explicit ScopedTempDir(std::string_view prefix = "simrv_test") {
        static std::mt19937_64 rng{std::random_device{}()};
        const auto id = rng();
        path_ = std::filesystem::temp_directory_path() /
                (std::string(prefix) + "_" + std::to_string(id));
        std::filesystem::create_directories(path_);
    }

    ~ScopedTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }

    ScopedTempDir(const ScopedTempDir&) = delete;
    auto operator=(const ScopedTempDir&) -> ScopedTempDir& = delete;
    ScopedTempDir(ScopedTempDir&&) noexcept = default;
    auto operator=(ScopedTempDir&&) noexcept -> ScopedTempDir& = default;

    [[nodiscard]] auto path() const noexcept -> const std::filesystem::path& { return path_; }
    [[nodiscard]] auto string() const -> std::string { return path_.string(); }
    operator const std::filesystem::path&() const noexcept { return path_; }
    operator std::filesystem::path() const { return path_; }

    auto operator/(const std::filesystem::path& sub) const -> std::filesystem::path {
        return path_ / sub;
    }

   private:
    std::filesystem::path path_;
};

}  // namespace simrv::test

#define TEST_CHECK(cond) ::simrv::test::check(static_cast<bool>(cond), #cond)
#define TEST_CHECK_EQ(a, b) ::simrv::test::check_eq((a), (b), #a, #b)
#define TEST_CHECK_NE(a, b) ::simrv::test::check_ne((a), (b), #a, #b)
