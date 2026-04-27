/**
 * @file ExecuteUnitInt.cpp
 * @brief SimRV integer execution unit implementation.
 */
#include <cstdint>

#include "Define.hpp"
#include "ExecuteUnit.hpp"
#include "XLen.hpp"

namespace {
constexpr Word D_SHIFT_MASK = xlen_shift_mask();
constexpr Instruction D_MULDIV_FLAG = static_cast<Instruction>(0x1U);
constexpr Register D_WORD_LOW_MASK = static_cast<Register>(0xffffffffU);
constexpr Register D_DIV_BY_ZERO_RESULT = static_cast<Register>(0xffffffffU);
constexpr Register D_NEG_ONE = static_cast<Register>(0xffffffffU);
constexpr unsigned D_WORD_BITS = 32U;
}  // namespace

auto ExecuteUnit::aluInt(Register in1, Register in2, Instruction funct3,
                         Instruction funct7)  // NOLINT(bugprone-easily-swappable-parameters)
    -> Register {
    Word ret = 0;
    Word const shamt = in2 & D_SHIFT_MASK;
    const auto f3 = static_cast<Funct3>(funct3);

    if ((funct7 & D_MULDIV_FLAG) == 0) {
        switch (f3) {
            case Funct3::Add:
                ret = (funct7 != 0u) ? (in1 - in2) : (in1 + in2);
                break;
            case Funct3::Sll:
                ret = in1 << shamt;
                break;
            case Funct3::Slt:
                ret =
                    static_cast<Word>(static_cast<SignedWord>(in1) < static_cast<SignedWord>(in2));
                break;
            case Funct3::Sltu:
                ret = static_cast<Word>(in1 < in2);
                break;
            case Funct3::Xor:
                ret = in1 ^ in2;
                break;
            case Funct3::Srl:
                ret = (funct7 != 0u) ? (static_cast<SignedWord>(in1) >> shamt) : (in1 >> shamt);
                break;
            case Funct3::Or:
                ret = in1 | in2;
                break;
            case Funct3::And:
                ret = in1 & in2;
                break;
            default:
                break;
        }
    } else {
        Counter const mul_SS = static_cast<int64_t>(static_cast<SignedWord>(in1)) *
                               static_cast<int64_t>(static_cast<SignedWord>(in2));
        Counter const mul_SU =
            static_cast<int64_t>(static_cast<SignedWord>(in1)) * static_cast<Counter>(in2);
        Counter const mul_UU = static_cast<Counter>(in1) * static_cast<Counter>(in2);
        switch (f3) {
            case Funct3::Mul:
                ret = mul_SS & D_WORD_LOW_MASK;
                break;
            case Funct3::Mulh:
                ret = (mul_SS >> D_WORD_BITS) & D_WORD_LOW_MASK;
                break;
            case Funct3::Mulhsu:
                ret = (mul_SU >> D_WORD_BITS) & D_WORD_LOW_MASK;
                break;
            case Funct3::Mulhu:
                ret = (mul_UU >> D_WORD_BITS) & D_WORD_LOW_MASK;
                break;
            case Funct3::Div:
                if (in2 == D_NEG_ONE) {
                    ret = in1;
                } else if (in2 == 0) {
                    ret = D_DIV_BY_ZERO_RESULT;
                } else {
                    ret = static_cast<SignedWord>(in1) / static_cast<SignedWord>(in2);
                }
                break;
            case Funct3::Divu:
                ret = (in2 == 0) ? D_DIV_BY_ZERO_RESULT : (in1 / in2);
                break;
            case Funct3::Rem:
                if (in2 == D_NEG_ONE) {
                    ret = 0;
                } else if (in2 == 0) {
                    ret = in1;
                } else {
                    ret = static_cast<SignedWord>(in1) % static_cast<SignedWord>(in2);
                }
                break;
            case Funct3::Remu:
                ret = (in2 == 0) ? in1 : (in1 % in2);
                break;
            default:
                break;
        }
    }
    return ret;
}

auto ExecuteUnit::branchTaken(Register in1, Register in2, Instruction funct3)
    -> Instruction {  // NOLINT(bugprone-easily-swappable-parameters)
    switch (static_cast<Funct3>(funct3)) {
        case Funct3::Beq:
            return static_cast<Instruction>(in1 == in2);
        case Funct3::Bne:
            return static_cast<Instruction>(in1 != in2);
        case Funct3::Blt:
            return static_cast<Instruction>(static_cast<SignedWord>(in1) <
                                            static_cast<SignedWord>(in2));
        case Funct3::Bge:
            return static_cast<Instruction>(static_cast<SignedWord>(in1) >=
                                            static_cast<SignedWord>(in2));
        case Funct3::Bltu:
            return static_cast<Instruction>(in1 < in2);
        case Funct3::Bgeu:
            return static_cast<Instruction>(in1 >= in2);
        default:
            return 0;
    }
}

auto ExecuteUnit::aluAmo(Register in1, Register in2, Instruction funct5)
    -> Register {  // NOLINT(bugprone-easily-swappable-parameters)
    switch (static_cast<Funct5Amo>(funct5)) {
        case Funct5Amo::Lr:
            return 0;
        case Funct5Amo::Sc:
        case Funct5Amo::Swap:
            return in1;
        case Funct5Amo::Add:
            return in1 + in2;
        case Funct5Amo::And:
            return in1 & in2;
        case Funct5Amo::Or:
            return in1 | in2;
        case Funct5Amo::Xor:
            return in1 ^ in2;
        case Funct5Amo::Min:
            return static_cast<SignedWord>(in1) < static_cast<SignedWord>(in2) ? in1 : in2;
        case Funct5Amo::Minu:
            return in1 < in2 ? in1 : in2;
        case Funct5Amo::Max:
            return static_cast<SignedWord>(in1) > static_cast<SignedWord>(in2) ? in1 : in2;
        case Funct5Amo::Maxu:
            return in1 > in2 ? in1 : in2;
        default:
            return 0;
    }
}

auto ExecuteUnit::csrWriteValue(CSRValue rcsr, Register rrs1, Instruction imm,
                                Instruction funct3)
    -> CSRValue {  // NOLINT(bugprone-easily-swappable-parameters)
    switch (static_cast<Funct3>(funct3)) {
        case Funct3::Csrrw:
            return rrs1;
        case Funct3::Csrrs:
            return rcsr | rrs1;
        case Funct3::Csrrc:
            return rcsr & (~rrs1);
        case Funct3::Csrrwi:
            return imm;
        case Funct3::Csrrsi:
            return rcsr | imm;
        case Funct3::Csrrci:
            return rcsr & (~imm);
        default:
            return 0;
    }
}
