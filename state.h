/******************************************************************************************/
/**** SimCore/RISC-V since 2018-07-05                             ArchLab. TokyoTech   ****/
/******************************************************************************************/
#ifndef __state_hpp__
#define __state_hpp__

#include "define.h"

/******************************************************************************************/
typedef struct { uint32_t v_addr; uint32_t p_addr;} TLBEntry;

/******************************************************************************************/
class CPU {
public:
    void     TLB_flush();
    void     set_mstatus(uint32_t);
    uint32_t get_mstatus(uint32_t);
    uint32_t read_csr(uint32_t);
    void     write_csr(uint32_t, uint32_t);
    void     mret();
    void     sret();
    void     plic_update_mip();
    void     plic_set_irq(int, int);
    void     raise_exception(uint32_t, uint32_t);

    /***** CPU architecture state *********************************************************/
    uint32_t pc;                   // program counter
    uint32_t reg[32];              // general purpose registers
    
    uint32_t mstatus;              // /***** CSRs *****/
    uint32_t mtvec;                //
    uint32_t mscratch;             //
    uint32_t mepc;                 //
    uint32_t mcause;               //
    uint32_t mtval;                //
    uint32_t mhartid;              //
    uint32_t misa  = 0x00141105;   // RV32acim, Machine ISA register (MISA)
//  uint32_t misa  = 0x0014112d;   // RV32acdfim
    uint32_t mie;                  //
    uint32_t mip;                  //
    uint32_t medeleg;              //
    uint32_t mideleg;              //
    uint32_t mcounteren;           //
    uint32_t stvec;                //
    uint32_t sscratch;             //
    uint32_t sepc;                 //
    uint32_t scause;               //
    uint32_t stval;                //
    uint32_t satp;                 //
    uint32_t scounteren;           // end of CSR
    
    uint32_t load_res;             // for atomic LR/SC
    uint32_t reserved;             // for atomic LR/SC
    uint32_t pending_exception;    // Exception: used during MMU exception handling
    uint32_t pending_tval;         // Exception:
    uint32_t priv = PRIV_M;        // 3:machine-mode, 1:supervisor-mode, 0:user-mode
    uint32_t plic_pending_irq;     // MachineState
    uint32_t plic_served_irq;      // MachineState
    uint64_t mtime =1;             // machine time (close to the number of executed insns)
    uint64_t mtimecmp =0;          // MachineState

    TLBEntry TLB_inst_r[TLB_SIZE]; // TLB for instruction
    TLBEntry TLB_data_r[TLB_SIZE]; // TLB for data (load)
    TLBEntry TLB_data_w[TLB_SIZE]; // TLB for data (store)
};

#endif /* state_hpp */
/******************************************************************************************/
