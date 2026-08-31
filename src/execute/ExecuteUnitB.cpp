#include <algorithm>
#include <bit>

#include "simrv/Define.hpp"
#include "simrv/execute/ExecuteUnit.hpp"

namespace simrv::execute {

namespace {

template <typename T>
constexpr auto alu_int_b_eval(T in1, T in2, isa::OperationId op_id, int xlen) -> T {
    using enum isa::OperationId;
    using SignedT = std::make_signed_t<T>;
    const auto s1 = static_cast<SignedT>(in1);
    const auto s2 = static_cast<SignedT>(in2);
    const auto shift_mask = static_cast<unsigned>(xlen - 1);

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
            return static_cast<T>(std::countl_zero(in1));
        case CTZ:
            return static_cast<T>(std::countr_zero(in1));
        case CPOP:
            return static_cast<T>(std::popcount(in1));

        // B-Extension: Zbb min/max
        case MIN:
            return static_cast<T>(std::min(s1, s2));
        case MAX:
            return static_cast<T>(std::max(s1, s2));
        case MINU:
            return std::min(in1, in2);
        case MAXU:
            return std::max(in1, in2);

        // B-Extension: Zbb sign/zero extensions
        case SEXT_B:
            return static_cast<T>(static_cast<SignedT>(static_cast<int8_t>(in1)));
        case SEXT_H:
            return static_cast<T>(static_cast<SignedT>(static_cast<int16_t>(in1)));
        case ZEXT_H:
            return in1 & static_cast<T>(0xFFFFu);

        // B-Extension: Zbb rotations
        case ROL:
            return std::rotl(in1, static_cast<int>(in2 & shift_mask));
        case ROR:
        case RORI:
            return std::rotr(in1, static_cast<int>(in2 & shift_mask));

        // B-Extension: Zbc carry-less multiply
        case CLMUL: {
            T r = 0;
            for (int i = 0; i < xlen; i++) {
                if ((in2 >> i) & 1) r ^= (in1 << i);
            }
            return r;
        }
        case CLMULH: {
            T r = 0;
            for (int i = 1; i < xlen; i++) {
                if ((in2 >> i) & 1) r ^= (in1 >> (xlen - i));
            }
            return r;
        }
        case CLMULR: {
            T r = 0;
            for (int i = 0; i < xlen; i++) {
                if ((in2 >> i) & 1) r ^= (in1 >> (xlen - 1 - i));
            }
            return r;
        }

        // B-Extension: Zbs single-bit
        case BSET:
        case BSETI:
            return in1 | (static_cast<T>(1) << (in2 & shift_mask));
        case BCLR:
        case BCLRI:
            return in1 & ~(static_cast<T>(1) << (in2 & shift_mask));
        case BINV:
        case BINVI:
            return in1 ^ (static_cast<T>(1) << (in2 & shift_mask));
        case BEXT:
        case BEXTI:
            return (in1 >> (in2 & shift_mask)) & static_cast<T>(1);

        // B-Extension: Zbb pseudo-ops
        case ORC_B: {
            T r = 0;
            for (size_t b = 0; b < sizeof(T); ++b) {
                if (((in1 >> (b * 8)) & 0xFFu) != 0) {
                    r |= (static_cast<T>(0xFFu) << (b * 8));
                }
            }
            return r;
        }
        case REV8:
            return std::byteswap(in1);
        case PACK: {
            constexpr int half_bits = sizeof(T) * 4;
            const T mask = (static_cast<T>(1) << half_bits) - 1;
            return (in1 & mask) | (in2 << half_bits);
        }

        default:
            return 0;
    }
}

}  // namespace

auto ExecuteUnit::aluIntB(Register in1, Register in2, isa::OperationId op_id, unsigned xlen_param)
    -> Register {
    if (xlen_param == 32) {
        const auto res = alu_int_b_eval<uint32_t>(static_cast<uint32_t>(in1),
                                                  static_cast<uint32_t>(in2), op_id, 32);
        return static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(res)));
    }
    return alu_int_b_eval<Register>(in1, in2, op_id, static_cast<int>(xlen_param));
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
