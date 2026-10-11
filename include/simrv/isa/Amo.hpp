/**
 * @file Amo.hpp
 * @brief RISC-V Atomic (A) extension definitions.
 */
#pragma once

#include <concepts>
#include <cstdint>
#include <utility>

#include "simrv/isa/Base.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::isa {

/**
 * @brief Return the natural access size encoded by an A-extension instruction.
 *
 * The standard encodes AMO.W with funct3=010 and AMO.D with funct3=011. Other encodings are
 * reserved and return zero so callers can reject them.
 */
constexpr auto amo_operand_bytes(Funct3 funct3) -> unsigned {
    if (funct3 == Funct3::Lw) return 4;
    if (funct3 == Funct3::Ld) return 8;
    return 0;
}

/** Return whether the AMO width exists for the build's architectural XLEN. */
constexpr auto amo_width_supported(Funct3 funct3) -> bool {
    return funct3 == Funct3::Lw || (simrv::xlen::kIsXLen64 && funct3 == Funct3::Ld);
}

/** Return whether an LR/SC/AMO effective address meets the A-extension alignment rule. */
constexpr auto amo_address_aligned(simrv::xlen::Address address, Funct3 funct3) -> bool {
    const unsigned bytes = amo_operand_bytes(funct3);
    return bytes != 0 && (address & static_cast<simrv::xlen::Address>(bytes - 1U)) == 0;
}

/**
 * @enum AmoStatus
 * @brief Return status for Load-Reserved / Store-Conditional operations.
 */
enum class AmoStatus : uint8_t {
    Success = 0,  ///< The store-conditional operation succeeded
    Failure = 1,  ///< The store-conditional operation failed or reservation was lost
};

/**
 * @enum Funct5Amo
 * @brief Represents the 5-bit atomic operation selector (bits [31:27] of AMO instructions).
 */
enum class Funct5Amo : uint8_t {
    Lr = 0x02,    ///< Load-Reserved
    Sc = 0x03,    ///< Store-Conditional
    Swap = 0x01,  ///< Atomic Swap
    Add = 0x00,   ///< Atomic Add
    And = 0x0c,   ///< Atomic Bitwise AND
    Or = 0x08,    ///< Atomic Bitwise OR
    Xor = 0x04,   ///< Atomic Bitwise XOR
    Min = 0x10,   ///< Atomic Signed Minimum
    Minu = 0x18,  ///< Atomic Unsigned Minimum
    Max = 0x14,   ///< Atomic Signed Maximum
    Maxu = 0x1c,  ///< Atomic Unsigned Maximum
};

template <std::integral I>
constexpr auto operator==(I lhs, Funct5Amo rhs) noexcept -> bool {
    return lhs == static_cast<I>(std::to_underlying(rhs));
}
template <std::integral I>
constexpr auto operator==(Funct5Amo lhs, I rhs) noexcept -> bool {
    return static_cast<I>(std::to_underlying(lhs)) == rhs;
}
template <std::integral I>
constexpr auto operator!=(I lhs, Funct5Amo rhs) noexcept -> bool {
    return !(lhs == rhs);
}
template <std::integral I>
constexpr auto operator!=(Funct5Amo lhs, I rhs) noexcept -> bool {
    return !(lhs == rhs);
}

/// Left-shift operator for Funct5Amo (e.g. funct5 << 27U) to construct instruction words.
template <std::integral I>
constexpr auto operator<<(Funct5Amo lhs, I shift) noexcept -> uint32_t {
    return static_cast<uint32_t>(std::to_underlying(lhs)) << shift;
}

}  // namespace simrv::isa
