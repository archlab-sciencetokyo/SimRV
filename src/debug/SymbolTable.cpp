/**
 * @file SymbolTable.cpp
 * @brief ELF symbol table loader implementation.
 */
#include "simrv/debug/SymbolTable.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <map>
#include <utility>
#include <vector>

#include "simrv/core/Logger.hpp"

namespace simrv::debug {

namespace {

constexpr size_t kElfIdentSize = 16;
constexpr uint8_t kElfClass32 = 1;
constexpr uint8_t kElfClass64 = 2;
constexpr uint32_t kElfSymbolTable = 2;
constexpr uint8_t kElfSymbolNotype = 0;
constexpr uint8_t kElfSymbolObject = 1;
constexpr uint8_t kElfSymbolFunction = 2;
constexpr std::array<uint8_t, 4> kElfMagic = {0x7f, 'E', 'L', 'F'};

struct Elf32_Ehdr {
    uint8_t e_ident[kElfIdentSize];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint32_t e_entry;
    uint32_t e_phoff;
    uint32_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf64_Ehdr {
    uint8_t e_ident[kElfIdentSize];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
};

struct Elf32_Shdr {
    uint32_t sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size;
    uint32_t sh_link, sh_info, sh_addralign, sh_entsize;
};

struct Elf64_Shdr {
    uint32_t sh_name, sh_type;
    uint64_t sh_flags, sh_addr, sh_offset, sh_size;
    uint32_t sh_link, sh_info;
    uint64_t sh_addralign, sh_entsize;
};

struct Elf32_Sym {
    uint32_t st_name, st_value, st_size;
    uint8_t st_info, st_other;
    uint16_t st_shndx;
};

struct Elf64_Sym {
    uint32_t st_name;
    uint8_t st_info, st_other;
    uint16_t st_shndx;
    uint64_t st_value, st_size;
};

static_assert(sizeof(Elf32_Ehdr) == 52 && sizeof(Elf64_Ehdr) == 64);
static_assert(sizeof(Elf32_Shdr) == 40 && sizeof(Elf64_Shdr) == 64);
static_assert(sizeof(Elf32_Sym) == 16 && sizeof(Elf64_Sym) == 24);

template <typename Ehdr, typename Shdr, typename Sym, typename StTypeFunc>
auto parse_elf_symbols(std::ifstream& fs, std::map<simrv::xlen::Address, std::string>& out_symbols,
                       std::map<simrv::xlen::Address, ElfFunctionSymbol>& out_functions,
                       std::optional<simrv::xlen::Address>& out_entry, StTypeFunc get_type,
                       SymbolLoadMode mode) -> bool {
    Ehdr ehdr{};
    if (!fs.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr)))
        return false;  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    out_entry = static_cast<simrv::xlen::Address>(ehdr.e_entry);

    std::vector<Shdr> shdrs(ehdr.e_shnum);
    fs.seekg(static_cast<std::streamoff>(ehdr.e_shoff), std::ios::beg);
    if (!fs.read(reinterpret_cast<char*>(shdrs.data()),
                 static_cast<std::streamsize>(
                     static_cast<size_t>(ehdr.e_shnum) *
                     sizeof(Shdr)))) {  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
        return false;
    }

    int symtab_idx = -1;
    int strtab_idx = -1;
    for (int i = 0; std::cmp_less(i, ehdr.e_shnum); ++i) {
        if (shdrs[i].sh_type == kElfSymbolTable) {
            symtab_idx = i;
            strtab_idx = static_cast<int>(shdrs[i].sh_link);
        }
    }

    if (symtab_idx != -1 && strtab_idx != -1) {
        std::vector<char> strtab(shdrs[strtab_idx].sh_size);
        fs.seekg(static_cast<std::streamoff>(shdrs[strtab_idx].sh_offset), std::ios::beg);
        if (!fs.read(strtab.data(), static_cast<std::streamsize>(shdrs[strtab_idx].sh_size))) {
            return false;
        }

        size_t num_syms = shdrs[symtab_idx].sh_size / sizeof(Sym);
        fs.seekg(static_cast<std::streamoff>(shdrs[symtab_idx].sh_offset), std::ios::beg);
        for (size_t index = 0; index < num_syms; ++index) {
            Sym sym{};
            if (!fs.read(reinterpret_cast<char*>(&sym), sizeof(sym))) {
                return false;  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
            }
            const auto type = get_type(sym.st_info);
            if (sym.st_name != 0 && static_cast<size_t>(sym.st_name) < strtab.size() &&
                sym.st_value != 0 &&
                (type == kElfSymbolFunction || type == kElfSymbolObject ||
                 type == kElfSymbolNotype)) {
                std::string name = &strtab[sym.st_name];
                const bool runtime_essential =
                    name == "_start" || name == "tohost" || name == "begin_signature" ||
                    name == "end_signature" || name == "rvtest_sig_begin" ||
                    name == "rvtest_sig_end";
                if (!name.empty() && name.find('$') == std::string::npos &&
                    (mode == SymbolLoadMode::FullDebug || runtime_essential)) {
                    out_symbols[sym.st_value] = name;
                    if (type == kElfSymbolFunction) {
                        out_functions[sym.st_value] = ElfFunctionSymbol{
                            .name = std::move(name),
                            .size = static_cast<simrv::xlen::Address>(sym.st_size)};
                    }
                }
            }
        }
    }
    return true;
}

}  // namespace

auto SymbolTable::append_from_elf(const std::string& elf_path) -> bool {
    return load_from_elf(elf_path, false, SymbolLoadMode::FullDebug);
}

auto SymbolTable::load_from_elf(const std::string& elf_path, bool clear_existing,
                                SymbolLoadMode mode) -> bool {
    if (clear_existing) {
        symbols_.clear();
        functions_.clear();
        entry_point_.reset();
    }
    std::string path_to_load = elf_path;

    // Helper to check if file has ELF magic
    auto is_valid_elf = [](const std::string& path) -> bool {
        std::ifstream test_fs(path, std::ios::binary);
        if (!test_fs.is_open()) return false;
        std::array<char, 4> magic{};
        if (!test_fs.read(magic.data(), 4)) return false;
        return std::equal(magic.begin(), magic.end(), kElfMagic.begin());
    };

    if (!is_valid_elf(path_to_load)) {
        // Try to find a companion ELF file if elf_path is a raw binary
        bool found = false;
        if (path_to_load.find_last_of('.') != std::string::npos) {
            std::string base = path_to_load.substr(0, path_to_load.find_last_of('.'));
            for (const auto& ext : {".elf", "", ".axf"}) {
                std::string candidate = base + ext;
                if (is_valid_elf(candidate)) {
                    path_to_load = candidate;
                    found = true;
                    break;
                }
            }
        }
        if (!found) {
            return false;
        }
    }

    std::ifstream fs(path_to_load, std::ios::binary);
    if (!fs.is_open()) {
        return false;
    }

    // Read ELF identity
    std::array<uint8_t, kElfIdentSize> ident{};
    if (!fs.read(reinterpret_cast<char*>(ident.data()), ident.size())) {
        return false;
    }

    // Check magic
    if (!std::equal(kElfMagic.begin(), kElfMagic.end(), ident.begin())) {
        return false;
    }

    const auto elf_class = ident[4];
    if (elf_class != kElfClass32 && elf_class != kElfClass64) {
        return false;
    }

    fs.seekg(0, std::ios::beg);

    std::optional<simrv::xlen::Address> ep;
    if (elf_class == kElfClass32) {
        if (!parse_elf_symbols<Elf32_Ehdr, Elf32_Shdr, Elf32_Sym>(
                fs, symbols_, functions_, ep, [](auto info) { return info & 0x0fU; }, mode)) {
            return false;
        }
    } else {
        if (!parse_elf_symbols<Elf64_Ehdr, Elf64_Shdr, Elf64_Sym>(
                fs, symbols_, functions_, ep, [](auto info) { return info & 0x0fU; }, mode)) {
            return false;
        }
    }
    if (!entry_point_.has_value() && ep.has_value()) {
        entry_point_ = ep;
    }

    // Companion images are presentation/debug metadata and are intentionally TUI-only.
    std::filesystem::path loaded_p(path_to_load);
    if (mode == SymbolLoadMode::FullDebug && loaded_p.has_parent_path()) {
        auto dir = loaded_p.parent_path();
        for (const auto& fname : {"vmlinux", "vmlinux.elf", "kernel.elf"}) {
            auto kpath = (dir / fname).string();
            if (kpath != path_to_load && is_valid_elf(kpath)) {
                std::ifstream kfs(kpath, std::ios::binary);
                if (kfs.is_open()) {
                    std::array<uint8_t, kElfIdentSize> kident{};
                    if (kfs.read(reinterpret_cast<char*>(kident.data()), kident.size())) {
                        const auto k_class = kident[4];
                        kfs.seekg(0, std::ios::beg);
                        std::optional<simrv::xlen::Address> kep;
                        if (k_class == kElfClass32) {
                            (void)parse_elf_symbols<Elf32_Ehdr, Elf32_Shdr, Elf32_Sym>(
                                kfs, symbols_, functions_, kep,
                                [](auto info) { return info & 0x0fU; }, mode);
                        } else if (k_class == kElfClass64) {
                            (void)parse_elf_symbols<Elf64_Ehdr, Elf64_Shdr, Elf64_Sym>(
                                kfs, symbols_, functions_, kep,
                                [](auto info) { return info & 0x0fU; }, mode);
                        }
                    }
                }
            }
        }
    }

    if (!symbols_.empty()) {
        simrv::log::info("Loaded {} {} from ELF image: {}", symbols_.size(),
                         mode == SymbolLoadMode::FullDebug ? "debug symbols" : "runtime symbols",
                         path_to_load);
        return true;
    }
    return false;
}

auto SymbolTable::lookup_symbol(simrv::xlen::Address addr) const
    -> std::optional<SymbolLookupResult> {
    if (symbols_.empty()) {
        return std::nullopt;
    }

    auto it = symbols_.upper_bound(addr);
    if (it == symbols_.begin()) {
        return std::nullopt;
    }
    --it;

    const simrv::xlen::Address sym_addr = it->first;
    const simrv::xlen::Address offset = addr - sym_addr;
    if (offset >= 0x2000) {  // Limit offset to prevent matching distant symbols
        return std::nullopt;
    }

    return SymbolLookupResult{
        .name = it->second,
        .base_addr = sym_addr,
        .offset = offset,
    };
}

auto SymbolTable::lookup_function(simrv::xlen::Address addr) const
    -> std::optional<FunctionLookupResult> {
    if (functions_.empty()) return std::nullopt;
    auto it = functions_.upper_bound(addr);
    if (it == functions_.begin()) return std::nullopt;
    --it;
    const simrv::xlen::Address offset = addr - it->first;
    const auto next = std::next(it);
    const simrv::xlen::Address next_function_start = next == functions_.end() ? 0 : next->first;
    if (it->second.size != 0 && offset >= it->second.size) return std::nullopt;
    if (it->second.size == 0 && (next == functions_.end() || addr >= next_function_start) &&
        offset != 0)
        return std::nullopt;
    if (next != functions_.end() && addr >= next_function_start) return std::nullopt;
    return FunctionLookupResult{.name = it->second.name, .base_addr = it->first, .offset = offset};
}

auto SymbolTable::lookup(simrv::xlen::Address addr) const -> std::string {
    const auto res = lookup_symbol(addr);
    if (!res) {
        return "";
    }
    if (res->is_exact()) {
        return std::string(res->name);
    }
    return std::format("{} + 0x{:x}", res->name, res->offset);
}

auto SymbolTable::lookup_name(const std::string& name) const
    -> std::optional<simrv::xlen::Address> {
    for (const auto& [addr, sym_name] : symbols_) {
        if (sym_name == name) {
            return addr;
        }
    }
    return std::nullopt;
}

}  // namespace simrv::debug
