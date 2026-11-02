/**
 * @file ElfTypes.hpp
 * @brief Minimal host-independent ELF32/ELF64 records used by the image loader.
 */
#pragma once

#include <cstdint>

namespace simrv::util::elf {

inline constexpr uint8_t kClass32 = 1;
inline constexpr uint8_t kClass64 = 2;
inline constexpr uint32_t kLoadSegment = 1;
inline constexpr uint32_t kSymbolTable = 2;
inline constexpr uint8_t kSymbolNotype = 0;
inline constexpr uint8_t kSymbolObject = 1;
inline constexpr uint8_t kSymbolFunction = 2;

struct Elf32_Ehdr {
    uint8_t e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version, e_entry, e_phoff, e_shoff, e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
};

struct Elf64_Ehdr {
    uint8_t e_ident[16];
    uint16_t e_type, e_machine;
    uint32_t e_version;
    uint64_t e_entry, e_phoff, e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize, e_phentsize, e_phnum, e_shentsize, e_shnum, e_shstrndx;
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

struct Elf32_Phdr {
    uint32_t p_type, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_flags, p_align;
};

struct Elf64_Phdr {
    uint32_t p_type, p_flags;
    uint64_t p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align;
};

static_assert(sizeof(Elf32_Ehdr) == 52 && sizeof(Elf64_Ehdr) == 64);
static_assert(sizeof(Elf32_Shdr) == 40 && sizeof(Elf64_Shdr) == 64);
static_assert(sizeof(Elf32_Sym) == 16 && sizeof(Elf64_Sym) == 24);
static_assert(sizeof(Elf32_Phdr) == 32 && sizeof(Elf64_Phdr) == 56);

}  // namespace simrv::util::elf
