/**
 * @file Common.hpp
 * @brief Common ISA decoding utilities, field extraction, and MISA profile logic.
 */
#pragma once

#include <string_view>

#include "simrv/isa/Amo.hpp"                   // IWYU pragma: export
#include "simrv/isa/Base.hpp"                  // IWYU pragma: export
#include "simrv/isa/Compressed.hpp"            // IWYU pragma: export
#include "simrv/isa/Fp.hpp"                    // IWYU pragma: export
#include "simrv/isa/OperationId.hpp"           // IWYU pragma: export
#include "simrv/isa/Priv.hpp"                  // IWYU pragma: export
#include "simrv/pipeline/OperationTraits.hpp"  // IWYU pragma: export
#include "simrv/xlen/Types.hpp"                // IWYU pragma: export

namespace simrv::isa {

constexpr Instruction kNop32 = 0x00000013;
constexpr Instruction RV32_NOP = kNop32;

/**
 * @brief Extracts the standard 7-bit base opcode from a 32-bit instruction word.
 * @param ir The raw instruction word.
 * @return Opcode enum representing bits [6:0].
 */
constexpr auto opcode_of(Instruction ir) -> Opcode { return static_cast<Opcode>(ir & 0x7F); }

/**
 * @brief Extracts the 2-bit compressed opcode quadrant from a 16-bit compressed instruction.
 * @param ir The compressed instruction.
 * @return CompressedOpcode enum representing bits [1:0].
 */
constexpr auto compressed_opcode_of(CompressedInstruction ir) -> CompressedOpcode {
    return static_cast<CompressedOpcode>(ir & 0x3);
}

/**
 * @brief Extracts the 3-bit funct3 field from an instruction word.
 * @param ir The raw instruction word.
 * @return Funct3 enum representing bits [14:12].
 */
constexpr auto funct3_of(Instruction ir) -> Funct3 { return static_cast<Funct3>((ir >> 12) & 0x7); }

/**
 * @brief Extracts the 12-bit funct12 field from a system/privileged instruction.
 * @param ir The raw instruction word.
 * @return 12-bit value representing bits [31:20].
 */
constexpr auto funct12_of(Instruction ir) -> Funct12 {
    return static_cast<Funct12>((ir >> 20) & 0xFFF);
}

/**
 * @brief Extracts the 7-bit funct7 field from a register-register instruction.
 * @param ir The raw instruction word.
 * @return 7-bit value representing bits [31:25].
 */
constexpr auto funct7_of(Instruction ir) -> Funct7 {
    return static_cast<Funct7>((ir >> 25) & 0x7F);
}

/**
 * @brief Extracts the 5-bit funct5 field from an atomic memory operation (AMO).
 * @param ir The raw instruction word.
 * @return Funct5Amo enum representing bits [31:27].
 */
constexpr auto funct5_of(Instruction ir) -> Funct5Amo {
    return static_cast<Funct5Amo>((ir >> 27) & 0x1F);
}

/**
 * @brief Return the natural access size encoded by an A-extension instruction.
 *
 * The standard encodes AMO.W with funct3=010 and AMO.D with funct3=011. Other encodings are
 * reserved and return zero so callers can reject them.
 */
constexpr auto amo_operand_bytes(Funct3 funct3) -> unsigned {
    if (funct3 == Funct3::Lw) return 4;
    if (funct3 == Funct3::Ld) return 8;
    return 0;
}

/** Return whether the AMO width exists for the build's architectural XLEN. */
constexpr auto amo_width_supported(Funct3 funct3) -> bool {
    return funct3 == Funct3::Lw || (simrv::xlen::kIsXLen64 && funct3 == Funct3::Ld);
}

/** Return whether an LR/SC/AMO effective address meets the A-extension alignment rule. */
constexpr auto amo_address_aligned(Address address, Funct3 funct3) -> bool {
    const unsigned bytes = amo_operand_bytes(funct3);
    return bytes != 0 && (address & static_cast<Address>(bytes - 1U)) == 0;
}

/**
 * @brief Computes the bit mask for a specific ISA extension in the MISA CSR.
 * @param ext The target ISA extension.
 * @return CSRValue containing a single set bit at the extension position.
 */
constexpr auto misa_extension_bit(IsaExtension ext) -> CSRValue {
    return static_cast<CSRValue>(CSRValue{1} << static_cast<unsigned>(ext));
}

/**
 * @brief Computes the combined bitmask of all baseline supported extensions.
 * @return CSRValue mask for all base extensions.
 */
constexpr auto misa_base_bits() -> CSRValue {
    return misa_extension_bit(IsaExtension::I) | misa_extension_bit(IsaExtension::M) |
           misa_extension_bit(IsaExtension::A) | misa_extension_bit(IsaExtension::B) |
           misa_extension_bit(IsaExtension::F) | misa_extension_bit(IsaExtension::D) |
           misa_extension_bit(IsaExtension::C) | misa_extension_bit(IsaExtension::S) |
           misa_extension_bit(IsaExtension::U) | misa_extension_bit(IsaExtension::V);
}

/**
 * @brief Decodes the extension set mask matching a specified MISA profile.
 * @param profile The profile definition (I, IMAC, GC, or GCBV).
 * @return CSRValue bitmask containing the profile's extensions.
 */
constexpr auto misa_profile_bits(MisaProfile profile) -> CSRValue {
    switch (profile) {
        case MisaProfile::E:
            return misa_extension_bit(IsaExtension::E);
        case MisaProfile::EM:
            return misa_extension_bit(IsaExtension::E) | misa_extension_bit(IsaExtension::M);
        case MisaProfile::EMAC:
            return misa_extension_bit(IsaExtension::E) | misa_extension_bit(IsaExtension::M) |
                   misa_extension_bit(IsaExtension::A) | misa_extension_bit(IsaExtension::C);
        case MisaProfile::I:
            return misa_extension_bit(IsaExtension::I);
        case MisaProfile::IM:
            return misa_extension_bit(IsaExtension::I) | misa_extension_bit(IsaExtension::M);
        case MisaProfile::IMA:
            return misa_extension_bit(IsaExtension::I) | misa_extension_bit(IsaExtension::M) |
                   misa_extension_bit(IsaExtension::A) | misa_extension_bit(IsaExtension::S) |
                   misa_extension_bit(IsaExtension::U);
        case MisaProfile::IMAC:
            return misa_extension_bit(IsaExtension::I) | misa_extension_bit(IsaExtension::M) |
                   misa_extension_bit(IsaExtension::A) | misa_extension_bit(IsaExtension::C);
        case MisaProfile::GC:
            return misa_extension_bit(IsaExtension::I) | misa_extension_bit(IsaExtension::M) |
                   misa_extension_bit(IsaExtension::A) | misa_extension_bit(IsaExtension::F) |
                   misa_extension_bit(IsaExtension::D) | misa_extension_bit(IsaExtension::C) |
                   misa_extension_bit(IsaExtension::S) | misa_extension_bit(IsaExtension::U);
        case MisaProfile::GCBV:
            return misa_base_bits();
        default:
            return misa_extension_bit(IsaExtension::I);
    }
}

/**
 * @brief Computes the MXL (Machine XLEN) field mask to write to MISA bits [XLEN-1 : XLEN-2].
 * @return CSRValue containing the MXL value for either RV32 (01) or RV64 (10).
 */
constexpr auto misa_mxl_field(unsigned int target_xlen = simrv::xlen::kXLenBits) -> CSRValue {
    if constexpr (sizeof(CSRValue) == 4) {
        // MXL=01 for RV32 in bits [31:30]
        return static_cast<CSRValue>(1u << 30);
    } else if constexpr (sizeof(CSRValue) == 8) {
        if (target_xlen == 32) {
            // MXL=01 for RV32 execution mode on RV64 in bits [63:62]
            return static_cast<CSRValue>(1ull << 62);
        }
        // MXL=10 for RV64 in bits [63:62]
        return static_cast<CSRValue>(2ull << 62);
    } else {
        return 0;
    }
}

/**
 * @brief Integrates the target MXL field into an extensions mask to construct a valid MISA CSR
 * value.
 * @param misa_extensions The mask of enabled extensions.
 * @param target_xlen Architecture register width (32 or 64, defaults to simulator build XLEN).
 * @return Combined CSRValue containing both extensions and MXL configuration.
 */
constexpr auto misa_with_mxl(CSRValue misa_extensions,
                             unsigned int target_xlen = simrv::xlen::kXLenBits) -> CSRValue {
    if constexpr (sizeof(CSRValue) == 4) {
        return (misa_extensions & ~(static_cast<CSRValue>(3u) << 30)) | misa_mxl_field(target_xlen);
    } else if constexpr (sizeof(CSRValue) == 8) {
        return (misa_extensions & ~(static_cast<CSRValue>(3ull) << 62)) |
               misa_mxl_field(target_xlen);
    } else {
        return misa_extensions | misa_mxl_field(target_xlen);
    }
}

/** Default advertised target; V remains subject to the qualification limits documented separately.
 */
constexpr CSRValue kMisaDefault = misa_profile_bits(MisaProfile::GCBV);

/**
 * @brief Checks if a specific extension bit is set in a MISA CSR value.
 * @param misa The MISA register value.
 * @param ext The target extension.
 * @return True if the extension is enabled, false otherwise.
 */
constexpr auto misa_has_extension(CSRValue misa, IsaExtension ext) -> bool {
    return (misa & misa_extension_bit(ext)) != 0;
}

/**
 * @brief Apply the architectural IALIGN read mask to an exception PC.
 *
 * xepc[0] is always zero. When C is disabled and IALIGN is therefore 32,
 * xepc[1] also reads as zero (including the implicit read by xRET), while its
 * underlying writable value may be retained for a later IALIGN=16 setting.
 */
constexpr auto epc_read_value(CSRValue epc, CSRValue misa) -> CSRValue {
    const CSRValue alignment_mask = misa_has_extension(misa, IsaExtension::C) ? 1U : 3U;
    return epc & ~alignment_mask;
}

/**
 * @brief Identifies the required ISA extension bit corresponding to an instruction word.
 * @param ir The raw instruction word.
 * @param compressed True if the instruction is compressed (16-bit).
 * @return The required IsaExtension.
 */
constexpr auto required_extension_for_instruction(Instruction ir, bool compressed) -> IsaExtension {
    if (compressed) {
        return IsaExtension::C;
    }

    switch (opcode_of(ir)) {
        case Opcode::Amo:
            return IsaExtension::A;
        case Opcode::Op:
        case Opcode::Op32:
            return (funct7_of(ir) & 0x1u) ? IsaExtension::M : IsaExtension::I;
        case Opcode::LoadFp:
        case Opcode::StoreFp: {
            const auto f3 = funct3_of(ir);
            if (f3 == Funct3::Fld || f3 == Funct3::Fsd) return IsaExtension::D;
            if (static_cast<uint8_t>(f3) == 2) return IsaExtension::F;
            return IsaExtension::V;
        }
        case Opcode::OpV:
            return IsaExtension::V;
        case Opcode::OpFp:
        case Opcode::MAdd:
        case Opcode::MSub:
        case Opcode::NMAdd:
        case Opcode::NMSub: {
            // FCVT.S.D has a single-precision destination encoding (fmt=00)
            // but consumes a double-precision source and therefore requires D.
            if (opcode_of(ir) == Opcode::OpFp && funct7_of(ir) == 0x20) {
                return IsaExtension::D;
            }
            return (((ir >> 25) & 0x3u) == 0x1u) ? IsaExtension::D : IsaExtension::F;
        }
        default:
            return IsaExtension::I;
    }
}

/**
 * @brief Verifies whether an instruction's required ISA extension is enabled in the current MISA.
 * @param misa The active MISA CSR value.
 * @param ir The raw instruction word.
 * @param compressed True if the instruction is compressed.
 * @return True if the instruction is supported and enabled, false otherwise.
 */
constexpr auto instruction_enabled_by_misa(CSRValue misa, Instruction ir, bool compressed) -> bool {
    return misa_has_extension(misa, required_extension_for_instruction(ir, compressed));
}

/** Resolve all MISA bits required by a decoded operation.
 *
 * Raw opcode classification cannot distinguish scalar B operations from I/M, nor the Zvbb/Zvbc
 * operations currently represented by SimRV's B profile bit. Keep this operation-aware resolver at
 * the decode boundary so extension-disabled instructions never reach execution.
 */
constexpr auto required_misa_extensions(OperationId op_id, bool compressed = false) -> CSRValue {
    const CSRValue compressed_requirement =
        compressed ? misa_extension_bit(IsaExtension::C) : CSRValue{0};

    const auto op = std::to_underlying(op_id);
    const auto in_range = [op](OperationId first, OperationId last) {
        return op >= std::to_underlying(first) && op <= std::to_underlying(last);
    };

    if (in_range(OperationId::MUL, OperationId::REMUW)) {
        return compressed_requirement | misa_extension_bit(IsaExtension::M);
    }
    if (in_range(OperationId::LR_W, OperationId::AMOMAXU_D)) {
        return compressed_requirement | misa_extension_bit(IsaExtension::A);
    }
    if (in_range(OperationId::FLW, OperationId::FCVT_S_LU)) {
        return compressed_requirement | misa_extension_bit(IsaExtension::F);
    }
    if (in_range(OperationId::FLD, OperationId::FCVT_D_LU)) {
        return compressed_requirement | misa_extension_bit(IsaExtension::F) |
               misa_extension_bit(IsaExtension::D);
    }
    if (in_range(OperationId::SH1ADD, OperationId::PACKW)) {
        return compressed_requirement | misa_extension_bit(IsaExtension::B);
    }
    if (in_range(OperationId::VSETVLI, OperationId::VWSLL_VI)) {
        CSRValue required = compressed_requirement | misa_extension_bit(IsaExtension::V);
        switch (op_id) {
            case OperationId::VFADD_VV:
            case OperationId::VFSLIDE1UP_VF:
            case OperationId::VFSLIDE1DOWN_VF:
            case OperationId::VFADD_VF:
            case OperationId::VFSUB_VV:
            case OperationId::VFSUB_VF:
            case OperationId::VFRSUB_VF:
            case OperationId::VFMUL_VV:
            case OperationId::VFMUL_VF:
            case OperationId::VFDIV_VV:
            case OperationId::VFDIV_VF:
            case OperationId::VFRDIV_VF:
            case OperationId::VFSQRT_V:
            case OperationId::VFRSQRT7_V:
            case OperationId::VFREC7_V:
            case OperationId::VFCLASS_V:
            case OperationId::VFCVT_XU_F_V:
            case OperationId::VFCVT_X_F_V:
            case OperationId::VFCVT_F_XU_V:
            case OperationId::VFCVT_F_X_V:
            case OperationId::VFCVT_RTZ_XU_F_V:
            case OperationId::VFCVT_RTZ_X_F_V:
            case OperationId::VFWCVT_XU_F_V:
            case OperationId::VFWCVT_X_F_V:
            case OperationId::VFWCVT_F_XU_V:
            case OperationId::VFWCVT_F_X_V:
            case OperationId::VFWCVT_F_F_V:
            case OperationId::VFWCVT_RTZ_XU_F_V:
            case OperationId::VFWCVT_RTZ_X_F_V:
            case OperationId::VFNCVT_XU_F_W:
            case OperationId::VFNCVT_X_F_W:
            case OperationId::VFNCVT_F_XU_W:
            case OperationId::VFNCVT_F_X_W:
            case OperationId::VFNCVT_F_F_W:
            case OperationId::VFNCVT_ROD_F_F_W:
            case OperationId::VFNCVT_RTZ_XU_F_W:
            case OperationId::VFNCVT_RTZ_X_F_W:
            case OperationId::VFSGNJ_VV:
            case OperationId::VFSGNJ_VF:
            case OperationId::VFSGNJN_VV:
            case OperationId::VFSGNJN_VF:
            case OperationId::VFSGNJX_VV:
            case OperationId::VFSGNJX_VF:
            case OperationId::VFMIN_VV:
            case OperationId::VFMIN_VF:
            case OperationId::VFMAX_VV:
            case OperationId::VFMAX_VF:
            case OperationId::VMFEQ_VV:
            case OperationId::VMFEQ_VF:
            case OperationId::VMFNE_VV:
            case OperationId::VMFNE_VF:
            case OperationId::VMFLT_VV:
            case OperationId::VMFLT_VF:
            case OperationId::VMFLE_VV:
            case OperationId::VMFLE_VF:
            case OperationId::VMFGT_VF:
            case OperationId::VMFGE_VF:
            case OperationId::VFMADD_VV:
            case OperationId::VFMADD_VF:
            case OperationId::VFNMADD_VV:
            case OperationId::VFNMADD_VF:
            case OperationId::VFMSUB_VV:
            case OperationId::VFMSUB_VF:
            case OperationId::VFNMSUB_VV:
            case OperationId::VFNMSUB_VF:
            case OperationId::VFMACC_VV:
            case OperationId::VFMACC_VF:
            case OperationId::VFNMACC_VV:
            case OperationId::VFNMACC_VF:
            case OperationId::VFMSAC_VV:
            case OperationId::VFMSAC_VF:
            case OperationId::VFNMSAC_VV:
            case OperationId::VFNMSAC_VF:
            case OperationId::VFMV_F_S:
            case OperationId::VFMV_S_F:
            case OperationId::VFMERGE_VFM:
            case OperationId::VFMV_V_F:
            case OperationId::VFREDUSUM_VS:
            case OperationId::VFREDOSUM_VS:
            case OperationId::VFREDMIN_VS:
            case OperationId::VFREDMAX_VS:
            case OperationId::VFWREDUSUM_VS:
            case OperationId::VFWREDOSUM_VS:
            case OperationId::VFWADD_VV:
            case OperationId::VFWADD_VF:
            case OperationId::VFWSUB_VV:
            case OperationId::VFWSUB_VF:
            case OperationId::VFWADD_WV:
            case OperationId::VFWADD_WF:
            case OperationId::VFWSUB_WV:
            case OperationId::VFWSUB_WF:
            case OperationId::VFWMUL_VV:
            case OperationId::VFWMUL_VF:
            case OperationId::VFWMACC_VV:
            case OperationId::VFWMACC_VF:
            case OperationId::VFWNMACC_VV:
            case OperationId::VFWNMACC_VF:
            case OperationId::VFWMSAC_VV:
            case OperationId::VFWMSAC_VF:
            case OperationId::VFWNMSAC_VV:
            case OperationId::VFWNMSAC_VF:
                required |= misa_extension_bit(IsaExtension::F);
                break;
            case OperationId::VANDN_VV:
            case OperationId::VANDN_VX:
            case OperationId::VROL_VV:
            case OperationId::VROL_VX:
            case OperationId::VROR_VV:
            case OperationId::VROR_VX:
            case OperationId::VROR_VI:
            case OperationId::VCLZ_V:
            case OperationId::VCTZ_V:
            case OperationId::VCPOP_V:
            case OperationId::VBREV_V:
            case OperationId::VBREV8_V:
            case OperationId::VREV8_V:
            case OperationId::VCLMUL_VV:
            case OperationId::VCLMUL_VX:
            case OperationId::VCLMULH_VV:
            case OperationId::VCLMULH_VX:
            case OperationId::VWSLL_VV:
            case OperationId::VWSLL_VX:
            case OperationId::VWSLL_VI:
                required |= misa_extension_bit(IsaExtension::B);
                break;
            default:
                break;
        }
        return required;
    }
    return compressed_requirement | misa_extension_bit(IsaExtension::I);
}

/** Verify the complete extension requirement of an already decoded instruction. */
constexpr auto instruction_enabled_by_misa(CSRValue misa, OperationId op_id,
                                           bool compressed = false) -> bool {
    CSRValue required = required_misa_extensions(op_id, compressed);
    // The base integer operation tables are shared by RV32I and RV32E.  Substitute E for I
    // when the reduced-register base is active; E and I are mutually exclusive in MISA.
    if (misa_has_extension(misa, IsaExtension::E) &&
        (required & misa_extension_bit(IsaExtension::I)) != 0) {
        required = (required & ~misa_extension_bit(IsaExtension::I)) |
                   misa_extension_bit(IsaExtension::E);
    }
    return required != 0 && (misa & required) == required;
}

/** RV32E exposes only x0-x15; floating-point and vector register banks are unaffected. */
constexpr auto rv32e_register_is_valid(RegId reg) noexcept -> bool {
    return std::to_underlying(reg) < 16;
}

/** Check only the integer register operands used by a decoded operation. */
constexpr auto rv32e_register_operands_valid(bool writes_int, RegId rd, bool reads_rs1_int,
                                             RegId rs1, bool reads_rs2_int,
                                             RegId rs2) noexcept -> bool {
    return (!writes_int || rv32e_register_is_valid(rd)) &&
           (!reads_rs1_int || rv32e_register_is_valid(rs1)) &&
           (!reads_rs2_int || rv32e_register_is_valid(rs2));
}

/**
 * @brief Checks if the instruction's destination register (rd) is a floating-point register.
 * @param opcode The instruction opcode.
 * @param op_id The instruction OperationId.
 * @return True if the destination register is floating-point, false otherwise.
 */
constexpr auto is_destination_fp(Opcode opcode, OperationId op_id) -> bool {
    if (op_id != OperationId::UNKNOWN) {
        return simrv::pipeline::operation::writes_float(op_id);
    }
    return opcode == Opcode::LoadFp || opcode == Opcode::MAdd || opcode == Opcode::MSub ||
           opcode == Opcode::NMAdd || opcode == Opcode::NMSub || opcode == Opcode::OpFp;
}

/**
 * @brief Checks if the instruction's source register 1 (rs1) is a floating-point register.
 * @param opcode The instruction opcode.
 * @param op_id The instruction OperationId.
 * @return True if rs1 is a floating-point register, false otherwise.
 */
constexpr auto is_rs1_fp(Opcode opcode, OperationId op_id) -> bool {
    if (op_id != OperationId::UNKNOWN) {
        return simrv::pipeline::operation::is_rs1_fp(op_id);
    }
    return opcode == Opcode::OpFp || opcode == Opcode::MAdd || opcode == Opcode::MSub ||
           opcode == Opcode::NMSub || opcode == Opcode::NMAdd;
}

/**
 * @brief Checks if the instruction's source register 2 (rs2) is a floating-point register.
 * @param opcode The instruction opcode.
 * @param op_id The instruction OperationId.
 * @return True if rs2 is a floating-point register, false otherwise.
 */
constexpr auto is_rs2_fp(Opcode opcode, OperationId op_id) -> bool {
    if (op_id != OperationId::UNKNOWN) {
        return simrv::pipeline::operation::is_rs2_fp(op_id);
    }
    return opcode == Opcode::StoreFp || opcode == Opcode::OpFp || opcode == Opcode::MAdd ||
           opcode == Opcode::MSub || opcode == Opcode::NMSub || opcode == Opcode::NMAdd;
}

/**
 * @brief Maps an opcode to its standard RISC-V instruction format category.
 * @param op The instruction opcode.
 * @return The InstFormat enum value.
 */
constexpr auto get_instruction_format(Opcode op) -> InstFormat {
    switch (op) {
        case Opcode::Load:
        case Opcode::LoadFp:
        case Opcode::MiscMem:
        case Opcode::OpImm:
        case Opcode::OpImm32:
        case Opcode::Jalr:
        case Opcode::System:
            return InstFormat::I;
        case Opcode::Store:
        case Opcode::StoreFp:
            return InstFormat::S;
        case Opcode::Branch:
            return InstFormat::B;
        case Opcode::Auipc:
        case Opcode::Lui:
            return InstFormat::U;
        case Opcode::Jal:
            return InstFormat::J;
        case Opcode::Op:
        case Opcode::Op32:
        case Opcode::Amo:
        case Opcode::OpFp:
        case Opcode::OpV:
            return InstFormat::R;
        case Opcode::MAdd:
        case Opcode::MSub:
        case Opcode::NMSub:
        case Opcode::NMAdd:
            return InstFormat::R4;
        default:
            return InstFormat::Unknown;
    }
}

/**
 * @brief Retrieves a human-readable name/description of an instruction format.
 * @param fmt The instruction format.
 * @return A string view containing the human-readable format description.
 */
constexpr auto get_instruction_format_name(InstFormat fmt) -> std::string_view {
    switch (fmt) {
        case InstFormat::R:
            return "R-Type";
        case InstFormat::I:
            return "I-Type";
        case InstFormat::S:
            return "S-Type";
        case InstFormat::B:
            return "B-Type";
        case InstFormat::U:
            return "U-Type";
        case InstFormat::J:
            return "J-Type";
        case InstFormat::R4:
            return "R4-Type";
        case InstFormat::Unknown:
            return "Unknown";
    }
    return "Unknown";
}

/**
 * @brief Checks if a given operation ID requires a 64-bit architecture (RV64).
 * @param op_id The OperationId to check.
 * @return True if the instruction is RV64-only, false if it is supported on RV32.
 */
constexpr auto requires_rv64(OperationId op_id) -> bool {
    switch (op_id) {
        case OperationId::LD:
        case OperationId::LWU:
        case OperationId::SD:
        case OperationId::ADDIW:
        case OperationId::SLLIW:
        case OperationId::SRLIW:
        case OperationId::SRAIW:
        case OperationId::ADDW:
        case OperationId::SUBW:
        case OperationId::SLLW:
        case OperationId::SRLW:
        case OperationId::SRAW:
        case OperationId::MULW:
        case OperationId::DIVW:
        case OperationId::DIVUW:
        case OperationId::REMW:
        case OperationId::REMUW:
        case OperationId::FCVT_L_S:
        case OperationId::FCVT_LU_S:
        case OperationId::FCVT_S_L:
        case OperationId::FCVT_S_LU:
        case OperationId::FCVT_L_D:
        case OperationId::FCVT_LU_D:
        case OperationId::FCVT_D_L:
        case OperationId::FCVT_D_LU:
        case OperationId::FMV_X_D:
        case OperationId::FMV_D_X:
        case OperationId::VLUXEI64_V:
        case OperationId::VLOXEI64_V:
        case OperationId::VSUXEI64_V:
        case OperationId::VSOXEI64_V:
            return true;
        default:
            return false;
    }
}

}  // namespace simrv::isa
