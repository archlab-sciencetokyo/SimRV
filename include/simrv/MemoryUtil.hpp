/**
 * @file MemoryUtil.hpp
 * @brief Memory utility functions for RAM access and device translation.
 *
 * Provides inline memory access helpers used by both MemorySubsystem and MMU
 * for efficient address translation and memory operations.
 */
#pragma once

#include "Define.hpp"

namespace simrv::memory_detail {

/// Check if a physical address is within DRAM range
inline bool is_dram_addr(Address p_addr) { return p_addr >= simrv::memory::kDramBaseAddress; }

/// Check if a physical address is in a legacy reserved region (MMIO)
inline bool is_legacy_reserved_region(Address p_addr) {
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

/// Fast inline RAM read with support for various load formats
inline Word ram_read_fast(Address addr, Instruction funct3, Byte* ram) {
    constexpr auto kFunct3Mask = static_cast<Instruction>(0x7u);
    const Address masked = addr & simrv::memory::kDramMask;

    switch (funct3 & kFunct3Mask) {
        case static_cast<Instruction>(Funct3::Lb): {
            const auto b = static_cast<int8_t>(std::to_integer<uint8_t>(ram[masked]));
            return static_cast<Word>(static_cast<SignedWord>(b));
        }
        case static_cast<Instruction>(Funct3::Lbu):
            return static_cast<Word>(std::to_integer<uint8_t>(ram[masked]));
        case static_cast<Instruction>(Funct3::Lh): {
            const Address m1 = (addr + 1) & simrv::memory::kDramMask;
            const uint16_t u = static_cast<uint16_t>(
                static_cast<uint16_t>(std::to_integer<uint8_t>(ram[masked])) |
                (static_cast<uint16_t>(std::to_integer<uint8_t>(ram[m1])) << 8));
            return static_cast<Word>(static_cast<SignedWord>(static_cast<int16_t>(u)));
        }
        case static_cast<Instruction>(Funct3::Lhu): {
            const Address m1 = (addr + 1) & simrv::memory::kDramMask;
            return static_cast<Word>(static_cast<uint16_t>(std::to_integer<uint8_t>(ram[masked])) |
                                     (static_cast<Word>(std::to_integer<uint8_t>(ram[m1])) << 8));
        }
        case static_cast<Instruction>(Funct3::Lw): {
            const Address m1 = (addr + 1) & simrv::memory::kDramMask;
            const Address m2 = (addr + 2) & simrv::memory::kDramMask;
            const Address m3 = (addr + 3) & simrv::memory::kDramMask;
            return static_cast<Word>(std::to_integer<uint8_t>(ram[masked])) |
                   (static_cast<Word>(std::to_integer<uint8_t>(ram[m1])) << 8) |
                   (static_cast<Word>(std::to_integer<uint8_t>(ram[m2])) << 16) |
                   (static_cast<Word>(std::to_integer<uint8_t>(ram[m3])) << 24);
        }
        default: {
            Word rdata = 0;
            constexpr auto kSizeBits = 0x3;
            const int byte_count = (1 << (funct3 & kSizeBits));
            for (int offset = 0; offset < byte_count; ++offset) {
                rdata |= static_cast<Word>(std::to_integer<uint8_t>(
                             ram[(addr + offset) & simrv::memory::kDramMask]))
                         << (8 * offset);
            }
            constexpr auto kSignExtendBit = 0x4;
            if ((funct3 & kSignExtendBit) == 0) {
                const Word sign_mask = (~Word{0}) << (8 * byte_count - 1);
                rdata |= ((sign_mask & rdata) ? sign_mask : 0);
            }
            return rdata;
        }
    }
}

/// Fast inline RAM write with support for various store formats
inline void ram_write_fast(Address addr, Word wdata, Instruction funct3, Byte* ram) {
    constexpr auto kFunct3WriteMask = static_cast<Instruction>(0x3u);
    const Address masked = addr & simrv::memory::kDramMask;
    switch (funct3 & kFunct3WriteMask) {
        case 0: {
            ram[masked] = static_cast<Byte>(static_cast<uint8_t>(wdata & 0xFF));
            break;
        }
        case 1: {
            const Address m1 = (addr + 1) & simrv::memory::kDramMask;
            ram[masked] = static_cast<Byte>(static_cast<uint8_t>(wdata & 0xFF));
            ram[m1] = static_cast<Byte>(static_cast<uint8_t>((wdata >> 8) & 0xFF));
            break;
        }
        case 2: {
            if (simrv::compiler::likely(masked <= (simrv::memory::kDramMask - 3))) {
                ram[masked] = static_cast<Byte>(static_cast<uint8_t>(wdata & 0xFF));
                ram[masked + 1] = static_cast<Byte>(static_cast<uint8_t>((wdata >> 8) & 0xFF));
                ram[masked + 2] = static_cast<Byte>(static_cast<uint8_t>((wdata >> 16) & 0xFF));
                ram[masked + 3] = static_cast<Byte>(static_cast<uint8_t>((wdata >> 24) & 0xFF));
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
        default: {
            const int byte_count = (1 << funct3);
            for (int offset = 0; offset < byte_count; ++offset) {
                ram[(addr + offset) & simrv::memory::kDramMask] =
                    static_cast<Byte>(static_cast<uint8_t>((wdata >> (8 * offset)) & 0xFF));
            }
            break;
        }
    }
}

}  // namespace simrv::memory_detail
