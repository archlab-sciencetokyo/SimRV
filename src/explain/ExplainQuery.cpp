/**
 * @file ExplainQuery.cpp
 * @brief Name-based instruction, profile, and ISA-extension explanations.
 */
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <format>
#include <print>

#include "simrv/explain/InstructionExplainer.hpp"
#include "simrv/isa/IsaConfig.hpp"
#include "simrv/isa/IsaTypes.hpp"

namespace simrv::explain {
namespace {

using namespace std::literals;

using simrv::isa::ArchitectureProfile;
using simrv::isa::IsaExtension;
using simrv::isa::MultiLetterExtension;
using simrv::isa::OperationId;
using enum simrv::isa::OperationId;

constexpr std::string_view kIsaRoot = "https://docs.riscv.org/reference/isa/";
constexpr std::string_view kProfilesUrl =
    "https://docs.riscv.org/reference/profiles-overview/index.html";
constexpr std::string_view kRva23Url =
    "https://docs.riscv.org/reference/rva23/v1.0/rva23-profiles.html";
constexpr std::string_view kNamingUrl =
    "https://docs.riscv.org/reference/isa/v20260120/unpriv/naming.html";

auto lower(std::string_view value) -> std::string {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char ch : value) {
        result.push_back(static_cast<char>(std::tolower(ch)));
    }
    return result;
}

auto upper(std::string_view value) -> std::string {
    std::string result;
    result.reserve(value.size());
    for (const unsigned char ch : value) {
        result.push_back(static_cast<char>(std::toupper(ch)));
    }
    return result;
}

auto contains_case_insensitive(std::string_view text, std::string_view query) -> bool {
    return lower(text).find(lower(query)) != std::string::npos;
}

auto profile_url(std::string_view name) -> std::string_view {
    return name.starts_with("rva23") ? kRva23Url : kProfilesUrl;
}

auto operation_url(const OperationSpecDetails& spec) -> std::string_view {
    const auto extension = spec.reference.extension;
    if (spec.reference.document == kPrivSpecDoc) {
        if (extension == "H") return "https://docs.riscv.org/reference/isa/priv/hypervisor.html";
        if (spec.reference.section.find("Supervisor") != std::string_view::npos ||
            extension == "Svinval") {
            return "https://docs.riscv.org/reference/isa/priv/supervisor.html";
        }
        return "https://docs.riscv.org/reference/isa/priv/machine.html";
    }
    if (extension == "RV32I") return "https://docs.riscv.org/reference/isa/unpriv/rv32.html";
    if (extension == "RV64I") return "https://docs.riscv.org/reference/isa/unpriv/rv64.html";
    if (extension == "A" || extension == "RV32A" || extension == "RV64A") {
        return "https://docs.riscv.org/reference/isa/unpriv/a-st-ext.html";
    }
    if (extension == "M" || extension == "RV32M" || extension == "RV64M") {
        return "https://docs.riscv.org/reference/isa/unpriv/m-st-ext.html";
    }
    if (extension == "F" || extension == "RV32F") {
        return "https://docs.riscv.org/reference/isa/unpriv/f-st-ext.html";
    }
    if (extension == "D" || extension == "RV32D") {
        return "https://docs.riscv.org/reference/isa/unpriv/d-st-ext.html";
    }
    if (extension == "C") return "https://docs.riscv.org/reference/isa/unpriv/c-st-ext.html";
    if (extension == "V" || extension == "Vector") {
        return "https://docs.riscv.org/reference/isa/unpriv/v-st-ext.html";
    }
    if (extension == "Zba" || extension == "Zbb" || extension == "Zbc" || extension == "Zbs" ||
        extension == "Zbkb") {
        return "https://docs.riscv.org/reference/isa/unpriv/b-st-ext.html";
    }
    if (extension.find("Zicbo") != std::string_view::npos || extension == "CMO") {
        return "https://docs.riscv.org/reference/isa/unpriv/cmo.html";
    }
    return kIsaRoot;
}

auto make_instruction_entry(OperationId op) -> std::optional<ExplainEntry> {
    if (op == ILLEGAL) return std::nullopt;
    const auto spec = get_operation_spec(op);
    if (spec.mnemonic.empty() || spec.mnemonic == "UNKNOWN") return std::nullopt;
    return ExplainEntry{
        .kind = ExplainEntryKind::Instruction,
        .name = std::string(spec.mnemonic),
        .summary = std::string(spec.reference.extension),
        .description = std::string(spec.description),
        .spec_reference = std::format("{} — {}", spec.reference.document, spec.reference.section),
        .spec_url = std::string(operation_url(spec)),
    };
}

auto make_profile_entry(const simrv::isa::ArchitectureProfileInfo& profile) -> ExplainEntry {
    const std::string mode = profile.supervisor_profile ? "Supervisor-capable (S)" : "User (U)";
    return ExplainEntry{
        .kind = ExplainEntryKind::Profile,
        .name = upper(profile.name),
        .summary = std::format("RV{} · {} profile", profile.xlen, mode),
        .description = std::format("Mandatory ISA: {}", profile.mandatory_isa),
        .spec_reference = "RISC-V Profiles Specification",
        .spec_url = std::string(profile_url(profile.name)),
    };
}

auto profile_requires(std::string_view profile_isa, std::string_view extension) -> bool {
    if (extension.size() == 1) {
        const auto base_end = profile_isa.find('_');
        const auto base = profile_isa.substr(0, base_end);
        return base.find(extension.front()) != std::string_view::npos;
    }
    size_t start = profile_isa.find('_');
    while (start != std::string_view::npos && start < profile_isa.size()) {
        ++start;
        const size_t end = profile_isa.find('_', start);
        const auto token = profile_isa.substr(
            start, end == std::string_view::npos ? profile_isa.size() - start : end - start);
        if (token == extension) return true;
        start = end;
    }
    return false;
}

auto extension_entry(std::string_view name) -> std::optional<ExplainEntry> {
    const auto normalized = lower(name);
    static constexpr std::array single_letter_descriptions{
        std::pair{"a"sv, "Atomic instructions"sv},
        std::pair{"b"sv, "Bit-manipulation instructions"sv},
        std::pair{"c"sv, "Compressed instructions"sv},
        std::pair{"d"sv, "Double-precision floating-point instructions"sv},
        std::pair{"e"sv, "Reduced-register embedded base ISA"sv},
        std::pair{"f"sv, "Single-precision floating-point instructions"sv},
        std::pair{"h"sv, "Hypervisor extension"sv},
        std::pair{"i"sv, "Base integer instruction set"sv},
        std::pair{"m"sv, "Integer multiplication and division"sv},
        std::pair{"q"sv, "Quad-precision floating-point instructions"sv},
        std::pair{"v"sv, "Vector extension"sv},
        std::pair{"x"sv, "Non-standard extension marker"sv},
    };

    if (normalized == "s" || normalized == "u") {
        const bool supervisor = normalized == "s";
        return ExplainEntry{
            .kind = ExplainEntryKind::Extension,
            .name = std::string(1, static_cast<char>(std::toupper(normalized.front()))),
            .summary =
                supervisor ? "MISA privilege-support bit 18" : "MISA privilege-support bit 20",
            .description =
                supervisor ? "Indicates supervisor-mode support. S is not an ISA extension letter."
                           : "Indicates user-mode support. U is not an ISA extension letter.",
            .spec_reference = "Machine-Level ISA, misa register",
            .spec_url = "https://docs.riscv.org/reference/isa/priv/machine.html",
        };
    }

    for (const auto& [letter, description] : single_letter_descriptions) {
        if (normalized == letter) {
            return ExplainEntry{
                .kind = ExplainEntryKind::Extension,
                .name = std::string(1, static_cast<char>(std::toupper(letter.front()))),
                .summary = "Standard single-letter ISA extension",
                .description = std::string(description),
                .spec_reference = "ISA naming conventions and the unprivileged ISA manual",
                .spec_url = std::string(kNamingUrl),
            };
        }
    }

    const auto multi = simrv::isa::parse_multi_letter_extension(normalized);
    if (!multi) return std::nullopt;

    std::string profiles;
    for (const auto& profile : simrv::isa::kArchitectureProfiles) {
        if (profile_requires(profile.mandatory_isa, normalized)) {
            if (!profiles.empty()) profiles += ", ";
            profiles += profile.name;
        }
    }
    if (profiles.empty()) profiles = "No registered profile mandates this extension";

    std::string_view url = kIsaRoot;
    if (normalized.starts_with("zb")) {
        url = "https://docs.riscv.org/reference/isa/unpriv/b-st-ext.html";
    } else if (normalized.starts_with("zicbo")) {
        url = "https://docs.riscv.org/reference/isa/unpriv/cmo.html";
    } else if (normalized.starts_with("ss") || normalized.starts_with("sv") ||
               normalized.starts_with("sh") || normalized.starts_with("sm")) {
        url = "https://docs.riscv.org/reference/isa/priv/supervisor.html";
    }

    return ExplainEntry{
        .kind = ExplainEntryKind::Extension,
        .name = std::string(simrv::isa::multi_letter_extension_name(*multi)),
        .summary = "Standard multi-letter extension tracked by SimRV",
        .description = std::format("Mandatory in: {}", profiles),
        .spec_reference = "RISC-V ISA Manual",
        .spec_url = std::string(url),
    };
}

auto parse_instruction_word(std::string_view query, uint32_t& value) -> bool {
    if (query.starts_with("0x") || query.starts_with("0X")) query.remove_prefix(2);
    if (query.empty()) return false;
    const auto [end, error] = std::from_chars(query.data(), query.data() + query.size(), value, 16);
    return error == std::errc{} && end == query.data() + query.size();
}

auto kind_from_string(std::string_view value) -> std::optional<ExplainEntryKind> {
    if (value == "instruction") return ExplainEntryKind::Instruction;
    if (value == "profile") return ExplainEntryKind::Profile;
    if (value == "extension") return ExplainEntryKind::Extension;
    return std::nullopt;
}

auto kind_name(ExplainEntryKind kind) -> std::string_view {
    switch (kind) {
        case ExplainEntryKind::Instruction:
            return "instruction";
        case ExplainEntryKind::Profile:
            return "profile";
        case ExplainEntryKind::Extension:
            return "extension";
    }
    return "entry";
}

}  // namespace

auto find_explain_entry(std::string_view query, ExplainEntryKind kind)
    -> std::optional<ExplainEntry> {
    const std::string normalized = lower(query);
    if (kind == ExplainEntryKind::Instruction) {
        for (size_t i = 0; i < static_cast<size_t>(ILLEGAL); ++i) {
            const auto op = static_cast<OperationId>(i);
            if (auto entry = make_instruction_entry(op);
                entry && lower(entry->name) == normalized) {
                return entry;
            }
        }
    } else if (kind == ExplainEntryKind::Profile) {
        for (const auto& profile : simrv::isa::kArchitectureProfiles) {
            if (lower(profile.name) == normalized) return make_profile_entry(profile);
        }
    } else if (auto entry = extension_entry(query)) {
        return entry;
    }
    return std::nullopt;
}

auto search_explain_entries(std::string_view query) -> std::vector<ExplainEntry> {
    std::vector<ExplainEntry> matches;
    if (query.empty()) return matches;
    const auto add_if_match = [&](ExplainEntry entry) {
        if (contains_case_insensitive(entry.name, query) ||
            contains_case_insensitive(entry.summary, query)) {
            matches.push_back(std::move(entry));
        }
    };
    for (size_t i = 0; i < static_cast<size_t>(ILLEGAL); ++i) {
        if (auto entry = make_instruction_entry(static_cast<OperationId>(i))) {
            add_if_match(std::move(*entry));
        }
    }
    for (const auto& profile : simrv::isa::kArchitectureProfiles) {
        add_if_match(make_profile_entry(profile));
    }
    for (const auto name : simrv::isa::kMultiLetterExtensionNames) {
        if (auto entry = extension_entry(name)) add_if_match(std::move(*entry));
    }
    for (const auto letter : std::array{"A"sv, "B"sv, "C"sv, "D"sv, "E"sv, "F"sv, "H"sv, "I"sv,
                                        "M"sv, "Q"sv, "S"sv, "U"sv, "V"sv, "X"sv}) {
        if (auto entry = extension_entry(letter)) add_if_match(std::move(*entry));
    }
    const auto normalized_query = lower(query);
    std::stable_sort(matches.begin(), matches.end(), [&](const auto& lhs, const auto& rhs) {
        return (lower(lhs.name) == normalized_query) > (lower(rhs.name) == normalized_query);
    });
    return matches;
}

auto explain_query(std::string_view query, std::string_view query_type) -> bool {
    const auto requested_kind = query_type == "auto" ? std::nullopt : kind_from_string(query_type);
    if (query_type != "auto" && !requested_kind) {
        std::println("Unknown explain query type '{}'. Use instruction, profile, or extension.",
                     query_type);
        return false;
    }

    std::vector<ExplainEntry> candidates;
    if (requested_kind) {
        if (auto entry = find_explain_entry(query, *requested_kind)) {
            candidates.push_back(std::move(*entry));
        }
    } else {
        for (const auto kind : {ExplainEntryKind::Instruction, ExplainEntryKind::Profile,
                                ExplainEntryKind::Extension}) {
            if (auto entry = find_explain_entry(query, kind)) {
                candidates.push_back(std::move(*entry));
            }
        }
    }

    if (candidates.size() > 1) {
        std::println(
            "'{}' matches more than one ISA entry. Specify instruction, profile, or "
            "extension.",
            query);
        return false;
    }
    if (!candidates.empty()) {
        const auto& entry = candidates.front();
        std::println("=== SimRV ISA Explainer: {} {} ===", kind_name(entry.kind), entry.name);
        std::println("{}", entry.summary);
        std::println("{}", entry.description);
        std::println("Spec: {}", entry.spec_reference);
        std::println("URL:  {}", entry.spec_url);
        return true;
    }

    if (!requested_kind) {
        uint32_t instruction = 0;
        if (parse_instruction_word(query, instruction)) {
            explain_instruction(instruction);
            return true;
        }
    }

    std::println("No {} named '{}' is present in the SimRV catalogs.",
                 requested_kind ? kind_name(*requested_kind) : "ISA entry", query);
    std::println(
        "Try: simrv explain instruction ADD | profile RVA23S64 | extension Zba | 0x00000013");
    return false;
}

}  // namespace simrv::explain
