#pragma once

#include <simrv/Define.hpp>

namespace simrv::core {

/**
 * @struct PipelineContext
 * @brief Carries decoded instruction state and stage intermediates.
 *
 * Fields are grouped by pipeline stage progression and reused during a
 * single machine cycle.
 */
struct PipelineContext {
    // IF stage transient values
    Address padr1 = 0;
    Address padr2 = 0;
    Register cpc = 0;
    Instruction ir_org = 0;

    // CVT stage transient values
    Instruction cinsn = 0;
    Instruction ir = 0;

    // ID stage decoded fields
    Word opcode = 0;
    Word rd = 0;
    Word rs1 = 0;
    Word rs2 = 0;
    Word funct3 = 0;
    Word funct5 = 0;
    Word funct7 = 0;
    Word funct12 = 0;
    Word imm = 0;

    // OF stage operands
    Register rrs1 = 0;
    Register rrs2 = 0;
    CSRValue rcsr = 0;

    // EX1 stage results and controls
    Word tkn = 0;
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
    Word fp_wb_enable = 0;
    Word int_wb_from_fp = 0;

    TrapCause pending_exception = simrv::xlen::kWordAllOnes;
    CSRValue pending_tval = 0;
};

}  // namespace simrv::core
