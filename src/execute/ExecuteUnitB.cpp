#include <algorithm>
#include <bit>

#include "simrv/Define.hpp"
#include "simrv/execute/ExecuteUnit.hpp"

namespace simrv::execute {

auto ExecuteUnit::aluIntB(Register in1, Register in2, isa::OperationId op_id) -> Register {
    using enum isa::OperationId;
    constexpr int xlen = sizeof(Register) * 8;

    switch (op_id) {
        // B-Extension: Zba
        case SH1ADD:
            return in2 + (in1 << 1);
        case SH2ADD:
            return in2 + (in1 << 2);
        case SH3ADD:
            return in2 + (in1 << 3);

        // B-Extension: Zbb logical
        case ANDN:
            return in1 & ~in2;
        case ORN:
            return in1 | ~in2;
        case XNOR:
            return ~(in1 ^ in2);

        // B-Extension: Zbb counting
        case CLZ:
            return static_cast<Register>(std::countl_zero(in1));
        case CTZ:
            return static_cast<Register>(std::countr_zero(in1));
        case CPOP:
            return static_cast<Register>(std::popcount(in1));

        // B-Extension: Zbb min/max
        case MIN:
            return static_cast<Register>(std::min(static_cast<SignedWord>(in1), static_cast<SignedWord>(in2)));
        case MAX:
            return static_cast<Register>(std::max(static_cast<SignedWord>(in1), static_cast<SignedWord>(in2)));
        case MINU:
            return std::min(in1, in2);
        case MAXU:
            return std::max(in1, in2);

        // B-Extension: Zbb sign/zero extensions
        case SEXT_B:
            return static_cast<Register>(static_cast<SignedWord>(static_cast<int8_t>(in1)));
        case SEXT_H:
            return static_cast<Register>(static_cast<SignedWord>(static_cast<int16_t>(in1)));
        case ZEXT_H:
            return in1 & 0xFFFFu;

        // B-Extension: Zbb rotations
        case ROL:
            return std::rotl(in1, static_cast<int>(in2 & (xlen - 1)));
        case ROR:
        case RORI:
            return std::rotr(in1, static_cast<int>(in2 & (xlen - 1)));

        // B-Extension: Zbc carry-less multiply
        case CLMUL: {
            Register r = 0;
            for (int i = 0; i < xlen; i++) {
                if ((in2 >> i) & 1) {
                    r ^= (in1 << i);
                }
            }
            return r;
        }
        case CLMULH: {
            Register r = 0;
            for (int i = 1; i < xlen; i++) {
                if ((in2 >> i) & 1) {
                    r ^= (in1 >> (xlen - i));
                }
            }
            return r;
        }
        case CLMULR: {
            Register r = 0;
            for (int i = 0; i < xlen; i++) {
                if ((in2 >> i) & 1) {
                    r ^= (in1 >> (xlen - 1 - i));
                }
            }
            return r;
        }

        // B-Extension: Zbs single-bit
        case BSET:
        case BSETI:
            return in1 | (static_cast<Register>(1) << (in2 & (xlen - 1)));
        case BCLR:
        case BCLRI:
            return in1 & ~(static_cast<Register>(1) << (in2 & (xlen - 1)));
        case BINV:
        case BINVI:
            return in1 ^ (static_cast<Register>(1) << (in2 & (xlen - 1)));
        case BEXT:
        case BEXTI:
            return (in1 >> (in2 & (xlen - 1))) & 1;

        // B-Extension: Zbb pseudo-ops
        case ORC_B: {
            Register r = 0;
            for (size_t i = 0; i < sizeof(Register); i++) {
                if ((in1 >> (i * 8)) & 0xFF) {
                    r |= static_cast<Register>(0xFF) << (i * 8);
                }
            }
            return r;
        }
        case REV8:
            return std::byteswap(in1);
        case PACK: {
            constexpr int half_xlen = sizeof(Register) * 4;
            Register mask = (static_cast<Register>(1) << half_xlen) - 1;
            return (in1 & mask) | (in2 << half_xlen);
        }

        default:
            return 0;
    }
}

auto ExecuteUnit::aluIntBW(Register in1, Register in2, isa::OperationId op_id) -> Register {
    if constexpr (!simrv::xlen::kIsXLen64) {
        return 0;
    } else {
        using enum isa::OperationId;
        const auto lhs32 = static_cast<uint32_t>(in1);
        const auto rhs32 = static_cast<uint32_t>(in2);
        int32_t res32 = 0;

        switch (op_id) {
            // B-Extension W: Zba
            case ADD_UW:
                return static_cast<Register>(static_cast<uint64_t>(lhs32) + in2);
            case SLLI_UW:
                return static_cast<Register>(static_cast<uint64_t>(lhs32) << (in2 & 0x3F));
            case SH1ADD_UW:
                return static_cast<Register>(in2 + (static_cast<uint64_t>(lhs32) << 1));
            case SH2ADD_UW:
                return static_cast<Register>(in2 + (static_cast<uint64_t>(lhs32) << 2));
            case SH3ADD_UW:
                return static_cast<Register>(in2 + (static_cast<uint64_t>(lhs32) << 3));

            // B-Extension W: Zbb
            case CLZW:
                res32 = static_cast<int32_t>(std::countl_zero(lhs32));
                break;
            case CTZW:
                res32 = static_cast<int32_t>(std::countr_zero(lhs32));
                break;
            case CPOPW:
                res32 = static_cast<int32_t>(std::popcount(lhs32));
                break;
            case ROLW:
                res32 = static_cast<int32_t>(std::rotl(lhs32, static_cast<int>(in2 & 31)));
                break;
            case RORW:
            case RORIW:
                res32 = static_cast<int32_t>(std::rotr(lhs32, static_cast<int>(in2 & 31)));
                break;
            case PACKW:
                res32 = static_cast<int32_t>((lhs32 & 0xFFFF) | (rhs32 << 16));
                break;

            default:
                break;
        }

        return static_cast<Register>(static_cast<SignedWord>(res32));
    }
}

} // namespace simrv::execute
