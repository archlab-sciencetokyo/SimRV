/**
 * @file TrapController.hpp
 * @brief Handles RISC-V architectural trap vectoring, exception escalation, and privilege
 * management.
 */
#pragma once

#include "simrv/Define.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

class CPU;
struct ArchState;

/**
 * @brief Return human-readable name of a RISC-V trap cause (interrupt or exception).
 */
[[nodiscard]] auto trap_cause_name(TrapCause cause) -> std::string;

/**
 * @class TrapController
 * @brief Handles trap entry/return sequencing, exception delegation, and privilege verification.
 */
class TrapController {
   public:
    /**
     * @brief Executes Machine-mode trap return (MRET).
     *
     * Restores privilege level from MPP, re-enables interrupt enabling bit MIE from MPIE,
     * resets MPIE to 1, sets MPP to U-mode, and updates program counter from MEPC.
     *
     * @param state Reference to architectural state.
     */
    static void mret(ArchState& state);

    /**
     * @brief Executes Supervisor-mode trap return (SRET).
     *
     * Restores privilege level from SPP, re-enables SIE from SPIE, resets SPIE to 1,
     * sets SPP to U-mode, clears MPRV, and updates program counter from SEPC.
     *
     * @param state Reference to architectural state.
     */
    static void sret(ArchState& state);

    /**
     * @brief Traps CPU execution to machine or supervisor handler for an exception or interrupt.
     *
     * Evaluates delegation vectors (medeleg/mideleg), populates cause (mcause/scause),
     * faulting address or payload (mtval/stval), saves return address (mepc/sepc), and updates
     * privilege level to handler target mode.
     *
     * @param cpu Reference to CPU instance.
     * @param cause Hardware trap cause code (interrupt or exception).
     * @param tval Auxiliary trap value (e.g. faulting virtual address or instruction encoding).
     */
    static void raise_exception(CPU& cpu, TrapCause cause, CSRValue tval);

    /**
     * @brief Validates if the current privilege level is sufficient to execute a given privileged
     * instruction.
     *
     * Checks requirements for instruction variants like mret, sret, or sfence.vma based on current
     * privilege, MISA extensions, and TSR/TVM bits of mstatus.
     *
     * @param current_priv The CPU's current privilege level.
     * @param misa The current machine ISA configuration (MISA).
     * @param mstatus The current status register (mstatus).
     * @param funct12 The 12-bit privileged function field of the system instruction.
     * @param funct7 The 7-bit function field (used for SFENCE.VMA).
     * @return true if the instruction is allowed under current privilege, false otherwise.
     */
    static auto can_execute_privileged_instruction(PrivilegeLevel current_priv, CSRValue misa,
                                                   CSRValue mstatus, Instruction funct12,
                                                   Word funct7) -> bool;

    /**
     * @brief Validates if the current privilege level has access to a specific CSR.
     *
     * Checks if the CPU has the privilege to read/write the requested CSR address,
     * including read-only restrictions.
     *
     * @param current_priv The CPU's current privilege level.
     * @param misa The current machine ISA register (misa).
     * @param csr_addr The address of the CSR.
     * @param is_write True if this is a write or read-write access to the CSR.
     * @return true if access is permitted, false if it triggers an illegal instruction exception.
     */
    static auto can_access_csr(PrivilegeLevel current_priv, CSRValue misa, CSRAddress csr_addr,
                               bool is_write) -> bool;

    // Backward-compatibility wrappers
    static void raiseException(CPU& cpu, TrapCause cause, CSRValue tval) {
        raise_exception(cpu, cause, tval);
    }
    static auto canExecutePrivilegedInstruction(PrivilegeLevel current_priv, CSRValue misa,
                                                CSRValue mstatus, Instruction funct12, Word funct7)
        -> bool {
        return can_execute_privileged_instruction(current_priv, misa, mstatus, funct12, funct7);
    }
    static auto canAccessCsr(PrivilegeLevel current_priv, CSRValue misa, CSRAddress csr_addr,
                             bool is_write) -> bool {
        return can_access_csr(current_priv, misa, csr_addr, is_write);
    }
};

}  // namespace simrv::core
