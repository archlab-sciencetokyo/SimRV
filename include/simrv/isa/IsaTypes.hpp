/**
 * @file IsaTypes.hpp
 * @brief ISA extension and preset identifiers.
 */
#pragma once

#include <cstdint>

namespace simrv::isa {

/**
 * @enum IsaExtension
 * @brief Identifies standard MISA extension bits and the reserved non-standard X bit.
 */
enum class IsaExtension : uint8_t {
    E = 4,   ///< Reduced 32-bit integer register-set extension
    A = 0,   ///< Atomic Instructions extension
    B = 1,   ///< Bitmanip Instructions extension
    C = 2,   ///< Compressed Instructions extension
    D = 3,   ///< Double-Precision Floating-Point extension
    F = 5,   ///< Single-Precision Floating-Point extension
    H = 7,   ///< Hypervisor extension
    I = 8,   ///< Base Integer Instruction Set extension
    M = 12,  ///< Integer Multiply/Divide extension
    S = 18,  ///< Supervisor Privilege Level support
    U = 20,  ///< User Privilege Level support
    V = 21,  ///< Vector Extension
    X = 23,  ///< Non-standard extensions present
};

/**
 * @enum IsaPreset
 * @brief SimRV ISA presets matching common RISC-V extension sets.
 *
 * These are implementation presets, not ratified RISC-V architecture profiles.
 */
enum class IsaPreset : uint8_t {
    E,     ///< RV32E reduced-register integer base
    EM,    ///< RV32E plus Multiply/Divide
    EMAC,  ///< RV32E plus Multiply/Divide, Atomic, and Compressed
    I,     ///< RV32I / RV64I base integer only
    IM,    ///< Integer, Multiply/Divide
    IMA,   ///< Integer, Multiply/Divide, Atomic, Supervisor, User
    IMAC,  ///< Integer, Multiply/Divide, Atomic, and Compressed
    G,     ///< General-purpose IMAFD plus the implied Zicsr and Zifencei extensions
    GC,    ///< G plus the separate C (Compressed) extension
    GCV,   ///< G plus Compressed and Vector 1.0 (V)
};

/**
 * @enum ArchitectureProfile
 * @brief Formal RISC-V Architectural Profiles per RVI Profiles Specification.
 */
enum class ArchitectureProfile : uint8_t {
    Legacy,    ///< Unconstrained standard RV32GC / RV64GC
    Rva20U64,  ///< RVA20 User-mode 64-bit profile
    Rva20S64,  ///< RVA20 Supervisor-mode 64-bit profile
    Rva22U64,  ///< RVA22 User-mode 64-bit profile
    Rva22S64,  ///< RVA22 Supervisor-mode 64-bit profile
    Rva23U64,  ///< RVA23 User-mode 64-bit profile
    Rva23S64,  ///< RVA23 Supervisor-mode 64-bit profile (Default for RV64)
};

}  // namespace simrv::isa
