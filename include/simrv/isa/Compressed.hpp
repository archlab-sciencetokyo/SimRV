/**
 * @file Compressed.hpp
 * @brief RISC-V Compressed (C) extension definitions.
 */
#pragma once

#include <cstdint>

namespace simrv::isa {

enum class CompressedOpcode : uint8_t {
    C0 = 0x0,
    C1 = 0x1,
    C2 = 0x2,
    C3 = 0x3,
};

} // namespace simrv::isa
