/**
 * @file Types.hpp
 * @brief Base types and widths for the XLEN abstraction layer.
 */
#pragma once

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <type_traits>
#include <utility>

#ifndef SIMRV_XLEN
#define SIMRV_XLEN 32
#endif

static_assert(SIMRV_XLEN == 32 || SIMRV_XLEN == 64, "SIMRV_XLEN must be 32 or 64");

namespace simrv::xlen {
inline constexpr bool kIsXLen64 = (SIMRV_XLEN == 64);
inline constexpr unsigned kXLenBits = SIMRV_XLEN;
inline constexpr unsigned kXLenHexDigits = kXLenBits / 4u;
}  // namespace simrv::xlen

// Expose fundamental constants for global compatibility
using simrv::xlen::kIsXLen64;
using simrv::xlen::kXLenBits;
using simrv::xlen::kXLenHexDigits;

// C++23 Conditional Type Selection
using Word = std::conditional_t<kIsXLen64, uint64_t, uint32_t>;
using SignedWord = std::conditional_t<kIsXLen64, int64_t, int32_t>;
using Register = Word;
using Byte = std::byte;
using CompressedInstruction = uint16_t;
using FloatingRegister = uint64_t;
using Counter = uint64_t;

// Forward declarations for strong semantic types
struct VirtAddr;
struct PhysAddr;
struct HartId;
struct CsrNumber;

// Architectural RISC-V Semantic Types
using Address = Word;
using Instruction = uint32_t;  // RISC-V base instructions are exactly 32 bits
using InstructionId = Counter;
using CycleCount = Counter;
using CSRValue = Word;
using CSRAddress = Address;
using ImmValue = SignedWord;
using TrapCause = Word;
using PageFaultCause = TrapCause;
using PortId = uint8_t;
using LatencyCycles = uint32_t;
using BhtIndex = uint32_t;
using BtbIndex = uint32_t;
using RasIndex = uint32_t;
using RasDepth = uint32_t;
using BranchTag = uint32_t;
using GlobalHistory = uint32_t;

enum class BranchDirection : uint8_t { NotTaken = 0, Taken = 1 };
using PageTableLevel = int;
using VpnIndex = uint32_t;
using PpnIndex = uint64_t;
using IrqNumber = uint32_t;
using InterruptSourceId = uint32_t;
using InterruptPriority = uint32_t;
using PlicContextId = uint32_t;
using Funct7 = uint8_t;
using Funct12 = uint16_t;
using Funct6 = uint8_t;
using Funct2 = uint8_t;
using CsrImm = uint8_t;

enum class AccessWidth : uint8_t { Byte = 1, HalfWord = 2, Word = 4, DoubleWord = 8 };

enum class FpPrecision : uint8_t { Single = 32, Double = 64, Quad = 128 };

enum class Vsew : uint8_t {
    SEW_8 = 0,
    SEW_16 = 1,
    SEW_32 = 2,
    SEW_64 = 3,
};

enum class Vlmul : uint8_t {
    LMUL_1 = 0,
    LMUL_2 = 1,
    LMUL_4 = 2,
    LMUL_8 = 3,
    LMUL_RESERVED = 4,
    LMUL_F8 = 5,
    LMUL_F4 = 6,
    LMUL_F2 = 7,
};

enum class Vta : uint8_t { Undisturbed = 0, Agnostic = 1 };
enum class Vma : uint8_t { Undisturbed = 0, Agnostic = 1 };

struct VtypeView {
    uint64_t raw{0};
    uint8_t xlen{64};

    [[nodiscard]] constexpr auto vill() const noexcept -> bool {
        uint64_t const vill_mask = 1ULL << (xlen - 1);
        if ((raw & vill_mask) != 0) return true;
        uint64_t const reserved_mask =
            (xlen == 32) ? ~(0xFFULL | vill_mask) & 0xFFFFFFFFULL : ~(0xFFULL | vill_mask);
        if ((raw & reserved_mask) != 0) return true;
        if (sew_field() > 3 || lmul_field() == 4) return true;
        return false;
    }

    [[nodiscard]] constexpr auto sew_field() const noexcept -> uint8_t {
        return static_cast<uint8_t>((raw >> 3) & 0x7);
    }
    [[nodiscard]] constexpr auto lmul_field() const noexcept -> uint8_t {
        return static_cast<uint8_t>(raw & 0x7);
    }
    [[nodiscard]] constexpr auto vsew() const noexcept -> Vsew {
        return static_cast<Vsew>(sew_field());
    }
    [[nodiscard]] constexpr auto vlmul() const noexcept -> Vlmul {
        return static_cast<Vlmul>(lmul_field());
    }
    [[nodiscard]] constexpr auto vta() const noexcept -> Vta {
        return static_cast<Vta>((raw >> 6) & 0x1);
    }
    [[nodiscard]] constexpr auto vma() const noexcept -> Vma {
        return static_cast<Vma>((raw >> 7) & 0x1);
    }

    [[nodiscard]] constexpr auto sew_bits() const noexcept -> uint32_t { return 8u << sew_field(); }
    [[nodiscard]] constexpr auto sew_bytes() const noexcept -> uint32_t {
        return 1u << sew_field();
    }
};

enum class PrivilegeLevel : uint8_t { User = 0, Supervisor = 1, Machine = 3 };

constexpr auto operator<(PrivilegeLevel lhs, PrivilegeLevel rhs) -> bool {
    return std::to_underlying(lhs) < std::to_underlying(rhs);
}

constexpr auto operator<=(PrivilegeLevel lhs, PrivilegeLevel rhs) -> bool {
    return std::to_underlying(lhs) <= std::to_underlying(rhs);
}

constexpr auto operator>(PrivilegeLevel lhs, PrivilegeLevel rhs) -> bool {
    return std::to_underlying(lhs) > std::to_underlying(rhs);
}

constexpr auto operator>=(PrivilegeLevel lhs, PrivilegeLevel rhs) -> bool {
    return std::to_underlying(lhs) >= std::to_underlying(rhs);
}

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

using PhysicalAddress = PhysAddr;
using VirtualAddress = VirtAddr;

/**
 * @struct HartId
 * @brief Strong zero-overhead semantic wrapper representing hardware thread (Hart) identifiers.
 */
struct HartId {
    uint32_t val{0};

    constexpr HartId() noexcept = default;
    constexpr HartId(uint32_t v) noexcept : val(v) {}

    [[nodiscard]] constexpr auto value() const noexcept -> uint32_t { return val; }
    [[nodiscard]] constexpr auto raw() const noexcept -> uint32_t { return val; }
    [[nodiscard]] constexpr explicit operator uint32_t() const noexcept { return val; }

    constexpr auto operator=(uint32_t v) noexcept -> HartId& {
        val = v;
        return *this;
    }

    constexpr auto operator<=>(const HartId&) const noexcept = default;
    constexpr bool operator==(const HartId&) const noexcept = default;
    template <std::integral I>
    constexpr bool operator==(I rhs) const noexcept {
        return val == static_cast<uint32_t>(rhs);
    }
    template <std::integral I>
    constexpr auto operator<=>(I rhs) const noexcept {
        return val <=> static_cast<uint32_t>(rhs);
    }

    template <std::integral I>
    constexpr auto operator+(I offset) const noexcept -> HartId {
        return HartId{val + static_cast<uint32_t>(offset)};
    }
    template <std::integral I>
    friend constexpr auto operator+(I lhs, HartId rhs) noexcept -> HartId {
        return HartId{static_cast<uint32_t>(lhs) + rhs.val};
    }
    template <std::integral I>
    constexpr auto operator-(I offset) const noexcept -> HartId {
        return HartId{val - static_cast<uint32_t>(offset)};
    }
    template <std::integral I>
    friend constexpr auto operator-(I lhs, HartId rhs) noexcept -> HartId {
        return HartId{static_cast<uint32_t>(lhs) - rhs.val};
    }
    template <std::integral I>
    friend constexpr auto operator<<(I lhs, HartId rhs) noexcept -> I {
        return static_cast<I>(lhs << rhs.val);
    }
    template <std::integral I>
    constexpr auto operator+=(I offset) noexcept -> HartId& {
        val += static_cast<uint32_t>(offset);
        return *this;
    }
    template <std::integral I>
    constexpr auto operator-=(I offset) noexcept -> HartId& {
        val -= static_cast<uint32_t>(offset);
        return *this;
    }
};

/**
 * @struct CsrNumber
 * @brief Strong semantic type for CSR (Control and Status Register) index numbers.
 */
struct CsrNumber {
    uint16_t val{0};

    constexpr CsrNumber() noexcept = default;
    constexpr CsrNumber(uint16_t v) noexcept : val(v) {}

    [[nodiscard]] constexpr auto value() const noexcept -> uint16_t { return val; }
    [[nodiscard]] constexpr auto raw() const noexcept -> uint16_t { return val; }
    [[nodiscard]] constexpr explicit operator uint16_t() const noexcept { return val; }

    constexpr auto operator=(uint16_t v) noexcept -> CsrNumber& {
        val = v;
        return *this;
    }

    constexpr auto operator<=>(const CsrNumber&) const noexcept = default;
    constexpr bool operator==(const CsrNumber&) const noexcept = default;
    template <std::integral I>
    constexpr bool operator==(I rhs) const noexcept {
        return val == static_cast<uint16_t>(rhs);
    }
    template <std::integral I>
    constexpr auto operator<=>(I rhs) const noexcept {
        return val <=> static_cast<uint16_t>(rhs);
    }

    [[nodiscard]] constexpr auto is_read_only() const noexcept -> bool {
        return ((val >> 10U) & 0x3U) == 0x3U;
    }
    [[nodiscard]] constexpr auto privilege_level() const noexcept -> PrivilegeLevel {
        return static_cast<PrivilegeLevel>((val >> 8U) & 0x3U);
    }
};

template <typename T>
concept StrongAddress = std::same_as<T, PhysAddr> || std::same_as<T, VirtAddr>;

enum class RegId : uint8_t {
    Zero = 0,
    Ra = 1,
    Sp = 2,
    Gp = 3,
    Tp = 4,
    T0 = 5,
    T1 = 6,
    T2 = 7,
    S0 = 8,
    Fp = 8,
    S1 = 9,
    A0 = 10,
    A1 = 11,
    A2 = 12,
    A3 = 13,
    A4 = 14,
    A5 = 15,
    A6 = 16,
    A7 = 17,
    S2 = 18,
    S3 = 19,
    S4 = 20,
    S5 = 21,
    S6 = 22,
    S7 = 23,
    S8 = 24,
    S9 = 25,
    S10 = 26,
    S11 = 27,
    T3 = 28,
    T4 = 29,
    T5 = 30,
    T6 = 31
};

enum class FpRegId : uint8_t {
    Ft0 = 0,
    Ft1 = 1,
    Ft2 = 2,
    Ft3 = 3,
    Ft4 = 4,
    Ft5 = 5,
    Ft6 = 6,
    Ft7 = 7,
    Fs0 = 8,
    Fs1 = 9,
    Fa0 = 10,
    Fa1 = 11,
    Fa2 = 12,
    Fa3 = 13,
    Fa4 = 14,
    Fa5 = 15,
    Fa6 = 16,
    Fa7 = 17,
    Fs2 = 18,
    Fs3 = 19,
    Fs4 = 20,
    Fs5 = 21,
    Fs6 = 22,
    Fs7 = 23,
    Fs8 = 24,
    Fs9 = 25,
    Fs10 = 26,
    Fs11 = 27,
    Ft8 = 28,
    Ft9 = 29,
    Ft10 = 30,
    Ft11 = 31
};

using FRegId = FpRegId;

enum class VecRegId : uint8_t {
    V0 = 0,
    V1 = 1,
    V2 = 2,
    V3 = 3,
    V4 = 4,
    V5 = 5,
    V6 = 6,
    V7 = 7,
    V8 = 8,
    V9 = 9,
    V10 = 10,
    V11 = 11,
    V12 = 12,
    V13 = 13,
    V14 = 14,
    V15 = 15,
    V16 = 16,
    V17 = 17,
    V18 = 18,
    V19 = 19,
    V20 = 20,
    V21 = 21,
    V22 = 22,
    V23 = 23,
    V24 = 24,
    V25 = 25,
    V26 = 26,
    V27 = 27,
    V28 = 28,
    V29 = 29,
    V30 = 30,
    V31 = 31
};

using VRegId = VecRegId;

template <typename T>
concept RegisterIdentifier =
    std::same_as<T, RegId> || std::same_as<T, FpRegId> || std::same_as<T, VecRegId>;

namespace simrv {
using ::CsrNumber;
using ::FpRegId;
using ::FRegId;
using ::HartId;
using ::PhysAddr;
using ::RegId;
using ::RegisterIdentifier;
using ::StrongAddress;
using ::VecRegId;
using ::VirtAddr;
using ::VRegId;

namespace xlen {
using ::CsrNumber;
using ::FpRegId;
using ::FRegId;
using ::HartId;
using ::PhysAddr;
using ::RegId;
using ::RegisterIdentifier;
using ::StrongAddress;
using ::VecRegId;
using ::VirtAddr;
using ::VRegId;
}  // namespace xlen
}  // namespace simrv

template <typename EnumType>
constexpr auto enum_mask(EnumType bit) {
    return std::to_underlying(bit);
}

template <typename EnumType>
constexpr bool has_enum_mask(std::underlying_type_t<EnumType> value, EnumType bit) {
    return (value & enum_mask(bit)) != 0;
}

// Specializations for std::hash
template <>
struct std::hash<PhysAddr> {
    constexpr auto operator()(PhysAddr addr) const noexcept -> size_t {
        return std::hash<Word>{}(addr.val);
    }
};

template <>
struct std::hash<VirtAddr> {
    constexpr auto operator()(VirtAddr addr) const noexcept -> size_t {
        return std::hash<Word>{}(addr.val);
    }
};

template <>
struct std::hash<HartId> {
    constexpr auto operator()(HartId id) const noexcept -> size_t {
        return std::hash<uint32_t>{}(id.val);
    }
};

template <>
struct std::hash<CsrNumber> {
    constexpr auto operator()(CsrNumber csr) const noexcept -> size_t {
        return std::hash<uint16_t>{}(csr.val);
    }
};

// Specializations for std::formatter
template <>
struct std::formatter<PhysAddr> : std::formatter<Word> {
    auto format(PhysAddr addr, std::format_context& ctx) const {
        return std::formatter<Word>::format(addr.val, ctx);
    }
};

template <>
struct std::formatter<VirtAddr> : std::formatter<Word> {
    auto format(VirtAddr addr, std::format_context& ctx) const {
        return std::formatter<Word>::format(addr.val, ctx);
    }
};

template <>
struct std::formatter<HartId> : std::formatter<uint32_t> {
    auto format(HartId id, std::format_context& ctx) const {
        return std::formatter<uint32_t>::format(id.val, ctx);
    }
};

template <>
struct std::formatter<CsrNumber> : std::formatter<uint16_t> {
    auto format(CsrNumber csr, std::format_context& ctx) const {
        return std::formatter<uint16_t>::format(csr.val, ctx);
    }
};
