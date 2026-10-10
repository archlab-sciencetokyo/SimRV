/**
 * @file MemoryAccess.hpp
 * @brief Typed memory access interfaces and operations.
 */
#pragma once

#include "simrv/Define.hpp"
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
    static auto target_read(MemorySubsystem& mem, core::CPU& cpu, VirtAddr v_addr,
                            isa::Funct3 funct3) -> Word;
    static void target_write(MemorySubsystem& mem, core::CPU& cpu, VirtAddr v_addr, Word wdata,
                             isa::Funct3 funct3);

    static auto target_read(MemorySubsystem& mem, core::CPU& cpu, VirtAddr v_addr,
                            Instruction funct3) -> Word {
        return target_read(mem, cpu, v_addr, static_cast<isa::Funct3>(funct3));
    }
    static void target_write(MemorySubsystem& mem, core::CPU& cpu, VirtAddr v_addr, Word wdata,
                             Instruction funct3) {
        target_write(mem, cpu, v_addr, wdata, static_cast<isa::Funct3>(funct3));
    }

    static auto loadInt(MemorySubsystem& mem, core::CPU& cpu, VirtAddr addr, isa::Funct3 funct3)
        -> Word;
    static auto loadFp(MemorySubsystem& mem, core::CPU& cpu, VirtAddr addr, isa::Funct3 funct3)
        -> FloatingRegister;

    static void storeInt(MemorySubsystem& mem, core::CPU& cpu, VirtAddr addr, Word data,
                         isa::Funct3 funct3);
    static void storeFp(MemorySubsystem& mem, core::CPU& cpu, VirtAddr addr, FloatingRegister data,
                        isa::Funct3 funct3);
};

}  // namespace simrv::memory
