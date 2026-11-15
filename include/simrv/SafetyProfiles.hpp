/**
 * @file SafetyProfiles.hpp
 * @brief Portable C++ Safety Profiles annotations and checked memory helpers.
 */
#pragma once

#include <cstddef>
#include <cstring>
#include <optional>
#include <span>
#include <type_traits>

#if defined(__has_cpp_attribute)
#if __has_cpp_attribute(clang::lifetimebound)
#define SIMRV_LIFETIMEBOUND [[clang::lifetimebound]]
#else
#define SIMRV_LIFETIMEBOUND
#endif
#if __has_cpp_attribute(gsl::Pointer)
#define SIMRV_GSL_POINTER [[gsl::Pointer]]
#else
#define SIMRV_GSL_POINTER
#endif
#if __has_cpp_attribute(gsl::Owner)
#define SIMRV_GSL_OWNER [[gsl::Owner]]
#else
#define SIMRV_GSL_OWNER
#endif
#if __has_cpp_attribute(profiles::enforce)
#define SIMRV_PROFILE_ENFORCE(profile) [[profiles::enforce(profile)]]
#else
#define SIMRV_PROFILE_ENFORCE(profile)
#endif
#if __has_cpp_attribute(profiles::suppress)
#define SIMRV_PROFILE_SUPPRESS(profile) [[profiles::suppress(profile)]]
#else
#define SIMRV_PROFILE_SUPPRESS(profile)
#endif
#else
#define SIMRV_LIFETIMEBOUND
#define SIMRV_GSL_POINTER
#define SIMRV_GSL_OWNER
#define SIMRV_PROFILE_ENFORCE(profile)
#define SIMRV_PROFILE_SUPPRESS(profile)
#endif

namespace simrv::profiles {

/// Bounds-checked subspan extraction returning std::nullopt on overflow or out-of-range.
template <typename T>
[[nodiscard]] constexpr auto checked_subspan(std::span<T> data, std::size_t offset,
                                             std::size_t count) noexcept
    -> std::optional<std::span<T>> {
    if (offset > data.size() || count > data.size() - offset) {
        return std::nullopt;
    }
    return data.subspan(offset, count);
}

/// Bounds-checked unaligned scalar/POD read from a byte span without aliasing UB.
template <typename T, typename ByteType>
[[nodiscard]] inline auto read_unaligned(std::span<ByteType> bytes, std::size_t offset = 0) noexcept
    -> std::optional<T> {
    static_assert(std::is_trivially_copyable_v<T>, "read_unaligned requires trivially copyable T");
    static_assert(sizeof(ByteType) == 1, "read_unaligned requires single-byte element type");
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
        return std::nullopt;
    }
    T value{};
    std::memcpy(&value, bytes.data() + offset, sizeof(T));
    return value;
}

/// Bounds-checked unaligned scalar/POD write into a mutable byte span without aliasing UB.
template <typename T, typename ByteType>
[[nodiscard]] inline auto write_unaligned(std::span<ByteType> bytes, std::size_t offset,
                                          const T& value) noexcept -> bool {
    static_assert(std::is_trivially_copyable_v<T>, "write_unaligned requires trivially copyable T");
    static_assert(sizeof(ByteType) == 1, "write_unaligned requires single-byte element type");
    static_assert(!std::is_const_v<ByteType>, "write_unaligned requires a mutable span");
    if (offset > bytes.size() || sizeof(T) > bytes.size() - offset) {
        return false;
    }
    std::memcpy(bytes.data() + offset, &value, sizeof(T));
    return true;
}

}  // namespace simrv::profiles
