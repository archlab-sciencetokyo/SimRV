/**
 * @file MemoryUtil.hpp
 * @brief Memory utility functions for RAM access and device translation.
 *
 * Provides inline memory access helpers used by both MemorySubsystem and MMU
 * for efficient address translation and memory operations. Supports both RV32
 * and RV64 address widths with C++20 concepts for type safety.
 */
#pragma once

#include <cstddef>
#include <cstring>
#include <ranges>

#include "simrv/Define.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::memory {

#ifndef SIMRV_DRAM_SIZE_MB
#define SIMRV_DRAM_SIZE_MB 256
#endif

inline constexpr Address kDramBaseAddress = static_cast<Address>(0x80000000u);
inline constexpr Address kDramSize = static_cast<Address>(SIMRV_DRAM_SIZE_MB * 1024u * 1024u);
inline constexpr Address kDramMask = kDramSize - 1;

inline constexpr Word kTlbSize = 2048;
inline constexpr Word kPageShift = 12;
inline constexpr Word kPageMask = (1u << kPageShift) - 1;

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

// ========== Memory Region Classification ==========

extern bool g_appmode;
extern Address g_dram_base;

/// Overflow-safe containment test for a physical memory region.
[[nodiscard]] constexpr auto address_range_contains(Address base, Address extent, Address address,
                                                    size_t size) -> bool {
    return size != 0 && size <= extent && address >= base && address - base <= extent - size;
}

/// Check whether the complete physical byte range is backed by DRAM.
inline auto is_dram_access(Address p_addr, size_t size) -> bool {
    return address_range_contains(g_dram_base, simrv::memory::kDramSize, p_addr, size);
}

/// Check if one physical byte is within the implemented DRAM range.
inline auto is_dram_addr(Address p_addr) -> bool { return is_dram_access(p_addr, 1); }

/// Check if a physical address is in a legacy reserved region (MMIO)
inline auto is_legacy_reserved_region(Address p_addr) -> bool {
    switch (p_addr & static_cast<Address>(0xF0000000u)) {
        case static_cast<Address>(0x10000000u):
        case static_cast<Address>(0x20000000u):
        case static_cast<Address>(0x30000000u):
        case static_cast<Address>(0x70000000u):
            return true;
        default:
            return false;
    }
}

// ========== XLEN-Aware Store Width Calculation ==========

/// Compute store byte count from funct3 field for the configured XLEN
/// Supports: SB (1), SH (2), SW (4), SD (8 on RV64 only)
constexpr auto store_width_bytes(Instruction funct3) -> size_t {
    constexpr auto kStoreSizeMask = static_cast<Instruction>(0x3u);
    const auto base_width = static_cast<size_t>(1u << (funct3 & kStoreSizeMask));

    // In RV64, SD (funct3=3) is 8 bytes; in RV32, it's treated as word (4 bytes)
    if constexpr (simrv::xlen::kIsXLen64) {
        if ((funct3 & kStoreSizeMask) == 3u) {
            return 8u;  // SD instruction in RV64
        }
    }
    return base_width;
}

// ========== Fast Host Memory Read/Write Operations (Direct Translation Fast Path) ==========

inline auto host_read_fast(const Byte* host_ptr, Instruction funct3) -> Word {
    auto read = [host_ptr]<typename T>() -> T {
        T value{};
        std::memcpy(&value, host_ptr, sizeof(value));
        return value;
    };
    switch (static_cast<isa::Funct3>(funct3 & 0x7u)) {
        case isa::Funct3::Lb:
            return static_cast<Word>(static_cast<SignedWord>(read.template operator()<int8_t>()));
        case isa::Funct3::Lbu:
            return static_cast<Word>(read.template operator()<uint8_t>());
        case isa::Funct3::Lh:
            return static_cast<Word>(static_cast<SignedWord>(read.template operator()<int16_t>()));
        case isa::Funct3::Lhu:
            return static_cast<Word>(read.template operator()<uint16_t>());
        case isa::Funct3::Lw:
            return static_cast<Word>(static_cast<SignedWord>(read.template operator()<int32_t>()));
        case isa::Funct3::Lwu:
            return static_cast<Word>(read.template operator()<uint32_t>());
        case isa::Funct3::Ld:
            return static_cast<Word>(read.template operator()<uint64_t>());
        default:
            return 0;
    }
}

inline void host_write_fast(Byte* host_ptr, Register val, Instruction funct3) {
    auto write = [host_ptr]<typename T>(Register value) {
        const T narrowed = static_cast<T>(value);
        std::memcpy(host_ptr, &narrowed, sizeof(narrowed));
    };
    switch (static_cast<isa::Funct3>(funct3 & 0x7u)) {
        case isa::Funct3::Sb:
            write.template operator()<uint8_t>(val);
            break;
        case isa::Funct3::Sh:
            write.template operator()<uint16_t>(val);
            break;
        case isa::Funct3::Sw:
            write.template operator()<uint32_t>(val);
            break;
        case isa::Funct3::Sd:
            write.template operator()<uint64_t>(val);
            break;
        default:
            break;
    }
}

/// ========== Fast RAM Read Operations ==========

/// Fast inline RAM read with support for various load formats
/// Requires: addr to be valid DRAM address; ram pointer must be non-null
inline auto ram_read_fast(Address addr, Instruction funct3, Byte* ram) -> Word {
    return host_read_fast(ram + (addr & simrv::memory::kDramMask), funct3);
}

// ========== Fast RAM Write Operations ==========

/// Fast inline RAM write with support for various store formats
/// Supports: SB (1 byte), SH (2 bytes), SW (4 bytes), SD (8 bytes on RV64)
inline void ram_write_fast(Address addr, Word wdata, Instruction funct3, Byte* ram) {
    host_write_fast(ram + (addr & simrv::memory::kDramMask), wdata, funct3);
}

}  // namespace simrv::memory
