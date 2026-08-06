/**
 * @file DecodedInstruction.hpp
 * @brief Fully unpacked RISC-V decoded instruction structure for execute and pipeline stages.
 */
#pragma once

#include <optional>

#include "simrv/Define.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::pipeline {

/**
 * @struct PendingException
 * @brief Lightweight drop-in replacement for std::optional<ExceptionCode>.
 *
 * Provides the same API surface (has_value, operator*, value_or, assignment
 * from ExceptionCode and std::nullopt_t) but avoids the overhead of
 * std::optional in this performance-critical struct.
 */
struct PendingException {
    ExceptionCode code_{};
    bool valid_ = false;

    constexpr PendingException() noexcept = default;
    constexpr PendingException(std::nullopt_t) noexcept : valid_(false) {}            // NOLINT
    constexpr PendingException(ExceptionCode c) noexcept : code_(c), valid_(true) {}  // NOLINT

    constexpr auto operator=(std::nullopt_t) noexcept -> PendingException& {
        valid_ = false;
        return *this;
    }
    constexpr auto operator=(ExceptionCode c) noexcept -> PendingException& {
        code_ = c;
        valid_ = true;
        return *this;
    }

    [[nodiscard]] constexpr auto has_value() const noexcept -> bool { return valid_; }
    constexpr explicit operator bool() const noexcept { return valid_; }
    [[nodiscard]] constexpr auto operator*() const noexcept -> ExceptionCode { return code_; }
    [[nodiscard]] constexpr auto value() const noexcept -> ExceptionCode { return code_; }
    [[nodiscard]] constexpr auto value_or(ExceptionCode fallback) const noexcept -> ExceptionCode {
        return valid_ ? code_ : fallback;
    }
};

/**
 * @struct DecodedInstruction
 * @brief Represents a fully decoded RISC-V instruction with pre-extracted operands and control
 * signals.
 */
struct DecodedInstruction {
    Register cpc = 0;                    ///< Current Program Counter of this instruction
    ImmValue imm = 0;                    ///< Sign-extended immediate value
    CSRValue pending_tval = 0;           ///< Trap value / faulting address for pending exception
    PendingException pending_exception;  ///< Pending exception (lightweight, no std::optional)

    Instruction ir = 0;      ///< Raw 32-bit (or decompressed) instruction word
    Instruction ir_org = 0;  ///< Original uncompressed 16-bit or 32-bit instruction word
    Instruction cinsn = 0;   ///< Compressed instruction indicator (non-zero for RVC)
    isa::OperationId op_id = isa::UNKNOWN;  ///< Internal operation identifier
    Word funct7 = 0;                        ///< R-type 7-bit function code
    Word funct12 = 0;                       ///< System/I-type 12-bit function code

    isa::Opcode opcode = static_cast<isa::Opcode>(0);        ///< 7-bit RISC-V opcode
    RegId rd = static_cast<RegId>(0);                        ///< Destination register ID
    RegId rs1 = static_cast<RegId>(0);                       ///< Source register 1 ID
    RegId rs2 = static_cast<RegId>(0);                       ///< Source register 2 ID
    isa::Funct3 funct3 = static_cast<isa::Funct3>(0);        ///< 3-bit function code
    isa::Funct5Amo funct5 = static_cast<isa::Funct5Amo>(0);  ///< 5-bit Atomic/Vector function code

    /**
     * @brief Copy decoded instruction content.
     * @param other Source decoded instruction.
     */
    constexpr void copy_from(const DecodedInstruction& other) { *this = other; }
};

}  // namespace simrv::pipeline
