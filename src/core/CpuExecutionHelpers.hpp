/**
 * @file CpuExecutionHelpers.hpp
 * @brief Private helpers shared by CPU execution translation units.
 */
#pragma once

#include "simrv/CompilerHints.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/isa/Amo.hpp"
#include "simrv/isa/InstructionFields.hpp"

namespace simrv::core {
namespace {
using isa::Funct3;
using isa::Funct5Amo;
using isa::Opcode;

[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto is_aligned_for_funct3(simrv::xlen::Address addr,
                                                                       isa::Funct3 funct3) noexcept
    -> bool {
    const unsigned size_bytes = 1u << (static_cast<unsigned>(funct3) & 0x3u);
    return (addr & (size_bytes - 1u)) == 0;
}

[[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto access_size_for_funct3(isa::Funct3 funct3) noexcept
    -> unsigned {
    return 1u << (static_cast<unsigned>(funct3) & 0x3u);
}

inline auto is_tohost_addr(const Machine& machine, simrv::xlen::Address addr) -> bool {
    return (addr - machine.isa_test_tohost() < 8) || (addr - 0x80001000ULL < 8) ||
           (addr - 0x40008000ULL < 8);
}

inline auto is_tohost_addr(const Machine& machine, simrv::xlen::PhysAddr addr) -> bool {
    return is_tohost_addr(machine, addr.raw());
}
}  // namespace
}  // namespace simrv::core
