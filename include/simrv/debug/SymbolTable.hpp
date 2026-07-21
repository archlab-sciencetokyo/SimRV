/**
 * @file SymbolTable.hpp
 * @brief ELF symbol table loading and address-to-symbol lookup for debug tracing.
 */
#pragma once

#include <string>
#include <map>
#include <optional>
#include "simrv/xlen/Types.hpp"

namespace simrv::debug {

class SymbolTable {
   public:
    SymbolTable() = default;

    /**
     * @brief Load symbols from a 32-bit or 64-bit ELF file.
     * @param elf_path Path to the ELF file.
     * @return true if symbols were successfully loaded, false otherwise.
     */
    auto load_from_elf(const std::string& elf_path) -> bool;

    /**
     * @brief Look up a symbol name for a given address.
     * @param addr Memory address to lookup.
     * @return Formatted string "symbol_name" or "symbol_name+0xoffset", or empty string if not found.
     */
    [[nodiscard]] auto lookup(Address addr) const -> std::string;
    [[nodiscard]] auto lookup_name(const std::string& name) const -> std::optional<Address>;

   private:
    std::map<Address, std::string> symbols_;
};

}  // namespace simrv::debug
