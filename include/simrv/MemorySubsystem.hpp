/**
 * @file MemorySubsystem.hpp
 * @brief Memory access and translation subsystem.
 */
#pragma once

#include "Define.hpp"

namespace simrv::memory_detail {
inline bool is_dram_addr(Address p_addr) { return p_addr >= simrv::memory::kDramBaseAddress; }

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

inline Word ram_read_fast(Address addr, Instruction funct3, Byte* ram) {
    const Address masked = addr & simrv::memory::kDramMask;

    switch (funct3 & static_cast<Instruction>(0x7u)) {
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
            const int n = (1 << (funct3 & 0x3));
            for (int i = 0; i < n; i++) {
                rdata |= static_cast<Word>(
                             std::to_integer<uint8_t>(ram[(addr + i) & simrv::memory::kDramMask]))
                         << (8 * i);
            }
            if ((funct3 & 0x4) == 0) {
                const Word sign_mask = (~Word{0}) << (8 * n - 1);
                rdata |= ((sign_mask & rdata) ? sign_mask : 0);
            }
            return rdata;
        }
    }
}

inline void ram_write_fast(Address addr, Word wdata, Instruction funct3, Byte* ram) {
    const Address masked = addr & simrv::memory::kDramMask;
    switch (funct3 & static_cast<Instruction>(0x3u)) {
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
            for (int i = 0; i < (1 << funct3); i++) {
                ram[(addr + i) & simrv::memory::kDramMask] =
                    static_cast<Byte>(static_cast<uint8_t>((wdata >> (8 * i)) & 0xFF));
            }
            break;
        }
    }
}
}  // namespace simrv::memory_detail

class Machine;
class CPU;

class MemorySubsystem {
   public:
    explicit MemorySubsystem(Machine& machine) : machine_(machine) {}

    Word target_read(CPU& cpu, Address v_addr, Instruction funct3);
    void target_write(CPU& cpu, Address v_addr, Word wdata, Instruction funct3);

   private:
    Machine& machine_;
};
