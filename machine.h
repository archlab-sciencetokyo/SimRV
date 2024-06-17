/**
 * @file machine.h
 * @brief This file contains the declaration of the Machine class and the
 * Microcn class.
 *
 * @note This file is part of the SimCore/RISC-V project developed by
 * ArchLab. TokyoTech.
 * @since 2018-07-05
 */

#ifndef __machine_hpp__
#define __machine_hpp__

#include "console.h"
#include "define.h"
#include "disk.h"
#include "module.h"
#include "state.h"

/**
 * @brief The Machine class represents the RISC-V machine.
 */
class Machine {
 public:
  /**
   * @brief Destructor for the Machine class.
   */
  ~Machine();

  /**
   * @brief Initializes the machine.
   * @param argc The number of command line arguments.
   * @param argv The command line arguments.
   * @return 0 if successful, otherwise an error code.
   */
  int init(int argc, char *argv[]);

  /**
   * @brief Executes the main loop of the machine.
   */
  void exec();

  /**
   * @brief Outputs trace information.
   */
  void trace_output();

  /**
   * @brief Displays the result.
   */
  void display_result();

  /**
   * @brief Outputs instruction mix information.
   */
  void instmix_output();

  /**
   * @brief Reads a value from the target memory.
   * @param addr The address to read from.
   * @param size The size of the value to read.
   * @return The value read from the memory.
   */
  uint32_t target_read(uint32_t addr, uint32_t size);

  /**
   * @brief Writes a value to the target memory.
   * @param addr The address to write to.
   * @param size The size of the value to write.
   * @param value The value to write to the memory.
   */
  void target_write(uint32_t addr, uint32_t size, uint32_t value);

  uint32_t tohost = 0;  // for application mode

  /**
   * @brief The number of executed instructions.
   */
  uint64_t e_icount = 0;

  /**
   * @brief The number of executed instructions on I/O controller.
   */
  uint64_t e_uc_cnt = 0;

  /**
   * @brief The number of executed compressed instructions.
   */
  uint64_t e_ccount = 0;

  /**
   * @brief The array for measuring instruction mix.
   */
  int e_instmix[NUMOFID___];

  /**
   * @brief Flag to identify whether the application mode or not.
   */
  int s_appmode = 0;

  /**
   * @brief Flag to identify whether the rtos mode or not.
   */
  int s_rtosmode = 0;

  /**
   * @brief Flag to identify whether the debug mode or not.
   */
  int s_debugmode = 0;

  /**
   * @brief Flag to identify whether the dlog mode or not.
   */
  int s_dlog_mode = 0;

  /**
   * @brief Flag to use IO controller (micro-controller).
   */
  int s_use_uc = 0;

  /**
   * @brief Flag to use disk image.
   */
  int s_use_disk = 0;

  /**
   * @brief Flag to measure instruction mix.
   */
  int s_use_mix = 0;

  /**
   * @brief Flag for bp_trace.
   */
  int s_bp_trace = 0;

  /**
   * @brief The start PC.
   */
  uint32_t s_start_pc = 0;

  /**
   * @brief The strace.
   */
  uint32_t s_strace = 0;

  /**
   * @brief Flag to generate binary image file for FPGA run.
   */
  uint64_t s_gen_binfile = 0;

  /**
   * @brief The memimg.
   */
  uint64_t s_memimg = 0;

  /**
   * @brief The fincnt.
   */
  uint64_t s_fincnt = ~0;

  /**
   * @brief The trace_begin.
   */
  uint64_t s_trace_begin = ~0;

  /**
   * @brief The trace_end.
   */
  uint64_t s_trace_end = ~0;

  /**
   * @brief The enabletimer.
   */
  uint64_t s_enabletimer = ~0;

  /**
   * @brief The file pointer of trace file.
   */
  FILE *s_fp_trace;

  /**
   * @brief The file pointer of dlog file.
   */
  FILE *s_fp_dlog;

  /**
   * @brief The file name of memory image.
   */
  char *s_fn_memimg;

  /**
   * @brief The file name of disk image.
   */
  char *s_fn_dskimg;

  /**
   * @brief The file name of device tree binary.
   */
  char *s_fn_dvtree;

  /**
   * @brief The file name of I/O controller program binary.
   */
  char *s_fn_iocon;

  /**
   * @brief The start time stamp.
   */
  struct timeval s_stime;

  CPU *cpu;
  Disk *disk;
  Console *console;

  uint8_t *mmem;  // main memory

 private:
  void INI();
  void IFA();
  void IFB(int);
  void IFC();
  void CVT();
  void ID_();
  void OF_();
  void EX1();
  void LD_();
  void EX2();
  void SD_();
  void WB_();
  void COM();
  void FIN();

  int r_running = 1;

  uint32_t r_padr1, r_padr2;
  uint32_t r_cpc;
  uint32_t r_ir_org;

  uint32_t r_cinsn;
  uint32_t r_ir;

  uint32_t r_opcode;
  uint32_t r_rd;
  uint32_t r_rs1;
  uint32_t r_rs2;
  uint32_t r_funct3;
  uint32_t r_funct5;
  uint32_t r_funct7;
  uint32_t r_funct12;
  uint32_t r_imm;

  uint32_t r_rrs1;
  uint32_t r_rrs2;
  uint32_t r_rcsr;

  uint32_t r_tkn;
  uint32_t r_jmp_pc;
  uint32_t r_mem_addr;
  uint32_t r_wb_data;
  uint32_t r_wb_data_csr;

  uint32_t r_mem_rdata;

  uint32_t r_mem_wdata;
};

/**
 * @brief The Microcn class represents the I/O controller (micro-controller).
 */
class Microcn {
 public:
  /**
   * @brief Initializes the I/O controller.
   * @param filename The filename of the I/O controller program binary.
   */
  void init(char *filename);

  /**
   * @brief Executes the I/O controller program.
   * @return 0 if successful, otherwise an error code.
   */
  int exec();

  uint8_t *cmem; /* local memory */
  uint8_t *mmem; /* main memory of processor */

  QueueState *cons_queue; /* Queue of Console */
  QueueState *disk_queue; /* Queue of Disk */

  uint8_t cons_fifo;
  uint8_t fifo_en;

  uint8_t *disk; /* Disk */

  uint32_t Mode;
  uint32_t Qnum;
  uint32_t Qsel;

  uint32_t pc = 0;  // D_START_PC;      // program counter
  uint32_t cpc;
  uint32_t reg[32];   // general purpose registers
  uint32_t icnt = 0;  // instruction count

  uint32_t r_ir;
  uint32_t r_opcode;
  uint32_t r_rd;
  uint32_t r_rs1;
  uint32_t r_rs2;
  uint32_t r_funct3;
  uint32_t r_funct5;
  uint32_t r_funct7;
  uint32_t r_funct12;
  uint32_t r_imm;

  uint32_t r_rrs1;
  uint32_t r_rrs2;

  uint32_t r_tkn;
  uint32_t r_jmp_pc;
  uint32_t r_mem_addr;
  uint32_t r_wb_data;
  uint32_t r_wb_data_csr;

  uint32_t r_mem_rdata;
};

#endif /* machine_hpp */
