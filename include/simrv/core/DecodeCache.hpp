/**
 * @file DecodeCache.hpp
 * @brief Fast direct-mapped instruction decode cache for simulator acceleration.
 */
#pragma once

#include <array>

#include "simrv/Define.hpp"
#include "simrv/pipeline/DecodedInstruction.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

/**
 * @struct CachedOp
 * @brief Compact decoded instruction payload used by the fast execution path.
 */
struct CachedOp {
    VirtAddr cpc{0};
    ImmValue imm = 0;
    Instruction ir = 0;
    Instruction ir_org = 0;
    Instruction cinsn = 0;
    isa::OperationId op_id = isa::UNKNOWN;
    Funct12 funct12 = 0;
    isa::Opcode opcode = static_cast<isa::Opcode>(0);
    RegId rd = RegId::Zero;
    RegId rs1 = RegId::Zero;
    RegId rs2 = RegId::Zero;
    isa::Funct3 funct3 = static_cast<isa::Funct3>(0);
    isa::Funct5Amo funct5 = static_cast<isa::Funct5Amo>(0);
    Funct7 funct7 = 0;
    uint8_t len = 4;
    bool valid = false;

    constexpr void copy_from(const simrv::pipeline::DecodedInstruction& decoded) noexcept {
        cpc = decoded.cpc;
        imm = decoded.imm;
        ir = decoded.ir;
        ir_org = decoded.ir_org;
        cinsn = decoded.cinsn;
        op_id = decoded.op_id;
        funct12 = decoded.funct12;
        opcode = decoded.opcode;
        rd = decoded.rd;
        rs1 = decoded.rs1;
        rs2 = decoded.rs2;
        funct3 = decoded.funct3;
        funct5 = decoded.funct5;
        funct7 = decoded.funct7;
        len = decoded.cinsn ? 2 : 4;
    }

    constexpr void copy_to(simrv::pipeline::DecodedInstruction& decoded) const noexcept {
        decoded.cpc = cpc;
        decoded.imm = imm;
        decoded.pending_tval = 0;
        decoded.pending_exception = std::nullopt;
        decoded.ir = ir;
        decoded.ir_org = ir_org;
        decoded.cinsn = cinsn;
        decoded.op_id = op_id;
        decoded.funct7 = funct7;
        decoded.funct12 = funct12;
        decoded.opcode = opcode;
        decoded.rd = rd;
        decoded.rs1 = rs1;
        decoded.rs2 = rs2;
        decoded.funct3 = funct3;
        decoded.funct5 = funct5;
    }
};

/**
 * @class DecodeCache
 * @brief 2-way set-associative cache for pre-decoded instructions to bypass fetch/decode stages.
 *
 * Indexed by XOR-hash of PC (`((pc >> 1) ^ (pc >> 13)) & kSetMask`), caching 4096 decoded
 * instructions (2048 2-way sets) with 64-byte set alignment for cache locality and 1-bit
 * round-robin replacement. Lookup hits are read-only so the hottest path does not dirty cache
 * metadata.
 */
class DecodeCache {
   public:
    static constexpr size_t kNumSets = 2048;            ///< Number of 2-way associative sets
    static constexpr size_t kSetMask = kNumSets - 1;    ///< Bitmask for set index calculation
    static constexpr size_t kCacheSize = kNumSets * 2;  ///< Total cached instruction entries (4096)

    struct alignas(64) CacheSet {
        std::array<CachedOp, 2> ways{};
        uint8_t next_victim = 0;
    };

    /**
     * @brief Invalidate all entries in the decode cache.
     */
    void flush() {
        for (auto& set : sets_) {
            set.ways[0].valid = false;
            set.ways[0].cpc = VirtAddr{~Register{0}};
            set.ways[1].valid = false;
            set.ways[1].cpc = VirtAddr{~Register{0}};
            set.next_victim = 0;
        }
    }

    /**
     * @brief Calculate the cache set index for a given program counter with bit-mixed XOR hashing.
     * @param pc Program counter address.
     * @return Cache set index in range [0, kNumSets - 1].
     */
    [[nodiscard]] static constexpr inline auto calc_set(Register pc) noexcept -> size_t {
        return ((pc >> 1) ^ (pc >> 13)) & kSetMask;
    }
    [[nodiscard]] static constexpr inline auto calc_set(VirtAddr pc) noexcept -> size_t {
        return ((pc.raw() >> 1) ^ (pc.raw() >> 13)) & kSetMask;
    }

    /**
     * @brief Lookup pre-decoded operation for given program counter.
     * @param pc Program counter address.
     * @return Pointer to CachedOp if hit, or nullptr on miss.
     */
    [[nodiscard]] inline auto lookup(Register pc) noexcept -> CachedOp* {
        const size_t set_idx = calc_set(pc);
        auto& set = sets_[set_idx];
        if (simrv::compiler::likely(set.ways[0].valid && set.ways[0].cpc == pc)) {
            return &set.ways[0];
        }
        if (set.ways[1].valid && set.ways[1].cpc == pc) {
            return &set.ways[1];
        }
        return nullptr;
    }
    [[nodiscard]] inline auto lookup(VirtAddr pc) noexcept -> CachedOp* { return lookup(pc.raw()); }

    /**
     * @brief Insert pre-decoded operation into the decode cache using LRU replacement.
     * @param pc Program counter address.
     * @param op Decoded instruction payload.
     */
    inline void insert(Register pc, const CachedOp& op) {
        const size_t set_idx = calc_set(pc);
        auto& set = sets_[set_idx];
        const uint8_t way = !set.ways[0].valid ? 0 : (!set.ways[1].valid ? 1 : set.next_victim);
        auto& entry = set.ways[way];
        entry = op;
        entry.cpc = VirtAddr{pc};
        entry.len = op.cinsn ? 2 : 4;
        entry.valid = true;
        set.next_victim = static_cast<uint8_t>(1U - way);
    }
    inline void insert(VirtAddr pc, const CachedOp& op) { insert(pc.raw(), op); }

   private:
    std::array<CacheSet, kNumSets> sets_{};
};

static_assert(sizeof(DecodeCache::CacheSet) <= 128);
static_assert(sizeof(DecodeCache) <= 256 * 1024);

}  // namespace simrv::core
