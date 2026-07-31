/**
 * @file Common.hpp
 * @brief Common ISA decoding utilities, field extraction, and MISA profile logic.
 */
#pragma once

#include <string_view>

#include "simrv/isa/Amo.hpp"          // IWYU pragma: export
#include "simrv/isa/Base.hpp"         // IWYU pragma: export
#include "simrv/isa/Compressed.hpp"   // IWYU pragma: export
#include "simrv/isa/Fp.hpp"           // IWYU pragma: export
#include "simrv/isa/OperationId.hpp"  // IWYU pragma: export
#include "simrv/isa/Priv.hpp"         // IWYU pragma: export
#include "simrv/xlen/Types.hpp"       // IWYU pragma: export

namespace simrv::isa {

constexpr Instruction RV32_NOP = 0x00000013;

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
constexpr auto funct12_of(Instruction ir) -> Instruction { return (ir >> 20) & 0xFFF; }

/**
 * @brief Extracts the 7-bit funct7 field from a register-register instruction.
 * @param ir The raw instruction word.
 * @return 7-bit value representing bits [31:25].
 */
constexpr auto funct7_of(Instruction ir) -> Instruction { return (ir >> 25) & 0x7F; }

/**
 * @brief Extracts the 5-bit funct5 field from an atomic memory operation (AMO).
 * @param ir The raw instruction word.
 * @return Funct5Amo enum representing bits [31:27].
 */
constexpr auto funct5_of(Instruction ir) -> Funct5Amo {
    return static_cast<Funct5Amo>((ir >> 27) & 0x1F);
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
 * @param profile The profile definition (I, IMAC, GC).
 * @return CSRValue bitmask containing the profile's extensions.
 */
constexpr auto misa_profile_bits(MisaProfile profile) -> CSRValue {
    switch (profile) {
        case MisaProfile::I:
            return misa_extension_bit(IsaExtension::I);
        case MisaProfile::IMAC:
            return misa_extension_bit(IsaExtension::I) | misa_extension_bit(IsaExtension::M) |
                   misa_extension_bit(IsaExtension::A) | misa_extension_bit(IsaExtension::C);
        case MisaProfile::GC:
            return misa_base_bits();
        default:
            return misa_extension_bit(IsaExtension::I);
    }
}

/**
 * @brief Computes the MXL (Machine XLEN) field mask to write to MISA bits [XLEN-1 : XLEN-2].
 * @return CSRValue containing the MXL value for either RV32 (01) or RV64 (10).
 */
constexpr auto misa_mxl_field() -> CSRValue {
    if constexpr (sizeof(CSRValue) == 4) {
        // MXL=01 for RV32 in bits [31:30]
        return static_cast<CSRValue>(1u << 30);
    } else if constexpr (sizeof(CSRValue) == 8) {
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
 * @return Combined CSRValue containing both extensions and MXL configuration.
 */
constexpr auto misa_with_mxl(CSRValue misa_extensions) -> CSRValue {
    return misa_extensions | misa_mxl_field();
}

constexpr CSRValue kMisaDefault = misa_profile_bits(MisaProfile::GC);

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
        case Opcode::NMSub:
            return (((ir >> 25) & 0x3u) == 0x1u) ? IsaExtension::D : IsaExtension::F;
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

/**
 * @brief Checks if the instruction's destination register (rd) is a floating-point register.
 * @param opcode The instruction opcode.
 * @param op_id The instruction OperationId.
 * @return True if the destination register is floating-point, false otherwise.
 */
constexpr auto is_destination_fp(Opcode opcode, OperationId op_id) -> bool {
    if (opcode == Opcode::LoadFp) {
        return true;
    }
    if (opcode != Opcode::OpFp) {
        return false;
    }
    switch (op_id) {
        case OperationId::FCVT_W_S:
        case OperationId::FCVT_WU_S:
        case OperationId::FCVT_L_S:
        case OperationId::FCVT_LU_S:
        case OperationId::FCVT_W_D:
        case OperationId::FCVT_WU_D:
        case OperationId::FCVT_L_D:
        case OperationId::FCVT_LU_D:
        case OperationId::FEQ_S:
        case OperationId::FLT_S:
        case OperationId::FLE_S:
        case OperationId::FEQ_D:
        case OperationId::FLT_D:
        case OperationId::FLE_D:
        case OperationId::FCLASS_S:
        case OperationId::FCLASS_D:
        case OperationId::FMV_X_W:
        case OperationId::FMV_X_D:
            return false;
        default:
            return true;
    }
}

/**
 * @brief Checks if the instruction's source register 1 (rs1) is a floating-point register.
 * @param opcode The instruction opcode.
 * @param op_id The instruction OperationId.
 * @return True if rs1 is a floating-point register, false otherwise.
 */
constexpr auto is_rs1_fp(Opcode opcode, OperationId op_id) -> bool {
    if (opcode == Opcode::StoreFp) {
        return true;
    }
    if (opcode != Opcode::OpFp && opcode != Opcode::MAdd && opcode != Opcode::MSub &&
        opcode != Opcode::NMSub && opcode != Opcode::NMAdd) {
        return false;
    }
    switch (op_id) {
        case OperationId::FCVT_S_W:
        case OperationId::FCVT_S_WU:
        case OperationId::FCVT_S_L:
        case OperationId::FCVT_S_LU:
        case OperationId::FCVT_D_W:
        case OperationId::FCVT_D_WU:
        case OperationId::FCVT_D_L:
        case OperationId::FCVT_D_LU:
        case OperationId::FMV_W_X:
        case OperationId::FMV_D_X:
            return false;
        default:
            return true;
    }
}

/**
 * @brief Checks if the instruction's source register 2 (rs2) is a floating-point register.
 * @param opcode The instruction opcode.
 * @param op_id The instruction OperationId.
 * @return True if rs2 is a floating-point register, false otherwise.
 */
constexpr auto is_rs2_fp(Opcode opcode, [[maybe_unused]] OperationId op_id) -> bool {
    if (opcode == Opcode::StoreFp) {
        return true;
    }
    if (opcode != Opcode::OpFp && opcode != Opcode::MAdd && opcode != Opcode::MSub &&
        opcode != Opcode::NMSub && opcode != Opcode::NMAdd) {
        return false;
    }
    return true;
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
            return "R-Type (Register-Register)";
        case InstFormat::I:
            return "I-Type (Register-Immediate / Load / Jump)";
        case InstFormat::S:
            return "S-Type (Store)";
        case InstFormat::B:
            return "B-Type (Branch)";
        case InstFormat::U:
            return "U-Type (Upper Immediate)";
        case InstFormat::J:
            return "J-Type (Unconditional Jump)";
        case InstFormat::R4:
            return "R4-Type (Fused Multiply-Add)";
        case InstFormat::Unknown:
            return "Unknown / Custom Format";
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
            return true;
        default:
            return false;
    }
}

}  // namespace simrv::isa
