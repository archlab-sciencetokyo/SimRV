/**
 * @file BlockCache.cpp
 * @brief Implementation of BasicBlock construction and caching.
 */
#include "simrv/core/BlockCache.hpp"

namespace simrv::core {

auto BlockCache::build_block(simrv::xlen::VirtAddr pc, DecodeCache& decode_cache) noexcept
    -> BasicBlock* {
    auto* head_op = decode_cache.lookup_aligned(pc);
    if (simrv::compiler::unlikely(head_op == nullptr)) {
        return nullptr;
    }

    auto& bb = blocks_[calc_set(pc)];
    bb.invalidate();
    dirty_ = true;
    bb.pc = pc;
    bb.num_guest_ops = 0;
    bb.num_ops = 0;
    bb.num_compressed = 0;
    bb.total_bytes = 0;
    bb.ends_with_branch = false;
    bb.has_mem_op = false;
    bb.generation = generation_;

    CachedOp* cur = head_op;
    simrv::xlen::VirtAddr cur_pc = pc;
    CachedOp* next_op = nullptr;

    while (bb.num_ops < kMaxBlockOps && cur != nullptr && cur->cpc == cur_pc) {
        const bool is_branch = is_branch_fast_op(cur->fast_op);
        const bool is_alu = is_alu_fast_op(cur->fast_op);
        const bool is_load = is_int_load_fast_op(cur->fast_op);
        const bool is_store = is_int_store_fast_op(cur->fast_op);

        // Only include instructions supported by the fast inline kernel.
        // Complex instructions (CSR, ECALL, FENCE, FP, etc.) terminate block formation.
        if (!is_branch && !is_alu && !is_load && !is_store) {
            break;
        }

        bb.num_guest_ops++;
        auto& cop = bb.ops[bb.num_ops++];
        cop.imm = cur->imm;
        cop.fast_op = cur->fast_op;
        cop.rd = (cur->rd == simrv::xlen::RegId::Zero) ? simrv::xlen::RegId::Discard : cur->rd;
        cop.rs1 = cur->rs1;
        cop.rs2 = cur->rs2;
        cop.op_len = cur->op_len;
        cop.is_compressed = cur->is_compressed() ? 1 : 0;

        // Optimization: Convert Auipc to direct constant load (Lui)
        if (cop.fast_op == FastOp::Auipc) {
            cop.fast_op = FastOp::Lui;
            cop.imm = static_cast<simrv::xlen::Register>(cur->cpc.raw() + cur->imm);
        } else if (cop.fast_op == FastOp::Addi && cop.rs1 == simrv::xlen::RegId::Zero) {
            // Optimization: 'li rd, imm' (addi rd, x0, imm) -> direct immediate write (0 registers
            // read)
            cop.fast_op = FastOp::Lui;
        }

        if (cur->is_compressed()) {
            bb.num_compressed++;
        }
        bb.total_bytes += cur->op_len;
        cur_pc += cur->op_len;

        // If this op is a branch or jump, it terminates the basic block.
        if (is_branch) {
            bb.ends_with_branch = true;
            bb.term_op = cur->fast_op;
            bb.branch_kind = cur->branch_kind;
            if (bb.term_op == FastOp::Branch) {
                switch (cur->branch_kind) {
                    case BranchKind::Beq:
                        bb.term_op = FastOp::Beq;
                        break;
                    case BranchKind::Bne:
                        bb.term_op = FastOp::Bne;
                        break;
                    case BranchKind::Blt:
                        bb.term_op = FastOp::Blt;
                        break;
                    case BranchKind::Bge:
                        bb.term_op = FastOp::Bge;
                        break;
                    case BranchKind::Bltu:
                        bb.term_op = FastOp::Bltu;
                        break;
                    case BranchKind::Bgeu:
                        bb.term_op = FastOp::Bgeu;
                        break;
                    case BranchKind::Jal:
                        bb.term_op = FastOp::Jal;
                        break;
                    case BranchKind::Jalr:
                        bb.term_op = FastOp::Jalr;
                        break;
                    default:
                        break;
                }
            }
            bb.fallthrough_pc = cur->cpc + cur->op_len;
            if (cur->fast_op != FastOp::Jalr && cur->branch_kind != BranchKind::Jalr) {
                bb.target_pc = cur->cpc + cur->imm;
                if (bb.target_pc == bb.pc) {
                    bb.is_self_loop = true;
                    bb.target_bb = &bb;
                } else {
                    bb.target_bb = lookup(bb.target_pc);
                }
            }
            bb.fallthrough_bb = lookup(bb.fallthrough_pc);
            break;
        }

        if (is_load || is_store) {
            bb.has_mem_op = true;
        }

        // Trace to next op in decode cache
        next_op = nullptr;
        if (cur->next_seq_idx != kInvalidSeqIndex) {
            next_op = decode_cache.op_by_index(cur->next_seq_idx);
        }
        if (next_op == nullptr || next_op->cpc != cur_pc) {
            next_op = decode_cache.lookup_aligned(cur_pc);
            if (next_op != nullptr && next_op->cpc == cur_pc) {
                cur->next_seq_idx = decode_cache.index_of(next_op);
            } else {
                next_op = nullptr;
            }
        }
        if (next_op == nullptr) {
            break;
        }
        cur = next_op;
    }

    if (simrv::compiler::unlikely(bb.num_ops == 0)) {
        bb.invalidate();
        return nullptr;
    }

    bb.non_branch_ops = bb.ends_with_branch ? (bb.num_ops - 1) : bb.num_ops;

    if (!bb.ends_with_branch) {
        // If block formation stopped purely because the sequential successor was not yet
        // decoded in DecodeCache, do not freeze a truncated stub into BlockCache.
        // Returning nullptr allows the scalar engine to execute and populate DecodeCache
        // so that the full basic block can be assembled on the next traversal.
        if (next_op == nullptr && bb.num_ops < kMaxBlockOps) {
            bb.invalidate();
            return nullptr;
        }
        bb.fallthrough_pc = cur_pc;
        bb.fallthrough_bb = lookup(bb.fallthrough_pc);
    }

    return &bb;
}

}  // namespace simrv::core
