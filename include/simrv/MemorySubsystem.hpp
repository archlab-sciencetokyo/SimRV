/**
 * @file MemorySubsystem.hpp
 * @brief Memory access and translation subsystem.
 */
#pragma once

#include "Define.hpp"

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
