/**
 * @file CpuFastBaremetal.cpp
 * @brief Baremetal basic-block and fast-chain execution engine.
 */
#include <atomic>
#include <cstring>
#include <utility>

#include "CpuFastExecutionInternal.hpp"
#include "simrv/CompilerHints.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/PlatformTiming.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/AddressTypes.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/RegisterTypes.hpp"

namespace simrv::core {

auto CPU::advance_sequential_miss_baremetal(CachedOp* chained,
                                            simrv::xlen::Register next_pc) noexcept -> CachedOp* {
    const auto* current_set = DecodeCache::set_of(chained);
    const auto* candidate = ((next_pc & 2) != 0 && chained->op_len == 2)
                                ? current_set
                                : decode_cache.next_set(current_set);
    auto* hit = decode_cache.lookup_sequential_baremetal(candidate, next_pc);
    if (simrv::compiler::likely(hit != nullptr)) {
        chained->next_seq_idx = decode_cache.index_of(hit);
        return hit;
    }
    hit = decode_cache.lookup_baremetal_aligned(next_pc);
    if (simrv::compiler::likely(hit != nullptr)) {
        chained->next_seq_idx = decode_cache.index_of(hit);
        return hit;
    }
    return nullptr;
}

auto CPU::resolve_branch_target_baremetal(CachedOp* chained,
                                          simrv::xlen::Register target_pc) noexcept -> CachedOp* {
    auto* hit = decode_cache.lookup_baremetal_aligned(target_pc);
    if (simrv::compiler::likely(hit != nullptr)) {
        chained->target_idx = decode_cache.index_of(hit);
        return hit;
    }
    return nullptr;
}

template <bool kCopyContext, bool kInstMix, int kHpmClassMode>
SIMRV_ALWAYS_INLINE auto CPU::run_fast_baremetal_kernel(Machine& machine, uint32_t batch_size,
                                                        const simrv::memory::RamView& ram,
                                                        bool poll_pause) -> uint32_t {
    uint32_t cached_ops = 0;
    uint32_t flushed_uart_ops = 0;
    simrv::xlen::Counter last_flushed_icount = e_icount;
    const auto tohost_addr = machine.isa_test_tohost();
    const simrv::xlen::Address tohost_masked = tohost_addr & ~simrv::xlen::Address{0x7ULL};
    const simrv::xlen::Address tohost_reject_mask = 0xFF8ULL & ~tohost_masked;
    uint32_t b = 0;
    while (b < batch_size) {
        // A CSR or cache miss may have enabled a trigger during this batch.
        if (simrv::compiler::unlikely(state_.has_active_triggers)) break;
        auto* cached = decode_cache.lookup_aligned(state_.pc);
        if (simrv::compiler::likely(cached != nullptr)) {
            if constexpr (!kCopyContext && !kInstMix) {
                const auto execute_fast_chain =
                    [this, &ram, batch_size, tohost_addr, tohost_reject_mask]<bool kGuestXlen64>(
                        CachedOp* initial_op, uint32_t& b_ref)
                        __attribute__((noinline)) -> CachedOp* {
                    auto* const __restrict__ ram_data = ram.data();
                    const simrv::xlen::Address ram_base = ram.base();
                    const simrv::xlen::Address ram_size = ram.size();
                    auto* const __restrict__ gpr = state_.regs.gpr_data();
                    auto* const __restrict__ ops_base = decode_cache.ops_data();
                    auto* chained = initial_op;
                    CachedOp* fallback_op = nullptr;
                    simrv::xlen::Register next_pc = state_.pc;
                    uint32_t local_b = b_ref;
                    const uint32_t start_b = local_b;
                    simrv::xlen::Register seg_start_pc = next_pc;
                    simrv::xlen::Register total_pc_span = 0;

                    const uint8_t active_ctx = decode_cache.active_context();
                    const auto advance_sequential = [&]() __attribute__((always_inline)) -> bool {
                        record_hpm_instruction_classes<kHpmClassMode>(
                            chained->op_id, chained->opcode(), chained->cinsn != 0);
                        ++local_b;
                        if constexpr (kGuestXlen64 || !simrv::xlen::kIsXLen64) {
                            const uint16_t seq_idx = chained->next_seq_idx;
                            if (simrv::compiler::likely(local_b < batch_size &&
                                                        seq_idx != kInvalidSeqIndex)) {
                                chained = ops_base + seq_idx;
                                return true;
                            }
                            next_pc = chained->cpc.raw() + chained->op_len;
                            if (local_b >= batch_size) {
                                return false;
                            }
                        } else {
                            next_pc += chained->op_len;
                            if (simrv::compiler::unlikely(local_b >= batch_size)) {
                                return false;
                            }
                            auto* next_op = ops_base + chained->next_seq_idx;
                            if (simrv::compiler::likely(next_op->cpc.raw() == next_pc &&
                                                        next_op->ctx == active_ctx)) {
                                chained = next_op;
                                return true;
                            }
                        }
                        auto* hit = advance_sequential_miss_baremetal(chained, next_pc);
                        if (simrv::compiler::likely(hit != nullptr)) {
                            chained = hit;
                            return true;
                        }
                        return false;
                    };

                    auto op_tag = chained->fast_op;

                    const auto write_rd = [&](simrv::xlen::RegId rd,
                                              simrv::xlen::Register val) noexcept {
                        if constexpr (simrv::xlen::kIsXLen64) {
                            if constexpr (kGuestXlen64) {
                                gpr[std::to_underlying(rd)] = val;
                            } else {
                                gpr[std::to_underlying(rd)] =
                                    static_cast<int64_t>(static_cast<int32_t>(val));
                            }
                        } else {
                            gpr[std::to_underlying(rd)] = val;
                        }
                    };

                    const auto commit_taken_branch = [&](simrv::xlen::Register target_pc)
                                                         __attribute__((always_inline)) -> bool {
                        if constexpr (simrv::xlen::kIsXLen64 && !kGuestXlen64) {
                            target_pc = static_cast<simrv::xlen::Register>(
                                static_cast<int64_t>(static_cast<int32_t>(target_pc)));
                        }
                        auto* next_op = ops_base + chained->target_idx;
                        if (simrv::compiler::likely(next_op->cpc.raw() == target_pc &&
                                                    next_op->ctx == active_ctx)) {
                            record_hpm_instruction_classes<kHpmClassMode>(
                                chained->op_id, chained->opcode(), chained->cinsn != 0);
                            ++local_b;
                            total_pc_span += (chained->cpc.raw() + chained->op_len) - seg_start_pc;
                            next_pc = target_pc;
                            seg_start_pc = target_pc;
                            if (simrv::compiler::unlikely(local_b >= batch_size)) {
                                return false;
                            }
                            chained = next_op;
                            op_tag = chained->fast_op;
                            return true;
                        }
                        if (simrv::compiler::unlikely((target_pc & state_.fetch_align_mask) != 0)) {
                            fallback_op = chained;
                            return false;
                        }
                        record_hpm_instruction_classes<kHpmClassMode>(
                            chained->op_id, chained->opcode(), chained->cinsn != 0);
                        ++local_b;
                        total_pc_span += (chained->cpc.raw() + chained->op_len) - seg_start_pc;
                        next_pc = target_pc;
                        seg_start_pc = target_pc;
                        if (simrv::compiler::unlikely(local_b >= batch_size)) {
                            return false;
                        }
                        chained = resolve_branch_target_baremetal(chained, next_pc);
                        if (simrv::compiler::unlikely(chained == nullptr)) {
                            return false;
                        }
                        op_tag = chained->fast_op;
                        return true;
                    };

                    const auto calc_mem_addr =
                        [&](const CachedOp* op) noexcept -> simrv::xlen::Address {
                        simrv::xlen::Address addr = gpr[std::to_underlying(op->rs1)] + op->imm;
                        if constexpr (simrv::xlen::kIsXLen64 && !kGuestXlen64) {
                            addr &= 0xFFFFFFFFULL;
                        }
                        return addr;
                    };

                    const auto try_fast_load = [&]<typename T, typename SignedExtT = T,
                                                   size_t AlignMask = sizeof(T) - 1>(
                                                   const CachedOp* op)
                                                   __attribute__((always_inline))
                                                       ->bool {
                        if constexpr (sizeof(T) == 8 &&
                                      (!simrv::xlen::kIsXLen64 || !kGuestXlen64)) {
                            return false;
                        } else {
                            const auto mem_addr = calc_mem_addr(op);
                            const auto mem_offset = mem_addr - ram_base;
                            if (simrv::compiler::likely(mem_offset <= (ram_size - sizeof(T)) &&
                                                        (mem_addr & AlignMask) == 0)) {
                                const auto* const host_ptr = ram_data + mem_offset;
                                T val;
                                std::memcpy(&val, host_ptr, sizeof(val));
                                if constexpr (std::is_signed_v<SignedExtT> &&
                                              sizeof(T) < sizeof(simrv::xlen::Register)) {
                                    write_rd(op->rd, static_cast<simrv::xlen::Register>(
                                                         static_cast<simrv::xlen::SignedWord>(
                                                             static_cast<SignedExtT>(val))));
                                } else {
                                    write_rd(op->rd, static_cast<simrv::xlen::Register>(val));
                                }
                                return true;
                            }
                        }
                        return false;
                    };

                    const auto try_fast_store = [&]<typename T, size_t AlignMask = sizeof(T) - 1>(
                                                    const CachedOp* op)
                                                    __attribute__((always_inline))
                                                        ->bool {
                        if constexpr (sizeof(T) == 8 &&
                                      (!simrv::xlen::kIsXLen64 || !kGuestXlen64)) {
                            return false;
                        } else {
                            const auto mem_addr = calc_mem_addr(op);
                            const auto mem_offset = mem_addr - ram_base;
                            if (simrv::compiler::likely(mem_offset <= (ram_size - sizeof(T)) &&
                                                        (mem_addr & AlignMask) == 0)) {
                                if constexpr (sizeof(T) >= 4) {
                                    if (simrv::compiler::unlikely(
                                            (mem_addr & tohost_reject_mask) == 0 &&
                                            is_tohost_addr_fast(tohost_addr, mem_addr))) {
                                        return false;
                                    }
                                }
                                auto* const host_ptr = ram_data + mem_offset;
                                const T val = static_cast<T>(gpr[std::to_underlying(op->rs2)]);
                                std::memcpy(host_ptr, &val, sizeof(val));
                                return true;
                            }
                        }
                        return false;
                    };

                    while (true) {
                        switch (op_tag) {
                            case FastOp::Add: {
                                write_rd(chained->rd, gpr[std::to_underlying(chained->rs1)] +
                                                          gpr[std::to_underlying(chained->rs2)]);
                                break;
                            }
                            case FastOp::Addi: {
                                write_rd(chained->rd,
                                         gpr[std::to_underlying(chained->rs1)] + chained->imm);
                                break;
                            }
                            case FastOp::Addw: {
#if SIMRV_XLEN == 64
                                gpr[std::to_underlying(chained->rd)] = static_cast<int64_t>(
                                    static_cast<int32_t>(gpr[std::to_underlying(chained->rs1)] +
                                                         gpr[std::to_underlying(chained->rs2)]));
#else
                                write_rd(chained->rd, gpr[std::to_underlying(chained->rs1)] +
                                                          gpr[std::to_underlying(chained->rs2)]);
#endif
                                break;
                            }
                            case FastOp::Addiw: {
#if SIMRV_XLEN == 64
                                gpr[std::to_underlying(chained->rd)] =
                                    static_cast<int64_t>(static_cast<int32_t>(
                                        gpr[std::to_underlying(chained->rs1)] + chained->imm));
#else
                                write_rd(chained->rd,
                                         gpr[std::to_underlying(chained->rs1)] + chained->imm);
#endif
                                break;
                            }
                            case FastOp::Sub: {
                                write_rd(chained->rd, gpr[std::to_underlying(chained->rs1)] -
                                                          gpr[std::to_underlying(chained->rs2)]);
                                break;
                            }
                            case FastOp::Subw: {
#if SIMRV_XLEN == 64
                                gpr[std::to_underlying(chained->rd)] = static_cast<int64_t>(
                                    static_cast<int32_t>(gpr[std::to_underlying(chained->rs1)] -
                                                         gpr[std::to_underlying(chained->rs2)]));
#else
                                write_rd(chained->rd, gpr[std::to_underlying(chained->rs1)] -
                                                          gpr[std::to_underlying(chained->rs2)]);
#endif
                                break;
                            }
                            case FastOp::Slli: {
                                constexpr simrv::xlen::Register shift_mask =
                                    kGuestXlen64 ? 0x3f : 0x1f;
                                write_rd(chained->rd, gpr[std::to_underlying(chained->rs1)]
                                                          << (chained->imm & shift_mask));
                                break;
                            }
                            case FastOp::Srli: {
                                if constexpr (kGuestXlen64) {
                                    write_rd(chained->rd, gpr[std::to_underlying(chained->rs1)] >>
                                                              (chained->imm & 0x3f));
                                } else {
                                    write_rd(chained->rd,
                                             static_cast<uint32_t>(
                                                 gpr[std::to_underlying(chained->rs1)]) >>
                                                 (chained->imm & 0x1f));
                                }
                                break;
                            }
                            case FastOp::Srai: {
                                if constexpr (kGuestXlen64) {
                                    write_rd(chained->rd,
                                             static_cast<simrv::xlen::Register>(
                                                 static_cast<int64_t>(
                                                     gpr[std::to_underlying(chained->rs1)]) >>
                                                 (chained->imm & 0x3f)));
                                } else {
                                    write_rd(chained->rd,
                                             static_cast<simrv::xlen::Register>(
                                                 static_cast<int32_t>(
                                                     gpr[std::to_underlying(chained->rs1)]) >>
                                                 (chained->imm & 0x1f)));
                                }
                                break;
                            }
                            case FastOp::Andi: {
                                write_rd(chained->rd,
                                         gpr[std::to_underlying(chained->rs1)] & chained->imm);
                                break;
                            }
                            case FastOp::And: {
                                write_rd(chained->rd, gpr[std::to_underlying(chained->rs1)] &
                                                          gpr[std::to_underlying(chained->rs2)]);
                                break;
                            }
                            case FastOp::Or: {
                                write_rd(chained->rd, gpr[std::to_underlying(chained->rs1)] |
                                                          gpr[std::to_underlying(chained->rs2)]);
                                break;
                            }
                            case FastOp::Ori: {
                                write_rd(chained->rd,
                                         gpr[std::to_underlying(chained->rs1)] | chained->imm);
                                break;
                            }
                            case FastOp::Xor: {
                                write_rd(chained->rd, gpr[std::to_underlying(chained->rs1)] ^
                                                          gpr[std::to_underlying(chained->rs2)]);
                                break;
                            }
                            case FastOp::Xori: {
                                write_rd(chained->rd,
                                         gpr[std::to_underlying(chained->rs1)] ^ chained->imm);
                                break;
                            }
                            case FastOp::Sll: {
                                constexpr simrv::xlen::Register shift_mask =
                                    kGuestXlen64 ? 0x3f : 0x1f;
                                write_rd(
                                    chained->rd,
                                    gpr[std::to_underlying(chained->rs1)]
                                        << (gpr[std::to_underlying(chained->rs2)] & shift_mask));
                                break;
                            }
                            case FastOp::Srl: {
                                if constexpr (kGuestXlen64) {
                                    write_rd(chained->rd,
                                             gpr[std::to_underlying(chained->rs1)] >>
                                                 (gpr[std::to_underlying(chained->rs2)] & 0x3f));
                                } else {
                                    write_rd(chained->rd,
                                             static_cast<uint32_t>(
                                                 gpr[std::to_underlying(chained->rs1)]) >>
                                                 (gpr[std::to_underlying(chained->rs2)] & 0x1f));
                                }
                                break;
                            }
                            case FastOp::Sra: {
                                if constexpr (kGuestXlen64) {
                                    write_rd(chained->rd,
                                             static_cast<simrv::xlen::Register>(
                                                 static_cast<int64_t>(
                                                     gpr[std::to_underlying(chained->rs1)]) >>
                                                 (gpr[std::to_underlying(chained->rs2)] & 0x3f)));
                                } else {
                                    write_rd(chained->rd,
                                             static_cast<simrv::xlen::Register>(
                                                 static_cast<int32_t>(
                                                     gpr[std::to_underlying(chained->rs1)]) >>
                                                 (gpr[std::to_underlying(chained->rs2)] & 0x1f)));
                                }
                                break;
                            }
                            case FastOp::Slt: {
                                const auto rrs1 = gpr[std::to_underlying(chained->rs1)];
                                const auto rrs2 = gpr[std::to_underlying(chained->rs2)];
                                if constexpr (kGuestXlen64) {
                                    write_rd(chained->rd,
                                             static_cast<int64_t>(rrs1) < static_cast<int64_t>(rrs2)
                                                 ? 1
                                                 : 0);
                                } else {
                                    write_rd(chained->rd,
                                             static_cast<int32_t>(rrs1) < static_cast<int32_t>(rrs2)
                                                 ? 1
                                                 : 0);
                                }
                                break;
                            }
                            case FastOp::Sltu: {
                                const auto rrs1 = gpr[std::to_underlying(chained->rs1)];
                                const auto rrs2 = gpr[std::to_underlying(chained->rs2)];
                                if constexpr (kGuestXlen64) {
                                    write_rd(chained->rd, rrs1 < rrs2 ? 1 : 0);
                                } else {
                                    write_rd(chained->rd, static_cast<uint32_t>(rrs1) <
                                                                  static_cast<uint32_t>(rrs2)
                                                              ? 1
                                                              : 0);
                                }
                                break;
                            }
                            case FastOp::Slti: {
                                const auto rrs1 = gpr[std::to_underlying(chained->rs1)];
                                if constexpr (kGuestXlen64) {
                                    write_rd(chained->rd, static_cast<int64_t>(rrs1) <
                                                                  static_cast<int64_t>(chained->imm)
                                                              ? 1
                                                              : 0);
                                } else {
                                    write_rd(chained->rd, static_cast<int32_t>(rrs1) <
                                                                  static_cast<int32_t>(chained->imm)
                                                              ? 1
                                                              : 0);
                                }
                                break;
                            }
                            case FastOp::Sltiu: {
                                const auto rrs1 = gpr[std::to_underlying(chained->rs1)];
                                if constexpr (kGuestXlen64) {
                                    write_rd(chained->rd,
                                             rrs1 < static_cast<simrv::xlen::Register>(chained->imm)
                                                 ? 1
                                                 : 0);
                                } else {
                                    write_rd(chained->rd,
                                             static_cast<uint32_t>(rrs1) <
                                                     static_cast<uint32_t>(chained->imm)
                                                 ? 1
                                                 : 0);
                                }
                                break;
                            }
                            case FastOp::Lui: {
                                write_rd(chained->rd, chained->imm);
                                break;
                            }
                            case FastOp::Auipc: {
                                write_rd(chained->rd, chained->cpc.raw() + chained->imm);
                                break;
                            }
                            case FastOp::Mul: {
                                write_rd(chained->rd, gpr[std::to_underlying(chained->rs1)] *
                                                          gpr[std::to_underlying(chained->rs2)]);
                                break;
                            }
                            case FastOp::Mulw: {
#if SIMRV_XLEN == 64
                                const uint32_t product =
                                    static_cast<uint32_t>(gpr[std::to_underlying(chained->rs1)]) *
                                    static_cast<uint32_t>(gpr[std::to_underlying(chained->rs2)]);
                                gpr[std::to_underlying(chained->rd)] =
                                    static_cast<int64_t>(static_cast<int32_t>(product));
#endif
                                break;
                            }
                            case FastOp::Slliw: {
#if SIMRV_XLEN == 64
                                gpr[std::to_underlying(chained->rd)] =
                                    static_cast<int64_t>(static_cast<int32_t>(
                                        static_cast<uint32_t>(gpr[std::to_underlying(chained->rs1)])
                                        << (chained->imm & 0x1f)));
#endif
                                break;
                            }
                            case FastOp::Srliw: {
#if SIMRV_XLEN == 64
                                gpr[std::to_underlying(chained->rd)] =
                                    static_cast<int64_t>(static_cast<int32_t>(
                                        static_cast<uint32_t>(
                                            gpr[std::to_underlying(chained->rs1)]) >>
                                        (chained->imm & 0x1f)));
#endif
                                break;
                            }
                            case FastOp::Sraiw: {
#if SIMRV_XLEN == 64
                                gpr[std::to_underlying(chained->rd)] = static_cast<int64_t>(
                                    static_cast<int32_t>(gpr[std::to_underlying(chained->rs1)]) >>
                                    (chained->imm & 0x1f));
#endif
                                break;
                            }
                            case FastOp::Sllw: {
#if SIMRV_XLEN == 64
                                gpr[std::to_underlying(chained->rd)] =
                                    static_cast<int64_t>(static_cast<int32_t>(
                                        static_cast<uint32_t>(gpr[std::to_underlying(chained->rs1)])
                                        << (gpr[std::to_underlying(chained->rs2)] & 0x1f)));
#endif
                                break;
                            }
                            case FastOp::Srlw: {
#if SIMRV_XLEN == 64
                                gpr[std::to_underlying(chained->rd)] =
                                    static_cast<int64_t>(static_cast<int32_t>(
                                        static_cast<uint32_t>(
                                            gpr[std::to_underlying(chained->rs1)]) >>
                                        (gpr[std::to_underlying(chained->rs2)] & 0x1f)));
#endif
                                break;
                            }
                            case FastOp::Sraw: {
#if SIMRV_XLEN == 64
                                gpr[std::to_underlying(chained->rd)] = static_cast<int64_t>(
                                    static_cast<int32_t>(gpr[std::to_underlying(chained->rs1)]) >>
                                    (gpr[std::to_underlying(chained->rs2)] & 0x1f));
#endif
                                break;
                            }
                            case FastOp::CzeroEqz: {
                                write_rd(chained->rd, gpr[std::to_underlying(chained->rs2)] == 0
                                                          ? 0
                                                          : gpr[std::to_underlying(chained->rs1)]);
                                break;
                            }
                            case FastOp::CzeroNez: {
                                write_rd(chained->rd, gpr[std::to_underlying(chained->rs2)] != 0
                                                          ? 0
                                                          : gpr[std::to_underlying(chained->rs1)]);
                                break;
                            }
                            // ---- Cold / complex ALU operations ----
                            case FastOp::Mulh:
                            case FastOp::Mulhsu:
                            case FastOp::Mulhu:
                            case FastOp::Div:
                            case FastOp::Divu:
                            case FastOp::Rem:
                            case FastOp::Remu:
                            case FastOp::Divw:
                            case FastOp::Divuw:
                            case FastOp::Remw:
                            case FastOp::Remuw:
                            case FastOp::Bitmanip: {
                                execute_cached_alu_fast_xlen<kGuestXlen64, false, false, true>(
                                    *chained, op_tag);
                                break;
                            }

                            case FastOp::Bne: {
                                const auto r1 = gpr[std::to_underlying(chained->rs1)];
                                const auto r2 = gpr[std::to_underlying(chained->rs2)];
                                const bool tkn =
                                    kGuestXlen64
                                        ? (r1 != r2)
                                        : (static_cast<uint32_t>(r1) != static_cast<uint32_t>(r2));
                                if (tkn) {
                                    const simrv::xlen::Register target_pc =
                                        static_cast<simrv::xlen::Register>(chained->cpc.raw() +
                                                                           chained->imm);
                                    if (simrv::compiler::unlikely(
                                            !commit_taken_branch(target_pc))) {
                                        goto chain_exit;
                                    }
                                    continue;
                                }
                                break;
                            }
                            case FastOp::Beq: {
                                const auto r1 = gpr[std::to_underlying(chained->rs1)];
                                const auto r2 = gpr[std::to_underlying(chained->rs2)];
                                const bool tkn =
                                    kGuestXlen64
                                        ? (r1 == r2)
                                        : (static_cast<uint32_t>(r1) == static_cast<uint32_t>(r2));
                                if (tkn) {
                                    const simrv::xlen::Register target_pc =
                                        static_cast<simrv::xlen::Register>(chained->cpc.raw() +
                                                                           chained->imm);
                                    if (simrv::compiler::unlikely(
                                            !commit_taken_branch(target_pc))) {
                                        goto chain_exit;
                                    }
                                    continue;
                                }
                                break;
                            }
                            case FastOp::Jal: {
                                const simrv::xlen::Register link_pc =
                                    static_cast<simrv::xlen::Register>(chained->cpc.raw() +
                                                                       chained->op_len);
                                write_rd(chained->rd, link_pc);
                                const simrv::xlen::Register target_pc =
                                    static_cast<simrv::xlen::Register>(chained->cpc.raw() +
                                                                       chained->imm);
                                if (simrv::compiler::unlikely(!commit_taken_branch(target_pc))) {
                                    goto chain_exit;
                                }
                                continue;
                            }
                            case FastOp::Bgeu: {
                                const simrv::xlen::Register rrs1 =
                                    gpr[std::to_underlying(chained->rs1)];
                                const simrv::xlen::Register rrs2 =
                                    gpr[std::to_underlying(chained->rs2)];
                                bool tkn = false;
                                if constexpr (kGuestXlen64) {
                                    tkn = (rrs1 >= rrs2);
                                } else {
                                    tkn = (static_cast<uint32_t>(rrs1) >=
                                           static_cast<uint32_t>(rrs2));
                                }
                                if (tkn) {
                                    const simrv::xlen::Register target_pc =
                                        static_cast<simrv::xlen::Register>(chained->cpc.raw() +
                                                                           chained->imm);
                                    if (simrv::compiler::unlikely(
                                            !commit_taken_branch(target_pc))) {
                                        goto chain_exit;
                                    }
                                    continue;
                                }
                                break;
                            }
                            case FastOp::Jalr: {
                                const simrv::xlen::Register rrs1 =
                                    gpr[std::to_underlying(chained->rs1)];
                                const simrv::xlen::Register target_pc =
                                    (rrs1 + chained->imm) & ~static_cast<simrv::xlen::Register>(1);
                                const simrv::xlen::Register link_pc =
                                    static_cast<simrv::xlen::Register>(chained->cpc.raw() +
                                                                       chained->op_len);
                                write_rd(chained->rd, link_pc);
                                if (simrv::compiler::unlikely(!commit_taken_branch(target_pc))) {
                                    goto chain_exit;
                                }
                                continue;
                            }
                            case FastOp::Bge: {
                                const simrv::xlen::Register rrs1 =
                                    gpr[std::to_underlying(chained->rs1)];
                                const simrv::xlen::Register rrs2 =
                                    gpr[std::to_underlying(chained->rs2)];
                                const bool tkn =
                                    kGuestXlen64
                                        ? (static_cast<int64_t>(rrs1) >= static_cast<int64_t>(rrs2))
                                        : (static_cast<int32_t>(rrs1) >=
                                           static_cast<int32_t>(rrs2));
                                if (tkn) {
                                    const simrv::xlen::Register target_pc =
                                        static_cast<simrv::xlen::Register>(chained->cpc.raw() +
                                                                           chained->imm);
                                    if (simrv::compiler::unlikely(
                                            !commit_taken_branch(target_pc))) {
                                        goto chain_exit;
                                    }
                                    continue;
                                }
                                break;
                            }
                            case FastOp::Bltu: {
                                const simrv::xlen::Register rrs1 =
                                    gpr[std::to_underlying(chained->rs1)];
                                const simrv::xlen::Register rrs2 =
                                    gpr[std::to_underlying(chained->rs2)];
                                bool tkn = false;
                                if constexpr (kGuestXlen64) {
                                    tkn = (rrs1 < rrs2);
                                } else {
                                    tkn =
                                        (static_cast<uint32_t>(rrs1) < static_cast<uint32_t>(rrs2));
                                }
                                if (tkn) {
                                    const simrv::xlen::Register target_pc =
                                        static_cast<simrv::xlen::Register>(chained->cpc.raw() +
                                                                           chained->imm);
                                    if (simrv::compiler::unlikely(
                                            !commit_taken_branch(target_pc))) {
                                        goto chain_exit;
                                    }
                                    continue;
                                }
                                break;
                            }
                            case FastOp::Blt: {
                                const simrv::xlen::Register rrs1 =
                                    gpr[std::to_underlying(chained->rs1)];
                                const simrv::xlen::Register rrs2 =
                                    gpr[std::to_underlying(chained->rs2)];
                                const bool tkn =
                                    kGuestXlen64
                                        ? (static_cast<int64_t>(rrs1) < static_cast<int64_t>(rrs2))
                                        : (static_cast<int32_t>(rrs1) < static_cast<int32_t>(rrs2));
                                if (tkn) {
                                    const simrv::xlen::Register target_pc =
                                        static_cast<simrv::xlen::Register>(chained->cpc.raw() +
                                                                           chained->imm);
                                    if (simrv::compiler::unlikely(
                                            !commit_taken_branch(target_pc))) {
                                        goto chain_exit;
                                    }
                                    continue;
                                }
                                break;
                            }

                            case FastOp::Ld: {
                                if (simrv::compiler::likely(
                                        try_fast_load.template operator()<uint64_t>(chained))) {
                                    break;
                                }
                                fallback_op = chained;
                                goto chain_exit;
                            }
                            case FastOp::Lw: {
                                if (simrv::compiler::likely(
                                        try_fast_load.template operator()<int32_t>(chained))) {
                                    break;
                                }
                                fallback_op = chained;
                                goto chain_exit;
                            }
                            case FastOp::Lwu: {
                                if (simrv::compiler::likely(
                                        try_fast_load.template operator()<uint32_t>(chained))) {
                                    break;
                                }
                                fallback_op = chained;
                                goto chain_exit;
                            }
                            case FastOp::Lh: {
                                if (simrv::compiler::likely(
                                        try_fast_load.template operator()<int16_t>(chained))) {
                                    break;
                                }
                                fallback_op = chained;
                                goto chain_exit;
                            }
                            case FastOp::Lhu: {
                                if (simrv::compiler::likely(
                                        try_fast_load.template operator()<uint16_t>(chained))) {
                                    break;
                                }
                                fallback_op = chained;
                                goto chain_exit;
                            }
                            case FastOp::Lb: {
                                if (simrv::compiler::likely(
                                        try_fast_load.template operator()<int8_t>(chained))) {
                                    break;
                                }
                                fallback_op = chained;
                                goto chain_exit;
                            }
                            case FastOp::Lbu: {
                                if (simrv::compiler::likely(
                                        try_fast_load.template operator()<uint8_t>(chained))) {
                                    break;
                                }
                                fallback_op = chained;
                                goto chain_exit;
                            }

                            case FastOp::Sd: {
                                if (simrv::compiler::likely(
                                        try_fast_store.template operator()<uint64_t>(chained))) {
                                    break;
                                }
                                fallback_op = chained;
                                goto chain_exit;
                            }
                            case FastOp::Sw: {
                                if (simrv::compiler::likely(
                                        try_fast_store.template operator()<uint32_t>(chained))) {
                                    break;
                                }
                                fallback_op = chained;
                                goto chain_exit;
                            }
                            case FastOp::Sh: {
                                if (simrv::compiler::likely(
                                        try_fast_store.template operator()<uint16_t>(chained))) {
                                    break;
                                }
                                fallback_op = chained;
                                goto chain_exit;
                            }
                            case FastOp::Sb: {
                                if (simrv::compiler::likely(
                                        try_fast_store.template operator()<uint8_t>(chained))) {
                                    break;
                                }
                                fallback_op = chained;
                                goto chain_exit;
                            }

                            case FastOp::FpMem: {
                                if (simrv::compiler::unlikely(
                                        !state_.extension_state_enabled(MstatusBit::Fs))) {
                                    fallback_op = chained;
                                    goto chain_exit;
                                }
                                if (chained->mem_class == FastMemClass::FpLoad) {
                                    if (simrv::xlen::kIsXLen64 ||
                                        chained->funct3 == isa::Funct3::Flw) {
                                        simrv::xlen::Address mem_addr =
                                            gpr[std::to_underlying(chained->rs1)] + chained->imm;
                                        if constexpr (simrv::xlen::kIsXLen64 && !kGuestXlen64) {
                                            mem_addr &= 0xFFFFFFFFULL;
                                        }
                                        const unsigned size_bytes = chained->mem_size;
                                        const auto mem_offset = mem_addr - ram_base;
                                        if (simrv::compiler::likely(
                                                mem_offset <= (ram_size - size_bytes) &&
                                                (mem_addr & chained->align_mask()) == 0)) {
                                            const auto* const host_ptr = ram_data + mem_offset;
                                            const simrv::xlen::Word mem_rdata =
                                                simrv::memory::host_read_fast(
                                                    host_ptr, static_cast<simrv::xlen::Instruction>(
                                                                  chained->funct3));
                                            state_.regs.write_fp(
                                                simrv::xlen::to_fp_reg_id(chained->rd),
                                                (chained->funct3 == isa::Funct3::Flw)
                                                    ? (static_cast<simrv::xlen::FloatingRegister>(
                                                           simrv::xlen::kF32BoxerBits) |
                                                       (static_cast<simrv::xlen::FloatingRegister>(
                                                            mem_rdata) &
                                                        0xFFFFFFFFULL))
                                                    : static_cast<simrv::xlen::FloatingRegister>(
                                                          mem_rdata));
                                            state_.mark_extension_dirty(MstatusBit::Fs);
                                            break;
                                        }
                                    }
                                    fallback_op = chained;
                                    goto chain_exit;
                                }
                                if (chained->mem_class == FastMemClass::FpStore) {
                                    if (simrv::xlen::kIsXLen64 ||
                                        chained->funct3 == isa::Funct3::Fsw) {
                                        simrv::xlen::Address mem_addr =
                                            gpr[std::to_underlying(chained->rs1)] + chained->imm;
                                        if constexpr (simrv::xlen::kIsXLen64 && !kGuestXlen64) {
                                            mem_addr &= 0xFFFFFFFFULL;
                                        }
                                        const unsigned size_bytes = chained->mem_size;
                                        const auto mem_offset = mem_addr - ram_base;
                                        if (simrv::compiler::likely(
                                                mem_offset <= (ram_size - size_bytes) &&
                                                (mem_addr & chained->align_mask()) == 0 &&
                                                !((mem_addr & tohost_reject_mask) == 0 &&
                                                  is_tohost_addr_fast(tohost_addr, mem_addr)))) {
                                            auto* const host_ptr = ram_data + mem_offset;
                                            const simrv::xlen::FloatingRegister fp_data =
                                                state_.regs.read_fp(
                                                    simrv::xlen::to_fp_reg_id(chained->rs2));
                                            simrv::memory::host_write_fast(
                                                host_ptr, static_cast<simrv::xlen::Word>(fp_data),
                                                static_cast<simrv::xlen::Instruction>(
                                                    chained->funct3));
                                            break;
                                        }
                                    }
                                    fallback_op = chained;
                                    goto chain_exit;
                                }
                                fallback_op = chained;
                                goto chain_exit;
                            }
                            default: {
                                fallback_op = chained;
                                goto chain_exit;
                            }
                        }

                        if (simrv::compiler::unlikely(!advance_sequential())) {
                            goto chain_exit;
                        }
                        op_tag = chained->fast_op;
                    }
                chain_exit:
                    if (fallback_op != nullptr) {
                        next_pc = fallback_op->cpc.raw();
                    }
                    total_pc_span += next_pc - seg_start_pc;
                    state_.pc = next_pc;
                    const uint32_t ops_executed = local_b - start_b;
                    const uint32_t compressed_count =
                        (2u * ops_executed) - static_cast<uint32_t>(total_pc_span >> 1);
                    e_ccount += compressed_count;
                    b_ref = local_b;
                    return fallback_op;
                };
                const bool fast_m_ram =
                    (state_.priv == kPrivMachine && (state_.mstatus & MstatusBit::Mprv) == 0 &&
                     !state_.has_locked_pmp && ram.data() != nullptr && ram.size() >= 8 &&
                     ram.has_nonwrapping_range());
                const bool multi_hart = machine.num_harts() > 1;
                // Chain operations do not change MIP or MIE, and device interrupt lines are
                // applied at the enclosing batch boundary. If one is already pending, or if an
                // LR reservation is active, or if SMP/non-fast-RAM requires individual
                // synchronization, execute a single instruction.
                if (simrv::compiler::unlikely((state_.mip & state_.mie) != 0u ||
                                              state_.lr_reservation_active || !fast_m_ram ||
                                              multi_hart)) {
                    const bool is_csr = is_csr_instruction(cached->op_id);
                    execute_cached_op_fast<false, false>(machine, *cached, ram);
                    ++cached_ops;
                    ++b;
                    if constexpr (kHpmClassMode != 2) {
                        if (is_csr && perf_counters.has_instruction_class_listeners() !=
                                          (kHpmClassMode == 1)) {
                            b = batch_size;
                        }
                    }
                } else {
                    const uint32_t start_b = b;
                    CachedOp* fallback_op = nullptr;
                    if constexpr (simrv::xlen::kIsXLen64) {
                        if (simrv::compiler::likely(state_.regs.xlen == 64)) {
                            fallback_op = execute_fast_chain.template operator()<true>(cached, b);
                        } else {
                            fallback_op = execute_fast_chain.template operator()<false>(cached, b);
                        }
                    } else {
                        fallback_op = execute_fast_chain.template operator()<false>(cached, b);
                    }
                    const uint32_t ops_executed = b - start_b;
                    cached_ops += ops_executed;
                    account_retirement(ops_executed);
                    record_hpm_event(HpmEvent::Instructions, ops_executed);
                    record_hpm_event(HpmEvent::Cycles, ops_executed);
                    if (fallback_op != nullptr) {
                        if (state_.mhartid == 0) {
                            if (auto* uart = machine.uart_device();
                                uart != nullptr && uart->has_active_transfer()) {
                                const uint32_t unushed = cached_ops - flushed_uart_ops;
                                if (unushed > 0) {
                                    uart->advance_cycles(unushed);
                                    flushed_uart_ops = cached_ops;
                                }
                            }
                        }
                        const bool is_csr = is_csr_instruction(fallback_op->op_id);
                        execute_cached_op_fast<false, false>(machine, *fallback_op, ram);
                        ++cached_ops;
                        ++b;
                        if constexpr (kHpmClassMode != 2) {
                            if (is_csr && perf_counters.has_instruction_class_listeners() !=
                                              (kHpmClassMode == 1)) {
                                b = batch_size;
                            }
                        }
                    }
                }
            } else {
                if (state_.mhartid == 0) {
                    if (auto* uart = machine.uart_device();
                        uart != nullptr && uart->has_active_transfer()) {
                        const uint32_t unushed = cached_ops - flushed_uart_ops;
                        if (unushed > 0) {
                            uart->advance_cycles(unushed);
                            flushed_uart_ops = cached_ops;
                        }
                    }
                }
                if (cached->has_fast_alu()) {
                    execute_cached_alu_fast<kCopyContext, kInstMix, false>(*cached);
                } else {
                    execute_cached_op_fast<kCopyContext, kInstMix>(machine, *cached, ram);
                }
                ++cached_ops;
                ++b;

                // Preserve per-instruction commits when tracing or execution context is requested.
                while (cached->has_fast_alu() && b < batch_size && ((b & 0x3Fu) != 0) &&
                       simrv::compiler::likely((state_.mip & state_.mie) == 0u)) {
                    auto* next_cached = decode_cache.lookup_next_aligned(*cached, state_.pc);
                    if (simrv::compiler::unlikely(next_cached == nullptr ||
                                                  !next_cached->has_fast_alu())) {
                        break;
                    }
                    cached = next_cached;
                    execute_cached_alu_fast<kCopyContext, kInstMix, false>(*cached);
                    ++cached_ops;
                    ++b;
                }
            }
            if constexpr (simrv::xlen::kIsXLen64) {
                if (simrv::compiler::unlikely(state_.regs.xlen == 32)) {
                    state_.canonicalize_pc();
                }
            }
            handle_cached_interrupts();
        } else {
            if (state_.mhartid == 0) {
                if (auto* uart = machine.uart_device();
                    uart != nullptr && uart->has_active_transfer()) {
                    const uint32_t unushed = cached_ops - flushed_uart_ops;
                    if (unushed > 0) {
                        uart->advance_cycles(unushed);
                        flushed_uart_ops = cached_ops;
                    }
                }
            }
            const auto delta = e_icount - last_flushed_icount;
            if (delta > 0) {
                machine.record_retired_instructions(delta);
            }
            run_cycle_baremetal_miss(machine);
            last_flushed_icount = e_icount;
            if (simrv::compiler::unlikely(machine.tohost != 0)) {
                break;
            }
            ++b;
        }
        if (simrv::compiler::unlikely((b & 0x3Fu) == 0)) {
            if (machine.tohost != 0 || !machine.is_running() ||
                (poll_pause && (machine.is_paused() || machine.debug_pause_requested()))) {
                break;
            }
        }
    }
    const auto delta = e_icount - last_flushed_icount;
    if (delta > 0) {
        machine.record_retired_instructions(delta);
    }
    if constexpr (simrv::xlen::kIsXLen64) {
        if (simrv::compiler::unlikely(state_.regs.xlen == 32)) {
            state_.canonicalize_pc();
        }
    }
    handle_cached_interrupts();
    if (state_.mhartid == 0 && cached_ops > flushed_uart_ops) {
        if (auto* uart = machine.uart_device(); uart != nullptr) {
            uart->advance_cycles(cached_ops - flushed_uart_ops);
        }
    }
    return cached_ops;
}

void CPU::run_fast_baremetal_batch(Machine& machine, uint32_t batch_size,
                                   const FastBatchPolicy& policy) {
    apply_hart_control_boundary();
    apply_pending_interrupt_lines(machine);
    sync_decode_context();
    CPU* previous_active_cpu = active_execution_cpu_;
    active_execution_cpu_ = this;
    struct BoundaryCommit {
        CPU& cpu;
        CPU* previous;
        ~BoundaryCommit() {
            cpu.apply_hart_control_boundary();
            active_execution_cpu_ = previous;
        }
    } boundary_commit{.cpu = *this, .previous = previous_active_cpu};
    const auto ram = machine.ram_view();
    uint32_t cached_ops = 0;
    if (simrv::compiler::likely(!policy.copy_pipeline_context && !policy.collect_instruction_mix)) {
        if (perf_counters.has_instruction_class_listeners()) {
            cached_ops = run_fast_baremetal_kernel<false, false, 1>(machine, batch_size, ram,
                                                                    policy.poll_pause);
        } else {
            cached_ops = run_fast_baremetal_kernel<false, false, 0>(machine, batch_size, ram,
                                                                    policy.poll_pause);
        }
    } else if (policy.copy_pipeline_context && policy.collect_instruction_mix) {
        cached_ops =
            run_fast_baremetal_kernel<true, true, 2>(machine, batch_size, ram, policy.poll_pause);
    } else if (policy.copy_pipeline_context) {
        cached_ops =
            run_fast_baremetal_kernel<true, false, 2>(machine, batch_size, ram, policy.poll_pause);
    } else {
        cached_ops =
            run_fast_baremetal_kernel<false, true, 2>(machine, batch_size, ram, policy.poll_pause);
    }

    if (cached_ops > 0) {
        account_arch_cycles(cached_ops);
        if (state_.mhartid == 0) {
            if (machine.clock_divider().advance_fast_cycles(cached_ops, clint_mmio.rtc_divider,
                                                            clint_mmio.mtime) > 0) {
                evaluate_timer_interrupt();
            }
        } else {
            clint_mmio.mtime.store(
                machine.primary_hart().clint_mmio.mtime.load(std::memory_order_relaxed),
                std::memory_order_relaxed);
            evaluate_timer_interrupt();
        }
    }
}

}  // namespace simrv::core
