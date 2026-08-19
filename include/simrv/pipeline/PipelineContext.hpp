#pragma once

#include <expected>
#include <optional>
#include <simrv/Define.hpp>
#include <simrv/pipeline/DecodedInstruction.hpp>

namespace simrv::pipeline {

/**
 * @struct StageError
 * @brief Represents precise pipeline stage exceptions.
 */
struct StageError {
    ExceptionCode code;
    CSRValue tval = 0;
};

/**
 * @struct PipelineContext
 * @brief Carries decoded instruction state and stage intermediates.
 *
 * Fields are grouped by pipeline stage progression and reused during a
 * single machine cycle.
 */
struct PipelineContext : public DecodedInstruction {
    // IF stage transient values
    Address padr1 = 0;
    Address padr2 = 0;

    // OF stage operands
    Register rrs1 = 0;
    Register rrs2 = 0;
    CSRValue rcsr = 0;
    CSRValue rcsr_write = 0;  ///< RMW base, excluding hardware-only mip.SEIP contribution.

    // EX1 stage results and controls
    bool tkn = false;
    Register jmp_pc = 0;
    Address mem_addr = 0;
    Register wb_data = 0;
    CSRValue wb_data_csr = 0;

    // LD/EX2 stage memory data
    Register mem_rdata = 0;
    Register mem_wdata = 0;

    // FP datapath temporaries
    FloatingRegister fp_mem_rdata = 0;
    FloatingRegister fp_mem_wdata = 0;
    FloatingRegister fp_wb_data = 0;
    bool fp_wb_enable = false;
    bool int_wb_from_fp = false;

    bool tlb_miss = false;
};

}  // namespace simrv::pipeline
