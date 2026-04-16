/**
 * @file MemorySubsystem.hpp
 * @brief Memory access and translation subsystem.
 */
#pragma once

#include <memory>

#include "Define.hpp"
#include "MemoryUtil.hpp"
#include "Mmu.hpp"

class Machine;

class MemorySubsystem {
   public:
    explicit MemorySubsystem(Machine& machine) : machine_(machine), mmu_(nullptr) {}

    ~MemorySubsystem() = default;

    /// Initialize MMU after CPU is constructed
    void initialize_mmu();

    Word target_read(CPU& cpu, Address v_addr, Instruction funct3);
    void target_write(CPU& cpu, Address v_addr, Word wdata, Instruction funct3);

   private:
    Machine& machine_;
    std::unique_ptr<simrv::Mmu> mmu_;
};
