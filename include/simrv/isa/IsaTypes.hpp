/**
 * @file IsaTypes.hpp
 * @brief ISA extension and preset identifiers.
 */
#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace simrv::isa {

/**
 * @enum IsaExtension
 * @brief Identifies standard MISA extension bits and the reserved non-standard X bit.
 */
enum class IsaExtension : uint8_t {
    E = 4,   ///< Reduced 32-bit integer register-set extension
    A = 0,   ///< Atomic Instructions extension
    B = 1,   ///< Bitmanip Instructions extension
    C = 2,   ///< Compressed Instructions extension
    D = 3,   ///< Double-Precision Floating-Point extension
    F = 5,   ///< Single-Precision Floating-Point extension
    H = 7,   ///< Hypervisor extension
    I = 8,   ///< Base Integer Instruction Set extension
    Q = 16,  ///< Quad-Precision Floating-Point extension
    M = 12,  ///< Integer Multiply/Divide extension
    V = 21,  ///< Vector Extension
    X = 23,  ///< Non-standard extensions present
};

/** Privilege-mode support bits exposed by MISA, separate from instruction extensions. */
enum class MisaPrivilegeSupport : uint8_t {
    Supervisor = 18,
    User = 20,
};

/**
 * @enum ArchitectureProfile
 * @brief Formal RISC-V Architectural Profiles per RVI Profiles Specification.
 */
enum class ArchitectureProfile : uint8_t {
    Rvi20U32,  ///< Generic RV32I unprivileged profile
    Rvi20U64,  ///< Generic RV64I unprivileged profile
    Rva20U64,  ///< RVA20 User-mode 64-bit profile
    Rva20S64,  ///< RVA20 Supervisor-mode 64-bit profile
    Rva22U64,  ///< RVA22 User-mode 64-bit profile
    Rva22S64,  ///< RVA22 Supervisor-mode 64-bit profile
    Rva23U64,  ///< RVA23 User-mode 64-bit profile
    Rva23S64,  ///< RVA23 Supervisor-mode 64-bit profile (Default for RV64)
    Rvb23U64,  ///< RVB23 User-mode 64-bit profile (Base 23 without mandatory Vector)
    Rvb23S64,  ///< RVB23 Supervisor-mode 64-bit profile (Base 23 without mandatory Vector)
};

/**
 * @enum MultiLetterExtension
 * @brief Standardized multi-letter RISC-V extensions tracked in ExtensionSet.
 */
enum class MultiLetterExtension : uint8_t {
    /* Unprivileged Base / RVA23 Mandatory */
    Zicsr = 0,
    Zifencei,
    Zicntr,
    Zihpm,
    Ziccif,
    Ziccrse,
    Ziccamoa,
    Zicclsm,
    Za64rs,
    Za128rs,
    Zihintpause,
    Zihintntl,
    Zic64b,
    Zicbom,
    Zicbop,
    Zicboz,
    Zba,
    Zbb,
    Zbc,
    Zbs,
    Zicond,
    Zimop,
    Zcmop,
    Zcb,
    Zca,
    Zcf,
    Zcd,
    Zfa,
    Zfhmin,
    Zawrs,
    Zkt,
    Zvfhmin,
    Zvbb,
    Zvkb,
    Zvbc,
    Zvkt,
    Zmmul,
    Zaamo,
    Zalrsc,
    Supm,

    /* Ratified privileged profile extensions */
    Ss1p11,
    Ss1p12,
    Ss1p13,
    Svbare,
    Sv32,
    Sv39,
    Svade,
    Ssccptr,
    Sstvecd,
    Sstvala,
    Sscounterenw,
    Svpbmt,
    Svinval,
    Svnapot,
    Sstc,
    Sscofpmf,
    Ssnpm,
    Ssu64xl,
    Ssstateen,
    Shcounterenw,
    Shvstvala,
    Shtvala,
    Shvstvecd,
    Shvsatpa,
    Shgatpa,

    /* RVA23 Optional & Wave 1-3 Planned Extensions */
    Sv48,
    Sv57,
    Sdtrig,
    Zkr,
    Svadu,
    Sspm,
    Svvptc,
    Ssstrict,
    Zacas,
    Zabha,
    Zfh,
    Zvfh,
    Zfbfmin,
    Zvfbfmin,
    Zvfbfwma,
    Zvkg,
    Zvkned,
    Zvknha,
    Zvknhb,
    Zvksed,
    Zvksh,
    Zbkb,
    Zbkc,
    Zbkx,
    Zknd,
    Zkne,
    Zknh,
    Zksed,
    Zksh,
    Zcmp,
    Zcmt,
    Zfinx,
    Zdinx,
    Zhinx,
    Smdbltrp,
    Ssdbltrp,
    Smcsrind,
    Sscsrind,
    Smrnmi,
    Smcntrpmf,
    Smaia,
    Ssaia,
    Zicfilp,
    Zicfiss,

    Count
};

static_assert(static_cast<unsigned>(MultiLetterExtension::Count) <= 128,
              "ExtensionSet holds at most 128 extensions");

/**
 * @struct ExtensionSet
 * @brief Compact O(1) 128-bit bitset of enabled multi-letter RISC-V extensions.
 */
struct ExtensionSet {
    std::array<uint64_t, 2> words{};

    [[nodiscard]] constexpr auto has(MultiLetterExtension ext) const noexcept -> bool {
        const auto idx = static_cast<unsigned>(ext);
        return (words[idx >> 6U] & (uint64_t{1} << (idx & 63U))) != 0;
    }

    constexpr void set(MultiLetterExtension ext) noexcept {
        const auto idx = static_cast<unsigned>(ext);
        words[idx >> 6U] |= (uint64_t{1} << (idx & 63U));
    }

    constexpr void clear(MultiLetterExtension ext) noexcept {
        const auto idx = static_cast<unsigned>(ext);
        words[idx >> 6U] &= ~(uint64_t{1} << (idx & 63U));
    }

    constexpr void merge(const ExtensionSet& other) noexcept {
        words[0] |= other.words[0];
        words[1] |= other.words[1];
    }

    [[nodiscard]] constexpr auto empty() const noexcept -> bool {
        return words[0] == 0 && words[1] == 0;
    }

    friend auto operator==(const ExtensionSet&, const ExtensionSet&) -> bool = default;
};

/**
 * @brief Maps a lowercase canonical multi-letter extension name to MultiLetterExtension.
 */
inline constexpr std::array<std::string_view, static_cast<size_t>(MultiLetterExtension::Count)>
    kMultiLetterExtensionNames = {{
        "zicsr",     "zifencei", "zicntr",       "zihpm",    "ziccif",      "ziccrse",
        "ziccamoa",  "zicclsm",  "za64rs",       "za128rs",  "zihintpause", "zihintntl",
        "zic64b",    "zicbom",   "zicbop",       "zicboz",   "zba",         "zbb",
        "zbc",       "zbs",      "zicond",       "zimop",    "zcmop",       "zcb",
        "zca",       "zcf",      "zcd",          "zfa",      "zfhmin",      "zawrs",
        "zkt",       "zvfhmin",  "zvbb",         "zvkb",     "zvbc",        "zvkt",
        "zmmul",     "zaamo",    "zalrsc",       "supm",     "ss1p11",      "ss1p12",
        "ss1p13",    "svbare",   "sv32",         "sv39",     "svade",       "ssccptr",
        "sstvecd",   "sstvala",  "sscounterenw", "svpbmt",   "svinval",     "svnapot",
        "sstc",      "sscofpmf", "ssnpm",        "ssu64xl",  "ssstateen",   "shcounterenw",
        "shvstvala", "shtvala",  "shvstvecd",    "shvsatpa", "shgatpa",     "sv48",
        "sv57",      "sdtrig",   "zkr",          "svadu",    "sspm",        "svvptc",
        "ssstrict",  "zacas",    "zabha",        "zfh",      "zvfh",        "zfbfmin",
        "zvfbfmin",  "zvfbfwma", "zvkg",         "zvkned",   "zvknha",      "zvknhb",
        "zvksed",    "zvksh",    "zbkb",         "zbkc",     "zbkx",        "zknd",
        "zkne",      "zknh",     "zksed",        "zksh",     "zcmp",        "zcmt",
        "zfinx",     "zdinx",    "zhinx",        "smdbltrp", "ssdbltrp",    "smcsrind",
        "sscsrind",  "smrnmi",   "smcntrpmf",    "smaia",    "ssaia",       "zicfilp",
        "zicfiss",
    }};

[[nodiscard]] constexpr auto multi_letter_extension_name(MultiLetterExtension extension) noexcept
    -> std::string_view {
    const auto index = static_cast<size_t>(extension);
    return index < kMultiLetterExtensionNames.size() ? kMultiLetterExtensionNames[index]
                                                     : std::string_view{};
}

[[nodiscard]] constexpr auto parse_multi_letter_extension(std::string_view name) noexcept
    -> std::optional<MultiLetterExtension> {
    for (size_t i = 0; i < kMultiLetterExtensionNames.size(); ++i) {
        if (kMultiLetterExtensionNames[i] == name) {
            return static_cast<MultiLetterExtension>(i);
        }
    }
    return std::nullopt;
}

/**
 * @brief Adds a lowercase extension name (or aggregate shorthand like zk/zkn/zks/zvkn/zvks/sha)
 *        into an ExtensionSet.
 */
constexpr void add_extension_to_set(ExtensionSet& set, std::string_view name) noexcept {
    if (name == "zk") {
        add_extension_to_set(set, "zkn");
        set.set(MultiLetterExtension::Zkr);
        set.set(MultiLetterExtension::Zkt);
        return;
    }
    if (name == "zkn") {
        set.set(MultiLetterExtension::Zbkb);
        set.set(MultiLetterExtension::Zbkc);
        set.set(MultiLetterExtension::Zbkx);
        set.set(MultiLetterExtension::Zknd);
        set.set(MultiLetterExtension::Zkne);
        set.set(MultiLetterExtension::Zknh);
        return;
    }
    if (name == "zks") {
        set.set(MultiLetterExtension::Zbkb);
        set.set(MultiLetterExtension::Zbkc);
        set.set(MultiLetterExtension::Zbkx);
        set.set(MultiLetterExtension::Zksed);
        set.set(MultiLetterExtension::Zksh);
        return;
    }
    if (name == "zvkn") {
        set.set(MultiLetterExtension::Zvkned);
        set.set(MultiLetterExtension::Zvknhb);
        set.set(MultiLetterExtension::Zvkb);
        set.set(MultiLetterExtension::Zvkt);
        return;
    }
    if (name == "zvks") {
        set.set(MultiLetterExtension::Zvksed);
        set.set(MultiLetterExtension::Zvksh);
        set.set(MultiLetterExtension::Zvkb);
        set.set(MultiLetterExtension::Zvkt);
        return;
    }
    if (name == "zvkng") {
        add_extension_to_set(set, "zvkn");
        set.set(MultiLetterExtension::Zvkg);
        return;
    }
    if (name == "zvknc") {
        add_extension_to_set(set, "zvkn");
        set.set(MultiLetterExtension::Zvbc);
        return;
    }
    if (name == "zvksg") {
        add_extension_to_set(set, "zvks");
        set.set(MultiLetterExtension::Zvkg);
        return;
    }
    if (name == "zvksc") {
        add_extension_to_set(set, "zvks");
        set.set(MultiLetterExtension::Zvbc);
        return;
    }
    if (name == "zvk") {
        set.set(MultiLetterExtension::Zvkned);
        set.set(MultiLetterExtension::Zvknhb);
        set.set(MultiLetterExtension::Zvksed);
        set.set(MultiLetterExtension::Zvksh);
        set.set(MultiLetterExtension::Zvkg);
        set.set(MultiLetterExtension::Zvkb);
        set.set(MultiLetterExtension::Zvkt);
        return;
    }
    if (name == "sha") {
        set.set(MultiLetterExtension::Ssstateen);
        set.set(MultiLetterExtension::Shcounterenw);
        set.set(MultiLetterExtension::Shvstvala);
        set.set(MultiLetterExtension::Shtvala);
        set.set(MultiLetterExtension::Shvstvecd);
        set.set(MultiLetterExtension::Shvsatpa);
        set.set(MultiLetterExtension::Shgatpa);
        return;
    }
    auto ext = parse_multi_letter_extension(name);
    if (!ext) {
        for (size_t split = name.size(); split > 0; --split) {
            const auto suffix = name.substr(split);
            const bool supported_version =
                suffix == "1" || suffix == "1p0" || suffix == "2" || suffix == "2p0";
            if (supported_version) {
                ext = parse_multi_letter_extension(name.substr(0, split));
                if (ext) break;
            }
        }
    }
    if (ext) {
        set.set(*ext);
        // Implied sub-extensions
        if (*ext == MultiLetterExtension::Zfh) set.set(MultiLetterExtension::Zfhmin);
        if (*ext == MultiLetterExtension::Zvfh) set.set(MultiLetterExtension::Zvfhmin);
        if (*ext == MultiLetterExtension::Zvbb) set.set(MultiLetterExtension::Zvkb);
        if (*ext == MultiLetterExtension::Zdinx) set.set(MultiLetterExtension::Zfinx);
        if (*ext == MultiLetterExtension::Zhinx) set.set(MultiLetterExtension::Zfinx);
    }
}

}  // namespace simrv::isa
