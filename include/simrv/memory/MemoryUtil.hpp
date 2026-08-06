/**
 * @file MemoryUtil.hpp
 * @brief Memory subsystem helper functions and address region utilities.
 */
#pragma once

#include <cstdint>

#include "simrv/Define.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::memory {

// ========== Physical Memory Size Constants ==========

/// Primary DRAM memory size: 128 MB (0x08000000)
constexpr uint64_t kDramSize = 0x08000000ULL;

/// DRAM address mask for 128MB region
constexpr Address kDramMask = kDramSize - 1ULL;

/// Default DRAM base physical address (matching QEMU/Spike virt machine)
constexpr Address kDramBaseAddress = 0x80000000ULL;

inline constexpr Word kTlbSize = 2048;
inline constexpr Word kPageShift = 12;
inline constexpr Word kPageMask = (1u << kPageShift) - 1;

// ========== Alignment Helpers ==========

/// Check if address is 2-byte aligned (RVC instruction boundary)
constexpr auto is_aligned2(Address addr) -> bool { return (addr & 0x1u) == 0; }

/// Check if address is 4-byte aligned (32-bit instruction boundary)
constexpr auto is_aligned4(Address addr) -> bool { return (addr & 0x3u) == 0; }

/// Check if address is 8-byte aligned (64-bit word boundary)
constexpr auto is_aligned8(Address addr) -> bool { return (addr & 0x7u) == 0; }

/// Align address down to 4KB page boundary
constexpr auto page_align_down(Address addr) -> Address {
    return addr & ~static_cast<Address>(0xFFFu);
}

/// Align address up to 4KB page boundary
constexpr auto page_align_up(Address addr) -> Address {
    return (addr + 0xFFFu) & ~static_cast<Address>(0xFFFu);
}

/// Calculate page offset (bottom 12 bits)
constexpr auto page_offset(Address addr) -> uint32_t {
    return static_cast<uint32_t>(addr & 0xFFFu);
}

// ========== Memory Region Classification ==========

extern bool g_appmode;
extern Address g_dram_base;

/// Check if a physical address is within DRAM range (fast single-subtraction path)
inline auto is_dram_addr(Address p_addr) -> bool {
    return (p_addr - kDramBaseAddress) < simrv::memory::kDramSize;
}

/// Check if a physical address is in a legacy reserved region (MMIO)
inline auto is_legacy_reserved_region(Address p_addr) -> bool {
    switch (p_addr & static_cast<Address>(0xF0000000u)) {
        case static_cast<Address>(0x10000000u):
        case static_cast<Address>(0x20000000u):
        case static_cast<Address>(0x30000000u):
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
            return static_cast<Word>(
                static_cast<SignedWord>(*reinterpret_cast<const int8_t*>(host_ptr)));
        case isa::Funct3::Lbu:
            return static_cast<Word>(*reinterpret_cast<const uint8_t*>(host_ptr));
        case isa::Funct3::Lh:
            return static_cast<Word>(
                static_cast<SignedWord>(*reinterpret_cast<const int16_t*>(host_ptr)));
        case isa::Funct3::Lhu:
            return static_cast<Word>(*reinterpret_cast<const uint16_t*>(host_ptr));
        case isa::Funct3::Lw:
            return static_cast<Word>(
                static_cast<SignedWord>(*reinterpret_cast<const int32_t*>(host_ptr)));
        case isa::Funct3::Lwu:
            return static_cast<Word>(*reinterpret_cast<const uint32_t*>(host_ptr));
        case isa::Funct3::Ld:
            return static_cast<Word>(*reinterpret_cast<const uint64_t*>(host_ptr));
        default:
            return 0;
    }
}

inline void host_write_fast(Byte* host_ptr, Register val, Instruction funct3) {
    switch (static_cast<isa::Funct3>(funct3 & 0x7u)) {
        case isa::Funct3::Sb:
            *reinterpret_cast<uint8_t*>(host_ptr) = static_cast<uint8_t>(val);
            break;
        case isa::Funct3::Sh:
            *reinterpret_cast<uint16_t*>(host_ptr) = static_cast<uint16_t>(val);
            break;
        case isa::Funct3::Sw:
            *reinterpret_cast<uint32_t*>(host_ptr) = static_cast<uint32_t>(val);
            break;
        case isa::Funct3::Sd:
            *reinterpret_cast<uint64_t*>(host_ptr) = static_cast<uint64_t>(val);
            break;
        default:
            break;
    }
}

/// Fast inline RAM read with support for various load formats
inline auto ram_read_fast(Address addr, Instruction funct3, Byte* ram) -> Word {
    return host_read_fast(ram + (addr & simrv::memory::kDramMask), funct3);
}

/// Fast inline RAM write with support for various store formats
inline void ram_write_fast(Address addr, Word wdata, Instruction funct3, Byte* ram) {
    host_write_fast(ram + (addr & simrv::memory::kDramMask), wdata, funct3);
}

}  // namespace simrv::memory
