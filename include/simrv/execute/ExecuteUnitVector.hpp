/**
 * @file ExecuteUnitVector.hpp
 * @brief Canonical Vector execution unit (RVV 1.0).
 */
#pragma once

#include "simrv/isa/OperationId.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::core {
class CPU;
class Machine;
}  // namespace simrv::core

namespace simrv::memory {
class MemorySubsystem;
}  // namespace simrv::memory

namespace simrv::execute {

/**
 * @class ExecuteUnitVector
 * @brief Orchestrates execution of RISC-V Vector Extension (RVV 1.0) instructions.
 */
class ExecuteUnitVector {
   public:
    /// Execute a vector instruction using the specified memory subsystem.
    static void execute_vector(core::CPU& cpu, memory::MemorySubsystem& mem, isa::OperationId op_id,
                               simrv::xlen::Instruction ir);

    /// Convenience overload resolving memory from the Machine instance.
    static void execute_vector(core::CPU& cpu, core::Machine& machine, isa::OperationId op_id,
                               simrv::xlen::Instruction ir);
};

}  // namespace simrv::execute
