/**
 * @file SymbolTable.cpp
 * @brief ELF symbol table loader implementation.
 */
#include "simrv/debug/SymbolTable.hpp"

#include <elf.h>

#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <utility>
#include <vector>

#include "simrv/core/Logger.hpp"

namespace simrv::debug {

namespace {

template <typename Ehdr, typename Shdr, typename Sym, typename StTypeFunc>
auto parse_elf_symbols(std::ifstream& fs, std::map<Address, std::string>& out_symbols,
                       std::optional<Address>& out_entry, StTypeFunc get_type, SymbolLoadMode mode)
    -> bool {
    Ehdr ehdr{};
    if (!fs.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr)))
        return false;  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
    out_entry = static_cast<Address>(ehdr.e_entry);

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
        if (shdrs[i].sh_type == SHT_SYMTAB) {
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
                (type == STT_FUNC || type == STT_OBJECT || type == STT_NOTYPE)) {
                std::string name = &strtab[sym.st_name];
                const bool runtime_essential = name == "_start" || name == "tohost";
                if (!name.empty() && name.find('$') == std::string::npos &&
                    (mode == SymbolLoadMode::FullDebug || runtime_essential)) {
                    out_symbols[sym.st_value] = name;
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
        entry_point_.reset();
    }
    std::string path_to_load = elf_path;

    // Helper to check if file has ELF magic
    auto is_valid_elf = [](const std::string& path) -> bool {
        std::ifstream test_fs(path, std::ios::binary);
        if (!test_fs.is_open()) return false;
        std::array<char, 4> magic{};
        if (!test_fs.read(magic.data(), 4)) return false;
        return (magic[0] == ELFMAG0 && magic[1] == ELFMAG1 && magic[2] == ELFMAG2 &&
                magic[3] == ELFMAG3);
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
    std::array<char, EI_NIDENT> ident{};
    if (!fs.read(ident.data(), EI_NIDENT)) {
        return false;
    }

    // Check magic
    if (ident[EI_MAG0] != ELFMAG0 || ident[EI_MAG1] != ELFMAG1 || ident[EI_MAG2] != ELFMAG2 ||
        ident[EI_MAG3] != ELFMAG3) {
        return false;
    }

    const auto elf_class = static_cast<uint8_t>(ident[EI_CLASS]);
    if (elf_class != ELFCLASS32 && elf_class != ELFCLASS64) {
        return false;
    }

    fs.seekg(0, std::ios::beg);

    std::optional<Address> ep;
    if (elf_class == ELFCLASS32) {
        if (!parse_elf_symbols<Elf32_Ehdr, Elf32_Shdr, Elf32_Sym>(
                fs, symbols_, ep, [](auto info) { return ELF32_ST_TYPE(info); }, mode)) {
            return false;
        }
    } else {
        if (!parse_elf_symbols<Elf64_Ehdr, Elf64_Shdr, Elf64_Sym>(
                fs, symbols_, ep, [](auto info) { return ELF64_ST_TYPE(info); }, mode)) {
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
                    std::array<char, EI_NIDENT> kident{};
                    if (kfs.read(kident.data(), EI_NIDENT)) {
                        const auto k_class = static_cast<uint8_t>(kident[EI_CLASS]);
                        kfs.seekg(0, std::ios::beg);
                        std::optional<Address> kep;
                        if (k_class == ELFCLASS32) {
                            (void)parse_elf_symbols<Elf32_Ehdr, Elf32_Shdr, Elf32_Sym>(
                                kfs, symbols_, kep, [](auto info) { return ELF32_ST_TYPE(info); },
                                mode);
                        } else if (k_class == ELFCLASS64) {
                            (void)parse_elf_symbols<Elf64_Ehdr, Elf64_Shdr, Elf64_Sym>(
                                kfs, symbols_, kep, [](auto info) { return ELF64_ST_TYPE(info); },
                                mode);
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

auto SymbolTable::lookup_symbol(Address addr) const -> std::optional<SymbolLookupResult> {
    if (symbols_.empty()) {
        return std::nullopt;
    }

    auto it = symbols_.upper_bound(addr);
    if (it == symbols_.begin()) {
        return std::nullopt;
    }
    --it;

    const Address sym_addr = it->first;
    const Address offset = addr - sym_addr;
    if (offset >= 0x2000) {  // Limit offset to prevent matching distant symbols
        return std::nullopt;
    }

    return SymbolLookupResult{
        .name = it->second,
        .base_addr = sym_addr,
        .offset = offset,
    };
}

auto SymbolTable::lookup(Address addr) const -> std::string {
    const auto res = lookup_symbol(addr);
    if (!res) {
        return "";
    }
    if (res->is_exact()) {
        return std::string(res->name);
    }
    return std::format("{} + 0x{:x}", res->name, res->offset);
}

auto SymbolTable::lookup_name(const std::string& name) const -> std::optional<Address> {
    for (const auto& [addr, sym_name] : symbols_) {
        if (sym_name == name) {
            return addr;
        }
    }
    return std::nullopt;
}

}  // namespace simrv::debug
