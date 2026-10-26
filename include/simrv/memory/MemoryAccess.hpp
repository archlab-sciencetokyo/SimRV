/**
 * @file MemoryAccess.hpp
 * @brief Typed memory access interfaces and operations.
 */
#pragma once

#include "simrv/isa/InstructionFields.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/xlen/AddressTypes.hpp"

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
    static auto target_read(MemorySubsystem& mem, core::CPU& cpu, simrv::xlen::VirtAddr v_addr,
                            isa::Funct3 funct3) -> simrv::xlen::Word;
    static void target_write(MemorySubsystem& mem, core::CPU& cpu, simrv::xlen::VirtAddr v_addr,
                             simrv::xlen::Word wdata, isa::Funct3 funct3);

    static auto target_read(MemorySubsystem& mem, core::CPU& cpu, simrv::xlen::VirtAddr v_addr,
                            simrv::xlen::Instruction funct3) -> simrv::xlen::Word {
        return target_read(mem, cpu, v_addr, static_cast<isa::Funct3>(funct3));
    }
    static void target_write(MemorySubsystem& mem, core::CPU& cpu, simrv::xlen::VirtAddr v_addr,
                             simrv::xlen::Word wdata, simrv::xlen::Instruction funct3) {
        target_write(mem, cpu, v_addr, wdata, static_cast<isa::Funct3>(funct3));
    }

    static auto loadInt(MemorySubsystem& mem, core::CPU& cpu, simrv::xlen::VirtAddr addr,
                        isa::Funct3 funct3) -> simrv::xlen::Word;
    static auto loadFp(MemorySubsystem& mem, core::CPU& cpu, simrv::xlen::VirtAddr addr,
                       isa::Funct3 funct3) -> simrv::xlen::FloatingRegister;

    static void storeInt(MemorySubsystem& mem, core::CPU& cpu, simrv::xlen::VirtAddr addr,
                         simrv::xlen::Word data, isa::Funct3 funct3);
    static void storeFp(MemorySubsystem& mem, core::CPU& cpu, simrv::xlen::VirtAddr addr,
                        simrv::xlen::FloatingRegister data, isa::Funct3 funct3);

    static void execute_cmo(MemorySubsystem& mem, core::CPU& cpu, simrv::xlen::VirtAddr addr,
                            isa::OperationId op);
};

}  // namespace simrv::memory
