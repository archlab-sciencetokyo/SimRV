/**
 * @file Amo.hpp
 * @brief RISC-V Atomic (A) extension definitions.
 */
#pragma once

#include <cstdint>

namespace simrv::isa {

/**
 * @enum AmoStatus
 * @brief Return status for Load-Reserved / Store-Conditional operations.
 */
enum class AmoStatus : uint8_t {
    Success = 0, ///< The store-conditional operation succeeded
    Failure = 1, ///< The store-conditional operation failed or reservation was lost
};

/**
 * @enum Funct5Amo
 * @brief Represents the 5-bit atomic operation selector (bits [31:27] of AMO instructions).
 */
enum class Funct5Amo : uint8_t {
    Lr = 0x02,   ///< Load-Reserved
    Sc = 0x03,   ///< Store-Conditional
    Swap = 0x01, ///< Atomic Swap
    Add = 0x00,  ///< Atomic Add
    And = 0x0c,  ///< Atomic Bitwise AND
    Or = 0x08,   ///< Atomic Bitwise OR
    Xor = 0x04,  ///< Atomic Bitwise XOR
    Min = 0x10,  ///< Atomic Signed Minimum
    Minu = 0x18, ///< Atomic Unsigned Minimum
    Max = 0x14,  ///< Atomic Signed Maximum
    Maxu = 0x1c, ///< Atomic Unsigned Maximum
};

} // namespace simrv::isa
