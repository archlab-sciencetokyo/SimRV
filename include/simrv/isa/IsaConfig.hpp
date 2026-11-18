#pragma once

#include <algorithm>
#include <array>
#include <cctype>
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

constexpr auto misa_privilege_support_bit(MisaPrivilegeSupport mode) -> simrv::xlen::CsrValue {
    return static_cast<simrv::xlen::CsrValue>(simrv::xlen::CsrValue{1}
                                              << static_cast<unsigned>(mode));
}

constexpr auto misa_with_privilege_support(simrv::xlen::CsrValue misa, bool supervisor, bool user)
    -> simrv::xlen::CsrValue {
    if (supervisor) misa |= misa_privilege_support_bit(MisaPrivilegeSupport::Supervisor);
    if (user) misa |= misa_privilege_support_bit(MisaPrivilegeSupport::User);
    return misa;
}

/**
 * @brief Computes the combined bitmask of all baseline supported extensions.
 * @return CsrValue mask for all base extensions.
 */
constexpr auto misa_base_bits() -> simrv::xlen::CsrValue {
    return misa_extension_bit(IsaExtension::I) | misa_extension_bit(IsaExtension::M) |
           misa_extension_bit(IsaExtension::A) | misa_extension_bit(IsaExtension::B) |
           misa_extension_bit(IsaExtension::F) | misa_extension_bit(IsaExtension::D) |
           misa_extension_bit(IsaExtension::C) | misa_extension_bit(IsaExtension::V);
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
        if (has(IsaExtension::Q)) out.push_back('q');
        if (has(IsaExtension::C)) out.push_back('c');
        if (has(IsaExtension::B)) out.push_back('b');
        if (has(IsaExtension::V)) out.push_back('v');
        auto canonical_extensions = extensions;
        std::ranges::sort(canonical_extensions);
        for (const auto& ext : canonical_extensions) {
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
    const size_t glued_extension = s.find_first_of("zx");
    const size_t extension_start = std::min(
        underscore, glued_extension == std::string_view::npos ? s.size() : glued_extension);
    const std::string_view single_letters = s.substr(0, extension_start);
    if (single_letters.empty()) return std::nullopt;

    const char base = single_letters.front();
    if (base == 'e') {
        spec.misa_bits |= misa_extension_bit(IsaExtension::E);
        if (spec.xlen == 0) spec.xlen = 32;
    } else if (base == 'i') {
        spec.misa_bits |= misa_extension_bit(IsaExtension::I);
    } else if (base == 'g') {
        spec.misa_bits |= misa_extension_bit(IsaExtension::I) |
                          misa_extension_bit(IsaExtension::M) |
                          misa_extension_bit(IsaExtension::A) |
                          misa_extension_bit(IsaExtension::F) | misa_extension_bit(IsaExtension::D);
        spec.extension_set.set(MultiLetterExtension::Zicsr);
        spec.extension_set.set(MultiLetterExtension::Zifencei);
    } else {
        return std::nullopt;
    }

    for (size_t i = 1; i < single_letters.size(); ++i) {
        const char ch = single_letters[i];
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
            case 'q':
                spec.misa_bits |= misa_extension_bit(IsaExtension::Q) |
                                  misa_extension_bit(IsaExtension::D) |
                                  misa_extension_bit(IsaExtension::F);
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
            case 'h':
                spec.misa_bits |= misa_extension_bit(IsaExtension::H);
                break;
            default:
                return std::nullopt;
        }
    }

    if ((spec.misa_bits & misa_extension_bit(IsaExtension::M)) != 0) {
        spec.extension_set.set(MultiLetterExtension::Zmmul);
    }
    if ((spec.misa_bits & misa_extension_bit(IsaExtension::F)) != 0) {
        spec.extension_set.set(MultiLetterExtension::Zicsr);
    }
    if ((spec.misa_bits & misa_extension_bit(IsaExtension::D)) != 0) {
        spec.misa_bits |= misa_extension_bit(IsaExtension::F);
        spec.extension_set.set(MultiLetterExtension::Zicsr);
    }
    if ((spec.misa_bits & misa_extension_bit(IsaExtension::A)) != 0) {
        spec.extension_set.set(MultiLetterExtension::Zaamo);
        spec.extension_set.set(MultiLetterExtension::Zalrsc);
    }
    if ((spec.misa_bits & misa_extension_bit(IsaExtension::B)) != 0) {
        spec.extension_set.set(MultiLetterExtension::Zba);
        spec.extension_set.set(MultiLetterExtension::Zbb);
        spec.extension_set.set(MultiLetterExtension::Zbs);
    }
    if ((spec.misa_bits & misa_extension_bit(IsaExtension::C)) != 0) {
        spec.extension_set.set(MultiLetterExtension::Zca);
        if (spec.xlen == 32 && (spec.misa_bits & misa_extension_bit(IsaExtension::F)) != 0) {
            spec.extension_set.set(MultiLetterExtension::Zcf);
        }
        if ((spec.misa_bits & misa_extension_bit(IsaExtension::D)) != 0) {
            spec.extension_set.set(MultiLetterExtension::Zcd);
        }
    }

    if (extension_start < s.size()) {
        std::string_view rest = s.substr(extension_start);
        if (rest.front() == '_') rest.remove_prefix(1);
        while (!rest.empty()) {
            const size_t next = rest.find('_');
            const std::string_view token =
                (next == std::string_view::npos) ? rest : rest.substr(0, next);
            if (token.empty()) return std::nullopt;
            constexpr std::array<std::string_view, 11> kAggregateExtensions = {
                "sha",  "zk",    "zkn",   "zks",   "zvk",  "zvkn",
                "zvks", "zvkng", "zvknc", "zvksg", "zvksc"};
            bool recognized =
                std::ranges::find(kAggregateExtensions, token) != kAggregateExtensions.end();
            auto known = parse_multi_letter_extension(token);
            if (!known) {
                for (size_t split = token.size(); split > 0; --split) {
                    const auto suffix = token.substr(split);
                    if (suffix != "1" && suffix != "1p0" && suffix != "2" && suffix != "2p0") {
                        continue;
                    }
                    known = parse_multi_letter_extension(token.substr(0, split));
                    if (known) break;
                }
            }
            recognized = recognized || known.has_value();
            if (!recognized && token.front() == 'x' && token.size() > 1 &&
                std::ranges::all_of(token,
                                    [](unsigned char ch) { return std::isalnum(ch) != 0; })) {
                recognized = true;
                spec.misa_bits |= misa_extension_bit(IsaExtension::X);
            }
            if (!recognized) return std::nullopt;
            if (std::ranges::find(spec.extensions, token) != spec.extensions.end()) {
                return std::nullopt;
            }
            spec.extensions.emplace_back(token);
            add_extension_to_set(spec.extension_set, token);
            if (next == std::string_view::npos) break;
            rest.remove_prefix(next + 1);
        }
    }

    if (std::ranges::find(spec.extensions, "sha") != spec.extensions.end()) {
        spec.misa_bits |= misa_extension_bit(IsaExtension::H);
    }
    if (spec.extension_set.has(MultiLetterExtension::Zicntr)) {
        spec.extension_set.set(MultiLetterExtension::Zicsr);
    }
    if (spec.extension_set.has(MultiLetterExtension::Zba) &&
        spec.extension_set.has(MultiLetterExtension::Zbb) &&
        spec.extension_set.has(MultiLetterExtension::Zbs)) {
        spec.misa_bits |= misa_extension_bit(IsaExtension::B);
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

struct ArchitectureProfileInfo {
    ArchitectureProfile profile;
    std::string_view name;
    std::string_view mandatory_isa;
    uint8_t xlen;
    bool supervisor_profile;
};

// Ratified profile definitions: RVI20/RVA20/RVA22 v1.0 and RVA23/RVB23 v1.0.
inline constexpr std::array kArchitectureProfiles{
    ArchitectureProfileInfo{.profile = ArchitectureProfile::Rvi20U32,
                            .name = "rvi20u32",
                            .mandatory_isa = "rv32i",
                            .xlen = 32,
                            .supervisor_profile = false},
    ArchitectureProfileInfo{.profile = ArchitectureProfile::Rvi20U64,
                            .name = "rvi20u64",
                            .mandatory_isa = "rv64i",
                            .xlen = 64,
                            .supervisor_profile = false},
    ArchitectureProfileInfo{
        .profile = ArchitectureProfile::Rva20U64,
        .name = "rva20u64",
        .mandatory_isa = "rv64imafdc_za128rs_zicclsm_ziccamoa_ziccif_ziccrse_zicntr",
        .xlen = 64,
        .supervisor_profile = false},
    ArchitectureProfileInfo{
        .profile = ArchitectureProfile::Rva20S64,
        .name = "rva20s64",
        .mandatory_isa =
            "rv64imafdc_za128rs_zicclsm_ziccamoa_ziccif_ziccrse_zicntr_zifencei_ss1p11_svade_"
            "svbare_sv39_ssccptr_sstvala_sstvecd",
        .xlen = 64,
        .supervisor_profile = true},
    ArchitectureProfileInfo{
        .profile = ArchitectureProfile::Rva22U64,
        .name = "rva22u64",
        .mandatory_isa =
            "rv64imafdc_zba_zbb_zbs_za64rs_zfhmin_zic64b_zicbom_zicbop_zicboz_zicclsm_ziccamoa_"
            "ziccif_ziccrse_zicntr_zihintpause_zihpm_zkt",
        .xlen = 64,
        .supervisor_profile = false},
    ArchitectureProfileInfo{
        .profile = ArchitectureProfile::Rva22S64,
        .name = "rva22s64",
        .mandatory_isa =
            "rv64imafdc_zba_zbb_zbs_za64rs_zfhmin_zic64b_zicbom_zicbop_zicboz_zicclsm_ziccamoa_"
            "ziccif_ziccrse_zicntr_zifencei_zihintpause_zihpm_zkt_ss1p12_ssccptr_sscounterenw_"
            "sstvala_sstvecd_svinval_svade_svbare_svpbmt_sv39",
        .xlen = 64,
        .supervisor_profile = true},
    ArchitectureProfileInfo{
        .profile = ArchitectureProfile::Rva23U64,
        .name = "rva23u64",
        .mandatory_isa =
            "rv64imafdcbv_zawrs_zba_zbb_zbs_za64rs_zfhmin_zfa_zic64b_zicbom_zicbop_zicboz_zicclsm_"
            "ziccamoa_ziccif_ziccrse_zicntr_zicond_zihintntl_zihintpause_zihpm_zcb_zcmop_zimop_zkt_"
            "zvbb_zvfhmin_zvkt_supm",
        .xlen = 64,
        .supervisor_profile = false},
    ArchitectureProfileInfo{
        .profile = ArchitectureProfile::Rva23S64,
        .name = "rva23s64",
        .mandatory_isa =
            "rv64imafdcbv_zawrs_zba_zbb_zbs_za64rs_zfhmin_zfa_zic64b_zicbom_zicbop_zicboz_"
            "zicclsm_ziccamoa_ziccif_ziccrse_zicntr_zicond_zihintntl_zihintpause_zihpm_zcb_"
            "zcmop_zimop_zkt_zvbb_zvfhmin_zvkt_supm_zifencei_ss1p13_ssccptr_sscounterenw_"
            "ssstateen_shcounterenw_shvstvala_shtvala_shvstvecd_shvsatpa_shgatpa_ssnpm_"
            "ssu64xl_sstc_sstvala_sstvecd_sscofpmf_svinval_svade_svbare_svnapot_svpbmt_sv39_sha",
        .xlen = 64,
        .supervisor_profile = true},
    ArchitectureProfileInfo{
        .profile = ArchitectureProfile::Rvb23U64,
        .name = "rvb23u64",
        .mandatory_isa =
            "rv64imafdcb_zawrs_zba_zbb_zbs_za64rs_zfa_zic64b_zicbom_zicbop_zicboz_zicclsm_"
            "ziccamoa_ziccif_ziccrse_zicntr_zicond_zihintntl_zihintpause_zihpm_zimop_zkt",
        .xlen = 64,
        .supervisor_profile = false},
    ArchitectureProfileInfo{
        .profile = ArchitectureProfile::Rvb23S64,
        .name = "rvb23s64",
        .mandatory_isa =
            "rv64imafdcb_zawrs_zba_zbb_zbs_za64rs_zfa_zic64b_zicbom_zicbop_zicboz_zicclsm_"
            "ziccamoa_ziccif_ziccrse_zicntr_zicond_zihintntl_zihintpause_zihpm_zimop_zkt_zifencei_"
            "ss1p13_ssccptr_sscounterenw_ssu64xl_sstvala_sstvecd_sscofpmf_svinval_svade_svbare_"
            "svnapot_svpbmt_sv39",
        .xlen = 64,
        .supervisor_profile = true},
};

[[nodiscard]] constexpr auto profile_info(ArchitectureProfile profile) noexcept
    -> const ArchitectureProfileInfo* {
    for (const auto& info : kArchitectureProfiles) {
        if (info.profile == profile) return &info;
    }
    return nullptr;
}

[[nodiscard]] constexpr auto to_profile_string(ArchitectureProfile profile) noexcept
    -> std::string_view {
    const auto* info = profile_info(profile);
    return info == nullptr ? "unknown" : info->name;
}

[[nodiscard]] constexpr auto parse_architecture_profile(std::string_view name) noexcept
    -> std::optional<ArchitectureProfile> {
    for (const auto& info : kArchitectureProfiles) {
        if (info.name == name) return info.profile;
    }
    return std::nullopt;
}

[[nodiscard]] constexpr auto profile_default_isa_string(ArchitectureProfile profile) noexcept
    -> std::string_view {
    const auto* info = profile_info(profile);
    return info == nullptr ? std::string_view{} : info->mandatory_isa;
}

/**
 * @brief Default architecture profile for the simulator instance.
 */
constexpr ArchitectureProfile kDefaultArchitectureProfile =
    simrv::xlen::kIsXLen64 ? ArchitectureProfile::Rva23S64 : ArchitectureProfile::Rvi20U32;

}  // namespace simrv::isa
