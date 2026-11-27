/**
 * @file AddressTypes.hpp
 * @brief Strong XLEN physical and virtual address types.
 */
#pragma once

#include <compare>
#include <concepts>
#include <type_traits>

#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::xlen {

/** A zero-overhead address value whose tag identifies its semantic domain. */
template <typename Tag>
struct BasicAddress {
    constexpr BasicAddress() noexcept = default;
    constexpr explicit BasicAddress(Word value) noexcept : val(value) {}
    template <std::integral I>
    constexpr explicit BasicAddress(I value) noexcept : val(static_cast<Word>(value)) {}

    [[nodiscard]] constexpr auto value() const noexcept -> Word { return val; }
    [[nodiscard]] constexpr auto raw() const noexcept -> Word { return val; }
    [[nodiscard]] constexpr explicit operator Word() const noexcept { return val; }

    constexpr auto operator<=>(const BasicAddress&) const noexcept = default;
    constexpr bool operator==(const BasicAddress&) const noexcept = default;

    template <std::integral I>
    constexpr bool operator==(I rhs) const noexcept {
        return val == static_cast<Word>(rhs);
    }

    template <std::integral I>
    constexpr auto operator<=>(I rhs) const noexcept {
        return val <=> static_cast<Word>(rhs);
    }

    template <std::integral I>
    constexpr auto operator+(I offset) const noexcept -> BasicAddress {
        return BasicAddress{val + static_cast<Word>(offset)};
    }

    template <std::integral I>
    friend constexpr auto operator+(I lhs, BasicAddress rhs) noexcept -> BasicAddress {
        return BasicAddress{static_cast<Word>(lhs) + rhs.val};
    }

    template <std::integral I>
    constexpr auto operator-(I offset) const noexcept -> BasicAddress {
        return BasicAddress{val - static_cast<Word>(offset)};
    }

    template <std::integral I>
    friend constexpr auto operator-(I lhs, BasicAddress rhs) noexcept -> BasicAddress {
        return BasicAddress{static_cast<Word>(lhs) - rhs.val};
    }

    constexpr auto operator-(BasicAddress other) const noexcept -> SignedWord {
        return static_cast<SignedWord>(val - other.val);
    }

    template <std::integral I>
    constexpr auto operator+=(I offset) noexcept -> BasicAddress& {
        val += static_cast<Word>(offset);
        return *this;
    }

    template <std::integral I>
    constexpr auto operator-=(I offset) noexcept -> BasicAddress& {
        val -= static_cast<Word>(offset);
        return *this;
    }

    template <std::integral I>
    constexpr auto operator&(I mask) const noexcept -> BasicAddress {
        return BasicAddress{val & static_cast<Word>(mask)};
    }

    template <std::integral I>
    friend constexpr auto operator&(I lhs, BasicAddress rhs) noexcept -> BasicAddress {
        return BasicAddress{static_cast<Word>(lhs) & rhs.val};
    }

    template <std::integral I>
    constexpr auto operator|(I mask) const noexcept -> BasicAddress {
        return BasicAddress{val | static_cast<Word>(mask)};
    }

    template <std::integral I>
    friend constexpr auto operator|(I lhs, BasicAddress rhs) noexcept -> BasicAddress {
        return BasicAddress{static_cast<Word>(lhs) | rhs.val};
    }

    template <std::integral I>
    constexpr auto operator^(I mask) const noexcept -> BasicAddress {
        return BasicAddress{val ^ static_cast<Word>(mask)};
    }

    template <std::integral I>
    friend constexpr auto operator^(I lhs, BasicAddress rhs) noexcept -> BasicAddress {
        return BasicAddress{static_cast<Word>(lhs) ^ rhs.val};
    }

    template <std::integral I>
    constexpr auto operator%(I divisor) const noexcept -> BasicAddress {
        return BasicAddress{val % static_cast<Word>(divisor)};
    }

    template <std::integral I>
    constexpr auto operator/(I divisor) const noexcept -> Word {
        return val / static_cast<Word>(divisor);
    }

    constexpr auto operator~() const noexcept -> BasicAddress { return BasicAddress{~val}; }

    constexpr auto operator>>(unsigned shift) const noexcept -> BasicAddress {
        return BasicAddress{val >> shift};
    }

    constexpr auto operator<<(unsigned shift) const noexcept -> BasicAddress {
        return BasicAddress{val << shift};
    }

    template <std::integral I>
    constexpr auto operator&=(I mask) noexcept -> BasicAddress& {
        val &= static_cast<Word>(mask);
        return *this;
    }

    template <std::integral I>
    constexpr auto operator|=(I mask) noexcept -> BasicAddress& {
        val |= static_cast<Word>(mask);
        return *this;
    }

    template <std::integral I>
    constexpr auto operator^=(I mask) noexcept -> BasicAddress& {
        val ^= static_cast<Word>(mask);
        return *this;
    }

   private:
    Word val{0};
};

struct VirtAddrTag {};
struct PhysAddrTag {};

template <typename T>
concept StrongAddress = std::same_as<T, PhysAddr> || std::same_as<T, VirtAddr>;

static_assert(sizeof(VirtAddr) == sizeof(Word));
static_assert(sizeof(PhysAddr) == sizeof(Word));
static_assert(std::is_standard_layout_v<VirtAddr> && std::is_trivially_copyable_v<VirtAddr>);
static_assert(std::is_standard_layout_v<PhysAddr> && std::is_trivially_copyable_v<PhysAddr>);
static_assert(!std::convertible_to<VirtAddr, PhysAddr>);
static_assert(!std::convertible_to<PhysAddr, VirtAddr>);
static_assert(!std::convertible_to<Word, VirtAddr>);
static_assert(!std::convertible_to<Word, PhysAddr>);

}  // namespace simrv::xlen
