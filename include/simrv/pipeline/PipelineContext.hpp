#pragma once

#include <expected>
#include <optional>
#include <simrv/CompilerHints.hpp>
#include <simrv/pipeline/DecodedInstruction.hpp>

namespace simrv::pipeline {

/**
 * @struct StageError
 * @brief Represents precise pipeline stage exceptions.
 */
struct StageError {
    ExceptionCode code;
    simrv::xlen::CsrValue tval = 0;
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
    simrv::xlen::PhysAddr padr1{0};
    simrv::xlen::PhysAddr padr2{0};

    // OF stage operands
    simrv::xlen::Register rrs1 = 0;
    simrv::xlen::Register rrs2 = 0;
    simrv::xlen::CsrValue rcsr = 0;
    simrv::xlen::CsrValue rcsr_write =
        0;  ///< RMW base, excluding hardware-only mip.SEIP contribution.

    // EX1 stage results and controls
    bool tkn = false;
    simrv::xlen::VirtAddr jmp_pc{0};
    simrv::xlen::Address mem_addr = 0;
    simrv::xlen::PhysAddr mem_paddr{0};
    simrv::xlen::Register wb_data = 0;
    simrv::xlen::CsrValue wb_data_csr = 0;

    // LD/EX2 stage memory data
    simrv::xlen::Register mem_rdata = 0;
    simrv::xlen::Register mem_wdata = 0;
    bool memory_load_complete = false;
    bool sc_reservation_checked = false;
    bool sc_reservation_succeeded = false;

    // FP datapath temporaries
    simrv::xlen::FloatingRegister fp_mem_rdata = 0;
    simrv::xlen::FloatingRegister fp_mem_wdata = 0;
    simrv::xlen::FloatingRegister fp_wb_data = 0;
    bool fp_wb_enable = false;
    bool int_wb_from_fp = false;

    bool tlb_miss = false;
};

}  // namespace simrv::pipeline
