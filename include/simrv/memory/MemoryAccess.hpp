/**
 * @file MemoryAccess.hpp
 * @brief Typed memory access interfaces and operations.
 */
#pragma once

#include "simrv/xlen/Types.hpp"

namespace simrv::core {
class CPU;
}

namespace simrv::memory {
class MemorySubsystem;

/**
 * @class MemoryAccess
 * @brief Helper routines to translate opcodes and data types into TileLink operations.
 */
class MemoryAccess {
   public:
    static auto target_read(MemorySubsystem& mem, core::CPU& cpu, Address v_addr,
                            Instruction funct3) -> Word;
    static void target_write(MemorySubsystem& mem, core::CPU& cpu, Address v_addr, Word wdata,
                             Instruction funct3);

    static auto loadInt(MemorySubsystem& mem, core::CPU& cpu, Address addr, Instruction funct3)
        -> Word;
    static auto loadFp(MemorySubsystem& mem, core::CPU& cpu, Address addr, Instruction funct3)
        -> FloatingRegister;

    static void storeInt(MemorySubsystem& mem, core::CPU& cpu, Address addr, Word data,
                         Instruction funct3);
    static void storeFp(MemorySubsystem& mem, core::CPU& cpu, Address addr, FloatingRegister data,
                        Instruction funct3);
};

}  // namespace simrv::memory