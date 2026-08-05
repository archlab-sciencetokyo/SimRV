#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "simrv/Define.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::pipeline {

using Opcode = simrv::isa::Opcode;

class Decoder {
   public:
    explicit constexpr Decoder(uint32_t inst) : inst_(inst) {}

    /// Get raw instruction word
    [[nodiscard]] constexpr auto raw() const -> uint32_t { return inst_; }

    /// Check if the instruction is compressed (16-bit)
    [[nodiscard]] constexpr auto is_compressed() const -> bool { return (inst_ & 0x3) != 0x3; }

    /// Extract standard 7-bit opcode
    [[nodiscard]] constexpr auto opcode() const -> Opcode {
        return static_cast<Opcode>(inst_ & 0x7F);
    }

    // =========================================================================
    // =========================================================================
    // Standard R-Type & Base Fields
    // =========================================================================

    /// Extract destination register specifier (rd)
    [[nodiscard]] constexpr auto rd() const -> RegId {
        return static_cast<RegId>((inst_ >> 7) & 0x1F);
    }

    /// Extract 3-bit function field (funct3)
    [[nodiscard]] constexpr auto funct3() const -> isa::Funct3 {
        return static_cast<isa::Funct3>((inst_ >> 12) & 0x7);
    }

    /// Extract first source register specifier (rs1)
    [[nodiscard]] constexpr auto rs1() const -> RegId {
        return static_cast<RegId>((inst_ >> 15) & 0x1F);
    }

    /// Extract second source register specifier (rs2)
    [[nodiscard]] constexpr auto rs2() const -> RegId {
        return static_cast<RegId>((inst_ >> 20) & 0x1F);
    }

    /// Extract 7-bit function field (funct7)
    [[nodiscard]] constexpr auto funct7() const -> uint32_t { return (inst_ >> 25) & 0x7F; }

    /// Extract third source register specifier (rs3, used for floating-point FMA)
    [[nodiscard]] constexpr auto rs3() const -> RegId {
        return static_cast<RegId>((inst_ >> 27) & 0x1F);
    }

    // =========================================================================
    // Standard Immediate Decoding
    // (Returns 32-bit sign-extended format; implicit widening handles RV64 XLENs)
    // =========================================================================

    /// I-Type Immediate
    [[nodiscard]] constexpr auto imm_i() const -> int32_t {
        // C++20 guarantees arithmetic right shift uses two's complement sign-extension
        return static_cast<int32_t>(inst_) >> 20;
    }

    /// S-Type Immediate
    [[nodiscard]] constexpr auto imm_s() const -> int32_t {
        return (static_cast<int32_t>(inst_ & 0xFE000000) >> 20) | ((inst_ >> 7) & 0x1F);
    }

    /// B-Type Immediate
    [[nodiscard]] constexpr auto imm_b() const -> int32_t {
        return (static_cast<int32_t>(inst_ & 0x80000000) >> 19) | ((inst_ & 0x7E000000) >> 20) |
               ((inst_ & 0x00000F00) >> 7) | ((inst_ & 0x00000080) << 4);
    }

    /// U-Type Immediate
    [[nodiscard]] constexpr auto imm_u() const -> int32_t {
        return static_cast<int32_t>(inst_ & 0xFFFFF000);
    }

    /// J-Type Immediate
    [[nodiscard]] constexpr auto imm_j() const -> int32_t {
        return (static_cast<int32_t>(inst_ & 0x80000000) >> 11) | (inst_ & 0x000FF000) |
               ((inst_ & 0x00100000) >> 9) | ((inst_ & 0x7FE00000) >> 20);
    }

    // =========================================================================
    // CSR & System Instructions
    // =========================================================================

    /// Extract 12-bit CSR address field
    [[nodiscard]] constexpr auto csr() const -> uint32_t { return (inst_ >> 20) & 0xFFF; }

    /// Extract zero-extended 5-bit immediate field stored in rs1 for CSR imm instructions
    [[nodiscard]] constexpr auto zimm() const -> uint32_t {
        return std::to_underlying(rs1());
    }

    // =========================================================================
    // Compressed Instruction Decoding Base Hooks (C Extension)
    // =========================================================================

    /// Extract compressed instruction opcode / funct3 field (bits 15-13)
    [[nodiscard]] constexpr auto c_op() const -> uint32_t { return (inst_ >> 13) & 0x7; }

    /// Extract compressed instruction 4-bit funct field (bits 15-12)
    [[nodiscard]] constexpr auto c_funct4() const -> uint32_t { return (inst_ >> 12) & 0xF; }

    /// Standard C register mapping for rs1 / rd (bits 11-7)
    [[nodiscard]] constexpr auto c_rs1_rd() const -> RegId {
        return static_cast<RegId>((inst_ >> 7) & 0x1F);
    }

    /// Standard C register mapping for rs2 (bits 6-2)
    [[nodiscard]] constexpr auto c_rs2() const -> RegId {
        return static_cast<RegId>((inst_ >> 2) & 0x1F);
    }

    /// Compressed prime register mapping for rs1 / rd (maps to x8-x15)
    [[nodiscard]] constexpr auto c_rs1_rd_p() const -> RegId {
        return static_cast<RegId>(8 + ((inst_ >> 7) & 0x7));
    }

    /// Compressed prime register mapping for rs2 (maps to x8-x15)
    [[nodiscard]] constexpr auto c_rs2_p() const -> RegId {
        return static_cast<RegId>(8 + ((inst_ >> 2) & 0x7));
    }

   private:
    uint32_t inst_;
};

/// Decode instruction word to internal OperationId enum
auto decoder(Instruction ir) -> isa::OperationId;

/// Expand 16-bit C-extension instructions to canonical 32-bit forms
auto decompressInstruction(Instruction ir, bool is_rv64) -> Instruction;

/// String mapping for instruction mix profiling
extern const std::array<std::string_view, static_cast<size_t>(isa::OperationIdCount)>
    OPERATION_NAME;

}  // namespace simrv::pipeline
