/**
 * @file State.hpp
 * @brief SimRV declarations.
 */
#pragma once

#include "CsrFile.hpp"
#include "Define.hpp"
#include "StateControl.hpp"

typedef struct {
    Word v_addr;
    Word p_addr;
} TLBEntry;

class CPU {
   public:
    CPU();
    void TLB_flush();
    void set_mstatus(CSRValue);
    CSRValue get_mstatus(CSRValue);
    CSRValue read_csr(CSRAddress);
    void write_csr(CSRAddress, CSRValue);
    void mret();
    void sret();
    void plic_update_mip();
    void plic_set_irq(int, int);
    void raise_exception(TrapCause, CSRValue);

    Register pc;       // program counter
    Register reg[32];  // general purpose registers

    CSRValue mstatus;             // /***** CSRs *****/
    CSRValue mtvec;               //
    CSRValue mscratch;            //
    CSRValue mepc;                //
    TrapCause mcause;             //
    CSRValue mtval;               //
    CSRValue mhartid;             //
    CSRValue misa = kMisaRv32gc;  // RV32GC + privileged S/U bits in current simulator profile.
    CSRValue mie;                 //
    CSRValue mip;                 //
    CSRValue medeleg;             //
    CSRValue mideleg;             //
    CSRValue mcounteren;          //
    CSRValue stvec;               //
    CSRValue sscratch;            //
    CSRValue sepc;                //
    TrapCause scause;             //
    CSRValue stval;               //
    CSRValue satp;                //
    CSRValue scounteren;          // end of CSR

    Address load_res;              // for atomic LR/SC
    CSRValue reserved;             // for atomic LR/SC
    TrapCause pending_exception;   // Exception: used during MMU exception handling
    CSRValue pending_tval;         // Exception:
    PrivilegeLevel priv = PRIV_M;  // 3:machine-mode, 1:supervisor-mode, 0:user-mode
    CSRValue plic_pending_irq;     // MachineState
    CSRValue plic_served_irq;      // MachineState
    Counter mtime = 1;             // machine time (close to the number of executed insns)
    Counter mtimecmp = 0;          // MachineState

    TLBEntry TLB_inst_r[TLB_SIZE];  // TLB for instruction
    TLBEntry TLB_data_r[TLB_SIZE];  // TLB for data (load)
    TLBEntry TLB_data_w[TLB_SIZE];  // TLB for data (store)

    TlbUnit tlb_unit;
    InterruptController interrupt_controller;
    TrapController trap_controller;
    CsrFile csr_file;
};
