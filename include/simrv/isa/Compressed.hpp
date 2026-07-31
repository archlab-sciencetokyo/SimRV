/**
 * @file Compressed.hpp
 * @brief RISC-V Compressed (C) extension definitions.
 */
#pragma once

#include <cstdint>

namespace simrv::isa {

/**
 * @enum CompressedOpcode
 * @brief Represents the 2-bit compressed opcode quadrant (bits [1:0] of 16-bit compressed
 * instructions).
 */
enum class CompressedOpcode : uint8_t {
    C0 = 0x0,  ///< Quadrant 00 (e.g. loads/stores with stack-pointer offsets, etc.)
    C1 = 0x1,  ///< Quadrant 01 (e.g. integer arithmetic/immediate instructions, jumps, etc.)
    C2 = 0x2,  ///< Quadrant 10 (e.g. stack-pointer arithmetic, register jumps, loads/stores)
    C3 = 0x3,  ///< Quadrant 11 (Reserved / Map to 32-bit instructions)
};

}  // namespace simrv::isa
