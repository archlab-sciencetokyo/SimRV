#include <algorithm>
#include <limits>

#include "simrv/Define.hpp"
#include "simrv/execute/ExecuteUnit.hpp"
#include "simrv/xlen/Math.hpp"

namespace simrv::execute {

using simrv::isa::Funct3;
using simrv::isa::Funct5Amo;
using simrv::isa::Opcode;

namespace {
template <typename T>
constexpr auto perform_amo_op(T reg_val, T mem_val, Funct5Amo funct5) -> T {
    using SignedT = std::make_signed_t<T>;
    using UnsignedT = std::make_unsigned_t<T>;
    switch (funct5) {
        case Funct5Amo::Swap:
            return reg_val;
        case Funct5Amo::Add:
            return mem_val + reg_val;
        case Funct5Amo::And:
            return mem_val & reg_val;
        case Funct5Amo::Or:
            return mem_val | reg_val;
        case Funct5Amo::Xor:
            return mem_val ^ reg_val;
        case Funct5Amo::Min:
            return static_cast<T>(
                std::min(static_cast<SignedT>(reg_val), static_cast<SignedT>(mem_val)));
        case Funct5Amo::Max:
            return static_cast<T>(
                std::max(static_cast<SignedT>(reg_val), static_cast<SignedT>(mem_val)));
        case Funct5Amo::Minu:
            return static_cast<T>(
                std::min(static_cast<UnsignedT>(reg_val), static_cast<UnsignedT>(mem_val)));
        case Funct5Amo::Maxu:
            return static_cast<T>(
                std::max(static_cast<UnsignedT>(reg_val), static_cast<UnsignedT>(mem_val)));
        default:
            return mem_val;
    }
}
}  // namespace

auto ExecuteUnit::aluInt32(Register in1, Register in2, isa::OperationId op_id) -> Register {
    using enum isa::OperationId;
    const auto u1 = static_cast<uint32_t>(in1);
    const auto u2 = static_cast<uint32_t>(in2);
    const auto s1 = static_cast<int32_t>(in1);
    const auto s2 = static_cast<int32_t>(in2);
    int32_t res32 = 0;

    switch (op_id) {
        // Base Arithmetic
        case ADD:
        case ADDI:
            res32 = static_cast<int32_t>(u1 + u2);
            break;
        case SUB:
            res32 = static_cast<int32_t>(u1 - u2);
            break;
        case SLL:
        case SLLI:
            res32 = static_cast<int32_t>(u1 << (u2 & 0x1F));
            break;
        case SLT:
        case SLTI:
            res32 = (s1 < s2) ? 1 : 0;
            break;
        case SLTU:
        case SLTIU:
            res32 = (u1 < u2) ? 1 : 0;
            break;
        case XOR:
        case XORI:
            res32 = static_cast<int32_t>(u1 ^ u2);
            break;
        case SRL:
        case SRLI:
            res32 = static_cast<int32_t>(u1 >> (u2 & 0x1F));
            break;
        case SRA:
        case SRAI:
            res32 = s1 >> (u2 & 0x1F);
            break;
        case OR:
        case ORI:
            res32 = static_cast<int32_t>(u1 | u2);
            break;
        case AND:
        case ANDI:
            res32 = static_cast<int32_t>(u1 & u2);
            break;

        // M-Extension
        case MUL:
            res32 = static_cast<int32_t>(u1 * u2);
            break;
        case MULH:
            res32 =
                static_cast<int32_t>((static_cast<int64_t>(s1) * static_cast<int64_t>(s2)) >> 32);
            break;
        case MULHSU:
            res32 =
                static_cast<int32_t>((static_cast<int64_t>(s1) * static_cast<uint64_t>(u2)) >> 32);
            break;
        case MULHU:
            res32 =
                static_cast<int32_t>((static_cast<uint64_t>(u1) * static_cast<uint64_t>(u2)) >> 32);
            break;
        case DIV:
            if (u2 == 0)
                res32 = -1;
            else if (s1 == std::numeric_limits<int32_t>::min() && s2 == -1)
                res32 = s1;
            else
                res32 = s1 / s2;
            break;
        case DIVU:
            if (u2 == 0)
                res32 = -1;
            else
                res32 = static_cast<int32_t>(u1 / u2);
            break;
        case REM:
            if (u2 == 0)
                res32 = s1;
            else if (s1 == std::numeric_limits<int32_t>::min() && s2 == -1)
                res32 = 0;
            else
                res32 = s1 % s2;
            break;
        case REMU:
            if (u2 == 0)
                res32 = static_cast<int32_t>(u1);
            else
                res32 = static_cast<int32_t>(u1 % u2);
            break;

        default:
            return ExecuteUnit::aluIntB(in1, in2, op_id, 32);
    }
    return static_cast<Register>(static_cast<int64_t>(res32));
}

auto ExecuteUnit::aluInt(Register in1, Register in2, isa::OperationId op_id, unsigned xlen)
    -> Register {
    using enum isa::OperationId;

    if constexpr (!simrv::xlen::kIsXLen64) {
        xlen = 32;
    }
    if (simrv::compiler::unlikely(xlen == 32)) {
        return aluInt32(in1, in2, op_id);
    }

    switch (op_id) {
        // Base Arithmetic
        case ADD:
        case ADDI:
            return in1 + in2;
        case SUB:
            return in1 - in2;
        case SLL:
        case SLLI:
            return in1 << (in2 & simrv::xlen::xlen_shift_mask());
        case SLT:
        case SLTI:
            return static_cast<Register>(static_cast<SignedWord>(in1) <
                                         static_cast<SignedWord>(in2));
        case SLTU:
        case SLTIU:
            return static_cast<Register>(in1 < in2);
        case XOR:
        case XORI:
            return in1 ^ in2;
        case SRL:
        case SRLI:
            return in1 >> (in2 & simrv::xlen::xlen_shift_mask());
        case SRA:
        case SRAI:
            return static_cast<Register>(static_cast<SignedWord>(in1) >>
                                         (in2 & simrv::xlen::xlen_shift_mask()));
        case OR:
        case ORI:
            return in1 | in2;
        case AND:
        case ANDI:
            return in1 & in2;

        // M-Extension
        case MUL:
            return simrv::xlen::mul_low(in1, in2);
        case MULH:
            return simrv::xlen::mul_high_signed(in1, in2);
        case MULHSU:
            return simrv::xlen::mul_high_signed_unsigned(in1, in2);
        case MULHU:
            return simrv::xlen::mul_high_unsigned(in1, in2);
        case DIV: {
            const auto lhs = static_cast<SignedWord>(in1);
            const auto rhs = static_cast<SignedWord>(in2);
            if (rhs == 0) return simrv::xlen::kWordAllOnes;
            if (lhs == std::numeric_limits<SignedWord>::min() && rhs == -1)
                return static_cast<Register>(lhs);
            return static_cast<Register>(lhs / rhs);
        }
        case DIVU: {
            if (in2 == 0) return simrv::xlen::kWordAllOnes;
            return in1 / in2;
        }
        case REM: {
            const auto lhs = static_cast<SignedWord>(in1);
            const auto rhs = static_cast<SignedWord>(in2);
            if (rhs == 0) return in1;
            if (lhs == std::numeric_limits<SignedWord>::min() && rhs == -1) return 0;
            return static_cast<Register>(lhs % rhs);
        }
        case REMU: {
            if (in2 == 0) return in1;
            return in1 % in2;
        }

        // B-Extension cases (delegated to ExecuteUnitB.cpp)
        case SH1ADD:
        case SH2ADD:
        case SH3ADD:
        case ANDN:
        case ORN:
        case XNOR:
        case CLZ:
        case CTZ:
        case CPOP:
        case MIN:
        case MAX:
        case MINU:
        case MAXU:
        case SEXT_B:
        case SEXT_H:
        case ZEXT_H:
        case ROL:
        case ROR:
        case RORI:
        case CLMUL:
        case CLMULH:
        case CLMULR:
        case BSET:
        case BSETI:
        case BCLR:
        case BCLRI:
        case BINV:
        case BINVI:
        case BEXT:
        case BEXTI:
        case ORC_B:
        case REV8:
        case PACK:
            return aluIntB(in1, in2, op_id, xlen);

        default:
            return 0;
    }
}

auto ExecuteUnit::aluIntW(Register in1, Register in2, isa::OperationId op_id) -> Register {
    if constexpr (!simrv::xlen::kIsXLen64) {
        return 0;  // W instructions are invalid on RV32
    } else {
        using enum isa::OperationId;
        const auto lhs32 = static_cast<uint32_t>(in1);
        const auto rhs32 = static_cast<uint32_t>(in2);
        int32_t res32 = 0;

        switch (op_id) {
            // Base W Arithmetic
            case ADDW:
            case ADDIW:
                res32 = static_cast<int32_t>(lhs32 + rhs32);
                break;
            case SUBW:
                res32 = static_cast<int32_t>(lhs32 - rhs32);
                break;
            case SLLW:
            case SLLIW:
                res32 = static_cast<int32_t>(lhs32 << (rhs32 & 0x1F));
                break;
            case SRLW:
            case SRLIW:
                res32 = static_cast<int32_t>(lhs32 >> (rhs32 & 0x1F));
                break;
            case SRAW:
            case SRAIW:
                res32 = static_cast<int32_t>(lhs32) >> (rhs32 & 0x1F);
                break;

            // M-Extension W
            case MULW:
                res32 = static_cast<int32_t>(lhs32 * rhs32);
                break;
            case DIVW: {
                const auto lhs_s = static_cast<int32_t>(lhs32);
                const auto rhs_s = static_cast<int32_t>(rhs32);
                if (rhs_s == 0)
                    res32 = -1;
                else if (lhs_s == std::numeric_limits<int32_t>::min() && rhs_s == -1)
                    res32 = lhs_s;
                else
                    res32 = lhs_s / rhs_s;
                break;
            }
            case DIVUW: {
                if (rhs32 == 0)
                    res32 = -1;
                else
                    res32 = static_cast<int32_t>(lhs32 / rhs32);
                break;
            }
            case REMW: {
                const auto lhs_s = static_cast<int32_t>(lhs32);
                const auto rhs_s = static_cast<int32_t>(rhs32);
                if (rhs_s == 0)
                    res32 = lhs_s;
                else if (lhs_s == std::numeric_limits<int32_t>::min() && rhs_s == -1)
                    res32 = 0;
                else
                    res32 = lhs_s % rhs_s;
                break;
            }
            case REMUW: {
                if (rhs32 == 0)
                    res32 = static_cast<int32_t>(lhs32);
                else
                    res32 = static_cast<int32_t>(lhs32 % rhs32);
                break;
            }

            // B-Extension W cases (delegated to ExecuteUnitB.cpp)
            case ADD_UW:
            case SLLI_UW:
            case SH1ADD_UW:
            case SH2ADD_UW:
            case SH3ADD_UW:
            case CLZW:
            case CTZW:
            case CPOPW:
            case ROLW:
            case RORW:
            case RORIW:
            case PACKW:
                return aluIntBW(in1, in2, op_id);

            default:
                break;
        }

        // Sign-extend 32-bit result to 64-bit register
        return static_cast<Register>(static_cast<SignedWord>(res32));
    }
}

auto ExecuteUnit::branchTaken(Register in1, Register in2, Funct3 funct3, unsigned xlen) -> bool {
    if constexpr (!simrv::xlen::kIsXLen64) {
        xlen = 32;
    }
    if (simrv::compiler::unlikely(xlen == 32)) {
        const auto u1 = static_cast<uint32_t>(in1);
        const auto u2 = static_cast<uint32_t>(in2);
        const auto s1 = static_cast<int32_t>(in1);
        const auto s2 = static_cast<int32_t>(in2);
        switch (enum_mask(funct3)) {
            case 0:
                return u1 == u2;  // BEQ
            case 1:
                return u1 != u2;  // BNE
            case 4:
                return s1 < s2;  // BLT
            case 5:
                return s1 >= s2;  // BGE
            case 6:
                return u1 < u2;  // BLTU
            case 7:
                return u1 >= u2;  // BGEU
            default:
                return false;
        }
    }

    switch (enum_mask(funct3)) {
        case 0:
            return in1 == in2;  // BEQ
        case 1:
            return in1 != in2;  // BNE
        case 4:
            return static_cast<SignedWord>(in1) < static_cast<SignedWord>(in2);  // BLT
        case 5:
            return static_cast<SignedWord>(in1) >= static_cast<SignedWord>(in2);  // BGE
        case 6:
            return in1 < in2;  // BLTU
        case 7:
            return in1 >= in2;  // BGEU
        default:
            return false;
    }
}

auto ExecuteUnit::aluAmo(Register in1, Register in2, Funct5Amo funct5, Funct3 funct3) -> Register {
    if constexpr (kIsXLen64) {
        if (funct3 == static_cast<Funct3>(2)) {  // AMO*W (32-bit)
            const auto res32 = perform_amo_op<int32_t>(static_cast<int32_t>(in1),
                                                       static_cast<int32_t>(in2), funct5);
            return static_cast<Register>(static_cast<int64_t>(res32));  // sign-extend to 64-bit
        }
    }

    return perform_amo_op<Word>(in1, in2, funct5);
}

auto ExecuteUnit::csrWriteValue(CSRValue rcsr, Register rrs1, ImmValue imm, Funct3 funct3)
    -> std::expected<CSRValue, TrapCause> {
    const Word zimm = static_cast<Word>(imm);
    switch (enum_mask(funct3)) {
        case 1:
            return static_cast<CSRValue>(rrs1);  // CSRRW
        case 2:
            return rcsr | static_cast<CSRValue>(rrs1);  // CSRRS
        case 3:
            return rcsr & ~static_cast<CSRValue>(rrs1);  // CSRRC
        case 5:
            return static_cast<CSRValue>(zimm);  // CSRRWI
        case 6:
            return rcsr | static_cast<CSRValue>(zimm);  // CSRRSI
        case 7:
            return rcsr & ~static_cast<CSRValue>(zimm);  // CSRRCI
        default:
            return std::unexpected(enum_mask(ExceptionCode::IllegalInstruction));
    }
}

}  // namespace simrv::execute
