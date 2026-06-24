/**
 * @file Amo.hpp
 * @brief RISC-V Atomic (A) extension definitions.
 */
#pragma once

#include <cstdint>

namespace simrv::isa {

enum class AmoStatus : uint8_t {
    Success = 0,
    Failure = 1,
};

enum class Funct5Amo : uint8_t {
    Lr = 0x02,
    Sc = 0x03,
    Swap = 0x01,
    Add = 0x00,
    And = 0x0c,
    Or = 0x08,
    Xor = 0x04,
    Min = 0x10,
    Minu = 0x18,
    Max = 0x14,
    Maxu = 0x1c,
};

} // namespace simrv::isa
