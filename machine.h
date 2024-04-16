/******************************************************************************************/
/**** SimCore/RISC-V since 2018-07-05                             ArchLab. TokyoTech   ****/
/******************************************************************************************/
#ifndef __machine_hpp__
#define __machine_hpp__

#include "define.h"
#include "module.h"
#include "state.h"
#include "disk.h"
#include "console.h"
/******************************************************************************************/
class Machine {
public:
    ~Machine();
    int init(int argc, char *argv[]);
    void exec();           // execution of the main loop
    void trace_output();   // output trace info
    void display_result(); //
    void instmix_output(); //
    uint32_t target_read(uint32_t, uint32_t);
    void target_write(uint32_t, uint32_t, uint32_t);
    uint32_t tohost = 0;   // for application mode

    /***** evaluation results                                                         *****/
    /**************************************************************************************/
    uint64_t e_icount = 0;      // the number of executed instructions
    uint64_t e_uc_cnt = 0;      // the number of executed instructions on I/O controller
    uint64_t e_ccount = 0;      // the number of executed compressed instructions
    int      e_instmix[NUMOFID___];  // the array for measuring instruction mix

    /***** system configuration and flags                                             *****/
    /**************************************************************************************/
    int      s_appmode     = 0; // flag to identify whether the application mode or not
    int      s_rtosmode    = 0; // flag to identify whether the rtos mode or not
    int      s_debugmode   = 0; // 
    int      s_dlog_mode   = 0; //
    int      s_use_uc      = 0; // flag to use IO controller (micro-controller)
    int      s_use_disk    = 0; // flag to use disk image
    int      s_use_mix     = 0; // flag to measure instruction mix
    int      s_bp_trace    = 0; // 
    uint32_t s_start_pc    = 0; // start PC
    uint32_t s_strace      = 0; // 
    uint64_t s_gen_binfile = 0; // flag: generate binary image file for FPGA run
    uint64_t s_memimg      = 0; // note!!
    uint64_t s_fincnt      =~0; // instruction count to finish the simulation
    uint64_t s_trace_begin =~0; // 
    uint64_t s_trace_end   =~0; // 
    uint64_t s_enabletimer =~0; // enable timer after N cycles Linux boots
    FILE    *s_fp_trace;        // file pointer of trace file
    FILE    *s_fp_dlog;         // 
    char    *s_fn_memimg;       // file name of memory image
    char    *s_fn_dskimg;       // file name of disk   image
    char    *s_fn_dvtree;       // file name of devide tree binary
    char    *s_fn_iocon;        // file name of I/O controller program binary 
    struct timeval s_stime;     // start time stamp

    CPU     *cpu;
    Disk    *disk;
    Console *console;
    
    uint8_t *mmem;    // main memory
private:
    /**************************************************************************************/
    void INI();
    void IFA(); /* the first IF */
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
    /**************************************************************************************/
    int      r_running = 1; // 
    /**************************************************************************************/
    // IF_ Stage
    uint32_t r_padr1, r_padr2;
    uint32_t r_cpc;     // current PC, the program counter of this instruction
    uint32_t r_ir_org;  // 32bit raw instruction (standard/compressed insn)
    
    // CVT stage
    uint32_t r_cinsn;  // set if the fetched insn is a compressed one
    uint32_t r_ir;      // 32bit standard instruction
    
    // ID_ stage
    uint32_t r_opcode;
    uint32_t r_rd;
    uint32_t r_rs1;
    uint32_t r_rs2;
    uint32_t r_funct3;
    uint32_t r_funct5;
    uint32_t r_funct7;
    uint32_t r_funct12;
    uint32_t r_imm;
    
    // OF_ stage
    uint32_t r_rrs1;
    uint32_t r_rrs2;
    uint32_t r_rcsr;
    
    // EX1 stage
    uint32_t r_tkn; // flag for branck taken or untaken
    uint32_t r_jmp_pc;
    uint32_t r_mem_addr;
    uint32_t r_wb_data;
    uint32_t r_wb_data_csr;
    
    // LD_ stage
    uint32_t r_mem_rdata;
    
    // EX2 stage
    uint32_t r_mem_wdata;
    
    // SD_ stage
    // WB_ stage
    // COM stage
};

/***** I/O controller (micro-controller)                                              *****/
/******************************************************************************************/
class Microcn {
public:
    void init(char *);
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

    uint32_t pc = 0; // D_START_PC;      // program counter
    uint32_t cpc;
    uint32_t reg[32];              // general purpose registers
    uint32_t icnt =0;              // instruction count

    // IF_ stage
    uint32_t r_ir;
    // ID_ stage
    uint32_t r_opcode;
    uint32_t r_rd;
    uint32_t r_rs1;
    uint32_t r_rs2;
    uint32_t r_funct3;
    uint32_t r_funct5;
    uint32_t r_funct7;
    uint32_t r_funct12;
    uint32_t r_imm;
    // OF_ stage
    uint32_t r_rrs1;
    uint32_t r_rrs2;
    // EX1 stage
    uint32_t r_tkn; // flag for branck taken or untaken
    uint32_t r_jmp_pc;
    uint32_t r_mem_addr;
    uint32_t r_wb_data;
    uint32_t r_wb_data_csr;
    // MEM stage
    uint32_t r_mem_rdata;
};

#endif /* machine_hpp */
/******************************************************************************************/
