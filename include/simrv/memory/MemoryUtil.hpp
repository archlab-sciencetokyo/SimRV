/**
 * @file MemoryUtil.hpp
 * @brief Memory utility functions for RAM access and device translation.
 *
 * Provides inline memory access helpers used by both MemorySubsystem and MMU
 * for efficient address translation and memory operations. Supports both RV32
 * and RV64 address widths with C++20 concepts for type safety.
 */
#pragma once

#include <bit>
#include <cstddef>
#include <cstring>
#include <ranges>

#include "simrv/CompilerHints.hpp"
#include "simrv/isa/Base.hpp"
#include "simrv/memory/RamView.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::memory {

inline constexpr simrv::xlen::Word kTlbSize = 2048;
inline constexpr simrv::xlen::Word kPageShift = 12;
inline constexpr simrv::xlen::Word kPageMask = (1u << kPageShift) - 1;

/**
 * @brief Check if an address or integer value is aligned to an N-byte boundary.
 * @tparam T Integral or pointer type.
 * @param val Address or integer value to check.
 * @param alignment Alignment boundary in bytes (must be a power of 2).
 * @return True if aligned, false otherwise.
 */
template <typename T>
    requires std::is_integral_v<T> || std::is_pointer_v<T>
[[nodiscard]] constexpr auto is_aligned(T val, size_t alignment) -> bool {
    if constexpr (std::is_pointer_v<T>) {
        return (std::bit_cast<uintptr_t>(val) & (alignment - 1)) == 0;
    } else {
        return (static_cast<uintptr_t>(val) & (alignment - 1)) == 0;
    }
}

/// Concept: Type represents a valid memory address
template <typename T>
concept AddressLike = std::unsigned_integral<T> && requires(T addr) {
    { addr & T{} } -> std::convertible_to<T>;  // Supports bitwise AND
};

/// Concept: Type represents a valid instruction/word for memory operations
template <typename T>
concept WordLike = std::integral<T> && requires(T w) {
    { w >> 8 } -> std::convertible_to<T>;  // Supports bit shifts
};

/// Concept: Type represents a valid RISC-V load/store funct3 field
template <typename T>
concept StoreFunct3Like = std::unsigned_integral<T>;

/// Check a physical range against explicitly supplied DRAM geometry.
[[nodiscard]] constexpr auto is_dram_access(simrv::xlen::Address base, simrv::xlen::Address extent,
                                            simrv::xlen::Address p_addr, size_t size) -> bool {
    return address_range_contains(base, extent, p_addr, size);
}

// ========== XLEN-Aware Store Width Calculation ==========

/// Compute store byte count from funct3 field for the configured XLEN
/// Supports: SB (1), SH (2), SW (4), SD (8 on RV64 only)
constexpr auto store_width_bytes(simrv::xlen::Instruction funct3) -> size_t {
    constexpr auto kStoreSizeMask = static_cast<simrv::xlen::Instruction>(0x3u);
    const auto base_width = static_cast<size_t>(1u << (funct3 & kStoreSizeMask));

    // In RV64, SD (funct3=3) is 8 bytes; in RV32, it's treated as word (4 bytes)
    if constexpr (simrv::xlen::kIsXLen64) {
        if ((funct3 & kStoreSizeMask) == 3u) {
            return 8u;  // SD instruction in RV64
        }
    }
    return base_width;
}

/// Compute store byte count from typed funct3 field
constexpr auto store_width_bytes(isa::Funct3 funct3) -> size_t {
    return store_width_bytes(static_cast<simrv::xlen::Instruction>(funct3));
}

// ========== Loaded Value Sign/Zero Extension Helper ==========

/// Extend raw memory read data according to RISC-V load funct3 rules (sign vs zero extension).
[[nodiscard]] constexpr auto extend_loaded_value(simrv::xlen::Word raw_data,
                                                 uint8_t funct3) noexcept -> simrv::xlen::Word {
    const unsigned req_size_bytes = 1u << (funct3 & 0x3u);
    const unsigned bits = 8 * req_size_bytes;
    if (bits < simrv::xlen::kXLenBits) {
        const simrv::xlen::Word mask = (static_cast<simrv::xlen::Word>(1) << bits) - 1;
        raw_data &= mask;
        constexpr auto kSignExtendBit = 0x4u;
        if ((funct3 & kSignExtendBit) == 0) {
            const simrv::xlen::Word sign_bit = static_cast<simrv::xlen::Word>(1) << (bits - 1);
            if ((raw_data & sign_bit) != 0) {
                raw_data |= ~mask;
            }
        }
    }
    return raw_data;
}

/// Extend raw memory read data according to typed RISC-V load funct3 rules.
[[nodiscard]] constexpr auto extend_loaded_value(simrv::xlen::Word raw_data,
                                                 isa::Funct3 funct3) noexcept -> simrv::xlen::Word {
    return extend_loaded_value(raw_data, static_cast<uint8_t>(funct3));
}

// ========== Fast Host Memory Read/Write Operations (Direct Translation Fast Path) ==========

template <std::unsigned_integral T>
[[nodiscard]] SIMRV_ALWAYS_INLINE auto load_unaligned(const simrv::xlen::Byte* host_ptr) noexcept
    -> T {
    T value{};
    std::memcpy(&value, host_ptr, sizeof(T));
    return value;
}

template <std::unsigned_integral T>
SIMRV_ALWAYS_INLINE void store_unaligned(simrv::xlen::Byte* host_ptr, T value) noexcept {
    std::memcpy(host_ptr, &value, sizeof(T));
}

SIMRV_ALWAYS_INLINE auto host_read_fast(const simrv::xlen::Byte* host_ptr,
                                        simrv::xlen::Instruction funct3) noexcept
    -> simrv::xlen::Word {
    switch (funct3 & 0x7u) {
        case 0:
            return static_cast<simrv::xlen::Word>(static_cast<simrv::xlen::SignedWord>(
                static_cast<int8_t>(load_unaligned<uint8_t>(host_ptr))));
        case 1:
            return static_cast<simrv::xlen::Word>(static_cast<simrv::xlen::SignedWord>(
                static_cast<int16_t>(load_unaligned<uint16_t>(host_ptr))));
        case 2:
            return static_cast<simrv::xlen::Word>(static_cast<simrv::xlen::SignedWord>(
                static_cast<int32_t>(load_unaligned<uint32_t>(host_ptr))));
        case 3:
            if constexpr (simrv::xlen::kIsXLen64) {
                return load_unaligned<uint64_t>(host_ptr);
            } else {
                return load_unaligned<uint32_t>(host_ptr);
            }
        case 4:
            return load_unaligned<uint8_t>(host_ptr);
        case 5:
            return load_unaligned<uint16_t>(host_ptr);
        case 6:
            return static_cast<simrv::xlen::Word>(load_unaligned<uint32_t>(host_ptr));
        default:
            if constexpr (simrv::xlen::kIsXLen64) {
                return load_unaligned<uint64_t>(host_ptr);
            } else {
                return load_unaligned<uint32_t>(host_ptr);
            }
    }
}

SIMRV_ALWAYS_INLINE void host_write_fast(simrv::xlen::Byte* host_ptr, simrv::xlen::Register val,
                                         simrv::xlen::Instruction funct3) noexcept {
    switch (funct3 & 0x3u) {
        case 0:
            store_unaligned(host_ptr, static_cast<uint8_t>(val));
            return;
        case 1:
            store_unaligned(host_ptr, static_cast<uint16_t>(val));
            return;
        case 2:
            store_unaligned(host_ptr, static_cast<uint32_t>(val));
            return;
        default:
            if constexpr (simrv::xlen::kIsXLen64) {
                store_unaligned(host_ptr, static_cast<uint64_t>(val));
            } else {
                store_unaligned(host_ptr, static_cast<uint32_t>(val));
            }
            return;
    }
}

/// Fast inline RAM read. The caller validates the address against its RamView.
inline auto ram_read_fast(simrv::xlen::Address addr, simrv::xlen::Instruction funct3, RamView ram)
    -> simrv::xlen::Word {
    return host_read_fast(ram.unchecked_ptr(addr), funct3);
}

inline auto ram_read_fast(simrv::xlen::PhysAddr addr, simrv::xlen::Instruction funct3, RamView ram)
    -> simrv::xlen::Word {
    return host_read_fast(ram.unchecked_ptr(addr), funct3);
}

inline auto ram_read_fast(simrv::xlen::Address addr, isa::Funct3 funct3, RamView ram)
    -> simrv::xlen::Word {
    return ram_read_fast(addr, static_cast<simrv::xlen::Instruction>(funct3), ram);
}

inline auto ram_read_fast(simrv::xlen::PhysAddr addr, isa::Funct3 funct3, RamView ram)
    -> simrv::xlen::Word {
    return ram_read_fast(addr, static_cast<simrv::xlen::Instruction>(funct3), ram);
}

// ========== Fast RAM Write Operations ==========

/// Fast inline RAM write. The caller validates the address against its RamView.
/// Supports: SB (1 byte), SH (2 bytes), SW (4 bytes), SD (8 bytes on RV64)
inline void ram_write_fast(simrv::xlen::Address addr, simrv::xlen::Word wdata,
                           simrv::xlen::Instruction funct3, RamView ram) {
    host_write_fast(ram.unchecked_ptr(addr), wdata, funct3);
}

inline void ram_write_fast(simrv::xlen::PhysAddr addr, simrv::xlen::Word wdata,
                           simrv::xlen::Instruction funct3, RamView ram) {
    host_write_fast(ram.unchecked_ptr(addr), wdata, funct3);
}

inline void ram_write_fast(simrv::xlen::Address addr, simrv::xlen::Word wdata, isa::Funct3 funct3,
                           RamView ram) {
    ram_write_fast(addr, wdata, static_cast<simrv::xlen::Instruction>(funct3), ram);
}

inline void ram_write_fast(simrv::xlen::PhysAddr addr, simrv::xlen::Word wdata, isa::Funct3 funct3,
                           RamView ram) {
    ram_write_fast(addr, wdata, static_cast<simrv::xlen::Instruction>(funct3), ram);
}

SIMRV_ALWAYS_INLINE auto host_read_fast(const simrv::xlen::Byte* host_ptr,
                                        isa::Funct3 funct3) noexcept -> simrv::xlen::Word {
    return host_read_fast(host_ptr, static_cast<simrv::xlen::Instruction>(funct3));
}

SIMRV_ALWAYS_INLINE void host_write_fast(simrv::xlen::Byte* host_ptr, simrv::xlen::Word val,
                                         isa::Funct3 funct3) noexcept {
    host_write_fast(host_ptr, val, static_cast<simrv::xlen::Instruction>(funct3));
}

}  // namespace simrv::memory
