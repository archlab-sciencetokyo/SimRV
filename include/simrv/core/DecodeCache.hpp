/**
 * @file DecodeCache.hpp
 * @brief Fast direct-mapped instruction decode cache for simulator acceleration.
 */
#pragma once

#include <array>
#include <cstdint>

#include "simrv/Define.hpp"
#include "simrv/pipeline/DecodedInstruction.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

/**
 * @struct CachedOp
 * @brief Compact, standalone decoded instruction structure aligned to a single 64-byte cache line.
 *
 * Contains only the control signals and operands required for fast execution dispatch.
 */
struct alignas(64) CachedOp {
    Register cpc = ~Register{0};            ///< Program counter for hit verification (8 bytes)
    ImmValue imm = 0;                       ///< Sign-extended immediate value (8 bytes)
    Instruction ir = 0;                     ///< Raw instruction word (4 bytes)
    isa::OperationId op_id = isa::UNKNOWN;  ///< Internal operation identifier (4 bytes)
    isa::Opcode opcode = static_cast<isa::Opcode>(0);  ///< 7-bit RISC-V opcode (1 byte)
    RegId rd = static_cast<RegId>(0);                  ///< Destination register ID (1 byte)
    RegId rs1 = static_cast<RegId>(0);                 ///< Source register 1 ID (1 byte)
    RegId rs2 = static_cast<RegId>(0);                 ///< Source register 2 ID (1 byte)
    isa::Funct3 funct3 = static_cast<isa::Funct3>(0);  ///< 3-bit function code (1 byte)
    isa::Funct5Amo funct5 =
        static_cast<isa::Funct5Amo>(0);  ///< 5-bit Atomic/Vector function code (1 byte)
    uint8_t funct7 = 0;                  ///< R-type 7-bit function code (1 byte)
    uint16_t funct12 = 0;                ///< System/I-type 12-bit function code (2 bytes)
    uint16_t next_op_idx = 0;            ///< Pre-calculated cache index for pc + len (2 bytes)
    uint8_t len = 4;     ///< Instruction length in bytes: 2 for RVC, 4 for 32-bit (1 byte)
    bool cinsn = false;  ///< RVC indicator (1 byte)
    bool valid = false;  ///< Cache line validity flag (1 byte)

    /**
     * @brief Copy decoded instruction payload into compact cached representation.
     */
    constexpr void copy_from(const simrv::pipeline::DecodedInstruction& dec) noexcept {
        cpc = dec.cpc;
        imm = dec.imm;
        ir = dec.ir;
        op_id = dec.op_id;
        opcode = dec.opcode;
        rd = dec.rd;
        rs1 = dec.rs1;
        rs2 = dec.rs2;
        funct3 = dec.funct3;
        funct5 = dec.funct5;
        funct7 = static_cast<uint8_t>(dec.funct7);
        funct12 = static_cast<uint16_t>(dec.funct12);
        cinsn = (dec.cinsn != 0);
        len = dec.cinsn ? 2 : 4;
        const Register next_pc = cpc + len;
        next_op_idx =
            static_cast<uint16_t>(((next_pc >> 1) ^ (next_pc >> 2) ^ (next_pc >> 10)) & 1023);
        valid = true;
    }

    /**
     * @brief Populate pipeline context from cached op during debug/fallback handling.
     */
    constexpr void copy_to(simrv::pipeline::DecodedInstruction& dec) const noexcept {
        dec.cpc = cpc;
        dec.imm = imm;
        dec.ir = ir;
        dec.op_id = op_id;
        dec.opcode = opcode;
        dec.rd = rd;
        dec.rs1 = rs1;
        dec.rs2 = rs2;
        dec.funct3 = funct3;
        dec.funct5 = funct5;
        dec.funct7 = funct7;
        dec.funct12 = funct12;
        dec.cinsn = cinsn ? 1 : 0;
        dec.pending_exception = std::nullopt;
        dec.pending_tval = 0;
    }
};

/**
 * @class DecodeCache
 * @brief Direct-mapped cache for pre-decoded instructions to bypass fetch/decode stages.
 *
 * Indexed by a bit-mixing XOR hash `((pc >> 1) ^ (pc >> 2) ^ (pc >> 10)) & kCacheMask`
 * to guarantee 100% set utilization for both 16-bit RVC and 32-bit aligned instructions.
 * Caches 1024 operations (64KB total) fitting inside CPU L1D/L2 cache.
 */
class DecodeCache {
   public:
    static constexpr size_t kCacheSize = 1024;  ///< Cache capacity in entries (64KB total layout)
    static constexpr size_t kCacheMask = kCacheSize - 1;

    /**
     * @brief Invalidate all entries in the decode cache.
     */
    void flush() {
        for (auto& entry : cache_) {
            entry.valid = false;
            entry.cpc = ~Register{0};
        }
    }

    /**
     * @brief Calculate cache set index for a program counter.
     */
    [[nodiscard]] static constexpr inline auto calc_index(Register pc) noexcept -> size_t {
        return ((pc >> 1) ^ (pc >> 2) ^ (pc >> 10)) & kCacheMask;
    }

    /**
     * @brief Fast lookup at a pre-calculated cache set index.
     */
    [[nodiscard]] inline auto lookup_at(size_t index, Register pc) noexcept -> CachedOp* {
        auto* entry = &cache_[index];
        if (simrv::compiler::likely(entry->cpc == pc)) {
            return entry;
        }
        return nullptr;
    }

    /**
     * @brief Lookup pre-decoded operation for given program counter.
     */
    [[nodiscard]] inline auto lookup(Register pc) noexcept -> CachedOp* {
        size_t index = calc_index(pc);
        auto* entry = &cache_[index];
        if (simrv::compiler::likely(entry->cpc == pc)) {
            return entry;
        }
        return nullptr;
    }

    /**
     * @brief Insert pre-decoded operation into decode cache.
     */
    inline void insert(Register pc, const simrv::pipeline::DecodedInstruction& dec) {
        size_t index = calc_index(pc);
        cache_[index].copy_from(dec);
        cache_[index].cpc = pc;
        const Register next_pc = pc + cache_[index].len;
        cache_[index].next_op_idx =
            static_cast<uint16_t>(((next_pc >> 1) ^ (next_pc >> 2) ^ (next_pc >> 10)) & kCacheMask);
    }

    inline void insert(Register pc, const CachedOp& op) {
        size_t index = calc_index(pc);
        cache_[index] = op;
        cache_[index].cpc = pc;
        const Register next_pc = pc + op.len;
        cache_[index].next_op_idx =
            static_cast<uint16_t>(((next_pc >> 1) ^ (next_pc >> 2) ^ (next_pc >> 10)) & kCacheMask);
        cache_[index].valid = true;
    }

   private:
    alignas(64) std::array<CachedOp, kCacheSize> cache_{};
};

}  // namespace simrv::core
