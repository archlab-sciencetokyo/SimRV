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
    M = 12,  ///< Integer Multiply/Divide extension
    S = 18,  ///< Supervisor Privilege Level support
    U = 20,  ///< User Privilege Level support
    V = 21,  ///< Vector Extension
    X = 23,  ///< Non-standard extensions present
};

/**
 * @enum ArchitectureProfile
 * @brief Formal RISC-V Architectural Profiles per RVI Profiles Specification.
 */
enum class ArchitectureProfile : uint8_t {
    Legacy,    ///< Unconstrained standard RV32GC / RV64GC
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

    /* Privileged RVA23 Mandatory */
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
[[nodiscard]] constexpr auto parse_multi_letter_extension(std::string_view name) noexcept
    -> std::optional<MultiLetterExtension> {
    if (name == "zicsr") return MultiLetterExtension::Zicsr;
    if (name == "zifencei") return MultiLetterExtension::Zifencei;
    if (name == "zicntr") return MultiLetterExtension::Zicntr;
    if (name == "zihpm") return MultiLetterExtension::Zihpm;
    if (name == "ziccif") return MultiLetterExtension::Ziccif;
    if (name == "ziccrse") return MultiLetterExtension::Ziccrse;
    if (name == "ziccamoa") return MultiLetterExtension::Ziccamoa;
    if (name == "zicclsm") return MultiLetterExtension::Zicclsm;
    if (name == "za64rs") return MultiLetterExtension::Za64rs;
    if (name == "za128rs") return MultiLetterExtension::Za128rs;
    if (name == "zihintpause") return MultiLetterExtension::Zihintpause;
    if (name == "zihintntl") return MultiLetterExtension::Zihintntl;
    if (name == "zic64b") return MultiLetterExtension::Zic64b;
    if (name == "zicbom") return MultiLetterExtension::Zicbom;
    if (name == "zicbop") return MultiLetterExtension::Zicbop;
    if (name == "zicboz") return MultiLetterExtension::Zicboz;
    if (name == "zba") return MultiLetterExtension::Zba;
    if (name == "zbb") return MultiLetterExtension::Zbb;
    if (name == "zbc") return MultiLetterExtension::Zbc;
    if (name == "zbs") return MultiLetterExtension::Zbs;
    if (name == "zicond") return MultiLetterExtension::Zicond;
    if (name == "zimop") return MultiLetterExtension::Zimop;
    if (name == "zcmop") return MultiLetterExtension::Zcmop;
    if (name == "zcb") return MultiLetterExtension::Zcb;
    if (name == "zca") return MultiLetterExtension::Zca;
    if (name == "zcd") return MultiLetterExtension::Zcd;
    if (name == "zfa") return MultiLetterExtension::Zfa;
    if (name == "zfhmin") return MultiLetterExtension::Zfhmin;
    if (name == "zawrs") return MultiLetterExtension::Zawrs;
    if (name == "zkt") return MultiLetterExtension::Zkt;
    if (name == "zvfhmin") return MultiLetterExtension::Zvfhmin;
    if (name == "zvbb") return MultiLetterExtension::Zvbb;
    if (name == "zvkb") return MultiLetterExtension::Zvkb;
    if (name == "zvbc") return MultiLetterExtension::Zvbc;
    if (name == "zvkt") return MultiLetterExtension::Zvkt;
    if (name == "zmmul") return MultiLetterExtension::Zmmul;
    if (name == "zaamo") return MultiLetterExtension::Zaamo;
    if (name == "zalrsc") return MultiLetterExtension::Zalrsc;
    if (name == "supm") return MultiLetterExtension::Supm;
    if (name == "ss1p13") return MultiLetterExtension::Ss1p13;
    if (name == "svbare") return MultiLetterExtension::Svbare;
    if (name == "sv32") return MultiLetterExtension::Sv32;
    if (name == "sv39") return MultiLetterExtension::Sv39;
    if (name == "svade") return MultiLetterExtension::Svade;
    if (name == "ssccptr") return MultiLetterExtension::Ssccptr;
    if (name == "sstvecd") return MultiLetterExtension::Sstvecd;
    if (name == "sstvala") return MultiLetterExtension::Sstvala;
    if (name == "sscounterenw") return MultiLetterExtension::Sscounterenw;
    if (name == "svpbmt") return MultiLetterExtension::Svpbmt;
    if (name == "svinval") return MultiLetterExtension::Svinval;
    if (name == "svnapot") return MultiLetterExtension::Svnapot;
    if (name == "sstc") return MultiLetterExtension::Sstc;
    if (name == "sscofpmf") return MultiLetterExtension::Sscofpmf;
    if (name == "ssnpm") return MultiLetterExtension::Ssnpm;
    if (name == "ssu64xl") return MultiLetterExtension::Ssu64xl;
    if (name == "ssstateen") return MultiLetterExtension::Ssstateen;
    if (name == "shcounterenw") return MultiLetterExtension::Shcounterenw;
    if (name == "shvstvala") return MultiLetterExtension::Shvstvala;
    if (name == "shtvala") return MultiLetterExtension::Shtvala;
    if (name == "shvstvecd") return MultiLetterExtension::Shvstvecd;
    if (name == "shvsatpa") return MultiLetterExtension::Shvsatpa;
    if (name == "shgatpa") return MultiLetterExtension::Shgatpa;
    if (name == "sv48") return MultiLetterExtension::Sv48;
    if (name == "sv57") return MultiLetterExtension::Sv57;
    if (name == "sdtrig") return MultiLetterExtension::Sdtrig;
    if (name == "zkr") return MultiLetterExtension::Zkr;
    if (name == "svadu") return MultiLetterExtension::Svadu;
    if (name == "sspm") return MultiLetterExtension::Sspm;
    if (name == "svvptc") return MultiLetterExtension::Svvptc;
    if (name == "ssstrict") return MultiLetterExtension::Ssstrict;
    if (name == "zacas") return MultiLetterExtension::Zacas;
    if (name == "zabha") return MultiLetterExtension::Zabha;
    if (name == "zfh") return MultiLetterExtension::Zfh;
    if (name == "zvfh") return MultiLetterExtension::Zvfh;
    if (name == "zfbfmin") return MultiLetterExtension::Zfbfmin;
    if (name == "zvfbfmin") return MultiLetterExtension::Zvfbfmin;
    if (name == "zvfbfwma") return MultiLetterExtension::Zvfbfwma;
    if (name == "zvkg") return MultiLetterExtension::Zvkg;
    if (name == "zvkned") return MultiLetterExtension::Zvkned;
    if (name == "zvknha") return MultiLetterExtension::Zvknha;
    if (name == "zvknhb") return MultiLetterExtension::Zvknhb;
    if (name == "zvksed") return MultiLetterExtension::Zvksed;
    if (name == "zvksh") return MultiLetterExtension::Zvksh;
    if (name == "zbkb") return MultiLetterExtension::Zbkb;
    if (name == "zbkc") return MultiLetterExtension::Zbkc;
    if (name == "zbkx") return MultiLetterExtension::Zbkx;
    if (name == "zknd") return MultiLetterExtension::Zknd;
    if (name == "zkne") return MultiLetterExtension::Zkne;
    if (name == "zknh") return MultiLetterExtension::Zknh;
    if (name == "zksed") return MultiLetterExtension::Zksed;
    if (name == "zksh") return MultiLetterExtension::Zksh;
    if (name == "zcmp") return MultiLetterExtension::Zcmp;
    if (name == "zcmt") return MultiLetterExtension::Zcmt;
    if (name == "zfinx") return MultiLetterExtension::Zfinx;
    if (name == "zdinx") return MultiLetterExtension::Zdinx;
    if (name == "zhinx") return MultiLetterExtension::Zhinx;
    if (name == "smdbltrp") return MultiLetterExtension::Smdbltrp;
    if (name == "ssdbltrp") return MultiLetterExtension::Ssdbltrp;
    if (name == "smcsrind") return MultiLetterExtension::Smcsrind;
    if (name == "sscsrind") return MultiLetterExtension::Sscsrind;
    if (name == "smrnmi") return MultiLetterExtension::Smrnmi;
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
    if (const auto ext = parse_multi_letter_extension(name)) {
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
