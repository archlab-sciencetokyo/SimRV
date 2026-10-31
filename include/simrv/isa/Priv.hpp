/**
 * @file Priv.hpp
 * @brief RISC-V Privileged architecture definitions.
 */
#pragma once

#include <concepts>
#include <cstdint>
#include <utility>

namespace simrv::isa {

/**
 * @enum Funct12Priv
 * @brief Represents the 12-bit privileged system instruction selectors (bits [31:20] of SYSTEM
 * instructions).
 */
enum class Funct12Priv : uint16_t {
    Ecall = 0x000,          ///< Environment Call (raises trap based on current privilege level)
    Ebreak = 0x001,         ///< Breakpoint trap (yields to debugger)
    Uret = 0x002,           ///< User-mode Trap Return
    Sret = 0x102,           ///< Supervisor-mode Trap Return
    Mret = 0x302,           ///< Machine-mode Trap Return
    Wfi = 0x105,            ///< Wait for Interrupt (stalls CPU until a pending interrupt is served)
    SfenceWInval = 0x180,   ///< Svinval SFENCE.W.INVAL
    SfenceInvalIr = 0x181,  ///< Svinval SFENCE.INVAL.IR
};

/**
 * @enum Funct7Priv
 * @brief Represents the 7-bit privileged virtual memory instruction selectors (bits [31:25] of
 * SYSTEM instructions).
 */
enum class Funct7Priv : uint8_t {
    SfenceVma = 0x09,   ///< Supervisor Memory-Management Fence (SFENCE.VMA, invalidates page table
                        ///< TLB entries)
    SinvalVma = 0x0B,   ///< Svinval SINVAL.VMA
    HfenceVvma = 0x11,  ///< Hypervisor VS-stage address-translation fence
    HinvalVvma = 0x13,  ///< Svinval hypervisor VS-stage address-translation invalidate
    HfenceGvma = 0x31,  ///< Hypervisor G-stage address-translation fence
    HinvalGvma = 0x33,  ///< Svinval hypervisor G-stage address-translation invalidate
};

template <std::integral I>
constexpr auto operator==(I lhs, Funct12Priv rhs) noexcept -> bool {
    return lhs == static_cast<I>(std::to_underlying(rhs));
}
template <std::integral I>
constexpr auto operator==(Funct12Priv lhs, I rhs) noexcept -> bool {
    return static_cast<I>(std::to_underlying(lhs)) == rhs;
}
template <std::integral I>
constexpr auto operator!=(I lhs, Funct12Priv rhs) noexcept -> bool {
    return !(lhs == rhs);
}
template <std::integral I>
constexpr auto operator!=(Funct12Priv lhs, I rhs) noexcept -> bool {
    return !(lhs == rhs);
}

template <std::integral I>
constexpr auto operator==(I lhs, Funct7Priv rhs) noexcept -> bool {
    return lhs == static_cast<I>(std::to_underlying(rhs));
}
template <std::integral I>
constexpr auto operator==(Funct7Priv lhs, I rhs) noexcept -> bool {
    return static_cast<I>(std::to_underlying(lhs)) == rhs;
}
template <std::integral I>
constexpr auto operator!=(I lhs, Funct7Priv rhs) noexcept -> bool {
    return !(lhs == rhs);
}
template <std::integral I>
constexpr auto operator!=(Funct7Priv lhs, I rhs) noexcept -> bool {
    return !(lhs == rhs);
}

}  // namespace simrv::isa
