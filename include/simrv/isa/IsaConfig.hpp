#pragma once

#include "simrv/isa/IsaTypes.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::isa {

/**
 * @brief Computes the bit mask for a specific ISA extension in the MISA CSR.
 * @param ext The target ISA extension.
 * @return CsrValue containing a single set bit at the extension position.
 */
constexpr auto misa_extension_bit(IsaExtension ext) -> simrv::xlen::CsrValue {
    return static_cast<simrv::xlen::CsrValue>(simrv::xlen::CsrValue{1}
                                              << static_cast<unsigned>(ext));
}

/**
 * @brief Computes the combined bitmask of all baseline supported extensions.
 * @return CsrValue mask for all base extensions.
 */
constexpr auto misa_base_bits() -> simrv::xlen::CsrValue {
    return misa_extension_bit(IsaExtension::I) | misa_extension_bit(IsaExtension::M) |
           misa_extension_bit(IsaExtension::A) | misa_extension_bit(IsaExtension::B) |
           misa_extension_bit(IsaExtension::F) | misa_extension_bit(IsaExtension::D) |
           misa_extension_bit(IsaExtension::C) | misa_extension_bit(IsaExtension::S) |
           misa_extension_bit(IsaExtension::U) | misa_extension_bit(IsaExtension::V);
}

/**
 * @brief Decodes the extension set mask matching a specified ISA preset.
 * @param preset The named ISA preset, including G/GC and the SimRV GCZBV target preset.
 * @return CsrValue bitmask containing the preset's extensions.
 */
constexpr auto isa_preset_bits(IsaPreset preset) -> simrv::xlen::CsrValue {
    switch (preset) {
        case IsaPreset::E:
            return misa_extension_bit(IsaExtension::E);
        case IsaPreset::EM:
            return misa_extension_bit(IsaExtension::E) | misa_extension_bit(IsaExtension::M);
        case IsaPreset::EMAC:
            return misa_extension_bit(IsaExtension::E) | misa_extension_bit(IsaExtension::M) |
                   misa_extension_bit(IsaExtension::A) | misa_extension_bit(IsaExtension::C);
        case IsaPreset::I:
            return misa_extension_bit(IsaExtension::I);
        case IsaPreset::IM:
            return misa_extension_bit(IsaExtension::I) | misa_extension_bit(IsaExtension::M);
        case IsaPreset::IMA:
            return misa_extension_bit(IsaExtension::I) | misa_extension_bit(IsaExtension::M) |
                   misa_extension_bit(IsaExtension::A) | misa_extension_bit(IsaExtension::S) |
                   misa_extension_bit(IsaExtension::U);
        case IsaPreset::IMAC:
            return misa_extension_bit(IsaExtension::I) | misa_extension_bit(IsaExtension::M) |
                   misa_extension_bit(IsaExtension::A) | misa_extension_bit(IsaExtension::C);
        case IsaPreset::G:
            return misa_extension_bit(IsaExtension::I) | misa_extension_bit(IsaExtension::M) |
                   misa_extension_bit(IsaExtension::A) | misa_extension_bit(IsaExtension::F) |
                   misa_extension_bit(IsaExtension::D) | misa_extension_bit(IsaExtension::S) |
                   misa_extension_bit(IsaExtension::U);
        case IsaPreset::GC:
            return isa_preset_bits(IsaPreset::G) | misa_extension_bit(IsaExtension::C);
        case IsaPreset::GCV:
            return isa_preset_bits(IsaPreset::GC) | misa_extension_bit(IsaExtension::V);
        case IsaPreset::GCZB:
            return isa_preset_bits(IsaPreset::GC) | misa_extension_bit(IsaExtension::B);
        case IsaPreset::GCZBV:
            return misa_base_bits();
        default:
            return misa_extension_bit(IsaExtension::I);
    }
}

/**
 * @brief Computes the MXL (Machine XLEN) field mask to write to MISA bits [XLEN-1 : XLEN-2].
 * @return CsrValue containing the MXL value for either RV32 (01) or RV64 (10).
 */
constexpr auto misa_mxl_field(unsigned int target_xlen = simrv::xlen::kXLenBits)
    -> simrv::xlen::CsrValue {
    if constexpr (sizeof(simrv::xlen::CsrValue) == 4) {
        // MXL=01 for RV32 in bits [31:30]
        return static_cast<simrv::xlen::CsrValue>(1u << 30);
    } else if constexpr (sizeof(simrv::xlen::CsrValue) == 8) {
        if (target_xlen == 32) {
            // MXL=01 for RV32 execution mode on RV64 in bits [63:62]
            return static_cast<simrv::xlen::CsrValue>(1ull << 62);
        }
        // MXL=10 for RV64 in bits [63:62]
        return static_cast<simrv::xlen::CsrValue>(2ull << 62);
    } else {
        return 0;
    }
}

/**
 * @brief Integrates the target MXL field into an extensions mask to construct a valid MISA CSR
 * value.
 * @param misa_extensions The mask of enabled extensions.
 * @param target_xlen Architecture register width (32 or 64, defaults to simulator build XLEN).
 * @return Combined CsrValue containing both extensions and MXL configuration.
 */
constexpr auto misa_with_mxl(simrv::xlen::CsrValue misa_extensions,
                             unsigned int target_xlen = simrv::xlen::kXLenBits)
    -> simrv::xlen::CsrValue {
    if constexpr (sizeof(simrv::xlen::CsrValue) == 4) {
        return (misa_extensions & ~(static_cast<simrv::xlen::CsrValue>(3u) << 30)) |
               misa_mxl_field(target_xlen);
    } else if constexpr (sizeof(simrv::xlen::CsrValue) == 8) {
        return (misa_extensions & ~(static_cast<simrv::xlen::CsrValue>(3ull) << 62)) |
               misa_mxl_field(target_xlen);
    } else {
        return misa_extensions | misa_mxl_field(target_xlen);
    }
}

/** Default advertised target; V remains subject to the qualification limits documented separately.
 */
constexpr simrv::xlen::CsrValue kMisaDefault = isa_preset_bits(IsaPreset::GCZBV);

/**
 * @brief Checks if a specific extension bit is set in a MISA CSR value.
 * @param misa The MISA register value.
 * @param ext The target extension.
 * @return True if the extension is enabled, false otherwise.
 */
constexpr auto misa_has_extension(simrv::xlen::CsrValue misa, IsaExtension ext) -> bool {
    return (misa & misa_extension_bit(ext)) != 0;
}

/**
 * @brief Apply the architectural IALIGN read mask to an exception PC.
 *
 * xepc[0] is always zero. When C is disabled and IALIGN is therefore 32,
 * xepc[1] also reads as zero (including the implicit read by xRET), while its
 * underlying writable value may be retained for a later IALIGN=16 setting.
 */
constexpr auto epc_read_value(simrv::xlen::CsrValue epc, simrv::xlen::CsrValue misa)
    -> simrv::xlen::CsrValue {
    const simrv::xlen::CsrValue alignment_mask =
        misa_has_extension(misa, IsaExtension::C) ? 1U : 3U;
    return epc & ~alignment_mask;
}

}  // namespace simrv::isa
