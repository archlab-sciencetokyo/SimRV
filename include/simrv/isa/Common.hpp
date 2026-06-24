/**
 * @file Common.hpp
 * @brief Common ISA decoding utilities, field extraction, and MISA profile logic.
 */
#pragma once

#include <string_view>
#include "simrv/xlen/Types.hpp" // IWYU pragma: export
#include "simrv/isa/Base.hpp" // IWYU pragma: export
#include "simrv/isa/Amo.hpp" // IWYU pragma: export
#include "simrv/isa/Fp.hpp" // IWYU pragma: export
#include "simrv/isa/Priv.hpp" // IWYU pragma: export
#include "simrv/isa/Compressed.hpp" // IWYU pragma: export
#include "simrv/isa/OperationId.hpp" // IWYU pragma: export

namespace simrv::isa {

constexpr Instruction RV32_NOP = 0x00000013;

constexpr auto opcode_of(Instruction ir) -> Opcode {
    return static_cast<Opcode>(ir & 0x7F);
}

constexpr auto compressed_opcode_of(CompressedInstruction ir) -> CompressedOpcode {
    return static_cast<CompressedOpcode>(ir & 0x3);
}

constexpr auto funct3_of(Instruction ir) -> Funct3 {
    return static_cast<Funct3>((ir >> 12) & 0x7);
}

constexpr auto funct12_of(Instruction ir) -> Instruction {
    return (ir >> 20) & 0xFFF;
}

constexpr auto funct7_of(Instruction ir) -> Instruction {
    return (ir >> 25) & 0x7F;
}

constexpr auto funct5_of(Instruction ir) -> Funct5Amo {
    return static_cast<Funct5Amo>((ir >> 27) & 0x1F);
}

constexpr auto misa_extension_bit(IsaExtension ext) -> CSRValue {
    return static_cast<CSRValue>(CSRValue{1} << static_cast<unsigned>(ext));
}

constexpr auto misa_base_bits() -> CSRValue {
    return misa_extension_bit(IsaExtension::I) | misa_extension_bit(IsaExtension::M) |
           misa_extension_bit(IsaExtension::A) | misa_extension_bit(IsaExtension::F) |
           misa_extension_bit(IsaExtension::D) | misa_extension_bit(IsaExtension::C) |
           misa_extension_bit(IsaExtension::S) | misa_extension_bit(IsaExtension::U);
}

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

constexpr auto misa_with_mxl(CSRValue misa_extensions) -> CSRValue {
    return misa_extensions | misa_mxl_field();
}

constexpr CSRValue kMisaDefault = misa_profile_bits(MisaProfile::GC);

constexpr auto misa_has_extension(CSRValue misa, IsaExtension ext) -> bool {
    return (misa & misa_extension_bit(ext)) != 0;
}

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
        case Opcode::StoreFp:
            return (funct3_of(ir) == Funct3::Fld || funct3_of(ir) == Funct3::Fsd) ? IsaExtension::D
                                                                                  : IsaExtension::F;
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

constexpr auto instruction_enabled_by_misa(CSRValue misa, Instruction ir, bool compressed) -> bool {
    return misa_has_extension(misa, required_extension_for_instruction(ir, compressed));
}

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

constexpr auto is_rs1_fp(Opcode opcode, OperationId op_id) -> bool {
    if (opcode == Opcode::StoreFp) {
        return true;
    }
    if (opcode != Opcode::OpFp && opcode != Opcode::MAdd && opcode != Opcode::MSub && opcode != Opcode::NMSub && opcode != Opcode::NMAdd) {
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

constexpr auto is_rs2_fp(Opcode opcode, [[maybe_unused]] OperationId op_id) -> bool {
    if (opcode == Opcode::StoreFp) {
        return true;
    }
    if (opcode != Opcode::OpFp && opcode != Opcode::MAdd && opcode != Opcode::MSub && opcode != Opcode::NMSub && opcode != Opcode::NMAdd) {
        return false;
    }
    return true;
}

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

constexpr auto get_instruction_format_name(InstFormat fmt) -> std::string_view {
    switch (fmt) {
        case InstFormat::R: return "R-Type (Register-Register)";
        case InstFormat::I: return "I-Type (Register-Immediate / Load / Jump)";
        case InstFormat::S: return "S-Type (Store)";
        case InstFormat::B: return "B-Type (Branch)";
        case InstFormat::U: return "U-Type (Upper Immediate)";
        case InstFormat::J: return "J-Type (Unconditional Jump)";
        case InstFormat::R4: return "R4-Type (Fused Multiply-Add)";
        case InstFormat::Unknown: return "Unknown / Custom Format";
    }
    return "Unknown";
}

} // namespace simrv::isa
