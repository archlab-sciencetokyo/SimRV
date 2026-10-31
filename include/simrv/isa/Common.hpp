/**
 * @file Common.hpp
 * @brief Common ISA decoding utilities, field extraction, and ISA preset logic.
 */
#pragma once

#include <string_view>

#include "simrv/isa/Amo.hpp"                // IWYU pragma: export
#include "simrv/isa/Compressed.hpp"         // IWYU pragma: export
#include "simrv/isa/Fp.hpp"                 // IWYU pragma: export
#include "simrv/isa/InstructionFields.hpp"  // IWYU pragma: export
#include "simrv/isa/InstructionFormat.hpp"  // IWYU pragma: export
#include "simrv/isa/IsaConfig.hpp"
#include "simrv/isa/IsaTypes.hpp"              // IWYU pragma: export
#include "simrv/isa/OperationId.hpp"           // IWYU pragma: export
#include "simrv/isa/Priv.hpp"                  // IWYU pragma: export
#include "simrv/pipeline/OperationTraits.hpp"  // IWYU pragma: export
#include "simrv/xlen/Types.hpp"                // IWYU pragma: export

namespace simrv::isa {

/**
 * @brief Extracts the standard 7-bit base opcode from a 32-bit instruction word.
 * @param ir The raw instruction word.
 * @return Opcode enum representing bits [6:0].
 */
constexpr auto opcode_of(simrv::xlen::Instruction ir) -> Opcode {
    return static_cast<Opcode>(ir & 0x7F);
}

/**
 * @brief Extracts the 2-bit compressed opcode quadrant from a 16-bit compressed instruction.
 * @param ir The compressed instruction.
 * @return CompressedOpcode enum representing bits [1:0].
 */
constexpr auto compressed_opcode_of(simrv::xlen::CompressedInstruction ir) -> CompressedOpcode {
    return static_cast<CompressedOpcode>(ir & 0x3);
}

/**
 * @brief Extracts the 3-bit funct3 field from an instruction word.
 * @param ir The raw instruction word.
 * @return Funct3 enum representing bits [14:12].
 */
constexpr auto funct3_of(simrv::xlen::Instruction ir) -> Funct3 {
    return static_cast<Funct3>((ir >> 12) & 0x7);
}

/**
 * @brief Extracts the 12-bit funct12 field from a system/privileged instruction.
 * @param ir The raw instruction word.
 * @return 12-bit value representing bits [31:20].
 */
constexpr auto funct12_of(simrv::xlen::Instruction ir) -> simrv::xlen::Funct12 {
    return static_cast<simrv::xlen::Funct12>((ir >> 20) & 0xFFF);
}

/**
 * @brief Extracts the 7-bit funct7 field from a register-register instruction.
 * @param ir The raw instruction word.
 * @return 7-bit value representing bits [31:25].
 */
constexpr auto funct7_of(simrv::xlen::Instruction ir) -> simrv::xlen::Funct7 {
    return static_cast<simrv::xlen::Funct7>((ir >> 25) & 0x7F);
}

/**
 * @brief Extracts the 5-bit funct5 field from an atomic memory operation (AMO).
 * @param ir The raw instruction word.
 * @return Funct5Amo enum representing bits [31:27].
 */
constexpr auto funct5_of(simrv::xlen::Instruction ir) -> Funct5Amo {
    return static_cast<Funct5Amo>((ir >> 27) & 0x1F);
}

/**
 * @brief Identifies the required ISA extension bit corresponding to an instruction word.
 * @param ir The raw instruction word.
 * @param compressed True if the instruction is compressed (16-bit).
 * @return The required IsaExtension.
 */
constexpr auto required_extension_for_instruction(simrv::xlen::Instruction ir, bool compressed)
    -> IsaExtension {
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
            if (f3 == Funct3::Flh || f3 == Funct3::Fsh || static_cast<uint8_t>(f3) == 2)
                return IsaExtension::F;
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
constexpr auto instruction_enabled_by_misa(simrv::xlen::CsrValue misa, simrv::xlen::Instruction ir,
                                           bool compressed) -> bool {
    return misa_has_extension(misa, required_extension_for_instruction(ir, compressed));
}

/** Resolve all MISA bits required by a decoded operation.
 *
 * Raw opcode classification cannot distinguish scalar B operations from I/M, nor the Zvbb/Zvbc
 * operations currently represented by SimRV's B profile bit. Keep this operation-aware resolver at
 * the decode boundary so extension-disabled instructions never reach execution.
 */
constexpr auto required_misa_extensions(OperationId op_id, bool compressed = false)
    -> simrv::xlen::CsrValue {
    const simrv::xlen::CsrValue compressed_requirement =
        compressed ? misa_extension_bit(IsaExtension::C) : simrv::xlen::CsrValue{0};
    return compressed_requirement | simrv::pipeline::operation::required_misa_mask(op_id);
}

/** Verify the complete extension requirement of an already decoded instruction. */
constexpr auto instruction_enabled_by_misa(simrv::xlen::CsrValue misa, OperationId op_id,
                                           bool compressed = false) -> bool {
    simrv::xlen::CsrValue required = required_misa_extensions(op_id, compressed);
    // The base integer operation tables are shared by RV32I and RV32E.  Substitute E for I
    // when the reduced-register base is active; E and I are mutually exclusive in MISA.
    if (misa_has_extension(misa, IsaExtension::E) &&
        (required & misa_extension_bit(IsaExtension::I)) != 0) {
        required =
            (required & ~misa_extension_bit(IsaExtension::I)) | misa_extension_bit(IsaExtension::E);
    }
    return required != 0 && (misa & required) == required;
}

/** RV32E exposes only x0-x15; floating-point and vector register banks are unaffected. */
constexpr auto rv32e_register_is_valid(simrv::xlen::RegId reg) noexcept -> bool {
    return std::to_underlying(reg) < 16;
}

/** Check only the integer register operands used by a decoded operation. */
constexpr auto rv32e_register_operands_valid(bool writes_int, simrv::xlen::RegId rd,
                                             bool reads_rs1_int, simrv::xlen::RegId rs1,
                                             bool reads_rs2_int, simrv::xlen::RegId rs2) noexcept
    -> bool {
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
    if (op_id != OperationId::ILLEGAL) {
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
    if (op_id != OperationId::ILLEGAL) {
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
    if (op_id != OperationId::ILLEGAL) {
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
        case Opcode::Custom0:
        case Opcode::Custom1:
        case Opcode::Custom2:
        case Opcode::Custom3:
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
    return simrv::pipeline::operation::requires_rv64(op_id);
}

}  // namespace simrv::isa
