#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/isa/IsaTypes.hpp"
#include "simrv/xlen/AddressTypes.hpp"
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
 * @struct IsaSpec
 * @brief Dynamic RISC-V ISA specification parsed from an ISA string (no fixed enum).
 */
struct IsaSpec {
    simrv::xlen::CsrValue misa_bits =
        misa_extension_bit(IsaExtension::I) | misa_extension_bit(IsaExtension::M) |
        misa_extension_bit(IsaExtension::A) | misa_extension_bit(IsaExtension::F) |
        misa_extension_bit(IsaExtension::D) | misa_extension_bit(IsaExtension::C) |
        misa_extension_bit(IsaExtension::S) | misa_extension_bit(IsaExtension::U) |
        misa_extension_bit(IsaExtension::V);
    unsigned int xlen = 0;  ///< 0 = inherit host/ELF XLEN, 32 = RV32, 64 = RV64
    std::vector<std::string> extensions{};
    ExtensionSet extension_set{};

    [[nodiscard]] constexpr auto has(IsaExtension ext) const noexcept -> bool {
        return (misa_bits & misa_extension_bit(ext)) != 0;
    }

    [[nodiscard]] constexpr auto has_extension(MultiLetterExtension ext) const noexcept -> bool {
        return extension_set.has(ext);
    }

    [[nodiscard]] inline auto canonical_string() const -> std::string {
        std::string out;
        if (xlen == 32) {
            out += "rv32";
        } else if (xlen == 64) {
            out += "rv64";
        }
        const bool is_g = has(IsaExtension::I) && has(IsaExtension::M) && has(IsaExtension::A) &&
                          has(IsaExtension::F) && has(IsaExtension::D);
        if (has(IsaExtension::E)) {
            out.push_back('e');
        } else if (is_g) {
            out.push_back('g');
        } else {
            out.push_back('i');
        }
        if (!is_g) {
            if (has(IsaExtension::M)) out.push_back('m');
            if (has(IsaExtension::A)) out.push_back('a');
            if (has(IsaExtension::F)) out.push_back('f');
            if (has(IsaExtension::D)) out.push_back('d');
        }
        if (has(IsaExtension::C)) out.push_back('c');
        if (has(IsaExtension::B)) out.push_back('b');
        if (has(IsaExtension::V)) out.push_back('v');
        if (!is_g && has(IsaExtension::S) && !has(IsaExtension::A)) out.push_back('s');
        if (!is_g && has(IsaExtension::U) && !has(IsaExtension::A)) out.push_back('u');
        for (const auto& ext : extensions) {
            out.push_back('_');
            out += ext;
        }
        return out;
    }

    friend auto operator==(const IsaSpec&, const IsaSpec&) -> bool = default;
};

[[nodiscard]] inline auto parse_isa_spec(std::string_view raw) -> std::optional<IsaSpec> {
    if (raw.empty()) return std::nullopt;
    std::string lower;
    lower.reserve(raw.size());
    for (const char ch : raw) {
        if (ch >= 'A' && ch <= 'Z') {
            lower.push_back(static_cast<char>(ch - 'A' + 'a'));
        } else {
            lower.push_back(ch);
        }
    }
    std::string_view s = lower;
    IsaSpec spec{};
    spec.misa_bits = 0;

    if (s.starts_with("rv32")) {
        spec.xlen = 32;
        s.remove_prefix(4);
    } else if (s.starts_with("rv64")) {
        spec.xlen = 64;
        s.remove_prefix(4);
    }
    if (s.empty()) return std::nullopt;

    const size_t underscore = s.find('_');
    const std::string_view single_letters =
        (underscore == std::string_view::npos) ? s : s.substr(0, underscore);
    if (single_letters.empty()) return std::nullopt;
    if (single_letters == "gcbv") return std::nullopt;

    const char base = single_letters.front();
    if (base == 'e') {
        spec.misa_bits |= misa_extension_bit(IsaExtension::E);
        if (spec.xlen == 0) spec.xlen = 32;
    } else if (base == 'i') {
        spec.misa_bits |= misa_extension_bit(IsaExtension::I);
    } else if (base == 'g') {
        spec.misa_bits |=
            misa_extension_bit(IsaExtension::I) | misa_extension_bit(IsaExtension::M) |
            misa_extension_bit(IsaExtension::A) | misa_extension_bit(IsaExtension::F) |
            misa_extension_bit(IsaExtension::D) | misa_extension_bit(IsaExtension::S) |
            misa_extension_bit(IsaExtension::U);
    } else {
        return std::nullopt;
    }

    for (size_t i = 1; i < single_letters.size(); ++i) {
        const char ch = single_letters[i];
        if (ch == 'z' && i + 1 < single_letters.size() && single_letters[i + 1] == 'b') {
            // Legacy compact shorthand such as gczb / gczbv
            spec.misa_bits |= misa_extension_bit(IsaExtension::B);
            spec.extensions.emplace_back("zba");
            spec.extensions.emplace_back("zbb");
            spec.extensions.emplace_back("zbc");
            spec.extensions.emplace_back("zbs");
            spec.extension_set.set(MultiLetterExtension::Zba);
            spec.extension_set.set(MultiLetterExtension::Zbb);
            spec.extension_set.set(MultiLetterExtension::Zbc);
            spec.extension_set.set(MultiLetterExtension::Zbs);
            ++i;
            continue;
        }
        switch (ch) {
            case 'm':
                spec.misa_bits |= misa_extension_bit(IsaExtension::M);
                break;
            case 'a':
                spec.misa_bits |= misa_extension_bit(IsaExtension::A);
                break;
            case 'f':
                spec.misa_bits |= misa_extension_bit(IsaExtension::F);
                break;
            case 'd':
                spec.misa_bits |= misa_extension_bit(IsaExtension::D);
                break;
            case 'c':
                spec.misa_bits |= misa_extension_bit(IsaExtension::C);
                break;
            case 'b':
                spec.misa_bits |= misa_extension_bit(IsaExtension::B);
                break;
            case 'v':
                spec.misa_bits |= misa_extension_bit(IsaExtension::V);
                break;
            case 's':
                spec.misa_bits |= misa_extension_bit(IsaExtension::S);
                break;
            case 'u':
                spec.misa_bits |= misa_extension_bit(IsaExtension::U);
                break;
            default:
                return std::nullopt;
        }
    }

    // Preserve Supervisor/User bits on "ima" (without "c") for Linux/RTOS parity.
    if (single_letters == "ima") {
        spec.misa_bits |= misa_extension_bit(IsaExtension::S) | misa_extension_bit(IsaExtension::U);
    }

    if (underscore != std::string_view::npos) {
        std::string_view rest = s.substr(underscore + 1);
        while (!rest.empty()) {
            const size_t next = rest.find('_');
            const std::string_view token =
                (next == std::string_view::npos) ? rest : rest.substr(0, next);
            if (token.empty()) return std::nullopt;
            if (std::ranges::find(spec.extensions, token) == spec.extensions.end()) {
                spec.extensions.emplace_back(token);
            }
            add_extension_to_set(spec.extension_set, token);
            if (next == std::string_view::npos) break;
            rest.remove_prefix(next + 1);
        }
    }

    return spec;
}

/**
 * @brief Computes the MXL (Machine XLEN) field mask to write to MISA bits [XLEN-1 :
 * XLEN-2].
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
 * @brief Integrates the target MXL field into an extensions mask to construct a valid
 * MISA CSR value.
 * @param misa_extensions The mask of enabled extensions.
 * @param target_xlen Architecture register width (32 or 64, defaults to simulator build
 * XLEN).
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

/** Default advertised target; V remains subject to the qualification limits documented
 * separately.
 */
constexpr simrv::xlen::CsrValue kMisaDefault = misa_base_bits();

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

/**
 * @brief Returns the canonical string representation of an architecture profile.
 */
constexpr auto to_profile_string(ArchitectureProfile profile) noexcept -> std::string_view {
    switch (profile) {
        case ArchitectureProfile::Legacy:
            return "legacy";
        case ArchitectureProfile::Rva20U64:
            return "rva20u64";
        case ArchitectureProfile::Rva20S64:
            return "rva20s64";
        case ArchitectureProfile::Rva22U64:
            return "rva22u64";
        case ArchitectureProfile::Rva22S64:
            return "rva22s64";
        case ArchitectureProfile::Rva23U64:
            return "rva23u64";
        case ArchitectureProfile::Rva23S64:
            return "rva23s64";
        case ArchitectureProfile::Rvb23U64:
            return "rvb23u64";
        case ArchitectureProfile::Rvb23S64:
            return "rvb23s64";
    }
    return "unknown";
}

/**
 * @brief Parses an architecture profile name from string.
 */
constexpr auto parse_architecture_profile(std::string_view str) noexcept
    -> std::optional<ArchitectureProfile> {
    if (str == "legacy" || str == "rv64gc" || str == "rv32gc") return ArchitectureProfile::Legacy;
    if (str == "rva20u64") return ArchitectureProfile::Rva20U64;
    if (str == "rva20s64" || str == "rva20") return ArchitectureProfile::Rva20S64;
    if (str == "rva22u64") return ArchitectureProfile::Rva22U64;
    if (str == "rva22s64" || str == "rva22") return ArchitectureProfile::Rva22S64;
    if (str == "rva23u64") return ArchitectureProfile::Rva23U64;
    if (str == "rva23s64" || str == "rva23") return ArchitectureProfile::Rva23S64;
    if (str == "rvb23u64") return ArchitectureProfile::Rvb23U64;
    if (str == "rvb23s64" || str == "rvb23") return ArchitectureProfile::Rvb23S64;
    return std::nullopt;
}

/**
 * @brief Returns the baseline ISA string implied by an ArchitectureProfile.
 */
constexpr auto profile_default_isa_string(ArchitectureProfile profile) noexcept
    -> std::string_view {
    switch (profile) {
        case ArchitectureProfile::Legacy:
            return simrv::xlen::kIsXLen64 ? "rv64gc" : "rv32gc";
        case ArchitectureProfile::Rva20U64:
        case ArchitectureProfile::Rva20S64:
            return "rv64gc_zicsr_zifencei_zicntr";
        case ArchitectureProfile::Rva22U64:
        case ArchitectureProfile::Rva22S64:
            return "rv64gc_zba_zbb_zbs_zicbom_zicboz_zicbop_zihintpause_zfhmin_zicsr_zifencei_"
                   "zicntr";
        case ArchitectureProfile::Rvb23U64:
        case ArchitectureProfile::Rvb23S64:
            return "rv64gcb_zicond_zicbom_zicboz_zicbop_zihintpause_zfhmin_zfa_zimop_zicsr_"
                   "zifencei_zicntr";
        case ArchitectureProfile::Rva23U64:
        case ArchitectureProfile::Rva23S64:
            return "rv64gczbv_zicond_zicbom_zicboz_zicbop_zihintpause_zfhmin_zfa_zimop_zicsr_"
                   "zifencei_zicntr";
    }
    return "rv64gc";
}

/**
 * @brief Returns the ISA string for an ArchitectureProfile including SimRV's
 *        implemented optional/recommended extensions (Sv48, Sv57, Svadu, Zkr, Sdtrig).
 */
constexpr auto profile_recommended_isa_string(ArchitectureProfile profile) noexcept
    -> std::string_view {
    switch (profile) {
        case ArchitectureProfile::Legacy:
            return simrv::xlen::kIsXLen64 ? "rv64gc_zicsr_zifencei_zicntr"
                                          : "rv32gc_zicsr_zifencei_zicntr";
        case ArchitectureProfile::Rva20U64:
            return "rv64gc_zicsr_zifencei_zicntr";
        case ArchitectureProfile::Rva20S64:
            return "rv64gc_zicsr_zifencei_zicntr_sv48_sv57";
        case ArchitectureProfile::Rva22U64:
            return "rv64gc_zba_zbb_zbs_zicbom_zicboz_zicbop_zihintpause_zfhmin_zicsr_zifencei_"
                   "zicntr_zkr";
        case ArchitectureProfile::Rva22S64:
            return "rv64gc_zba_zbb_zbs_zicbom_zicboz_zicbop_zihintpause_zfhmin_zicsr_zifencei_"
                   "zicntr_zkr_sv48_sv57_svadu_sdtrig";
        case ArchitectureProfile::Rvb23U64:
            return "rv64gcb_zicond_zicbom_zicboz_zicbop_zihintpause_zfhmin_zfa_zimop_zicsr_"
                   "zifencei_zicntr_zkr";
        case ArchitectureProfile::Rvb23S64:
            return "rv64gcb_zicond_zicbom_zicboz_zicbop_zihintpause_zfhmin_zfa_zimop_zicsr_"
                   "zifencei_zicntr_zkr_sv48_sv57_svadu_sdtrig";
        case ArchitectureProfile::Rva23U64:
            return "rv64gczbv_zicond_zicbom_zicboz_zicbop_zihintpause_zfhmin_zfa_zimop_zicsr_"
                   "zifencei_zicntr_zkr";
        case ArchitectureProfile::Rva23S64:
            return "rv64gczbv_zicond_zicbom_zicboz_zicbop_zihintpause_zfhmin_zfa_zimop_zicsr_"
                   "zifencei_zicntr_zkr_sv48_sv57_svadu_sdtrig";
    }
    return "rv64gc";
}

/**
 * @brief Default architecture profile for the simulator instance.
 */
constexpr ArchitectureProfile kDefaultArchitectureProfile =
    simrv::xlen::kIsXLen64 ? ArchitectureProfile::Rva23S64 : ArchitectureProfile::Legacy;

}  // namespace simrv::isa
