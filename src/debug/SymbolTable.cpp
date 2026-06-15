/**
 * @file SymbolTable.cpp
 * @brief ELF symbol table loader implementation.
 */
#include "simrv/debug/SymbolTable.hpp"

#include <elf.h>
#include <fstream>
#include <vector>
#include <format>
#include <algorithm>

#include "simrv/core/Logger.hpp"

namespace simrv::debug {

auto SymbolTable::load_from_elf(const std::string& elf_path) -> bool {
    symbols_.clear();
    std::string path_to_load = elf_path;

    // Helper to check if file has ELF magic
    auto is_valid_elf = [](const std::string& path) -> bool {
        std::ifstream test_fs(path, std::ios::binary);
        if (!test_fs.is_open()) return false;
        unsigned char magic[4];
        if (!test_fs.read(reinterpret_cast<char*>(magic), 4)) return false;
        return (magic[0] == ELFMAG0 && magic[1] == ELFMAG1 &&
                magic[2] == ELFMAG2 && magic[3] == ELFMAG3);
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
    unsigned char ident[EI_NIDENT];
    if (!fs.read(reinterpret_cast<char*>(ident), EI_NIDENT)) {
        return false;
    }

    // Check magic
    if (ident[EI_MAG0] != ELFMAG0 || ident[EI_MAG1] != ELFMAG1 ||
        ident[EI_MAG2] != ELFMAG2 || ident[EI_MAG3] != ELFMAG3) {
        return false;
    }

    const uint8_t elf_class = ident[EI_CLASS];
    if (elf_class != ELFCLASS32 && elf_class != ELFCLASS64) {
        return false;
    }

    fs.seekg(0, std::ios::beg);


    if (elf_class == ELFCLASS32) {
        Elf32_Ehdr ehdr;
        if (!fs.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr))) return false;

        std::vector<Elf32_Shdr> shdrs(ehdr.e_shnum);
        fs.seekg(ehdr.e_shoff, std::ios::beg);
        if (!fs.read(reinterpret_cast<char*>(shdrs.data()), ehdr.e_shnum * sizeof(Elf32_Shdr))) return false;

        int symtab_idx = -1;
        int strtab_idx = -1;
        for (int i = 0; i < ehdr.e_shnum; ++i) {
            if (shdrs[i].sh_type == SHT_SYMTAB) {
                symtab_idx = i;
                strtab_idx = static_cast<int>(shdrs[i].sh_link);
            }
        }

        if (symtab_idx != -1 && strtab_idx != -1) {
            std::vector<char> strtab(shdrs[strtab_idx].sh_size);
            fs.seekg(shdrs[strtab_idx].sh_offset, std::ios::beg);
            if (!fs.read(strtab.data(), static_cast<std::streamsize>(shdrs[strtab_idx].sh_size))) return false;

            size_t num_syms = shdrs[symtab_idx].sh_size / sizeof(Elf32_Sym);
            std::vector<Elf32_Sym> syms(num_syms);
            fs.seekg(shdrs[symtab_idx].sh_offset, std::ios::beg);
            if (!fs.read(reinterpret_cast<char*>(syms.data()), static_cast<std::streamsize>(shdrs[symtab_idx].sh_size))) return false;

            for (const auto& sym : syms) {
                if (sym.st_name != 0 && sym.st_value != 0 &&
                    (ELF32_ST_TYPE(sym.st_info) == STT_FUNC || ELF32_ST_TYPE(sym.st_info) == STT_NOTYPE)) {
                    std::string name = &strtab[sym.st_name];
                    if (!name.empty() && name.find('$') == std::string::npos) {
                        symbols_[sym.st_value] = name;
                    }
                }
            }
        }
    } else { // ELFCLASS64
        Elf64_Ehdr ehdr;
        if (!fs.read(reinterpret_cast<char*>(&ehdr), sizeof(ehdr))) return false;

        std::vector<Elf64_Shdr> shdrs(ehdr.e_shnum);
        fs.seekg(ehdr.e_shoff, std::ios::beg);
        if (!fs.read(reinterpret_cast<char*>(shdrs.data()), ehdr.e_shnum * sizeof(Elf64_Shdr))) return false;

        int symtab_idx = -1;
        int strtab_idx = -1;
        for (int i = 0; i < ehdr.e_shnum; ++i) {
            if (shdrs[i].sh_type == SHT_SYMTAB) {
                symtab_idx = i;
                strtab_idx = static_cast<int>(shdrs[i].sh_link);
            }
        }

        if (symtab_idx != -1 && strtab_idx != -1) {
            std::vector<char> strtab(shdrs[strtab_idx].sh_size);
            fs.seekg(shdrs[strtab_idx].sh_offset, std::ios::beg);
            if (!fs.read(strtab.data(), static_cast<std::streamsize>(shdrs[strtab_idx].sh_size))) return false;

            size_t num_syms = shdrs[symtab_idx].sh_size / sizeof(Elf64_Sym);
            std::vector<Elf64_Sym> syms(num_syms);
            fs.seekg(shdrs[symtab_idx].sh_offset, std::ios::beg);
            if (!fs.read(reinterpret_cast<char*>(syms.data()), static_cast<std::streamsize>(shdrs[symtab_idx].sh_size))) return false;

            for (const auto& sym : syms) {
                if (sym.st_name != 0 && sym.st_value != 0 &&
                    (ELF64_ST_TYPE(sym.st_info) == STT_FUNC || ELF64_ST_TYPE(sym.st_info) == STT_NOTYPE)) {
                    std::string name = &strtab[sym.st_name];
                    if (!name.empty() && name.find('$') == std::string::npos) {
                        symbols_[sym.st_value] = name;
                    }
                }
            }
        }
    }

    if (!symbols_.empty()) {
        simrv::log::info("[Debug] Loaded {} symbols from ELF image: {}", symbols_.size(), path_to_load);
        return true;
    }
    return false;
}

auto SymbolTable::lookup(Address addr) const -> std::string {
    if (symbols_.empty()) {
        return "";
    }

    auto it = symbols_.upper_bound(addr);
    if (it == symbols_.begin()) {
        return "";
    }
    --it;

    Address sym_addr = it->first;
    const std::string& name = it->second;

    Address offset = addr - sym_addr;
    if (offset == 0) {
        return name;
    } else if (offset < 0x2000) { // Limit offset to prevent matching distant symbols
        return std::format("{} + 0x{:x}", name, offset);
    }
    return "";
}

}  // namespace simrv::debug
