/**
 * @file state.hpp
 * @brief Definition of the CPU state class
 */
#ifndef __state_hpp__
#define __state_hpp__

#include "define.h"

/**
 * @brief TLB entry
 *
 * This structure represents an entry in the TLB. It includes the virtual
 * address and the physical address of the page.
 */
typedef struct {
  uint32_t v_addr;
  uint32_t p_addr;
} TLBEntry;

/**
 * @brief CPU state
 *
 * This class represents the state of the CPU.
 * It includes the general purpose registers, the program counter, and the
 * control and status registers (CSRs). The class also includes the TLB for
 * instruction and data accesses.
 */
class CPU {
 public:
  /// @name Control Methods
  ///@{
  void TLB_flush();
  void set_mstatus(uint32_t);
  uint32_t get_mstatus(uint32_t);
  ///@}

  /// @name Register Access
  ///@{
  uint32_t read_csr(uint32_t);
  void write_csr(uint32_t, uint32_t);
  ///@}

  /// @name Exception Handling
  ///@{
  void raise_exception(uint32_t, uint32_t);
  void mret();
  void sret();
  void plic_update_mip();
  void plic_set_irq(int, int);
  ///@}

  /// @name CPU State
  ///@{
  uint32_t pc;       ///< program counter
  uint32_t reg[32];  ///< general purpose registers

  // Control and Status Registers (CSRs)
  uint32_t mstatus;            ///< Machine status register.
  uint32_t mtvec;              ///< Machine trap-handler base address.
  uint32_t mscratch;           ///< Scratch register for machine trap handlers.
  uint32_t mepc;               ///< Machine exception program counter.
  uint32_t mcause;             ///< Machine trap cause.
  uint32_t mtval;              ///< Machine bad address or instruction.
  uint32_t mhartid;            ///< Hardware thread ID.
  uint32_t misa = 0x00141105;  ///< Machine ISA register. RV32acim by default.
  uint32_t mie;                ///< Machine interrupt-enable register.
  uint32_t mip;                ///< Machine interrupt pending.
  uint32_t medeleg;            ///< Machine exception delegation register.
  uint32_t mideleg;            ///< Machine interrupt delegation register.
  uint32_t mcounteren;         ///< Machine counter enable.
  uint32_t stvec;              ///< Supervisor trap handler base address.
  uint32_t sscratch;    ///< Scratch register for supervisor trap handlers.
  uint32_t sepc;        ///< Supervisor exception program counter.
  uint32_t scause;      ///< Supervisor trap cause.
  uint32_t stval;       ///< Supervisor bad address or instruction.
  uint32_t satp;        ///< Supervisor address translation and protection.
  uint32_t scounteren;  ///< Supervisor counter enable.

  // Additional state variables
  uint32_t load_res;           ///< For atomic LR/SC.
  uint32_t reserved;           ///< Reserved for future use.
  uint32_t pending_exception;  ///< Exception code for pending exception.
  uint32_t pending_tval;  ///< Additional information for pending exception.
  uint32_t priv =
      PRIV_M;  ///< Current privilege mode: 3=machine, 1=supervisor, 0=user.
  uint32_t plic_pending_irq;  ///< Pending IRQ number in PLIC.
  uint32_t plic_served_irq;   ///< Last served IRQ number in PLIC.
  uint64_t mtime =
      1;  ///< Machine time, approximates the number of executed instructions.
  uint64_t mtimecmp = 0;  ///< Time comparator for machine timer interrupts.
  ///@}

  /// @name TLB
  ///@{
  TLBEntry TLB_inst_r[TLB_SIZE];  ///< TLB for instruction
  TLBEntry TLB_data_r[TLB_SIZE];  ///< TLB for data (load)
  TLBEntry TLB_data_w[TLB_SIZE];  ///< TLB for data (store)
  ///@}
};

#endif /* state_hpp */
