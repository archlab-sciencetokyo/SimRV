/**
 * @file BreakpointManager.hpp
 * @brief Manages hardware/software breakpoints and memory/register watchpoints.
 */
#pragma once

#include <cstdint>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/xlen/Types.hpp"

namespace simrv::core {
struct ArchState;
}

namespace simrv::debug {

/// Access type trigger condition for watchpoints.
enum class WatchType : uint8_t { Read, Write, Access };

/// Target resource monitored by a watchpoint.
enum class WatchTarget : uint8_t { Memory, Register };

/// Architectural register file type monitored by a watchpoint.
enum class RegType : uint8_t { GPR, FPR, VEC, PC };

/// Descriptor for a memory address or register watchpoint.
struct Watchpoint {
    WatchTarget target = WatchTarget::Memory;
    Address addr = 0;
    RegType reg_type = RegType::GPR;
    uint8_t reg_index = 0;
    std::string reg_name;
    size_t size = 4;
    WatchType type = WatchType::Write;
    std::string label;
};

/// Details of a triggered breakpoint or watchpoint hit.
struct BreakpointHit {
    enum class Reason : uint8_t { Breakpoint, Watchpoint } reason;
    Address addr = 0;
    std::string description;
};

/// Parsed register specification resulting from string lookup.
struct ParsedReg {
    RegType type;
    uint8_t index;
    std::string canonical_name;
};

/// Parse human-readable register string (e.g. "x1", "ra", "fa0") into register metadata.
auto parse_register_name(std::string_view input) -> std::optional<ParsedReg>;

/// Manages PC breakpoints, memory watchpoints, and register watchpoints.
class BreakpointManager {
   public:
    BreakpointManager() = default;

    /// Add a PC breakpoint at the given virtual address
    void add_pc_breakpoint(Address addr);
    /// Remove a PC breakpoint at the given virtual address
    void remove_pc_breakpoint(Address addr);
    /// Clear all configured PC breakpoints
    void clear_pc_breakpoints();
    /// Check whether any PC breakpoint or watchpoint is active
    [[nodiscard]] auto has_any() const -> bool { return active_; }
    /// Check if a PC breakpoint is set at the given address
    [[nodiscard]] auto has_pc_breakpoint(Address addr) const -> bool;
    /// Get the set of all active PC breakpoint addresses
    [[nodiscard]] auto get_pc_breakpoints() const -> const std::set<Address>& {
        return pc_breakpoints_;
    }

    /// Add a memory watchpoint at physical address with specified byte size and access type
    void add_watchpoint(Address addr, size_t size = 4, WatchType type = WatchType::Write,
                        const std::string& label = "");
    /// Add a register watchpoint monitoring changes to a specific register
    void add_reg_watchpoint(RegType reg_type, uint8_t reg_index, const std::string& reg_name);
    /// Remove a memory watchpoint at physical address
    void remove_watchpoint(Address addr);
    /// Remove a register watchpoint for the given register type and index
    void remove_reg_watchpoint(RegType reg_type, uint8_t reg_index);
    /// Clear all active memory and register watchpoints
    void clear_watchpoints();
    /// Get list of configured watchpoints
    [[nodiscard]] auto get_watchpoints() const -> const std::vector<Watchpoint>& {
        return watchpoints_;
    }

    /// Test if PC matches an active breakpoint (respecting skip-once setting)
    [[nodiscard]] auto check_pc(Address pc) const -> std::optional<BreakpointHit>;
    /// Check if a memory write access overlaps any active write/access watchpoints
    [[nodiscard]] auto check_mem_write(Address paddr, size_t size) const
        -> std::optional<BreakpointHit>;
    /// Check if a memory read access overlaps any active read/access watchpoints
    [[nodiscard]] auto check_mem_read(Address paddr, size_t size) const
        -> std::optional<BreakpointHit>;
    /// Compare current and previous architectural states to check for register watchpoint hits
    [[nodiscard]] auto check_reg_changes(const simrv::core::ArchState& state,
                                         const simrv::core::ArchState& prev_state) const
        -> std::optional<BreakpointHit>;

    /// Set single-step skip address to ignore breakpoint once
    void set_skip_once_pc(std::optional<Address> pc) const { skip_once_pc_ = pc; }
    /// Get currently set single-step skip address
    [[nodiscard]] auto get_skip_once_pc() const -> std::optional<Address> { return skip_once_pc_; }

   private:
    void update_active() { active_ = !pc_breakpoints_.empty() || !watchpoints_.empty(); }
    bool active_ = false;
    mutable std::optional<Address> skip_once_pc_;
    std::set<Address> pc_breakpoints_;
    std::vector<Watchpoint> watchpoints_;
};

}  // namespace simrv::debug
