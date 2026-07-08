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

// ========== C++20 Concepts for Type-Safe Memory Access ==========

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

/// Check if a physical address is within DRAM range
inline auto is_dram_addr(Address p_addr) -> bool {
    return (p_addr - g_dram_base) < simrv::memory::kDramSize;
}

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
    switch (static_cast<isa::Funct3>(funct3 & 0x7u)) {
        case isa::Funct3::Lb:
            return static_cast<Word>(static_cast<SignedWord>(*reinterpret_cast<const int8_t*>(host_ptr)));
        case isa::Funct3::Lbu:
            return static_cast<Word>(*reinterpret_cast<const uint8_t*>(host_ptr));
        case isa::Funct3::Lh: {
            int16_t val = 0;
            std::memcpy(&val, host_ptr, sizeof(val));
            return static_cast<Word>(static_cast<SignedWord>(val));
        }
        case isa::Funct3::Lhu: {
            uint16_t val = 0;
            std::memcpy(&val, host_ptr, sizeof(val));
            return static_cast<Word>(val);
        }
        case isa::Funct3::Lw: {
            int32_t val = 0;
            std::memcpy(&val, host_ptr, sizeof(val));
            return static_cast<Word>(val);
        }
        case static_cast<isa::Funct3>(6): {
            uint32_t val = 0;
            std::memcpy(&val, host_ptr, sizeof(val));
            return static_cast<Word>(val);
        }
        case isa::Funct3::Ld: {
            uint64_t val = 0;
            std::memcpy(&val, host_ptr, sizeof(val));
            return static_cast<Word>(val);
        }
        default:
            return 0;
    }
}

inline void host_write_fast(Byte* host_ptr, Register val, Instruction funct3) {
    switch (static_cast<isa::Funct3>(funct3 & 0x7u)) {
        case isa::Funct3::Sb:
            *reinterpret_cast<uint8_t*>(host_ptr) = static_cast<uint8_t>(val);
            break;
        case isa::Funct3::Sh: {
            uint16_t const tmp = static_cast<uint16_t>(val);
            std::memcpy(host_ptr, &tmp, sizeof(tmp));
            break;
        }
        case isa::Funct3::Sw: {
            uint32_t const tmp = static_cast<uint32_t>(val);
            std::memcpy(host_ptr, &tmp, sizeof(tmp));
            break;
        }
        case isa::Funct3::Sd: {
            uint64_t const tmp = static_cast<uint64_t>(val);
            std::memcpy(host_ptr, &tmp, sizeof(tmp));
            break;
        }
        default:
            break;
    }
}

// ========== Fast RAM Read Operations ==========

/// Fast inline RAM read with support for various load formats
/// Requires: addr to be valid DRAM address; ram pointer must be non-null
inline auto ram_read_fast(Address addr, Instruction funct3, Byte* ram) -> Word {
    constexpr auto kFunct3Mask = static_cast<Instruction>(0x7u);
    const Address masked = addr & simrv::memory::kDramMask;

    switch (funct3 & kFunct3Mask) {
        case static_cast<Instruction>(isa::Funct3::Lb): {
            const auto b = static_cast<int8_t>(std::to_integer<uint8_t>(ram[masked]));
            return static_cast<Word>(static_cast<SignedWord>(b));
        }
        case static_cast<Instruction>(isa::Funct3::Lbu):
            return static_cast<Word>(std::to_integer<uint8_t>(ram[masked]));
        case static_cast<Instruction>(isa::Funct3::Lh): {
            int16_t val;
            if (simrv::compiler::likely(masked <= (simrv::memory::kDramMask - 1))) {
                std::memcpy(&val, ram + masked, sizeof(val));
            } else {
                const Address m1 = (addr + 1) & simrv::memory::kDramMask;
                val = static_cast<int16_t>(
                    static_cast<uint16_t>(std::to_integer<uint8_t>(ram[masked])) |
                    (static_cast<uint16_t>(std::to_integer<uint8_t>(ram[m1])) << 8));
            }
            return static_cast<Word>(static_cast<SignedWord>(val));
        }
        case static_cast<Instruction>(isa::Funct3::Lhu): {
            uint16_t val;
            if (simrv::compiler::likely(masked <= (simrv::memory::kDramMask - 1))) {
                std::memcpy(&val, ram + masked, sizeof(val));
            } else {
                const Address m1 = (addr + 1) & simrv::memory::kDramMask;
                val = static_cast<uint16_t>(std::to_integer<uint8_t>(ram[masked])) |
                      (static_cast<uint16_t>(std::to_integer<uint8_t>(ram[m1])) << 8);
            }
            return static_cast<Word>(val);
        }
        case static_cast<Instruction>(isa::Funct3::Lw): {
            int32_t val;
            if (simrv::compiler::likely(masked <= (simrv::memory::kDramMask - 3))) {
                std::memcpy(&val, ram + masked, sizeof(val));
            } else {
                const Address m1 = (addr + 1) & simrv::memory::kDramMask;
                const Address m2 = (addr + 2) & simrv::memory::kDramMask;
                const Address m3 = (addr + 3) & simrv::memory::kDramMask;
                val = static_cast<int32_t>(
                    static_cast<uint32_t>(std::to_integer<uint8_t>(ram[masked])) |
                    (static_cast<uint32_t>(std::to_integer<uint8_t>(ram[m1])) << 8) |
                    (static_cast<uint32_t>(std::to_integer<uint8_t>(ram[m2])) << 16) |
                    (static_cast<uint32_t>(std::to_integer<uint8_t>(ram[m3])) << 24));
            }
            return static_cast<Word>(static_cast<SignedWord>(val));
        }
        // RV64 LD (load double-word) instruction
        case static_cast<Instruction>(isa::Funct3::Ld): {
            if constexpr (simrv::xlen::kIsXLen64) {
                uint64_t val;
                if (simrv::compiler::likely(masked <= (simrv::memory::kDramMask - 7))) {
                    std::memcpy(&val, ram + masked, sizeof(val));
                } else {
                    val = 0;
                    for (int offset : std::views::iota(0, 8)) {
                        val |= static_cast<uint64_t>(std::to_integer<uint8_t>(
                                   ram[(addr + offset) & simrv::memory::kDramMask]))
                               << (8 * offset);
                    }
                }
                return static_cast<Word>(val);
            } else {
                // In RV32, LD is not defined; treat as reserved
                return 0;
            }
        }
        default: {
            Word rdata = 0;
            constexpr auto kSizeBits = 0x3;
            const int byte_count = (1 << (funct3 & kSizeBits));
            for (int offset : std::views::iota(0, byte_count)) {
                rdata |= static_cast<Word>(std::to_integer<uint8_t>(
                             ram[(addr + offset) & simrv::memory::kDramMask]))
                         << (8 * offset);
            }
            constexpr auto kSignExtendBit = 0x4;
            if ((funct3 & kSignExtendBit) == 0) {
                const unsigned bits = 8 * byte_count;
                const Word sign_bit = static_cast<Word>(1) << (bits - 1);
                if ((rdata & sign_bit) != 0) {
                    const Word extend_mask =
                        kXLenMask & (~((static_cast<Word>(1) << bits) - static_cast<Word>(1)));
                    rdata |= extend_mask;
                }
            }
            return rdata;
        }
    }
}

// ========== Fast RAM Write Operations ==========

/// Fast inline RAM write with support for various store formats
/// Supports: SB (1 byte), SH (2 bytes), SW (4 bytes), SD (8 bytes on RV64)
inline void ram_write_fast(Address addr, Word wdata, Instruction funct3, Byte* ram) {
    constexpr auto kFunct3WriteMask = static_cast<Instruction>(0x3u);
    const Address masked = addr & simrv::memory::kDramMask;

    switch (funct3 & kFunct3WriteMask) {
        case 0: {  // SB: Store Byte
            ram[masked] = static_cast<Byte>(static_cast<uint8_t>(wdata & 0xFF));
            break;
        }
        case 1: {  // SH: Store Halfword (2 bytes)
            if (simrv::compiler::likely(masked <= (simrv::memory::kDramMask - 1))) {
                uint16_t val = static_cast<uint16_t>(wdata);
                std::memcpy(ram + masked, &val, sizeof(val));
            } else {
                const Address m1 = (addr + 1) & simrv::memory::kDramMask;
                ram[masked] = static_cast<Byte>(static_cast<uint8_t>(wdata & 0xFF));
                ram[m1] = static_cast<Byte>(static_cast<uint8_t>((wdata >> 8) & 0xFF));
            }
            break;
        }
        case 2: {  // SW: Store Word (4 bytes)
            if (simrv::compiler::likely(masked <= (simrv::memory::kDramMask - 3))) {
                uint32_t val = static_cast<uint32_t>(wdata);
                std::memcpy(ram + masked, &val, sizeof(val));
            } else {
                const Address m1 = (addr + 1) & simrv::memory::kDramMask;
                const Address m2 = (addr + 2) & simrv::memory::kDramMask;
                const Address m3 = (addr + 3) & simrv::memory::kDramMask;
                ram[masked] = static_cast<Byte>(static_cast<uint8_t>(wdata & 0xFF));
                ram[m1] = static_cast<Byte>(static_cast<uint8_t>((wdata >> 8) & 0xFF));
                ram[m2] = static_cast<Byte>(static_cast<uint8_t>((wdata >> 16) & 0xFF));
                ram[m3] = static_cast<Byte>(static_cast<uint8_t>((wdata >> 24) & 0xFF));
            }
            break;
        }
        case 3: {  // SD: Store Doubleword (8 bytes, RV64 only)
            if constexpr (simrv::xlen::kIsXLen64) {
                if (simrv::compiler::likely(masked <= (simrv::memory::kDramMask - 7))) {
                    uint64_t val = static_cast<uint64_t>(wdata);
                    std::memcpy(ram + masked, &val, sizeof(val));
                } else {
                    for (int offset : std::views::iota(0, 8)) {
                        ram[(addr + offset) & simrv::memory::kDramMask] =
                            static_cast<Byte>(static_cast<uint8_t>((wdata >> (8 * offset)) & 0xFF));
                    }
                }
            } else {
                // In RV32, funct3=3 is reserved; treat as SW (4 bytes)
                if (simrv::compiler::likely(masked <= (simrv::memory::kDramMask - 3))) {
                    uint32_t val = static_cast<uint32_t>(wdata);
                    std::memcpy(ram + masked, &val, sizeof(val));
                } else {
                    const Address m1 = (addr + 1) & simrv::memory::kDramMask;
                    const Address m2 = (addr + 2) & simrv::memory::kDramMask;
                    const Address m3 = (addr + 3) & simrv::memory::kDramMask;
                    ram[masked] = static_cast<Byte>(static_cast<uint8_t>(wdata & 0xFF));
                    ram[m1] = static_cast<Byte>(static_cast<uint8_t>((wdata >> 8) & 0xFF));
                    ram[m2] = static_cast<Byte>(static_cast<uint8_t>((wdata >> 16) & 0xFF));
                    ram[m3] = static_cast<Byte>(static_cast<uint8_t>((wdata >> 24) & 0xFF));
                }
            }
            break;
        }
        default: {
            const int byte_count = (1 << funct3);
            for (int offset : std::views::iota(0, byte_count)) {
                ram[(addr + offset) & simrv::memory::kDramMask] =
                    static_cast<Byte>(static_cast<uint8_t>((wdata >> (8 * offset)) & 0xFF));
            }
            break;
        }
    }
}

}  // namespace simrv::memory
