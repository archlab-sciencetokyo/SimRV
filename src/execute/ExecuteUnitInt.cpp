/**
 * @file ExecuteUnitInt.cpp
 * @brief Integer ALU and control flow execution paths.
 */
#include <limits>

#include "simrv/Define.hpp"
#include "simrv/execute/ExecuteUnit.hpp"
#include "simrv/xlen/Math.hpp"

namespace simrv::execute {

auto ExecuteUnit::aluInt(Register in1, Register in2, Instruction funct3, Instruction funct7)
    -> Register {
    const bool is_m_extension = (funct7 & 0x01) != 0;

    if (is_m_extension) {
        switch (funct3) {
            case 0:
                return simrv::xlen::mul_low(in1, in2);  // MUL
            case 1:
                return simrv::xlen::mul_high_signed(in1, in2);  // MULH
            case 2:
                return simrv::xlen::mul_high_signed_unsigned(in1, in2);  // MULHSU
            case 3:
                return simrv::xlen::mul_high_unsigned(in1, in2);  // MULHU
            case 4: {                                             // DIV
                const auto lhs = static_cast<SignedWord>(in1);
                const auto rhs = static_cast<SignedWord>(in2);
                if (rhs == 0) return simrv::xlen::kWordAllOnes;
                if (lhs == std::numeric_limits<SignedWord>::min() && rhs == -1)
                    return static_cast<Register>(lhs);
                return static_cast<Register>(lhs / rhs);
            }
            case 5: {  // DIVU
                if (in2 == 0) return simrv::xlen::kWordAllOnes;
                return in1 / in2;
            }
            case 6: {  // REM
                const auto lhs = static_cast<SignedWord>(in1);
                const auto rhs = static_cast<SignedWord>(in2);
                if (rhs == 0) return in1;
                if (lhs == std::numeric_limits<SignedWord>::min() && rhs == -1) return 0;
                return static_cast<Register>(lhs % rhs);
            }
            case 7: {  // REMU
                if (in2 == 0) return in1;
                return in1 % in2;
            }
            default:
                return 0;
        }
    } else {
        switch (funct3) {
            case 0:
                return (funct7 & 0x20) ? (in1 - in2) : (in1 + in2);  // ADD / SUB
            case 1:
                return in1 << (in2 & simrv::xlen::xlen_shift_mask());  // SLL
            case 2:
                return static_cast<Register>(static_cast<SignedWord>(in1) <
                                             static_cast<SignedWord>(in2));  // SLT
            case 3:
                return static_cast<Register>(in1 < in2);  // SLTU
            case 4:
                return in1 ^ in2;  // XOR
            case 5: {              // SRL / SRA
                const Word shamt = in2 & simrv::xlen::xlen_shift_mask();
                if (funct7 & 0x20) {
                    // C++20 guarantees arithmetic right shift for signed types
                    return static_cast<Register>(static_cast<SignedWord>(in1) >> shamt);
                } else {
                    return in1 >> shamt;
                }
            }
            case 6:
                return in1 | in2;  // OR
            case 7:
                return in1 & in2;  // AND
            default:
                return 0;
        }
    }
}

auto ExecuteUnit::aluIntW(Opcode opcode, Register in1, Register in2, Instruction funct3,
                          Instruction funct7) -> Register {
    if constexpr (!simrv::xlen::kIsXLen64) {
        return 0;  // W instructions are invalid on RV32
    } else {
        const bool is_m_extension = (funct7 & 0x01) != 0;
        const uint32_t lhs32 = static_cast<uint32_t>(in1);
        const uint32_t rhs32 = static_cast<uint32_t>(in2);
        int32_t res32 = 0;

        if (opcode == Opcode::Op32 && is_m_extension) {
            switch (funct3) {
                case 0:
                    res32 = static_cast<int32_t>(lhs32 * rhs32);
                    break;  // MULW
                case 4: {   // DIVW
                    const int32_t lhs_s = static_cast<int32_t>(lhs32);
                    const int32_t rhs_s = static_cast<int32_t>(rhs32);
                    if (rhs_s == 0)
                        res32 = -1;
                    else if (lhs_s == std::numeric_limits<int32_t>::min() && rhs_s == -1)
                        res32 = lhs_s;
                    else
                        res32 = lhs_s / rhs_s;
                    break;
                }
                case 5: {  // DIVUW
                    if (rhs32 == 0)
                        res32 = -1;
                    else
                        res32 = static_cast<int32_t>(lhs32 / rhs32);
                    break;
                }
                case 6: {  // REMW
                    const int32_t lhs_s = static_cast<int32_t>(lhs32);
                    const int32_t rhs_s = static_cast<int32_t>(rhs32);
                    if (rhs_s == 0)
                        res32 = lhs_s;
                    else if (lhs_s == std::numeric_limits<int32_t>::min() && rhs_s == -1)
                        res32 = 0;
                    else
                        res32 = lhs_s % rhs_s;
                    break;
                }
                case 7: {  // REMUW
                    if (rhs32 == 0)
                        res32 = static_cast<int32_t>(lhs32);
                    else
                        res32 = static_cast<int32_t>(lhs32 % rhs32);
                    break;
                }
                default:
                    break;
            }
        } else {
            switch (funct3) {
                case 0:
                    res32 = static_cast<int32_t>(funct7 & 0x20 ? lhs32 - rhs32 : lhs32 + rhs32);
                    break;  // ADDW/SUBW
                case 1:
                    res32 = static_cast<int32_t>(lhs32 << (rhs32 & 0x1F));
                    break;  // SLLW
                case 5: {   // SRLW / SRAW
                    const uint32_t shamt = rhs32 & 0x1F;
                    if (funct7 & 0x20) {
                        res32 = static_cast<int32_t>(lhs32) >> shamt;  // SRAW
                    } else {
                        res32 = static_cast<int32_t>(lhs32 >> shamt);  // SRLW
                    }
                    break;
                }
                default:
                    break;
            }
        }
        // Sign-extend 32-bit result to 64-bit register via std::conditional SignedWord cast
        return static_cast<Register>(static_cast<SignedWord>(res32));
    }
}

auto ExecuteUnit::branchTaken(Register in1, Register in2, Instruction funct3) -> Instruction {
    switch (funct3) {
        case 0:
            return in1 == in2 ? 1 : 0;  // BEQ
        case 1:
            return in1 != in2 ? 1 : 0;  // BNE
        case 4:
            return static_cast<SignedWord>(in1) < static_cast<SignedWord>(in2) ? 1 : 0;  // BLT
        case 5:
            return static_cast<SignedWord>(in1) >= static_cast<SignedWord>(in2) ? 1 : 0;  // BGE
        case 6:
            return in1 < in2 ? 1 : 0;  // BLTU
        case 7:
            return in1 >= in2 ? 1 : 0;  // BGEU
        default:
            return 0;
    }
}

auto ExecuteUnit::aluAmo(Register in1, Register in2, Instruction funct5) -> Register {
    switch (funct5) {
        case 0x01:
            return in1;  // SWAP
        case 0x00:
            return in2 + in1;  // ADD
        case 0x0c:
            return in2 & in1;  // AND
        case 0x08:
            return in2 | in1;  // OR
        case 0x04:
            return in2 ^ in1;  // XOR
        case 0x10:
            return static_cast<SignedWord>(in1) < static_cast<SignedWord>(in2) ? in1 : in2;  // MIN
        case 0x14:
            return static_cast<SignedWord>(in1) > static_cast<SignedWord>(in2) ? in1 : in2;  // MAX
        case 0x18:
            return in1 < in2 ? in1 : in2;  // MINU
        case 0x1c:
            return in1 > in2 ? in1 : in2;  // MAXU
        default:
            return in2;
    }
}

auto ExecuteUnit::csrWriteValue(CSRValue rcsr, Register rrs1, Instruction imm, Instruction funct3)
    -> std::expected<CSRValue, TrapCause> {
    const Word zimm = static_cast<Word>(imm);
    switch (funct3) {
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