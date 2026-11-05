/**
 * @file BlockCache.hpp
 * @brief Basic block execution cache for accelerating straight-line instruction sequences.
 */
#pragma once

#include <array>
#include <cstdint>

#include "simrv/CompilerHints.hpp"
#include "simrv/core/DecodeCache.hpp"
#include "simrv/xlen/AddressTypes.hpp"
#include "simrv/xlen/RegisterTypes.hpp"

namespace simrv::core {

static constexpr size_t kMaxBlockOps = 16;
static constexpr size_t kNumBlockSets = 2048;
static constexpr size_t kBlockSetMask = kNumBlockSets - 1;

/**
 * @struct CompactOp
 * @brief High-density 16-byte micro-op structure for maximum L1 data cache locality.
 */
struct alignas(8) CompactOp {
    simrv::xlen::Register imm{0};   ///< 64-bit immediate or precomputed AUIPC / branch target
    FastOp fast_op{FastOp::Other};  ///< FastOp tag
    uint8_t rd{0};                  ///< Destination register index (0..32, 32 = discard)
    uint8_t rs1{0};                 ///< Source 1 register index (0..32)
    uint8_t rs2{0};                 ///< Source 2 register index (0..32)
    uint8_t op_len{4};              ///< Guest instruction length in bytes (2 or 4)
    uint8_t is_compressed{0};       ///< 1 if compressed 16-bit instruction, else 0
    uint16_t pad{0};
};
static_assert(sizeof(CompactOp) == 16);

/**
 * @struct BasicBlock
 * @brief Pre-coalesced straight-line instruction sequence ending in a branch, jump, or fallback.
 */
struct alignas(64) BasicBlock {
    simrv::xlen::VirtAddr pc{~simrv::xlen::Register{0}};  ///< Tag: start virtual PC of block
    BasicBlock* target_bb = nullptr;            ///< Direct pointer to taken branch target block
    BasicBlock* fallthrough_bb = nullptr;       ///< Direct pointer to fallthrough block
    simrv::xlen::Register target_pc = 0;        ///< Precomputed taken target PC
    simrv::xlen::Register fallthrough_pc = 0;   ///< Precomputed fallthrough PC
    uint16_t num_guest_ops = 0;                 ///< Architectural guest instruction count
    uint16_t num_ops = 0;                       ///< Number of instructions (1..16)
    uint16_t non_branch_ops = 0;                ///< Precomputed number of non-branch ops
    uint16_t num_compressed = 0;                ///< Number of 16-bit compressed instructions
    uint16_t total_bytes = 0;                   ///< Total guest instruction bytes spanned by block
    FastOp term_op = FastOp::Other;             ///< FastOp tag of the terminal instruction
    BranchKind branch_kind = BranchKind::None;  ///< Branch kind for FastOp::Branch
    bool ends_with_branch = false;              ///< True if block ends with a branch or jump
    bool has_mem_op = false;                    ///< True if block contains any load or store
    bool is_self_loop = false;                  ///< True if block branches directly to itself
    uint8_t generation = 0;                     ///< Generation counter for fast invalidation
    uint8_t pad = 0;

    std::array<CompactOp, kMaxBlockOps> ops{};  ///< Contiguous compact micro-ops

    [[nodiscard]] constexpr auto valid() const noexcept -> bool {
        return pc.raw() != ~simrv::xlen::Register{0} && num_ops > 0;
    }

    constexpr void invalidate() noexcept {
        pc = simrv::xlen::VirtAddr{~simrv::xlen::Register{0}};
        target_bb = nullptr;
        fallthrough_bb = nullptr;
        target_pc = 0;
        fallthrough_pc = 0;
        num_guest_ops = 0;
        num_ops = 0;
        non_branch_ops = 0;
        num_compressed = 0;
        total_bytes = 0;
        ends_with_branch = false;
        has_mem_op = false;
        is_self_loop = false;
        term_op = FastOp::Other;
        branch_kind = BranchKind::None;
    }
};
static_assert(sizeof(BasicBlock) <= 320);

/**
 * @class BlockCache
 * @brief Cache for compiled basic blocks to eliminate per-instruction dispatch overhead.
 */
class BlockCache {
   public:
    BlockCache() noexcept { flush(); }

    void flush() noexcept {
        for (auto& bb : blocks_) {
            bb.invalidate();
        }
        ++generation_;
    }

    [[nodiscard]] SIMRV_ALWAYS_INLINE static constexpr auto calc_set(
        simrv::xlen::Register pc) noexcept -> size_t {
        return (pc >> 1) & kBlockSetMask;
    }

    [[nodiscard]] SIMRV_ALWAYS_INLINE auto lookup(simrv::xlen::Register pc) noexcept
        -> BasicBlock* {
        auto& bb = blocks_[calc_set(pc)];
        if (simrv::compiler::likely(bb.pc == pc && bb.valid())) {
            return &bb;
        }
        return nullptr;
    }

    [[nodiscard]] auto build_block(simrv::xlen::Register pc, DecodeCache& decode_cache) noexcept
        -> BasicBlock*;

    [[nodiscard]] SIMRV_ALWAYS_INLINE auto block_by_index(uint16_t idx) noexcept -> BasicBlock* {
        if (simrv::compiler::likely(idx < kNumBlockSets)) {
            auto& bb = blocks_[idx];
            if (simrv::compiler::likely(bb.valid())) {
                return &bb;
            }
        }
        return nullptr;
    }

    [[nodiscard]] SIMRV_ALWAYS_INLINE auto index_of(const BasicBlock* bb) const noexcept
        -> uint16_t {
        return static_cast<uint16_t>(bb - blocks_.data());
    }

   private:
    alignas(64) std::array<BasicBlock, kNumBlockSets> blocks_{};
    uint8_t generation_{0};
};

}  // namespace simrv::core
