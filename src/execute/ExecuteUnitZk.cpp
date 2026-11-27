/**
 * @file ExecuteUnitZk.cpp
 * @brief RISC-V Scalar Cryptography execution unit implementation.
 */
#include "simrv/execute/ExecuteUnitZk.hpp"

#include "simrv/isa/Crypto.hpp"

namespace simrv::execute {

namespace {
thread_local uint32_t s_current_funct7 = 0;
}

void ExecuteUnitZk::set_current_funct7(uint32_t funct7) noexcept { s_current_funct7 = funct7; }

auto ExecuteUnitZk::get_current_funct7() noexcept -> uint32_t { return s_current_funct7; }

auto ExecuteUnitZk::aluIntZk(simrv::xlen::Register in1, simrv::xlen::Register in2,
                             isa::OperationId op_id, unsigned /*xlen*/, uint32_t funct7) noexcept
    -> simrv::xlen::Register {
    using enum isa::OperationId;

    const uint32_t eff_f7 = (funct7 != 0) ? funct7 : s_current_funct7;
    const unsigned bs = (eff_f7 >> 5) & 0x3U;

    switch (op_id) {
        // RV32 AES (Zkne / Zknd)
        case AES32ESI:
            return static_cast<simrv::xlen::Register>(static_cast<int64_t>(static_cast<int32_t>(
                crypto::aes32_esi(static_cast<uint32_t>(in1), static_cast<uint32_t>(in2), bs))));
        case AES32ESMI:
            return static_cast<simrv::xlen::Register>(static_cast<int64_t>(static_cast<int32_t>(
                crypto::aes32_esmi(static_cast<uint32_t>(in1), static_cast<uint32_t>(in2), bs))));
        case AES32DSI:
            return static_cast<simrv::xlen::Register>(static_cast<int64_t>(static_cast<int32_t>(
                crypto::aes32_dsi(static_cast<uint32_t>(in1), static_cast<uint32_t>(in2), bs))));
        case AES32DSMI:
            return static_cast<simrv::xlen::Register>(static_cast<int64_t>(static_cast<int32_t>(
                crypto::aes32_dsmi(static_cast<uint32_t>(in1), static_cast<uint32_t>(in2), bs))));

        // RV64 AES (Zkne / Zknd)
        case AES64ES:
            return static_cast<simrv::xlen::Register>(crypto::aes64_es(in1, in2));
        case AES64ESM:
            return static_cast<simrv::xlen::Register>(crypto::aes64_esm(in1, in2));
        case AES64DS:
            return static_cast<simrv::xlen::Register>(crypto::aes64_ds(in1, in2));
        case AES64DSM:
            return static_cast<simrv::xlen::Register>(crypto::aes64_dsm(in1, in2));
        case AES64IM:
            return static_cast<simrv::xlen::Register>(crypto::aes64_im(in1));
        case AES64KS1I: {
            const unsigned rnum = static_cast<unsigned>(in2 & 0xFU);
            return static_cast<simrv::xlen::Register>(crypto::aes64_ks1i(in1, rnum));
        }
        case AES64KS2:
            return static_cast<simrv::xlen::Register>(crypto::aes64_ks2(in1, in2));

        // SHA-256 (Zknh, RV32 & RV64) - sign-extended on RV64
        case SHA256SIG0:
            return static_cast<simrv::xlen::Register>(static_cast<int64_t>(
                static_cast<int32_t>(crypto::sha256_sig0(static_cast<uint32_t>(in1)))));
        case SHA256SIG1:
            return static_cast<simrv::xlen::Register>(static_cast<int64_t>(
                static_cast<int32_t>(crypto::sha256_sig1(static_cast<uint32_t>(in1)))));
        case SHA256SUM0:
            return static_cast<simrv::xlen::Register>(static_cast<int64_t>(
                static_cast<int32_t>(crypto::sha256_sum0(static_cast<uint32_t>(in1)))));
        case SHA256SUM1:
            return static_cast<simrv::xlen::Register>(static_cast<int64_t>(
                static_cast<int32_t>(crypto::sha256_sum1(static_cast<uint32_t>(in1)))));

        // RV64 SHA-512 (Zknh)
        case SHA512SIG0:
            return static_cast<simrv::xlen::Register>(crypto::sha512_sig0(in1));
        case SHA512SIG1:
            return static_cast<simrv::xlen::Register>(crypto::sha512_sig1(in1));
        case SHA512SUM0:
            return static_cast<simrv::xlen::Register>(crypto::sha512_sum0(in1));
        case SHA512SUM1:
            return static_cast<simrv::xlen::Register>(crypto::sha512_sum1(in1));

        // RV32 SHA-512 (Zknh)
        case SHA512SIG0L:
            return static_cast<simrv::xlen::Register>(static_cast<int64_t>(static_cast<int32_t>(
                crypto::sha512_sig0l(static_cast<uint32_t>(in1), static_cast<uint32_t>(in2)))));
        case SHA512SIG0H:
            return static_cast<simrv::xlen::Register>(static_cast<int64_t>(static_cast<int32_t>(
                crypto::sha512_sig0h(static_cast<uint32_t>(in1), static_cast<uint32_t>(in2)))));
        case SHA512SIG1L:
            return static_cast<simrv::xlen::Register>(static_cast<int64_t>(static_cast<int32_t>(
                crypto::sha512_sig1l(static_cast<uint32_t>(in1), static_cast<uint32_t>(in2)))));
        case SHA512SIG1H:
            return static_cast<simrv::xlen::Register>(static_cast<int64_t>(static_cast<int32_t>(
                crypto::sha512_sig1h(static_cast<uint32_t>(in1), static_cast<uint32_t>(in2)))));
        case SHA512SUM0R:
            return static_cast<simrv::xlen::Register>(static_cast<int64_t>(static_cast<int32_t>(
                crypto::sha512_sum0r(static_cast<uint32_t>(in1), static_cast<uint32_t>(in2)))));
        case SHA512SUM1R:
            return static_cast<simrv::xlen::Register>(static_cast<int64_t>(static_cast<int32_t>(
                crypto::sha512_sum1r(static_cast<uint32_t>(in1), static_cast<uint32_t>(in2)))));

        // ShangMi SM4 (Zksed, RV32 & RV64) - sign-extended on RV64
        case SM4ED:
            return static_cast<simrv::xlen::Register>(
                static_cast<int64_t>(static_cast<int32_t>(crypto::sm4_round_byte(
                    static_cast<uint32_t>(in1), static_cast<uint32_t>(in2), bs))));
        case SM4KS:
            return static_cast<simrv::xlen::Register>(
                static_cast<int64_t>(static_cast<int32_t>(crypto::sm4_key_schedule_byte(
                    static_cast<uint32_t>(in1), static_cast<uint32_t>(in2), bs))));

        // ShangMi SM3 (Zksh, RV32 & RV64) - sign-extended on RV64
        case SM3P0:
            return static_cast<simrv::xlen::Register>(static_cast<int64_t>(
                static_cast<int32_t>(crypto::sm3_p0(static_cast<uint32_t>(in1)))));
        case SM3P1:
            return static_cast<simrv::xlen::Register>(static_cast<int64_t>(
                static_cast<int32_t>(crypto::sm3_p1(static_cast<uint32_t>(in1)))));

        default:
            return 0;
    }
}

}  // namespace simrv::execute
