#include "simrv/execute/ExecuteUnitInt.hpp"

#include <limits>

#include "simrv/CompilerHints.hpp"
#include "simrv/execute/ExecuteUnitZb.hpp"
#include "simrv/isa/TrapTypes.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Math.hpp"

namespace simrv::execute {

using simrv::isa::Funct3;
using simrv::isa::Funct5Amo;
using simrv::isa::Opcode;

using simrv::isa::perform_amo_op;

auto ExecuteUnitInt::aluInt32(simrv::xlen::Register in1, simrv::xlen::Register in2,
                              isa::OperationId op_id) -> simrv::xlen::Register {
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
            return ExecuteUnitZb::aluIntZb(in1, in2, op_id, 32);
    }
    return static_cast<simrv::xlen::Register>(static_cast<int64_t>(res32));
}

auto ExecuteUnitInt::aluInt(simrv::xlen::Register in1, simrv::xlen::Register in2,
                            isa::OperationId op_id, unsigned xlen) -> simrv::xlen::Register {
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
            return static_cast<simrv::xlen::Register>(static_cast<simrv::xlen::SignedWord>(in1) <
                                                      static_cast<simrv::xlen::SignedWord>(in2));
        case SLTU:
        case SLTIU:
            return static_cast<simrv::xlen::Register>(in1 < in2);
        case XOR:
        case XORI:
            return in1 ^ in2;
        case SRL:
        case SRLI:
            return in1 >> (in2 & simrv::xlen::xlen_shift_mask());
        case SRA:
        case SRAI:
            return static_cast<simrv::xlen::Register>(static_cast<simrv::xlen::SignedWord>(in1) >>
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
            const auto lhs = static_cast<simrv::xlen::SignedWord>(in1);
            const auto rhs = static_cast<simrv::xlen::SignedWord>(in2);
            if (rhs == 0) return simrv::xlen::kWordAllOnes;
            if (lhs == std::numeric_limits<simrv::xlen::SignedWord>::min() && rhs == -1)
                return static_cast<simrv::xlen::Register>(lhs);
            return static_cast<simrv::xlen::Register>(lhs / rhs);
        }
        case DIVU: {
            if (in2 == 0) return simrv::xlen::kWordAllOnes;
            return in1 / in2;
        }
        case REM: {
            const auto lhs = static_cast<simrv::xlen::SignedWord>(in1);
            const auto rhs = static_cast<simrv::xlen::SignedWord>(in2);
            if (rhs == 0) return in1;
            if (lhs == std::numeric_limits<simrv::xlen::SignedWord>::min() && rhs == -1) return 0;
            return static_cast<simrv::xlen::Register>(lhs % rhs);
        }
        case REMU: {
            if (in2 == 0) return in1;
            return in1 % in2;
        }

        default:
            if (isa::is_zb_op(op_id)) {
                return ExecuteUnitZb::aluIntZb(in1, in2, op_id, xlen);
            }
            return 0;
    }
}

auto ExecuteUnitInt::aluIntW(simrv::xlen::Register in1, simrv::xlen::Register in2,
                             isa::OperationId op_id) -> simrv::xlen::Register {
    if constexpr (!simrv::xlen::kIsXLen64) {
        return 0;  // W instructions are invalid on RV32
    } else {
        using enum isa::OperationId;
        const auto lhs32 = static_cast<uint32_t>(in1);
        const auto rhs32 = static_cast<uint32_t>(in2);

        switch (op_id) {
            // Base W Arithmetic
            case ADDW:
            case ADDIW:
                return static_cast<simrv::xlen::Register>(
                    static_cast<int64_t>(static_cast<int32_t>(lhs32 + rhs32)));
            case SUBW:
                return static_cast<simrv::xlen::Register>(
                    static_cast<int64_t>(static_cast<int32_t>(lhs32 - rhs32)));
            case SLLW:
            case SLLIW:
                return static_cast<simrv::xlen::Register>(
                    static_cast<int64_t>(static_cast<int32_t>(lhs32 << (rhs32 & 0x1F))));
            case SRLW:
            case SRLIW:
                return static_cast<simrv::xlen::Register>(
                    static_cast<int64_t>(static_cast<int32_t>(lhs32 >> (rhs32 & 0x1F))));
            case SRAW:
            case SRAIW:
                return static_cast<simrv::xlen::Register>(
                    static_cast<int64_t>(static_cast<int32_t>(lhs32) >> (rhs32 & 0x1F)));

            // M-Extension W
            case MULW:
            case DIVW:
            case DIVUW:
            case REMW:
            case REMUW: {
                isa::OperationId base_op = ADD;
                if (op_id == MULW)
                    base_op = MUL;
                else if (op_id == DIVW)
                    base_op = DIV;
                else if (op_id == DIVUW)
                    base_op = DIVU;
                else if (op_id == REMW)
                    base_op = REM;
                else if (op_id == REMUW)
                    base_op = REMU;
                return aluInt32(lhs32, rhs32, base_op);
            }

            default:
                if (isa::is_zb_op(op_id)) {
                    return ExecuteUnitZb::aluIntZbW(in1, in2, op_id);
                }
                return 0;
        }
    }
}

auto ExecuteUnitInt::branchTaken(simrv::xlen::Register in1, simrv::xlen::Register in2,
                                 Funct3 funct3, unsigned xlen) -> bool {
    if constexpr (!simrv::xlen::kIsXLen64) {
        xlen = 32;
    }
    if (simrv::compiler::unlikely(xlen == 32)) {
        const auto u1 = static_cast<uint32_t>(in1);
        const auto u2 = static_cast<uint32_t>(in2);
        const auto s1 = static_cast<int32_t>(in1);
        const auto s2 = static_cast<int32_t>(in2);
        switch (funct3) {
            case Funct3::Beq:
                return u1 == u2;
            case Funct3::Bne:
                return u1 != u2;
            case Funct3::Blt:
                return s1 < s2;
            case Funct3::Bge:
                return s1 >= s2;
            case Funct3::Bltu:
                return u1 < u2;
            case Funct3::Bgeu:
                return u1 >= u2;
            default:
                return false;
        }
    }

    switch (funct3) {
        case Funct3::Beq:
            return in1 == in2;
        case Funct3::Bne:
            return in1 != in2;
        case Funct3::Blt:
            return static_cast<simrv::xlen::SignedWord>(in1) <
                   static_cast<simrv::xlen::SignedWord>(in2);
        case Funct3::Bge:
            return static_cast<simrv::xlen::SignedWord>(in1) >=
                   static_cast<simrv::xlen::SignedWord>(in2);
        case Funct3::Bltu:
            return in1 < in2;
        case Funct3::Bgeu:
            return in1 >= in2;
        default:
            return false;
    }
}

auto ExecuteUnitInt::aluAmo(simrv::xlen::Register in1, simrv::xlen::Register in2, Funct5Amo funct5,
                            Funct3 funct3) -> simrv::xlen::Register {
    if constexpr (simrv::xlen::kIsXLen64) {
        if (funct3 == static_cast<Funct3>(2)) {  // AMO*W (32-bit)
            const auto res32 = perform_amo_op<int32_t>(static_cast<int32_t>(in1),
                                                       static_cast<int32_t>(in2), funct5);
            return static_cast<simrv::xlen::Register>(
                static_cast<int64_t>(res32));  // sign-extend to 64-bit
        }
    }

    return perform_amo_op<simrv::xlen::Word>(in1, in2, funct5);
}

auto ExecuteUnitInt::csrWriteValue(simrv::xlen::CsrValue rcsr, simrv::xlen::Register rrs1,
                                   simrv::xlen::ImmValue imm, Funct3 funct3)
    -> std::expected<simrv::xlen::CsrValue, simrv::xlen::TrapCause> {
    const auto zimm = static_cast<simrv::xlen::Word>(imm);
    switch (funct3) {
        case Funct3::Csrrw:
            return static_cast<simrv::xlen::CsrValue>(rrs1);
        case Funct3::Csrrs:
            return rcsr | static_cast<simrv::xlen::CsrValue>(rrs1);
        case Funct3::Csrrc:
            return rcsr & ~static_cast<simrv::xlen::CsrValue>(rrs1);
        case Funct3::Csrrwi:
            return static_cast<simrv::xlen::CsrValue>(zimm);
        case Funct3::Csrrsi:
            return rcsr | static_cast<simrv::xlen::CsrValue>(zimm);
        case Funct3::Csrrci:
            return rcsr & ~static_cast<simrv::xlen::CsrValue>(zimm);
        default:
            return std::unexpected(to_trap_cause(ExceptionCode::IllegalInstruction));
    }
}

}  // namespace simrv::execute
