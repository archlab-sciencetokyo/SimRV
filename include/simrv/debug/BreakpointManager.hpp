/**
 * @file BreakpointManager.hpp
 * @brief Manages hardware/software breakpoints and memory/register watchpoints.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <map>
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

/// Frontend that owns a breakpoint or watchpoint.
enum class BreakpointOwner : uint8_t { Tui = 1, Gdb = 2 };

/// Logical instruction breakpoint owned by one debugger frontend.
struct PcBreakpoint {
    BreakpointId id = 0;
    BreakpointOwner owner = BreakpointOwner::Tui;
    Address addr = 0;
};

/// Target resource monitored by a watchpoint.
enum class WatchTarget : uint8_t { Memory, Register };

/// Architectural register file type monitored by a watchpoint.
enum class RegType : uint8_t { GPR, FPR, VEC, PC };

/// Descriptor for a memory address or register watchpoint.
struct Watchpoint {
    WatchpointId id = 0;
    BreakpointOwner owner = BreakpointOwner::Tui;
    WatchTarget target = WatchTarget::Memory;
    Address addr = 0;
    RegType reg_type = RegType::GPR;
    RegId reg_index = RegId::Zero;
    std::string reg_name;
    size_t size = 4;
    WatchType type = WatchType::Write;
    std::string label;
};

/// Details of a triggered breakpoint or watchpoint hit.
struct BreakpointHit {
    enum class Reason : uint8_t { Breakpoint, Watchpoint } reason;
    BreakpointId breakpoint_id = 0;
    Address addr = 0;
    std::string description;
    WatchType watch_type = WatchType::Write;
};

/// Parsed register specification resulting from string lookup.
struct ParsedReg {
    RegType type;
    RegId index;
    std::string canonical_name;
};

/// Parse human-readable register string (e.g. "x1", "ra", "fa0") into register metadata.
auto parse_register_name(std::string_view input) -> std::optional<ParsedReg>;

/// Manages PC breakpoints, memory watchpoints, and register watchpoints.
class BreakpointManager {
   public:
    BreakpointManager() = default;

    /// Add a PC breakpoint at the given virtual address
    void add_pc_breakpoint(Address addr, BreakpointOwner owner = BreakpointOwner::Tui);
    /// Remove a PC breakpoint at the given virtual address
    void remove_pc_breakpoint(Address addr, BreakpointOwner owner = BreakpointOwner::Tui);
    /// Clear all configured PC breakpoints
    void clear_pc_breakpoints();
    /// Check whether any PC breakpoint or watchpoint is active
    [[nodiscard]] auto has_any() const -> bool { return active_.load(std::memory_order_acquire); }
    /// Check if a PC breakpoint is set at the given address
    [[nodiscard]] auto has_pc_breakpoint(Address addr) const -> bool;
    /// Get the set of all active PC breakpoint addresses
    [[nodiscard]] auto get_pc_breakpoints() const -> const std::set<Address>& {
        return pc_breakpoints_;
    }
    /// Get stable-ID logical breakpoint records, including frontend ownership.
    [[nodiscard]] auto get_pc_breakpoint_records() const -> const std::vector<PcBreakpoint>& {
        return pc_breakpoint_records_;
    }

    /// Add a memory watchpoint at physical address with specified byte size and access type
    void add_watchpoint(Address addr, size_t size = 4, WatchType type = WatchType::Write,
                        const std::string& label = "",
                        BreakpointOwner owner = BreakpointOwner::Tui);
    /// Add a register watchpoint monitoring changes to a specific register
    void add_reg_watchpoint(RegType reg_type, RegId reg_index, const std::string& reg_name);
    /// Remove a memory watchpoint at physical address
    void remove_watchpoint(Address addr);
    /// Remove memory watchpoints at an address owned by one frontend.
    void remove_watchpoint(Address addr, BreakpointOwner owner);
    void remove_watchpoint(Address addr, size_t size, WatchType type, BreakpointOwner owner);
    /// Remove a watchpoint by index in the watchpoints list
    void remove_watchpoint_by_index(size_t index);
    /// Remove a register watchpoint for the given register type and index
    void remove_reg_watchpoint(RegType reg_type, RegId reg_index);
    /// Clear all active memory and register watchpoints
    void clear_watchpoints();
    /// Clear all breakpoint and watchpoint state owned by one frontend.
    void clear_owner(BreakpointOwner owner);
    /// Get list of configured watchpoints
    [[nodiscard]] auto get_watchpoints() const -> const std::vector<Watchpoint>& {
        return watchpoints_;
    }

    /// Test if PC matches an active breakpoint (respecting skip-once setting)
    [[nodiscard]] auto check_pc(Address pc, HartId hart = HartId{0}, Counter retired = 0) const
        -> std::optional<BreakpointHit>;
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
    void set_skip_once_pc(Address pc, HartId hart, Counter retired) const {
        skipped_instructions_[hart.raw()] = std::pair{pc, retired};
    }

   private:
    void update_active() {
        active_.store(!pc_breakpoints_.empty() || !watchpoints_.empty(), std::memory_order_release);
    }
    std::atomic<bool> active_{false};
    mutable std::map<uint32_t, std::pair<Address, Counter>> skipped_instructions_;
    std::set<Address> pc_breakpoints_;
    std::vector<PcBreakpoint> pc_breakpoint_records_;
    std::vector<Watchpoint> watchpoints_;
    BreakpointId next_breakpoint_id_ = 1;
    WatchpointId next_watchpoint_id_ = 1;
};

}  // namespace simrv::debug
