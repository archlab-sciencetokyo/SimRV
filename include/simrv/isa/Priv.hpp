/**
 * @file Priv.hpp
 * @brief RISC-V Privileged architecture definitions.
 */
#pragma once

#include <cstdint>

namespace simrv::isa {

/**
 * @enum Funct12Priv
 * @brief Represents the 12-bit privileged system instruction selectors (bits [31:20] of SYSTEM
 * instructions).
 */
enum class Funct12Priv : uint16_t {
    Ecall = 0x000,   ///< Environment Call (raises trap based on current privilege level)
    Ebreak = 0x001,  ///< Breakpoint trap (yields to debugger)
    Uret = 0x002,    ///< User-mode Trap Return
    Sret = 0x102,    ///< Supervisor-mode Trap Return
    Mret = 0x302,    ///< Machine-mode Trap Return
    Wfi = 0x105,     ///< Wait for Interrupt (stalls CPU until a pending interrupt is served)
};

/**
 * @enum Funct7Priv
 * @brief Represents the 7-bit privileged virtual memory instruction selectors (bits [31:25] of
 * SYSTEM instructions).
 */
enum class Funct7Priv : uint8_t {
    SfenceVma = 0x09,  ///< Supervisor Memory-Management Fence (SFENCE.VMA, invalidates page table
                       ///< TLB entries)
};

}  // namespace simrv::isa
