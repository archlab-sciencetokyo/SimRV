/**
 * @file CpuFastExecutionInternal.hpp
 * @brief Shared inline ALU execution helpers for cached single-op, baremetal, and OS fast paths.
 */
#pragma once

#include <limits>

#include "CpuExecutionHelpers.hpp"
#include "simrv/CompilerHints.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/execute/ExecuteUnitZb.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Math.hpp"
#include "simrv/xlen/RegisterTypes.hpp"

namespace simrv::core {

[[nodiscard]] constexpr auto is_csr_instruction(isa::OperationId op) noexcept -> bool {
    return op >= isa::CSRRW && op <= isa::CSRRCI;
}

[[nodiscard]] constexpr auto is_time_counter_csr(const CachedOp& op) noexcept -> bool {
    if (!is_csr_instruction(op.op_id)) return false;
    const auto csr = op.funct12();
    return csr == csr_addr(Csr::Time) || csr == csr_addr(Csr::Timeh);
}

template <bool kGuestXlen64, bool kCopyContext, bool kInstMix, bool kDeferAluCommit>
SIMRV_ALWAYS_INLINE auto CPU::execute_cached_alu_fast_xlen(CachedOp& op) -> bool {
    return execute_cached_alu_fast_xlen<kGuestXlen64, kCopyContext, kInstMix, kDeferAluCommit>(
        op, op.fast_op);
}

template <bool kGuestXlen64, bool kCopyContext, bool kInstMix, bool kDeferAluCommit>
SIMRV_ALWAYS_INLINE auto CPU::execute_cached_alu_fast_xlen(CachedOp& op, FastOp kind) -> bool {
    simrv::xlen::Register wb_data = 0;
    const simrv::xlen::Register rrs1 = state_.regs.read(op.rs1);

    switch (kind) {
        case FastOp::Add:
            wb_data = rrs1 + state_.regs.read(op.rs2);
            break;
        case FastOp::Addw:
#if SIMRV_XLEN == 64
            wb_data = static_cast<simrv::xlen::Register>(static_cast<simrv::xlen::SignedWord>(
                static_cast<int32_t>(rrs1 + state_.regs.read(op.rs2))));
#else
            wb_data = rrs1 + state_.regs.read(op.rs2);
#endif
            break;
        case FastOp::Slli: {
            constexpr simrv::xlen::Register shift_mask = kGuestXlen64 ? 0x3f : 0x1f;
            wb_data = rrs1 << (op.imm & shift_mask);
            break;
        }
        case FastOp::Srli:
            if constexpr (kGuestXlen64) {
                wb_data = rrs1 >> (op.imm & 0x3f);
            } else {
                wb_data = static_cast<uint32_t>(rrs1) >> (op.imm & 0x1f);
            }
            break;
        case FastOp::Andi:
            wb_data = rrs1 & op.imm;
            break;
        case FastOp::Mulw: {
#if SIMRV_XLEN == 64
            const uint32_t product =
                static_cast<uint32_t>(rrs1) * static_cast<uint32_t>(state_.regs.read(op.rs2));
            wb_data = static_cast<simrv::xlen::Register>(
                static_cast<simrv::xlen::SignedWord>(static_cast<int32_t>(product)));
#endif
            break;
        }
        case FastOp::Or:
            wb_data = rrs1 | state_.regs.read(op.rs2);
            break;
        case FastOp::Slliw:
#if SIMRV_XLEN == 64
            wb_data = static_cast<simrv::xlen::Register>(static_cast<simrv::xlen::SignedWord>(
                static_cast<int32_t>(static_cast<uint32_t>(rrs1) << (op.imm & 0x1f))));
#endif
            break;
        case FastOp::And:
            wb_data = rrs1 & state_.regs.read(op.rs2);
            break;
        case FastOp::Srliw:
#if SIMRV_XLEN == 64
            wb_data = static_cast<simrv::xlen::Register>(static_cast<simrv::xlen::SignedWord>(
                static_cast<int32_t>(static_cast<uint32_t>(rrs1) >> (op.imm & 0x1f))));
#endif
            break;
        case FastOp::Sub:
            wb_data = rrs1 - state_.regs.read(op.rs2);
            break;
        case FastOp::Xor:
            wb_data = rrs1 ^ state_.regs.read(op.rs2);
            break;
        case FastOp::Addi:
            wb_data = rrs1 + op.imm;
            break;
        case FastOp::Addiw:
#if SIMRV_XLEN == 64
            wb_data = static_cast<simrv::xlen::Register>(
                static_cast<simrv::xlen::SignedWord>(static_cast<int32_t>(rrs1 + op.imm)));
#else
            wb_data = rrs1 + op.imm;
#endif
            break;
        case FastOp::Ori:
            wb_data = rrs1 | op.imm;
            break;
        case FastOp::Xori:
            wb_data = rrs1 ^ op.imm;
            break;
        case FastOp::Srai:
            if constexpr (kGuestXlen64) {
                wb_data = static_cast<simrv::xlen::Register>(
                    static_cast<simrv::xlen::SignedWord>(rrs1) >> (op.imm & 0x3f));
            } else {
                wb_data = static_cast<simrv::xlen::Register>(
                    static_cast<simrv::xlen::SignedWord>(rrs1) >> (op.imm & 0x1f));
            }
            break;
        case FastOp::Sll: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            constexpr simrv::xlen::Register shift_mask = kGuestXlen64 ? 0x3f : 0x1f;
            wb_data = rrs1 << (rrs2 & shift_mask);
            break;
        }
        case FastOp::Srl: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            if constexpr (kGuestXlen64) {
                wb_data = rrs1 >> (rrs2 & 0x3f);
            } else {
                wb_data = static_cast<uint32_t>(rrs1) >> (rrs2 & 0x1f);
            }
            break;
        }
        case FastOp::Sra: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            constexpr simrv::xlen::Register shift_mask = kGuestXlen64 ? 0x3f : 0x1f;
            wb_data = static_cast<simrv::xlen::Register>(
                static_cast<simrv::xlen::SignedWord>(rrs1) >> (rrs2 & shift_mask));
            break;
        }
        case FastOp::Slt: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            if constexpr (kGuestXlen64) {
                wb_data = static_cast<simrv::xlen::Register>(static_cast<int64_t>(rrs1) <
                                                             static_cast<int64_t>(rrs2));
            } else {
                wb_data = static_cast<simrv::xlen::Register>(static_cast<int32_t>(rrs1) <
                                                             static_cast<int32_t>(rrs2));
            }
            break;
        }
        case FastOp::Sltu: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            if constexpr (kGuestXlen64) {
                wb_data = static_cast<simrv::xlen::Register>(rrs1 < rrs2);
            } else {
                wb_data = static_cast<simrv::xlen::Register>(static_cast<uint32_t>(rrs1) <
                                                             static_cast<uint32_t>(rrs2));
            }
            break;
        }
        case FastOp::Slti:
            if constexpr (kGuestXlen64) {
                wb_data = static_cast<simrv::xlen::Register>(static_cast<int64_t>(rrs1) <
                                                             static_cast<int64_t>(op.imm));
            } else {
                wb_data = static_cast<simrv::xlen::Register>(static_cast<int32_t>(rrs1) <
                                                             static_cast<int32_t>(op.imm));
            }
            break;
        case FastOp::Sltiu:
            if constexpr (kGuestXlen64) {
                wb_data = static_cast<simrv::xlen::Register>(
                    rrs1 < static_cast<simrv::xlen::Register>(op.imm));
            } else {
                wb_data = static_cast<simrv::xlen::Register>(static_cast<uint32_t>(rrs1) <
                                                             static_cast<uint32_t>(op.imm));
            }
            break;
        case FastOp::Mul: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            wb_data = simrv::xlen::mul_low(rrs1, rrs2);
            break;
        }
        case FastOp::Mulh: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            if constexpr (kGuestXlen64) {
                wb_data = simrv::xlen::mul_high_signed(rrs1, rrs2);
            } else {
                const int64_t product = static_cast<int64_t>(static_cast<int32_t>(rrs1)) *
                                        static_cast<int64_t>(static_cast<int32_t>(rrs2));
                wb_data = static_cast<uint32_t>(static_cast<uint64_t>(product) >> 32u);
            }
            break;
        }
        case FastOp::Mulhsu: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            if constexpr (kGuestXlen64) {
                wb_data = simrv::xlen::mul_high_signed_unsigned(rrs1, rrs2);
            } else {
                const int64_t product = static_cast<int64_t>(static_cast<int32_t>(rrs1)) *
                                        static_cast<int64_t>(static_cast<uint32_t>(rrs2));
                wb_data = static_cast<uint32_t>(static_cast<uint64_t>(product) >> 32u);
            }
            break;
        }
        case FastOp::Mulhu: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            if constexpr (kGuestXlen64) {
                wb_data = simrv::xlen::mul_high_unsigned(rrs1, rrs2);
            } else {
                const uint64_t product = static_cast<uint64_t>(static_cast<uint32_t>(rrs1)) *
                                         static_cast<uint64_t>(static_cast<uint32_t>(rrs2));
                wb_data = static_cast<uint32_t>(product >> 32u);
            }
            break;
        }
        case FastOp::Div: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            if constexpr (kGuestXlen64) {
                const auto lhs = static_cast<simrv::xlen::SignedWord>(rrs1);
                const auto rhs = static_cast<simrv::xlen::SignedWord>(rrs2);
                if (rhs == 0) {
                    wb_data = simrv::xlen::kWordAllOnes;
                } else if (lhs == std::numeric_limits<simrv::xlen::SignedWord>::min() &&
                           rhs == -1) {
                    wb_data = static_cast<simrv::xlen::Register>(lhs);
                } else {
                    wb_data = static_cast<simrv::xlen::Register>(lhs / rhs);
                }
            } else {
                const auto lhs = static_cast<int32_t>(rrs1);
                const auto rhs = static_cast<int32_t>(rrs2);
                if (rhs == 0) {
                    wb_data = static_cast<simrv::xlen::Register>(static_cast<int64_t>(-1));
                } else if (lhs == std::numeric_limits<int32_t>::min() && rhs == -1) {
                    wb_data = static_cast<simrv::xlen::Register>(static_cast<int64_t>(lhs));
                } else {
                    wb_data = static_cast<simrv::xlen::Register>(static_cast<int64_t>(lhs / rhs));
                }
            }
            break;
        }
        case FastOp::Divu: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            if constexpr (kGuestXlen64) {
                wb_data = (rrs2 == 0) ? simrv::xlen::kWordAllOnes : (rrs1 / rrs2);
            } else {
                const auto lhs = static_cast<uint32_t>(rrs1);
                const auto rhs = static_cast<uint32_t>(rrs2);
                wb_data = (rhs == 0) ? static_cast<simrv::xlen::Register>(static_cast<int64_t>(-1))
                                     : static_cast<simrv::xlen::Register>(
                                           static_cast<int64_t>(static_cast<int32_t>(lhs / rhs)));
            }
            break;
        }
        case FastOp::Rem: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            if constexpr (kGuestXlen64) {
                const auto lhs = static_cast<simrv::xlen::SignedWord>(rrs1);
                const auto rhs = static_cast<simrv::xlen::SignedWord>(rrs2);
                if (rhs == 0) {
                    wb_data = rrs1;
                } else if (lhs == std::numeric_limits<simrv::xlen::SignedWord>::min() &&
                           rhs == -1) {
                    wb_data = 0;
                } else {
                    wb_data = static_cast<simrv::xlen::Register>(lhs % rhs);
                }
            } else {
                const auto lhs = static_cast<int32_t>(rrs1);
                const auto rhs = static_cast<int32_t>(rrs2);
                if (rhs == 0) {
                    wb_data = static_cast<simrv::xlen::Register>(static_cast<int64_t>(lhs));
                } else if (lhs == std::numeric_limits<int32_t>::min() && rhs == -1) {
                    wb_data = 0;
                } else {
                    wb_data = static_cast<simrv::xlen::Register>(static_cast<int64_t>(lhs % rhs));
                }
            }
            break;
        }
        case FastOp::Remu: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            if constexpr (kGuestXlen64) {
                wb_data = (rrs2 == 0) ? rrs1 : (rrs1 % rrs2);
            } else {
                const auto lhs = static_cast<uint32_t>(rrs1);
                const auto rhs = static_cast<uint32_t>(rrs2);
                wb_data = (rhs == 0) ? static_cast<simrv::xlen::Register>(
                                           static_cast<int64_t>(static_cast<int32_t>(lhs)))
                                     : static_cast<simrv::xlen::Register>(
                                           static_cast<int64_t>(static_cast<int32_t>(lhs % rhs)));
            }
            break;
        }
#if SIMRV_XLEN == 64
        case FastOp::Subw: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            wb_data = static_cast<simrv::xlen::Register>(
                static_cast<simrv::xlen::SignedWord>(static_cast<int32_t>(rrs1 - rrs2)));
            break;
        }
        case FastOp::Sllw: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            wb_data = static_cast<simrv::xlen::Register>(static_cast<simrv::xlen::SignedWord>(
                static_cast<int32_t>(static_cast<uint32_t>(rrs1) << (rrs2 & 0x1f))));
            break;
        }
        case FastOp::Srlw: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            wb_data = static_cast<simrv::xlen::Register>(static_cast<simrv::xlen::SignedWord>(
                static_cast<int32_t>(static_cast<uint32_t>(rrs1) >> (rrs2 & 0x1f))));
            break;
        }
        case FastOp::Sraw: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            wb_data = static_cast<simrv::xlen::Register>(
                static_cast<simrv::xlen::SignedWord>(static_cast<int32_t>(rrs1) >> (rrs2 & 0x1f)));
            break;
        }
        case FastOp::Divw: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            const auto lhs = static_cast<int32_t>(rrs1);
            const auto rhs = static_cast<int32_t>(rrs2);
            if (rhs == 0) {
                wb_data = static_cast<simrv::xlen::Register>(static_cast<int64_t>(-1));
            } else if (lhs == std::numeric_limits<int32_t>::min() && rhs == -1) {
                wb_data = static_cast<simrv::xlen::Register>(static_cast<int64_t>(lhs));
            } else {
                wb_data = static_cast<simrv::xlen::Register>(static_cast<int64_t>(lhs / rhs));
            }
            break;
        }
        case FastOp::Divuw: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            const auto lhs = static_cast<uint32_t>(rrs1);
            const auto rhs = static_cast<uint32_t>(rrs2);
            wb_data = (rhs == 0) ? static_cast<simrv::xlen::Register>(static_cast<int64_t>(-1))
                                 : static_cast<simrv::xlen::Register>(
                                       static_cast<int64_t>(static_cast<int32_t>(lhs / rhs)));
            break;
        }
        case FastOp::Remw: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            const auto lhs = static_cast<int32_t>(rrs1);
            const auto rhs = static_cast<int32_t>(rrs2);
            if (rhs == 0) {
                wb_data = static_cast<simrv::xlen::Register>(static_cast<int64_t>(lhs));
            } else if (lhs == std::numeric_limits<int32_t>::min() && rhs == -1) {
                wb_data = 0;
            } else {
                wb_data = static_cast<simrv::xlen::Register>(static_cast<int64_t>(lhs % rhs));
            }
            break;
        }
        case FastOp::Remuw: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            const auto lhs = static_cast<uint32_t>(rrs1);
            const auto rhs = static_cast<uint32_t>(rrs2);
            wb_data = (rhs == 0) ? static_cast<simrv::xlen::Register>(
                                       static_cast<int64_t>(static_cast<int32_t>(lhs)))
                                 : static_cast<simrv::xlen::Register>(
                                       static_cast<int64_t>(static_cast<int32_t>(lhs % rhs)));
            break;
        }
        case FastOp::Sraiw:
            wb_data = static_cast<simrv::xlen::Register>(static_cast<simrv::xlen::SignedWord>(
                static_cast<int32_t>(rrs1) >> (op.imm & 0x1f)));
            break;
#endif
        case FastOp::Lui:
            wb_data = op.imm;
            break;
        case FastOp::Auipc:
            wb_data = op.cpc.raw() + op.imm;
            break;
        case FastOp::CzeroEqz: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            wb_data = (rrs2 == 0) ? 0 : rrs1;
            break;
        }
        case FastOp::CzeroNez: {
            const simrv::xlen::Register rrs2 = state_.regs.read(op.rs2);
            wb_data = (rrs2 != 0) ? 0 : rrs1;
            break;
        }
        case FastOp::Bitmanip: {
            const auto opc = op.opcode();
            const simrv::xlen::Register in2 =
                (opc == isa::Opcode::OpImm || opc == isa::Opcode::OpImm32)
                    ? static_cast<simrv::xlen::Register>(op.imm)
                    : state_.regs.read(op.rs2);
#if SIMRV_XLEN == 64
            if (opc == isa::Opcode::Op32 || opc == isa::Opcode::OpImm32) {
                wb_data = execute::ExecuteUnitZb::aluIntZbW(rrs1, in2, op.op_id);
                break;
            }
#endif
            wb_data = execute::ExecuteUnitZb::aluIntZb(rrs1, in2, op.op_id, kGuestXlen64 ? 64 : 32);
            break;
        }
        default:
            __builtin_unreachable();
    }
    if constexpr (kCopyContext) {
        op.copy_to(pipeline_context);
        pipeline_context.tlb_miss = false;
        pipeline_context.pending_tval = 0;
    }
    if constexpr (kInstMix) {
        e_instmix[static_cast<std::size_t>(op.op_id)]++;
        perf_counters.record_retire(op.op_id, 1);
    }
    if constexpr (simrv::xlen::kIsXLen64) {
        if constexpr (kGuestXlen64) {
            state_.regs.write_xlen64(op.rd, wb_data);
        } else {
            state_.regs.write_xlen32(op.rd, wb_data);
        }
    } else {
        state_.regs.write_xlen32(op.rd, wb_data);
    }
    if constexpr (!kDeferAluCommit) {
        advance_cached_pc_alu(op);
    }
    return true;
}

template <bool kCopyContext, bool kInstMix, bool kDeferAluCommit>
SIMRV_ALWAYS_INLINE auto CPU::execute_cached_alu_fast(CachedOp& op) -> bool {
    if constexpr (simrv::xlen::kIsXLen64) {
        if (simrv::compiler::likely(state_.regs.xlen == 64)) {
            return execute_cached_alu_fast_xlen<true, kCopyContext, kInstMix, kDeferAluCommit>(op);
        }
    }
    return execute_cached_alu_fast_xlen<false, kCopyContext, kInstMix, kDeferAluCommit>(op);
}

}  // namespace simrv::core
