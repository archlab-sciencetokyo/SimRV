/******************************************************************************************/
/**** SimCore/RISC-V since 2018-07-05                             ArchLab. TokyoTech   ****/
/******************************************************************************************/
#ifndef __define_h__
#define __define_h__

#include <iostream>
#include <fstream>
#include <iomanip>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <signal.h>
#include <stack>
#include <sys/time.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>
#include <termios.h>

/******************************************************************************************/
/**** Simulation                                                                       ****/
/******************************************************************************************/
#define D_START_PC   (0x80000000) // initial PC, the simulation starts from this address
#define D_INITD_ADDR (0x01000000) // after 16MB area, write the init data

#define FLAG_DUMP_EXEC  (1 << 0)
#define FLAG_DUMP_REG   (1 << 1)
#define FLAG_DUMP_CSR   (1 << 2)
#define FLAG_DUMP_MIURA (1 << 3)

/**** TLB                                                                              ****/
/******************************************************************************************/
#define PTE_V_MASK (1 << 0)
#define PTE_R_MASK (1 << 1)
#define PTE_W_MASK (1 << 2)
#define PTE_X_MASK (1 << 3)
#define PTE_U_MASK (1 << 4)
#define PTE_A_MASK (1 << 6)
#define PTE_D_MASK (1 << 7)

enum PTE_ACCESS {
    ACCESS_READ  = 0,
    ACCESS_WRITE = 1,
    ACCESS_CODE  = 2
};

#define LEVELS    2
#define PTE_SIZE  4
#define PAGE_SIZE (1 << 12)

/**** VirtIO 0x40000000 ~                                                              ****/
/******************************************************************************************/
#define VIRTIO_BASE_ADDR (0x40000000) // NotChange
#define VIRTIO_SIZE      (0x08000000) //
#define VRING_DESC_F_NEXT     (1)
#define VRING_DESC_F_WRITE    (2)
#define VRING_DESC_F_INDIRECT (4)

/* console */
#define CONSOLE_MAX_QUEUE_NUM (2)
#define VIRTIO_CONSOLE_IRQ (1)

/* block device (disk) */
#define SECTOR_SIZE         (512)
#define DISK_BUF_SIZE       (512 * 512)
#define DISK_SIZE           (64 * 1024 * 1024)
#define DISK_MAX_QUEUE_NUM  (4)
#define VIRTIO_DISK_IRQ     (2)
#define VIRTIO_BLK_T_IN     (0)
#define VIRTIO_BLK_T_OUT    (1)
#define VIRTIO_BLK_S_OK     (0)
#define VIRTIO_BLK_S_IOERR  (1)
#define VIRTIO_BLK_S_UNSUPP (2)

#define DISK_MASK           (0x3ffffff)

/**** PLIC (Platform-Level Interrupt Contoroller) 0x50000000 ~                         ****/
/******************************************************************************************/
#define PLIC_BASE_ADDR   (0x50000000) // NotChange:
#define PLIC_SIZE        (0x00400000)
#define PLIC_HART_BASE   (0x200000)
#define PLIC_HART_SIZE   (0x1000)

/**** CLINT (Core Local Interruputer) 0x60000000 ~                                     ****/
/******************************************************************************************/
#define CLINT_BASE_ADDR  (0x60000000) // NotChange:
#define CLINT_SIZE       (0x000c0000) //

/**** DRAM (Main Memory) 0x80000000 ~                                                  ****/
/******************************************************************************************/
#define DRAM_BASE_ADDR (0x80000000)
#define DRAM_SIZE      (64 * 1024 * 1024)
#define DRAM_MASK      (0x3ffffff)
#define D_PAGE_SHIFT   (12)          // page shift for page size of4KB
#define D_PAGE_MASK    (0x00000fff)  // page mask  for page size of4KB
#define TLB_SIZE       (4)

/**** Micro Controller                                                                 ****/
/******************************************************************************************/
#define LCMEM_SIZE     (32*1024)

/**** exception & interrupt                                                            ****/
/******************************************************************************************/
#define CAUSE_INTERRUPT  ((uint32_t)1 << 31)

enum EXCEPTION_CODE {
    CAUSE_MISALIGNED_FETCH    = 0x0,
    CAUSE_FAULT_FETCH         = 0x1,
    CAUSE_ILLEGAL_INSTRUCTION = 0x2,
    CAUSE_BREAKPOINT          = 0x3,
    CAUSE_MISALIGNED_LOAD     = 0x4,
    CAUSE_FAULT_LOAD          = 0x5,
    CAUSE_MISALIGNED_STORE    = 0x6,
    CAUSE_FAULT_STORE         = 0x7,
    CAUSE_USER_ECALL          = 0x8,
    CAUSE_SUPERVISOR_ECALL    = 0x9,
    CAUSE_HYPERVISOR_ECALL    = 0xa,
    CAUSE_MACHINE_ECALL       = 0xb,
    CAUSE_FETCH_PAGE_FAULT    = 0xc,
    CAUSE_LOAD_PAGE_FAULT     = 0xd,
    CAUSE_STORE_PAGE_FAULT    = 0xf
};

/**** CSR Mask                                                                         ****/
/******************************************************************************************/
enum PRIV_MODE {
    PRIV_U = 0,
    PRIV_S = 1,
    PRIV_H = 2,
    PRIV_M = 3
};

#define COUNTEREN_MASK ((1 << 0) | (1 << 2))

#define MSTATUS_UIE  (1 << 0)
#define MSTATUS_SIE  (1 << 1)
#define MSTATUS_HIE  (1 << 2)
#define MSTATUS_MIE  (1 << 3)
#define MSTATUS_UPIE (1 << 4)
#define MSTATUS_SPIE (1 << 5)
#define MSTATUS_HPIE (1 << 6)
#define MSTATUS_MPIE (1 << 7)
#define MSTATUS_SPP  (1 << 8)
#define MSTATUS_HPP  (3 << 9)
#define MSTATUS_MPP  (3 << 11)
#define MSTATUS_FS   (3 << 13)
#define MSTATUS_XS   (3 << 15)
#define MSTATUS_MPRV (1 << 17)
#define MSTATUS_SUM  (1 << 18)
#define MSTATUS_MXR  (1 << 19)

#define MSTATUS_SPIE_SHIFT 5
#define MSTATUS_MPIE_SHIFT 7
#define MSTATUS_SPP_SHIFT  8
#define MSTATUS_MPP_SHIFT 11
#define MSTATUS_FS_SHIFT  13

#define MSTATUS_MASK (MSTATUS_UIE | MSTATUS_SIE | MSTATUS_MIE | \
MSTATUS_UPIE | MSTATUS_SPIE | MSTATUS_MPIE | \
MSTATUS_SPP | MSTATUS_MPP | \
MSTATUS_FS | \
MSTATUS_MPRV | MSTATUS_SUM | MSTATUS_MXR)

#define SSTATUS_MASK0 (MSTATUS_UIE | MSTATUS_SIE | MSTATUS_UPIE | MSTATUS_SPIE | \
MSTATUS_SPP | MSTATUS_FS | MSTATUS_XS | MSTATUS_SUM | MSTATUS_MXR)

#define SSTATUS_MASK SSTATUS_MASK0

#define MIP_USIP (1 << 0)
#define MIP_SSIP (1 << 1)
#define MIP_HSIP (1 << 2)
#define MIP_MSIP (1 << 3)
#define MIP_UTIP (1 << 4)
#define MIP_STIP (1 << 5)
#define MIP_HTIP (1 << 6)
#define MIP_MTIP (1 << 7)
#define MIP_UEIP (1 << 8)
#define MIP_SEIP (1 << 9)
#define MIP_HEIP (1 << 10)
#define MIP_MEIP (1 << 11)

/**** CSR Addr                                                                         ****/
/******************************************************************************************/
/* User-Mode */
#define CSR_USTATUS      0x000
#define CSR_UIE          0x004
#define CSR_UTVEC        0x005
#define CSR_USCRATCH     0x040
#define CSR_UEPC         0x041
#define CSR_UCAUSE       0x042
#define CSR_UTVAL        0x043
#define CSR_UIP          0x044
#define CSR_FFLAGS       0x001
#define CSR_FRM          0x002
#define CSR_FCSR         0x003
#define CSR_CYCLE        0xC00
#define CSR_TIME         0xC01
#define CSR_INSTRET      0xC02

/* Superviser-Mode */
#define CSR_SSTATUS    0x100
#define CSR_SEDELEG    0x102
#define CSR_SIDELEG    0x103
#define CSR_SIE        0x104
#define CSR_STVEC      0x105
#define CSR_SCOUNTEREN 0x106
#define CSR_SSCRATCH   0x140
#define CSR_SEPC       0x141
#define CSR_SCAUSE     0x142
#define CSR_STVAL      0x143
#define CSR_SIP        0x144
#define CSR_SATP       0x180

/* Machine-Mode */
#define CSR_MVENDORID     0xF11
#define CSR_MARCHID       0xF12
#define CSR_MIMPID        0xF13
#define CSR_MHARTID       0xF14
#define CSR_MSTATUS       0x300
#define CSR_MISA          0x301
#define CSR_MEDELEG       0x302
#define CSR_MIDELEG       0x303
#define CSR_MIE           0x304
#define CSR_MTVEC         0x305
#define CSR_MCOUNTEREN    0x306
#define CSR_MSCRATCH      0x340
#define CSR_MEPC          0x341
#define CSR_MCAUSE        0x342
#define CSR_MTVAL         0x343
#define CSR_MIP           0x344
#define CSR_MCYCLE        0xB00
#define CSR_MINSTRET      0xB02
#define CSR_MCYCLEH       0xB80
#define CSR_MINSTRETH     0xB82
#define CSR_CYCLEH        0xC80
#define CSR_TIMEH         0xC81
#define CSR_INSTRETH      0xC82

/******************************************************************************************/
//
//
//
// RISC-V Instruction Set Architecture 
//
//
// The RISC-V Instruction Set Manual
// Volume I: User-Level ISA
// Document Version 2.2
//
//
//


#define XLEN 32
#define MLEN 32
#define FLEN 64
#define RV32_NOP 0x00000013

/**** OPCODE                                                                           ****/
/******************************************************************************************/
#define OPCODE_C0        0x0
#define OPCODE_C1        0x1
#define OPCODE_C2        0x2
#define OPCODE_W         0x3

#define OPCODE_OP        0x33
#define OPCODE_OP_FP     0x53
#define OPCODE_AMO       0x2F
#define OPCODE_OP_IMM    0x13
#define OPCODE_LOAD      0x03
#define OPCODE_LOAD_FP   0x07
#define OPCODE_JALR      0x67
#define OPCODE_STORE     0x23
#define OPCODE_STORE_FP  0x27
#define OPCODE_BRANCH    0x63
#define OPCODE_LUI       0x37
#define OPCODE_AUIPC     0x17
#define OPCODE_JAL       0x6F
#define OPCODE_MADD      0x43
#define OPCODE_MSUB      0x47
#define OPCODE_NMADD     0x4F
#define OPCODE_NMSUB     0x4B
#define OPCODE_MISC_M    0x0F
#define OPCODE_SYSTEM    0x73

/**** FUNCT_OP                                                                         ****/
/******************************************************************************************/
#define FUNCT3_ADD    0x0
#define FUNCT3_SLL    0x1
#define FUNCT3_SLT    0x2
#define FUNCT3_SLTU   0x3
#define FUNCT3_XOR    0x4
#define FUNCT3_SRL    0x5
#define FUNCT3_OR     0x6
#define FUNCT3_AND    0x7

#define FUNCT3_MUL    0x0
#define FUNCT3_MULH   0x1
#define FUNCT3_MULHSU 0x2
#define FUNCT3_MULHU  0x3
#define FUNCT3_DIV    0x4
#define FUNCT3_DIVU   0x5
#define FUNCT3_REM    0x6
#define FUNCT3_REMU   0x7

/**** FUNCT_STORE                                                                      ****/
/******************************************************************************************/
#define FUNCT3_SB  0x0
#define FUNCT3_SH  0x1
#define FUNCT3_SW  0x2
#define FUNCT3_SD  0x3
#define FUNCT3_FSW 0x2
#define FUNCT3_FSD 0x3

/**** FUNCT_LOAD                                                                       ****/
/******************************************************************************************/
#define FUNCT3_LB  0x0
#define FUNCT3_LH  0x1
#define FUNCT3_LW  0x2
#define FUNCT3_LD  0x3
#define FUNCT3_LBU 0x4
#define FUNCT3_LHU 0x5
#define FUNCT3_LWU 0x6
#define FUNCT3_FLW 0x2
#define FUNCT3_FLD 0x3

/**** FUNCT_BRANCH                                                                     ****/
/******************************************************************************************/
#define FUNCT3_BEQ  0x0
#define FUNCT3_BNE  0x1
#define FUNCT3_BLT  0x4
#define FUNCT3_BGE  0x5
#define FUNCT3_BLTU 0x6
#define FUNCT3_BGEU 0x7

/**** FUNCT_MISC_MEM                                                                   ****/
/******************************************************************************************/
#define FUNCT3_FENCE   0x0
#define FUNCT3_FENCE_I 0x1

/**** FUNCT_SYSTEM                                                                     ****/
/******************************************************************************************/
#define FUNCT3_PRIV    0x0
#define FUNCT3_CSRRW   0x1
#define FUNCT3_CSRRS   0x2
#define FUNCT3_CSRRC   0x3
#define FUNCT3_CSRRWI  0x5
#define FUNCT3_CSRRSI  0x6
#define FUNCT3_CSRRCI  0x7

#define FUNCT12_ECALL  0x000
#define FUNCT12_EBREAK 0x001
#define FUNCT12_ERET   0x100
#define FUNCT12_MRET   0x302
#define FUNCT12_SRET   0x102
#define FUNCT12_URET   0x002
#define FUNCT12_WFI    0x105
#define FUNCT7_SFENCE_VMA 0x09

/**** A Extension                                                                      ****/
/******************************************************************************************/
#define FUNCT5_AMO_LR   0x02
#define FUNCT5_AMO_SC   0x03
#define FUNCT5_AMO_SWAP 0x01
#define FUNCT5_AMO_ADD  0x00
#define FUNCT5_AMO_AND  0x0c
#define FUNCT5_AMO_OR   0x08
#define FUNCT5_AMO_XOR  0x04
#define FUNCT5_AMO_MIN  0x10
#define FUNCT5_AMO_MINU 0x18
#define FUNCT5_AMO_MAX  0x14
#define FUNCT5_AMO_MAXU 0x1c

#define AMO_FAILURE_CODE 1
#define AMO_SUCCESS_CODE 0

/**** Operation ID                                                                     ****/
/******************************************************************************************/
enum OPERATION_ID {
    /* RV32I */
    LUI_______,
    AUIPC_____,
    JAL_______,
    JALR______,
    BEQ_______,
    BNE_______,
    BLT_______,
    BGE_______,
    BLTU______,
    BGEU______,
    LB________,
    LH________,
    LW________,
    LBU_______,
    LHU_______,
    SB________,
    SH________,
    SW________,
    ADDI______,
    SLTI______,
    SLTIU_____,
    XORI______,
    ORI_______,
    ANDI______,
    SLLI______,
    SRLI______,
    SRAI______,
    ADD_______,
    SUB_______,
    SLL_______,
    SLT_______,
    SLTU______,
    XOR_______,
    SRL_______,
    SRA_______,
    OR________,
    AND_______,
    FENCE_____,
    FENCE_I___,
    ECALL_____,
    EBREAK____,
    CSRRW_____,
    CSRRS_____,
    CSRRC_____,
    CSRRWI____,
    CSRRSI____,
    CSRRCI____,
    /* Privileged */
    URET______,
    SRET______,
    MRET______,
    WFI_______,
    SFENCE_VMA,
    /* RV32M */
    MUL_______,
    MULH______,
    MULHSU____,
    MULHU_____,
    DIV_______,
    DIVU______,
    REM_______,
    REMU______,
    /* RV32A */
    LR_W______,
    SC_W______,
    AMOSWAP_W_,
    AMOADD_W__,
    AMOXOR_W__,
    AMOAND_W__,
    AMOOR_W___,
    AMOMIN_W__,
    AMOMAX_W__,
    AMOMINU_W_,
    AMOMAXU_W_,
    /* /\* RV32F *\/ */
    /* FLW_______, */
    /* FSW_______, */
    /* FMADD_S___, */
    /* FMSUB_S___, */
    /* FNMADD_S__, */
    /* FNMSUB_S__, */
    /* FADD_S____, */
    /* FSUB_S____, */
    /* FMUL_S____, */
    /* FDIV_S____, */
    /* FSQRT_S___, */
    /* FSGNJ_S___, */
    /* FSGNJN_S__, */
    /* FSGNJX_S__, */
    /* FMIN_S____, */
    /* FMAX_S____, */
    /* FCVT_W_S__, */
    /* FCVT_WU_S_, */
    /* FMV_X_W___, */
    /* FEQ_S_____, */
    /* FLT_S_____, */
    /* FLE_S_____, */
    /* FCLASS_S__, */
    /* FCVT_S_W__, */
    /* FCVT_S_WU_, */
    /* FMV_W_X___, */
    /* /\* RV32D *\/ */
    /* FLD_______, */
    /* FSD_______, */
    /* FMADD_D___, */
    /* FMSUB_D___, */
    /* FNMSUB_D__, */
    /* FNMADD_D__, */
    /* FADD_D____, */
    /* FSUB_D____, */
    /* FMUL_D____, */
    /* FDIV_D____, */
    /* FSQRT_D___, */
    /* FSGNJ_D___, */
    /* FSGNJN_D__, */
    /* FSGNJX_D__, */
    /* FMIN_D____, */
    /* FMAX_D____, */
    /* FCVT_S_D__, */
    /* FCVT_D_S__, */
    /* FEQ_D_____, */
    /* FLT_D_____, */
    /* FLE_D_____, */
    /* FCLASS_D__, */
    /* FCVT_W_D__, */
    /* FCVT_WU_D_, */
    /* FCVT_D_W__, */
    /* FCVT_D_WU_, */
    /* Others */
    UNKNOWN___,
    NUMOFID___
};

/******************************************************************************************/
struct QueueState {
    uint32_t Ready;
    uint32_t Notify;
    uint32_t DescLow;
    uint32_t DescHigh;
    uint32_t AvailLow;
    uint32_t AvailHigh;
    uint32_t UsedLow;
    uint32_t UsedHigh;
    uint32_t last_avail_idx; //    uint16_t last_avail_idx;
};

struct BlockRequestHeader {
    uint32_t type;
    uint32_t ioprio;
    uint64_t sector_num;
};

struct Descriptor {
    uint64_t adr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
};

#endif /* constant_h */
/******************************************************************************************/


