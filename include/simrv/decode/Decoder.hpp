#pragma once

#include <array>
#include <cstdint>
#include <string_view>

#include "simrv/Define.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::decode {

/// Standard RISC-V Major Opcodes (lower 7 bits)
enum class Opcode : uint32_t {
    kLoad = 0x03,
    kLoadFp = 0x07,
    kCustom0 = 0x0B,
    kMiscMem = 0x0F,
    kOpImm = 0x13,
    kAuipc = 0x17,
    kOpImm32 = 0x1B,  // RV64
    kStore = 0x23,
    kStoreFp = 0x27,
    kCustom1 = 0x2B,
    kAmo = 0x2F,
    kOp = 0x33,
    kLui = 0x37,
    kOp32 = 0x3B,  // RV64
    kMadd = 0x43,
    kMsub = 0x47,
    kNmsub = 0x4B,
    kNmadd = 0x4F,
    kOpFp = 0x53,
    kReserved0 = 0x57,
    kCustom2 = 0x5B,
    kBranch = 0x63,
    kJalr = 0x67,
    kReserved1 = 0x6B,
    kJal = 0x6F,
    kSystem = 0x73,
    kCustom3 = 0x7B
};

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
    // Standard R-Type & Base Fields
    // =========================================================================

    [[nodiscard]] constexpr auto rd() const -> RegId { return static_cast<RegId>((inst_ >> 7) & 0x1F); }
    [[nodiscard]] constexpr auto funct3() const -> Funct3 { return static_cast<Funct3>((inst_ >> 12) & 0x7); }
    [[nodiscard]] constexpr auto rs1() const -> RegId { return static_cast<RegId>((inst_ >> 15) & 0x1F); }
    [[nodiscard]] constexpr auto rs2() const -> RegId { return static_cast<RegId>((inst_ >> 20) & 0x1F); }
    [[nodiscard]] constexpr auto funct7() const -> uint32_t { return (inst_ >> 25) & 0x7F; }
    [[nodiscard]] constexpr auto rs3() const -> RegId {
        return static_cast<RegId>((inst_ >> 27) & 0x1F);
    }  // For FP FMA

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

    [[nodiscard]] constexpr auto csr() const -> uint32_t { return (inst_ >> 20) & 0xFFF; }
    [[nodiscard]] constexpr auto zimm() const -> uint32_t {
        return std::to_underlying(rs1());
    }  // CSR uimm is in the rs1 field

    // =========================================================================
    // Compressed Instruction Decoding Base Hooks (C Extension)
    // =========================================================================

    [[nodiscard]] constexpr auto c_op() const -> uint32_t { return (inst_ >> 13) & 0x7; }
    [[nodiscard]] constexpr auto c_funct4() const -> uint32_t { return (inst_ >> 12) & 0xF; }

    // Standard C register mappings
    [[nodiscard]] constexpr auto c_rs1_rd() const -> RegId { return static_cast<RegId>((inst_ >> 7) & 0x1F); }
    [[nodiscard]] constexpr auto c_rs2() const -> RegId { return static_cast<RegId>((inst_ >> 2) & 0x1F); }

    // Compressed 'prime' register mappings (x8-x15 limiters)
    [[nodiscard]] constexpr auto c_rs1_rd_p() const -> RegId { return static_cast<RegId>(8 + ((inst_ >> 7) & 0x7)); }
    [[nodiscard]] constexpr auto c_rs2_p() const -> RegId { return static_cast<RegId>(8 + ((inst_ >> 2) & 0x7)); }

   private:
    uint32_t inst_;
};

// Instruction-mix identification helper (currently stubbed)
auto decoder(Instruction ir) -> OperationId;

// Expand 16-bit C-extension instructions to canonical 32-bit forms
auto decompressInstruction(Instruction ir) -> Instruction;

/// String mapping for instruction mix profiling
extern const std::array<std::string_view, static_cast<size_t>(OperationIdCount)> OPERATION_NAME;

}  // namespace simrv::decode
