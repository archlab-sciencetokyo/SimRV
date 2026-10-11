/**
 * @file AddressTypes.hpp
 * @brief Strong XLEN physical and virtual address types.
 */
#pragma once

#include <compare>
#include <concepts>

#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::xlen {

/**
 * @struct PhysAddr
 * @brief Strong zero-overhead semantic wrapper representing physical memory addresses.
 */
struct PhysAddr {
    Word val{0};

    constexpr PhysAddr() noexcept = default;
    constexpr PhysAddr(Word v) noexcept : val(v) {}
    constexpr PhysAddr(const VirtAddr&) = delete;
    auto operator=(const VirtAddr&) -> PhysAddr& = delete;

    [[nodiscard]] constexpr auto value() const noexcept -> Word { return val; }
    [[nodiscard]] constexpr auto raw() const noexcept -> Word { return val; }
    [[nodiscard]] constexpr explicit operator Word() const noexcept { return val; }

    constexpr auto operator=(Word v) noexcept -> PhysAddr& {
        val = v;
        return *this;
    }

    constexpr auto operator<=>(const PhysAddr&) const noexcept = default;
    constexpr bool operator==(const PhysAddr&) const noexcept = default;
    template <std::integral I>
    constexpr bool operator==(I rhs) const noexcept {
        return val == static_cast<Word>(rhs);
    }
    template <std::integral I>
    constexpr auto operator<=>(I rhs) const noexcept {
        return val <=> static_cast<Word>(rhs);
    }

    template <std::integral I>
    constexpr auto operator+(I offset) const noexcept -> PhysAddr {
        return PhysAddr{val + static_cast<Word>(offset)};
    }
    template <std::integral I>
    friend constexpr auto operator+(I lhs, PhysAddr rhs) noexcept -> PhysAddr {
        return PhysAddr{static_cast<Word>(lhs) + rhs.val};
    }
    template <std::integral I>
    constexpr auto operator-(I offset) const noexcept -> PhysAddr {
        return PhysAddr{val - static_cast<Word>(offset)};
    }
    template <std::integral I>
    friend constexpr auto operator-(I lhs, PhysAddr rhs) noexcept -> PhysAddr {
        return PhysAddr{static_cast<Word>(lhs) - rhs.val};
    }
    constexpr auto operator-(PhysAddr other) const noexcept -> SignedWord {
        return static_cast<SignedWord>(val - other.val);
    }
    template <std::integral I>
    constexpr auto operator+=(I offset) noexcept -> PhysAddr& {
        val += static_cast<Word>(offset);
        return *this;
    }
    template <std::integral I>
    constexpr auto operator-=(I offset) noexcept -> PhysAddr& {
        val -= static_cast<Word>(offset);
        return *this;
    }

    template <std::integral I>
    constexpr auto operator&(I mask) const noexcept -> PhysAddr {
        return PhysAddr{val & static_cast<Word>(mask)};
    }
    template <std::integral I>
    friend constexpr auto operator&(I lhs, PhysAddr rhs) noexcept -> PhysAddr {
        return PhysAddr{static_cast<Word>(lhs) & rhs.val};
    }
    template <std::integral I>
    constexpr auto operator|(I mask) const noexcept -> PhysAddr {
        return PhysAddr{val | static_cast<Word>(mask)};
    }
    template <std::integral I>
    friend constexpr auto operator|(I lhs, PhysAddr rhs) noexcept -> PhysAddr {
        return PhysAddr{static_cast<Word>(lhs) | rhs.val};
    }
    template <std::integral I>
    constexpr auto operator^(I mask) const noexcept -> PhysAddr {
        return PhysAddr{val ^ static_cast<Word>(mask)};
    }
    template <std::integral I>
    friend constexpr auto operator^(I lhs, PhysAddr rhs) noexcept -> PhysAddr {
        return PhysAddr{static_cast<Word>(lhs) ^ rhs.val};
    }
    template <std::integral I>
    constexpr auto operator%(I mask) const noexcept -> PhysAddr {
        return PhysAddr{val % static_cast<Word>(mask)};
    }
    constexpr auto operator~() const noexcept -> PhysAddr { return PhysAddr{~val}; }
    constexpr auto operator>>(unsigned shift) const noexcept -> PhysAddr {
        return PhysAddr{val >> shift};
    }
    constexpr auto operator<<(unsigned shift) const noexcept -> PhysAddr {
        return PhysAddr{val << shift};
    }

    template <std::integral I>
    constexpr auto operator&=(I mask) noexcept -> PhysAddr& {
        val &= static_cast<Word>(mask);
        return *this;
    }
    template <std::integral I>
    constexpr auto operator|=(I mask) noexcept -> PhysAddr& {
        val |= static_cast<Word>(mask);
        return *this;
    }
    template <std::integral I>
    constexpr auto operator^=(I mask) noexcept -> PhysAddr& {
        val ^= static_cast<Word>(mask);
        return *this;
    }
};

/**
 * @struct VirtAddr
 * @brief Strong zero-overhead semantic wrapper representing virtual memory addresses.
 */
struct VirtAddr {
    Word val{0};

    constexpr VirtAddr() noexcept = default;
    constexpr VirtAddr(Word v) noexcept : val(v) {}
    constexpr VirtAddr(const PhysAddr&) = delete;
    auto operator=(const PhysAddr&) -> VirtAddr& = delete;

    [[nodiscard]] constexpr auto value() const noexcept -> Word { return val; }
    [[nodiscard]] constexpr auto raw() const noexcept -> Word { return val; }
    [[nodiscard]] constexpr explicit operator Word() const noexcept { return val; }

    constexpr auto operator=(Word v) noexcept -> VirtAddr& {
        val = v;
        return *this;
    }

    constexpr auto operator<=>(const VirtAddr&) const noexcept = default;
    constexpr bool operator==(const VirtAddr&) const noexcept = default;
    template <std::integral I>
    constexpr bool operator==(I rhs) const noexcept {
        return val == static_cast<Word>(rhs);
    }
    template <std::integral I>
    constexpr auto operator<=>(I rhs) const noexcept {
        return val <=> static_cast<Word>(rhs);
    }

    template <std::integral I>
    constexpr auto operator+(I offset) const noexcept -> VirtAddr {
        return VirtAddr{val + static_cast<Word>(offset)};
    }
    template <std::integral I>
    friend constexpr auto operator+(I lhs, VirtAddr rhs) noexcept -> VirtAddr {
        return VirtAddr{static_cast<Word>(lhs) + rhs.val};
    }
    template <std::integral I>
    constexpr auto operator-(I offset) const noexcept -> VirtAddr {
        return VirtAddr{val - static_cast<Word>(offset)};
    }
    template <std::integral I>
    friend constexpr auto operator-(I lhs, VirtAddr rhs) noexcept -> VirtAddr {
        return VirtAddr{static_cast<Word>(lhs) - rhs.val};
    }
    constexpr auto operator-(VirtAddr other) const noexcept -> SignedWord {
        return static_cast<SignedWord>(val - other.val);
    }
    template <std::integral I>
    constexpr auto operator+=(I offset) noexcept -> VirtAddr& {
        val += static_cast<Word>(offset);
        return *this;
    }
    template <std::integral I>
    constexpr auto operator-=(I offset) noexcept -> VirtAddr& {
        val -= static_cast<Word>(offset);
        return *this;
    }

    template <std::integral I>
    constexpr auto operator&(I mask) const noexcept -> VirtAddr {
        return VirtAddr{val & static_cast<Word>(mask)};
    }
    template <std::integral I>
    friend constexpr auto operator&(I lhs, VirtAddr rhs) noexcept -> VirtAddr {
        return VirtAddr{static_cast<Word>(lhs) & rhs.val};
    }
    template <std::integral I>
    constexpr auto operator|(I mask) const noexcept -> VirtAddr {
        return VirtAddr{val | static_cast<Word>(mask)};
    }
    template <std::integral I>
    friend constexpr auto operator|(I lhs, VirtAddr rhs) noexcept -> VirtAddr {
        return VirtAddr{static_cast<Word>(lhs) | rhs.val};
    }
    template <std::integral I>
    constexpr auto operator^(I mask) const noexcept -> VirtAddr {
        return VirtAddr{val ^ static_cast<Word>(mask)};
    }
    template <std::integral I>
    friend constexpr auto operator^(I lhs, VirtAddr rhs) noexcept -> VirtAddr {
        return VirtAddr{static_cast<Word>(lhs) ^ rhs.val};
    }
    template <std::integral I>
    constexpr auto operator%(I mask) const noexcept -> VirtAddr {
        return VirtAddr{val % static_cast<Word>(mask)};
    }
    constexpr auto operator~() const noexcept -> VirtAddr { return VirtAddr{~val}; }
    constexpr auto operator>>(unsigned shift) const noexcept -> VirtAddr {
        return VirtAddr{val >> shift};
    }
    constexpr auto operator<<(unsigned shift) const noexcept -> VirtAddr {
        return VirtAddr{val << shift};
    }

    template <std::integral I>
    constexpr auto operator&=(I mask) noexcept -> VirtAddr& {
        val &= static_cast<Word>(mask);
        return *this;
    }
    template <std::integral I>
    constexpr auto operator|=(I mask) noexcept -> VirtAddr& {
        val |= static_cast<Word>(mask);
        return *this;
    }
    template <std::integral I>
    constexpr auto operator^=(I mask) noexcept -> VirtAddr& {
        val ^= static_cast<Word>(mask);
        return *this;
    }
};

template <typename T>
concept StrongAddress = std::same_as<T, PhysAddr> || std::same_as<T, VirtAddr>;

}  // namespace simrv::xlen
