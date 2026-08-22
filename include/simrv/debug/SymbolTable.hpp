/**
 * @file SymbolTable.hpp
 * @brief ELF symbol table loading and address-to-symbol lookup for debug tracing.
 */
#pragma once

#include <map>
#include <optional>
#include <string>

#include "simrv/xlen/Types.hpp"

namespace simrv::debug {

enum class SymbolLoadMode : uint8_t { RuntimeEssentials, FullDebug };

class SymbolTable {
   public:
    SymbolTable() = default;

    /**
     * @brief Load symbols from a 32-bit or 64-bit ELF file and optional companion ELFs (e.g.
     * vmlinux).
     * @param elf_path Path to the ELF file or image directory.
     * @param clear_existing Whether to clear pre-existing symbols before loading.
     * @return true if symbols were successfully loaded, false otherwise.
     */
    auto load_from_elf(const std::string& elf_path, bool clear_existing = true,
                       SymbolLoadMode mode = SymbolLoadMode::FullDebug) -> bool;
    auto append_from_elf(const std::string& elf_path) -> bool;

    /**
     * @brief Look up a symbol name for a given address.
     * @param addr Memory address to lookup.
     * @return Formatted string "symbol_name" or "symbol_name+0xoffset", or empty string if not
     * found.
     */
    [[nodiscard]] auto lookup(Address addr) const -> std::string;
    [[nodiscard]] auto lookup_name(const std::string& name) const -> std::optional<Address>;
    [[nodiscard]] auto entry_point() const -> std::optional<Address> { return entry_point_; }

   private:
    std::map<Address, std::string> symbols_;
    std::optional<Address> entry_point_;
};

}  // namespace simrv::debug
