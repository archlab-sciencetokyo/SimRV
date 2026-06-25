/**
 * @file OperationId.hpp
 * @brief Unified RISC-V instruction operation identifiers.
 */
#pragma once

#include <cstdint>
#include <cstddef>

namespace simrv::isa {

enum OperationId : uint8_t {
    /* RV32I */
    LUI,
    AUIPC,
    JAL,
    JALR,
    BEQ,
    BNE,
    BLT,
    BGE,
    BLTU,
    BGEU,
    LB,
    LH,
    LW,
    LD,
    LBU,
    LHU,
    LWU,
    SB,
    SH,
    SW,
    SD,
    ADDI,
    SLTI,
    SLTIU,
    XORI,
    ORI,
    ANDI,
    SLLI,
    SRLI,
    SRAI,
    ADDIW,
    SLLIW,
    SRLIW,
    SRAIW,
    ADD,
    SUB,
    SLL,
    SLT,
    SLTU,
    XOR,
    SRL,
    SRA,
    OR,
    AND,
    ADDW,
    SUBW,
    SLLW,
    SRLW,
    SRAW,
    FENCE,
    FENCE_I,
    ECALL,
    EBREAK,
    CSRRW,
    CSRRS,
    CSRRC,
    CSRRWI,
    CSRRSI,
    CSRRCI,
    /* Privileged */
    URET,
    SRET,
    MRET,
    WFI,
    SFENCE_VMA,
    /* RV32M */
    MUL,
    MULH,
    MULHSU,
    MULHU,
    DIV,
    DIVU,
    REM,
    REMU,
    MULW,
    DIVW,
    DIVUW,
    REMW,
    REMUW,
    /* RV32A */
    LR_W,
    SC_W,
    AMOSWAP_W,
    AMOADD_W,
    AMOXOR_W,
    AMOAND_W,
    AMOOR_W,
    AMOMIN_W,
    AMOMAX_W,
    AMOMINU_W,
    AMOMAXU_W,
    /* RV64A */
    LR_D,
    SC_D,
    AMOSWAP_D,
    AMOADD_D,
    AMOXOR_D,
    AMOAND_D,
    AMOOR_D,
    AMOMIN_D,
    AMOMAX_D,
    AMOMINU_D,
    AMOMAXU_D,
    /* RV32F */
    FLW,
    FSW,
    FMADD_S,
    FMSUB_S,
    FNMADD_S,
    FNMSUB_S,
    FADD_S,
    FSUB_S,
    FMUL_S,
    FDIV_S,
    FSQRT_S,
    FSGNJ_S,
    FSGNJN_S,
    FSGNJX_S,
    FMIN_S,
    FMAX_S,
    FCVT_W_S,
    FCVT_WU_S,
    FMV_X_W,
    FEQ_S,
    FLT_S,
    FLE_S,
    FCLASS_S,
    FCVT_S_W,
    FCVT_S_WU,
    FMV_W_X,
    FCVT_L_S,
    FCVT_LU_S,
    FCVT_S_L,
    FCVT_S_LU,
    /* RV32D */
    FLD,
    FSD,
    FMADD_D,
    FMSUB_D,
    FNMSUB_D,
    FNMADD_D,
    FADD_D,
    FSUB_D,
    FMUL_D,
    FDIV_D,
    FSQRT_D,
    FSGNJ_D,
    FSGNJN_D,
    FSGNJX_D,
    FMIN_D,
    FMAX_D,
    FCVT_S_D,
    FCVT_D_S,
    FEQ_D,
    FLT_D,
    FLE_D,
    FCLASS_D,
    FCVT_W_D,
    FCVT_WU_D,
    FCVT_D_W,
    FCVT_D_WU,
    FMV_X_D,
    FMV_D_X,
    FCVT_L_D,
    FCVT_LU_D,
    FCVT_D_L,
    FCVT_D_LU,
    /* Others */
    UNKNOWN,
    OperationIdCount
};

constexpr OperationId kOpRangeRv32iBegin = LUI;
constexpr OperationId kOpRangeRv32iEnd = CSRRCI;

constexpr OperationId kOpRangePrivBegin = URET;
constexpr OperationId kOpRangePrivEnd = SFENCE_VMA;

constexpr OperationId kOpRangeRv32mBegin = MUL;
constexpr OperationId kOpRangeRv32mEnd = REMU;

constexpr OperationId kOpRangeRv32aBegin = LR_W;
constexpr OperationId kOpRangeRv32aEnd = AMOMAXU_W;

constexpr OperationId kOpRangeRv32fBegin = FLW;
constexpr OperationId kOpRangeRv32fEnd = FCVT_S_LU;

constexpr OperationId kOpRangeRv32dBegin = FLD;
constexpr OperationId kOpRangeRv32dEnd = FCVT_D_LU;

constexpr size_t kOperationIdCount = static_cast<size_t>(OperationIdCount);

} // namespace simrv::isa
