#include <algorithm>
#include <bit>

#include "simrv/Define.hpp"
#include "simrv/execute/ExecuteUnit.hpp"

namespace simrv::execute {

namespace {

auto alu_int_b32(uint32_t u1, uint32_t u2, int32_t s1, int32_t s2, isa::OperationId op_id)
    -> int32_t {
    using enum isa::OperationId;
    int32_t res32 = 0;
    switch (op_id) {
        case SH1ADD:
            res32 = static_cast<int32_t>(u2 + (u1 << 1));
            break;
        case SH2ADD:
            res32 = static_cast<int32_t>(u2 + (u1 << 2));
            break;
        case SH3ADD:
            res32 = static_cast<int32_t>(u2 + (u1 << 3));
            break;
        case ANDN:
            res32 = static_cast<int32_t>(u1 & ~u2);
            break;
        case ORN:
            res32 = static_cast<int32_t>(u1 | ~u2);
            break;
        case XNOR:
            res32 = static_cast<int32_t>(~(u1 ^ u2));
            break;
        case CLZ:
            res32 = static_cast<int32_t>(std::countl_zero(u1));
            break;
        case CTZ:
            res32 = static_cast<int32_t>(std::countr_zero(u1));
            break;
        case CPOP:
            res32 = static_cast<int32_t>(std::popcount(u1));
            break;
        case MIN:
            res32 = std::min(s1, s2);
            break;
        case MAX:
            res32 = std::max(s1, s2);
            break;
        case MINU:
            res32 = static_cast<int32_t>(std::min(u1, u2));
            break;
        case MAXU:
            res32 = static_cast<int32_t>(std::max(u1, u2));
            break;
        case SEXT_B:
            res32 = static_cast<int32_t>(static_cast<int8_t>(u1));
            break;
        case SEXT_H:
            res32 = static_cast<int32_t>(static_cast<int16_t>(u1));
            break;
        case ZEXT_H:
            res32 = static_cast<int32_t>(u1 & 0xFFFFu);
            break;
        case ROL:
            res32 = static_cast<int32_t>(std::rotl(u1, static_cast<int>(u2 & 31)));
            break;
        case ROR:
        case RORI:
            res32 = static_cast<int32_t>(std::rotr(u1, static_cast<int>(u2 & 31)));
            break;
        case BSET:
        case BSETI:
            res32 = static_cast<int32_t>(u1 | (1u << (u2 & 31)));
            break;
        case BCLR:
        case BCLRI:
            res32 = static_cast<int32_t>(u1 & ~(1u << (u2 & 31)));
            break;
        case BINV:
        case BINVI:
            res32 = static_cast<int32_t>(u1 ^ (1u << (u2 & 31)));
            break;
        case BEXT:
        case BEXTI:
            res32 = static_cast<int32_t>((u1 >> (u2 & 31)) & 1u);
            break;
        case ORC_B: {
            uint32_t r = 0;
            if ((u1 & 0x000000FFu) != 0) r |= 0x000000FFu;
            if ((u1 & 0x0000FF00u) != 0) r |= 0x0000FF00u;
            if ((u1 & 0x00FF0000u) != 0) r |= 0x00FF0000u;
            if ((u1 & 0xFF000000u) != 0) r |= 0xFF000000u;
            res32 = static_cast<int32_t>(r);
            break;
        }
        case REV8: {
            res32 = static_cast<int32_t>(std::byteswap(u1));
            break;
        }
        case PACK:
            res32 = static_cast<int32_t>((u1 & 0xFFFFu) | ((u2 & 0xFFFFu) << 16));
            break;
        case CLMUL: {
            uint32_t r = 0;
            for (int i = 0; i < 32; i++) {
                if ((u2 >> i) & 1) r ^= (u1 << i);
            }
            res32 = static_cast<int32_t>(r);
            break;
        }
        case CLMULH: {
            uint32_t r = 0;
            for (int i = 1; i < 32; i++) {
                if ((u2 >> i) & 1) r ^= (u1 >> (32 - i));
            }
            res32 = static_cast<int32_t>(r);
            break;
        }
        case CLMULR: {
            uint32_t r = 0;
            for (int i = 0; i < 32; i++) {
                if ((u2 >> i) & 1) r ^= (u1 >> (31 - i));
            }
            res32 = static_cast<int32_t>(r);
            break;
        }
        default:
            break;
    }
    return res32;
}

auto alu_int_b64(Register in1, Register in2, isa::OperationId op_id, int xlen) -> Register {
    using enum isa::OperationId;
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
            return static_cast<Register>(
                std::min(static_cast<SignedWord>(in1), static_cast<SignedWord>(in2)));
        case MAX:
            return static_cast<Register>(
                std::max(static_cast<SignedWord>(in1), static_cast<SignedWord>(in2)));
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
            if ((in1 & 0x00000000000000FFULL) != 0) r |= 0x00000000000000FFULL;
            if ((in1 & 0x000000000000FF00ULL) != 0) r |= 0x000000000000FF00ULL;
            if ((in1 & 0x0000000000FF0000ULL) != 0) r |= 0x0000000000FF0000ULL;
            if ((in1 & 0x00000000FF000000ULL) != 0) r |= 0x00000000FF000000ULL;
            if constexpr (simrv::xlen::kIsXLen64) {
                if ((in1 & 0x000000FF00000000ULL) != 0) r |= 0x000000FF00000000ULL;
                if ((in1 & 0x0000FF0000000000ULL) != 0) r |= 0x0000FF0000000000ULL;
                if ((in1 & 0x00FF000000000000ULL) != 0) r |= 0x00FF000000000000ULL;
                if ((in1 & 0xFF00000000000000ULL) != 0) r |= 0xFF00000000000000ULL;
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

}  // namespace

auto ExecuteUnit::aluIntB(Register in1, Register in2, isa::OperationId op_id, unsigned xlen_param)
    -> Register {
    if (xlen_param == 32) {
        const auto u1 = static_cast<uint32_t>(in1);
        const auto u2 = static_cast<uint32_t>(in2);
        const auto s1 = static_cast<int32_t>(in1);
        const auto s2 = static_cast<int32_t>(in2);
        int32_t res32 = alu_int_b32(u1, u2, s1, s2, op_id);
        return static_cast<Register>(static_cast<int64_t>(res32));
    }
    return alu_int_b64(in1, in2, op_id, static_cast<int>(xlen_param));
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

}  // namespace simrv::execute
