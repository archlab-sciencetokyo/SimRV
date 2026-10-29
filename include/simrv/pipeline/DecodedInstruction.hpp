/**
 * @file DecodedInstruction.hpp
 * @brief Fully unpacked RISC-V decoded instruction structure for execute and pipeline stages.
 */
#pragma once

#include <optional>

namespace simrv::pipeline {

/**
 * @struct DependencyTraits
 * @brief Compact precomputed operand and pipeline dependency flags.
 */
struct DependencyTraits {
    bool reads_rs1_int : 1 = false;
    bool reads_rs2_int : 1 = false;
    bool reads_rs1_fp : 1 = false;
    bool reads_rs2_fp : 1 = false;
    bool reads_rs3_fp : 1 = false;
    bool writes_int : 1 = false;
    bool writes_fp : 1 = false;
    bool writes_vec : 1 = false;
    bool is_mem_load : 1 = false;
    bool is_mem_store : 1 = false;
    bool is_control : 1 = false;
    bool is_serializing : 1 = false;
    bool is_cfu : 1 = false;
};

}  // namespace simrv::pipeline

#include "simrv/isa/Amo.hpp"
#include "simrv/isa/InstructionFields.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/isa/TrapTypes.hpp"
#include "simrv/xlen/AddressTypes.hpp"
#include "simrv/xlen/RegisterTypes.hpp"

namespace simrv::pipeline {

/**
 * @struct DecodedInstruction
 * @brief Represents a fully decoded RISC-V instruction with pre-extracted operands and control
 * signals.
 */
struct DecodedInstruction {
    simrv::xlen::VirtAddr cpc{0};   ///< Current Program Counter of this instruction
    simrv::xlen::ImmValue imm = 0;  ///< Sign-extended immediate value
    simrv::xlen::CsrValue pending_tval =
        0;  ///< Trap value / faulting address for pending exception
    simrv::xlen::CsrValue pending_htval = 0;  ///< Guest physical address for a guest-page fault
    simrv::xlen::CsrValue pending_htinst = 0; ///< Trap pseudoinstruction for an implicit VS walk fault
    std::optional<ExceptionCode>
        pending_exception;  ///< Pending exception code (if decode/fetch raised exception)

    simrv::xlen::Instruction ir = 0;  ///< Raw 32-bit (or decompressed) instruction word
    simrv::xlen::Instruction ir_org =
        0;  ///< Original uncompressed 16-bit or 32-bit instruction word
    simrv::xlen::Instruction cinsn = 0;     ///< Compressed instruction indicator (non-zero for RVC)
    isa::OperationId op_id = isa::ILLEGAL;  ///< Internal operation identifier
    simrv::xlen::Funct7 funct7 = 0;         ///< R-type 7-bit function code
    simrv::xlen::Funct12 funct12 = 0;       ///< System/I-type 12-bit function code

    isa::Opcode opcode = static_cast<isa::Opcode>(0);             ///< 7-bit RISC-V opcode
    simrv::xlen::RegId rd = static_cast<simrv::xlen::RegId>(0);   ///< Destination register ID
    simrv::xlen::RegId rs1 = static_cast<simrv::xlen::RegId>(0);  ///< Source register 1 ID
    simrv::xlen::RegId rs2 = static_cast<simrv::xlen::RegId>(0);  ///< Source register 2 ID
    isa::Funct3 funct3 = static_cast<isa::Funct3>(0);             ///< 3-bit function code
    isa::Funct5Amo funct5 = static_cast<isa::Funct5Amo>(0);  ///< 5-bit Atomic/Vector function code
    DependencyTraits traits{};
    simrv::xlen::Counter trace_sequence =
        0;  ///< Dynamic ID used only by optional register tracing.
    simrv::xlen::FloatingRegister fp_old_value =
        0;  ///< Destination value captured before architectural write.

    /**
     * @brief Copy decoded instruction content.
     * @param other Source decoded instruction.
     */
    constexpr void copy_from(const DecodedInstruction& other) { *this = other; }
};

}  // namespace simrv::pipeline
