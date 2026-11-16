#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>

#include "VectorHelpers.hpp"
#include "VectorSubunits.hpp"
#include "simrv/isa/Common.hpp"
#include "simrv/isa/Crypto.hpp"
#include "simrv/isa/InstructionFields.hpp"
#include "simrv/isa/IsaTypes.hpp"

namespace simrv::execute::vector {

namespace {

constexpr auto egw_elements_for(isa::OperationId op_id) noexcept -> uint32_t {
    if (op_id == isa::OperationId::VSM3ME_VV || op_id == isa::OperationId::VSM3C_VI) {
        return 8U;
    }
    return 4U;
}

constexpr auto is_vs_op(isa::OperationId op_id) noexcept -> bool {
    switch (op_id) {
        case isa::OperationId::VAESEF_VS:
        case isa::OperationId::VAESDF_VS:
        case isa::OperationId::VAESEM_VS:
        case isa::OperationId::VAESDM_VS:
        case isa::OperationId::VAESZ_VS:
        case isa::OperationId::VSM4R_VS:
            return true;
        default:
            return false;
    }
}

constexpr auto has_vs1_operand(isa::OperationId op_id) noexcept -> bool {
    switch (op_id) {
        case isa::OperationId::VGHSH_VV:
        case isa::OperationId::VSHA2MS_VV:
        case isa::OperationId::VSHA2CH_VV:
        case isa::OperationId::VSHA2CL_VV:
        case isa::OperationId::VSM3ME_VV:
            return true;
        default:
            return false;
    }
}

constexpr auto forbids_vd_overlap_vs2(isa::OperationId op_id) noexcept -> bool {
    switch (op_id) {
        case isa::OperationId::VSHA2MS_VV:
        case isa::OperationId::VSHA2CH_VV:
        case isa::OperationId::VSHA2CL_VV:
        case isa::OperationId::VSM3ME_VV:
        case isa::OperationId::VSM3C_VI:
            return true;
        default:
            return false;
    }
}

constexpr auto forbids_vd_overlap_vs1(isa::OperationId op_id) noexcept -> bool {
    switch (op_id) {
        case isa::OperationId::VSHA2MS_VV:
        case isa::OperationId::VSHA2CH_VV:
        case isa::OperationId::VSHA2CL_VV:
        case isa::OperationId::VSM3ME_VV:
            return true;
        default:
            return false;
    }
}

constexpr auto reg_groups_overlap(uint32_t a, uint32_t a_regs, uint32_t b, uint32_t b_regs) noexcept
    -> bool {
    return (a < b + b_regs) && (b < a + a_regs);
}

template <typename T, std::size_t N>
void read_eg(const core::RegisterFile& regs, simrv::xlen::RegId reg, uint32_t base_elem,
             std::array<T, N>& out) noexcept {
    for (std::size_t k = 0; k < N; ++k) {
        out[k] = get_group_element<T>(regs, reg, base_elem + static_cast<uint32_t>(k));
    }
}

template <typename T, std::size_t N>
void write_eg(core::RegisterFile& regs, simrv::xlen::RegId reg, uint32_t base_elem,
              const std::array<T, N>& in) noexcept {
    for (std::size_t k = 0; k < N; ++k) {
        set_group_element<T>(regs, reg, base_elem + static_cast<uint32_t>(k), in[k]);
    }
}

template <typename T, std::size_t N, typename Fn>
void for_each_eg(core::CPU& cpu, uint32_t vl, Fn&& fn) {
    const auto vstart = static_cast<uint32_t>(cpu.state().vstart);
    const uint32_t start_eg = vstart / static_cast<uint32_t>(N);
    const uint32_t end_eg = vl / static_cast<uint32_t>(N);
    for (uint32_t eg = start_eg; eg < end_eg; ++eg) {
        fn(eg * static_cast<uint32_t>(N));
    }
}

auto validate_crypto_constraints(const core::CPU& cpu, isa::OperationId op_id,
                                 simrv::xlen::RegId rd, simrv::xlen::RegId rs1,
                                 simrv::xlen::RegId rs2, bool vm, uint32_t vl, uint32_t sew,
                                 uint32_t uimm5) -> bool {
    if (!vm) return false;
    if (op_id == isa::OperationId::VAESKF1_VI && (uimm5 == 0U || uimm5 > 10U)) return false;
    if (op_id == isa::OperationId::VSM4K_VI && uimm5 > 7U) return false;
    const auto vtype =
        VtypeView{.raw = cpu.state().vtype, .xlen = static_cast<uint8_t>(cpu.state().regs.xlen)};
    if (vtype.vill()) return false;

    const Vlmul lmul = vtype.vlmul();
    uint32_t lmul_num = 1;
    uint32_t lmul_den = 1;
    switch (lmul) {
        case Vlmul::LMUL_1:
            lmul_num = 1;
            break;
        case Vlmul::LMUL_2:
            lmul_num = 2;
            break;
        case Vlmul::LMUL_4:
            lmul_num = 4;
            break;
        case Vlmul::LMUL_8:
            lmul_num = 8;
            break;
        case Vlmul::LMUL_F2:
            lmul_den = 2;
            break;
        case Vlmul::LMUL_F4:
            lmul_den = 4;
            break;
        case Vlmul::LMUL_F8:
            lmul_den = 8;
            break;
        default:
            return false;
    }

    const uint32_t egw_elems = egw_elements_for(op_id);
    if (isa::is_zvknh_operation(op_id)) {
        if (sew == 64U) {
            if (!cpu.state().enabled_extensions.has(isa::MultiLetterExtension::Zvknhb)) {
                return false;
            }
        } else if (sew != 32U) {
            return false;
        }
    } else if (sew != 32U) {
        return false;
    }

    const uint32_t egw_bits = egw_elems * sew;
    const uint32_t vlen_bits = cpu.state().regs.vlen;
    const uint32_t lmul_vlen_bits = (vlen_bits * lmul_num) / lmul_den;
    if (lmul_vlen_bits < egw_bits) return false;

    const auto vstart = static_cast<uint32_t>(cpu.state().vstart);
    if ((vstart % egw_elems) != 0U || (vl % egw_elems) != 0U) return false;

    const uint32_t emul_regs = std::max(1U, lmul_num / lmul_den);
    const uint32_t egw_regs = std::max(1U, egw_bits / vlen_bits);
    const uint32_t vd_idx = std::to_underlying(rd);
    const uint32_t vs2_idx = std::to_underlying(rs2);
    const uint32_t vs1_idx = std::to_underlying(rs1);

    if ((vd_idx % emul_regs) != 0U) return false;
    if (is_vs_op(op_id)) {
        if ((vs2_idx % egw_regs) != 0U) return false;
        if (emul_regs > 1U && reg_groups_overlap(vd_idx, emul_regs, vs2_idx, egw_regs)) {
            return false;
        }
    } else {
        if ((vs2_idx % emul_regs) != 0U) return false;
        if (forbids_vd_overlap_vs2(op_id) &&
            reg_groups_overlap(vd_idx, emul_regs, vs2_idx, emul_regs)) {
            return false;
        }
    }

    if (has_vs1_operand(op_id)) {
        if ((vs1_idx % emul_regs) != 0U) return false;
        if (forbids_vd_overlap_vs1(op_id) &&
            reg_groups_overlap(vd_idx, emul_regs, vs1_idx, emul_regs)) {
            return false;
        }
    }
    return true;
}

}  // namespace

auto execute_vector_crypto(core::CPU& cpu, isa::OperationId op_id, simrv::xlen::RegId rd,
                           simrv::xlen::RegId rs1, simrv::xlen::RegId rs2, bool vm, uint32_t vl,
                           uint32_t sew, uint32_t uimm5) -> bool {
    if (!validate_crypto_constraints(cpu, op_id, rd, rs1, rs2, vm, vl, sew, uimm5)) {
        return false;
    }

    auto& regs = cpu.state().regs;

    switch (op_id) {
        case isa::OperationId::VAESEF_VV:
        case isa::OperationId::VAESEF_VS:
        case isa::OperationId::VAESDF_VV:
        case isa::OperationId::VAESDF_VS:
        case isa::OperationId::VAESEM_VV:
        case isa::OperationId::VAESEM_VS:
        case isa::OperationId::VAESDM_VV:
        case isa::OperationId::VAESDM_VS:
        case isa::OperationId::VAESZ_VS: {
            const bool scalar_eg = is_vs_op(op_id);
            std::array<uint32_t, 4> eg0_key{};
            if (scalar_eg) read_eg<uint32_t, 4>(regs, rs2, 0U, eg0_key);
            for_each_eg<uint32_t, 4>(cpu, vl, [&](uint32_t base) {
                std::array<uint32_t, 4> state{};
                std::array<uint32_t, 4> rkey{};
                read_eg<uint32_t, 4>(regs, rd, base, state);
                if (scalar_eg) {
                    rkey = eg0_key;
                } else {
                    read_eg<uint32_t, 4>(regs, rs2, base, rkey);
                }
                if (op_id == isa::OperationId::VAESEF_VV || op_id == isa::OperationId::VAESEF_VS) {
                    isa::crypto::vaes_encrypt_final(state.data(), rkey.data());
                } else if (op_id == isa::OperationId::VAESDF_VV ||
                           op_id == isa::OperationId::VAESDF_VS) {
                    isa::crypto::vaes_decrypt_final(state.data(), rkey.data());
                } else if (op_id == isa::OperationId::VAESEM_VV ||
                           op_id == isa::OperationId::VAESEM_VS) {
                    isa::crypto::vaes_encrypt_mid(state.data(), rkey.data());
                } else if (op_id == isa::OperationId::VAESDM_VV ||
                           op_id == isa::OperationId::VAESDM_VS) {
                    isa::crypto::vaes_decrypt_mid(state.data(), rkey.data());
                } else {
                    for (std::size_t k = 0; k < 4U; ++k) state[k] ^= rkey[k];
                }
                write_eg<uint32_t, 4>(regs, rd, base, state);
            });
            return true;
        }
        case isa::OperationId::VAESKF1_VI:
            for_each_eg<uint32_t, 4>(cpu, vl, [&](uint32_t base) {
                std::array<uint32_t, 4> prev{};
                std::array<uint32_t, 4> out{};
                read_eg<uint32_t, 4>(regs, rs2, base, prev);
                isa::crypto::vaes_kf1(prev.data(), uimm5, out.data());
                write_eg<uint32_t, 4>(regs, rd, base, out);
            });
            return true;
        case isa::OperationId::VAESKF2_VI:
            for_each_eg<uint32_t, 4>(cpu, vl, [&](uint32_t base) {
                std::array<uint32_t, 4> prev2{};
                std::array<uint32_t, 4> prev1{};
                std::array<uint32_t, 4> out{};
                read_eg<uint32_t, 4>(regs, rd, base, prev2);
                read_eg<uint32_t, 4>(regs, rs2, base, prev1);
                isa::crypto::vaes_kf2(prev2.data(), prev1.data(), uimm5, out.data());
                write_eg<uint32_t, 4>(regs, rd, base, out);
            });
            return true;
        case isa::OperationId::VGMUL_VV:
            for_each_eg<uint32_t, 4>(cpu, vl, [&](uint32_t base) {
                std::array<uint32_t, 4> acc{};
                std::array<uint32_t, 4> h{};
                std::array<uint32_t, 4> out{};
                read_eg<uint32_t, 4>(regs, rd, base, acc);
                read_eg<uint32_t, 4>(regs, rs2, base, h);
                isa::crypto::zvkg_gmul(acc.data(), h.data(), out.data());
                write_eg<uint32_t, 4>(regs, rd, base, out);
            });
            return true;
        case isa::OperationId::VGHSH_VV:
            for_each_eg<uint32_t, 4>(cpu, vl, [&](uint32_t base) {
                std::array<uint32_t, 4> acc{};
                std::array<uint32_t, 4> h{};
                std::array<uint32_t, 4> x{};
                read_eg<uint32_t, 4>(regs, rd, base, acc);
                read_eg<uint32_t, 4>(regs, rs2, base, h);
                read_eg<uint32_t, 4>(regs, rs1, base, x);
                isa::crypto::zvkg_ghsh(acc.data(), h.data(), x.data());
                write_eg<uint32_t, 4>(regs, rd, base, acc);
            });
            return true;
        case isa::OperationId::VSHA2MS_VV:
            if (sew == 32U) {
                for_each_eg<uint32_t, 4>(cpu, vl, [&](uint32_t base) {
                    std::array<uint32_t, 4> w0{};
                    std::array<uint32_t, 4> w4{};
                    std::array<uint32_t, 4> w12{};
                    read_eg<uint32_t, 4>(regs, rd, base, w0);
                    read_eg<uint32_t, 4>(regs, rs2, base, w4);
                    read_eg<uint32_t, 4>(regs, rs1, base, w12);
                    isa::crypto::vsha256_ms(w0.data(), w4.data(), w12.data());
                    write_eg<uint32_t, 4>(regs, rd, base, w0);
                });
            } else {
                for_each_eg<uint64_t, 4>(cpu, vl, [&](uint32_t base) {
                    std::array<uint64_t, 4> w0{};
                    std::array<uint64_t, 4> w4{};
                    std::array<uint64_t, 4> w12{};
                    read_eg<uint64_t, 4>(regs, rd, base, w0);
                    read_eg<uint64_t, 4>(regs, rs2, base, w4);
                    read_eg<uint64_t, 4>(regs, rs1, base, w12);
                    isa::crypto::vsha512_ms(w0.data(), w4.data(), w12.data());
                    write_eg<uint64_t, 4>(regs, rd, base, w0);
                });
            }
            return true;
        case isa::OperationId::VSHA2CH_VV:
        case isa::OperationId::VSHA2CL_VV: {
            const bool high_half = op_id == isa::OperationId::VSHA2CH_VV;
            if (sew == 32U) {
                for_each_eg<uint32_t, 4>(cpu, vl, [&](uint32_t base) {
                    std::array<uint32_t, 4> cdgh{};
                    std::array<uint32_t, 4> abef{};
                    std::array<uint32_t, 4> wk{};
                    read_eg<uint32_t, 4>(regs, rd, base, cdgh);
                    read_eg<uint32_t, 4>(regs, rs2, base, abef);
                    read_eg<uint32_t, 4>(regs, rs1, base, wk);
                    isa::crypto::vsha256_compress(cdgh.data(), abef.data(),
                                                  high_half ? &wk[2] : &wk[0]);
                    write_eg<uint32_t, 4>(regs, rd, base, cdgh);
                });
            } else {
                for_each_eg<uint64_t, 4>(cpu, vl, [&](uint32_t base) {
                    std::array<uint64_t, 4> cdgh{};
                    std::array<uint64_t, 4> abef{};
                    std::array<uint64_t, 4> wk{};
                    read_eg<uint64_t, 4>(regs, rd, base, cdgh);
                    read_eg<uint64_t, 4>(regs, rs2, base, abef);
                    read_eg<uint64_t, 4>(regs, rs1, base, wk);
                    isa::crypto::vsha512_compress(cdgh.data(), abef.data(),
                                                  high_half ? &wk[2] : &wk[0]);
                    write_eg<uint64_t, 4>(regs, rd, base, cdgh);
                });
            }
            return true;
        }
        case isa::OperationId::VSM3ME_VV:
            for_each_eg<uint32_t, 8>(cpu, vl, [&](uint32_t base) {
                std::array<uint32_t, 8> w0_7{};
                std::array<uint32_t, 8> w8_15{};
                std::array<uint32_t, 8> out{};
                read_eg<uint32_t, 8>(regs, rs1, base, w0_7);
                read_eg<uint32_t, 8>(regs, rs2, base, w8_15);
                isa::crypto::vsm3_me(w0_7.data(), w8_15.data(), out.data());
                write_eg<uint32_t, 8>(regs, rd, base, out);
            });
            return true;
        case isa::OperationId::VSM3C_VI:
            for_each_eg<uint32_t, 8>(cpu, vl, [&](uint32_t base) {
                std::array<uint32_t, 8> state{};
                std::array<uint32_t, 8> w_eg{};
                read_eg<uint32_t, 8>(regs, rd, base, state);
                read_eg<uint32_t, 8>(regs, rs2, base, w_eg);
                isa::crypto::vsm3_compress(state.data(), w_eg.data(), uimm5);
                write_eg<uint32_t, 8>(regs, rd, base, state);
            });
            return true;
        case isa::OperationId::VSM4K_VI:
            for_each_eg<uint32_t, 4>(cpu, vl, [&](uint32_t base) {
                std::array<uint32_t, 4> prev{};
                std::array<uint32_t, 4> out{};
                read_eg<uint32_t, 4>(regs, rs2, base, prev);
                isa::crypto::vsm4_key_schedule(prev.data(), uimm5, out.data());
                write_eg<uint32_t, 4>(regs, rd, base, out);
            });
            return true;
        case isa::OperationId::VSM4R_VV:
        case isa::OperationId::VSM4R_VS: {
            const bool scalar_eg = op_id == isa::OperationId::VSM4R_VS;
            std::array<uint32_t, 4> eg0_rk{};
            if (scalar_eg) read_eg<uint32_t, 4>(regs, rs2, 0U, eg0_rk);
            for_each_eg<uint32_t, 4>(cpu, vl, [&](uint32_t base) {
                std::array<uint32_t, 4> state{};
                std::array<uint32_t, 4> rk{};
                read_eg<uint32_t, 4>(regs, rd, base, state);
                if (scalar_eg) {
                    rk = eg0_rk;
                } else {
                    read_eg<uint32_t, 4>(regs, rs2, base, rk);
                }
                isa::crypto::vsm4_rounds(state.data(), rk.data());
                write_eg<uint32_t, 4>(regs, rd, base, state);
            });
            return true;
        }
        default:
            return false;
    }
}

}  // namespace simrv::execute::vector
