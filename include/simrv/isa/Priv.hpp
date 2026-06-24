/**
 * @file Priv.hpp
 * @brief RISC-V Privileged architecture definitions.
 */
#pragma once

#include <cstdint>

namespace simrv::isa {

enum class Funct12Priv : uint16_t {
    Ecall = 0x000,
    Ebreak = 0x001,
    Uret = 0x002,
    Sret = 0x102,
    Mret = 0x302,
    Wfi = 0x105,
};

enum class Funct7Priv : uint8_t {
    SfenceVma = 0x09,
};

} // namespace simrv::isa
