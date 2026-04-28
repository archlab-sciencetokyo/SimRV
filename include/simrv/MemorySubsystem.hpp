/**
 * @file MemorySubsystem.hpp
 * @brief Memory access and translation subsystem.
 */
#pragma once

#include <memory>

#include "Mmu.hpp"

class Machine;

class MemorySubsystem {
   public:
    explicit MemorySubsystem(Machine& machine) : machine_(machine), mmu_(nullptr) {}

    ~MemorySubsystem() = default;

    /// Initialize MMU after CPU is constructed
    void initialize_mmu();

    auto target_read(CPU& cpu, Address v_addr, Instruction funct3) -> Word;
    void target_write(CPU& cpu, Address v_addr, Word wdata, Instruction funct3);

   private:
    Machine& machine_;
    std::unique_ptr<simrv::Mmu> mmu_;
};
