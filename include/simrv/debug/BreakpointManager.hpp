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

enum class WatchType : uint8_t { Read, Write, Access };

enum class WatchTarget : uint8_t { Memory, Register };

enum class RegType : uint8_t { GPR, FPR, VEC, PC };

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

struct BreakpointHit {
    enum class Reason : uint8_t { Breakpoint, Watchpoint } reason;
    Address addr = 0;
    std::string description;
};

struct ParsedReg {
    RegType type;
    uint8_t index;
    std::string canonical_name;
};

auto parse_register_name(std::string_view input) -> std::optional<ParsedReg>;

class BreakpointManager {
   public:
    BreakpointManager() = default;

    void add_pc_breakpoint(Address addr);
    void remove_pc_breakpoint(Address addr);
    void clear_pc_breakpoints();
    [[nodiscard]] auto has_any() const -> bool { return active_; }
    [[nodiscard]] auto has_pc_breakpoint(Address addr) const -> bool;
    [[nodiscard]] auto get_pc_breakpoints() const -> const std::set<Address>& {
        return pc_breakpoints_;
    }

    void add_watchpoint(Address addr, size_t size = 4, WatchType type = WatchType::Write,
                        const std::string& label = "");
    void add_reg_watchpoint(RegType reg_type, uint8_t reg_index, const std::string& reg_name);
    void remove_watchpoint(Address addr);
    void remove_reg_watchpoint(RegType reg_type, uint8_t reg_index);
    void clear_watchpoints();
    [[nodiscard]] auto get_watchpoints() const -> const std::vector<Watchpoint>& {
        return watchpoints_;
    }

    [[nodiscard]] auto check_pc(Address pc) const -> std::optional<BreakpointHit>;
    [[nodiscard]] auto check_mem_write(Address paddr, size_t size) const
        -> std::optional<BreakpointHit>;
    [[nodiscard]] auto check_mem_read(Address paddr, size_t size) const
        -> std::optional<BreakpointHit>;
    [[nodiscard]] auto check_reg_changes(const simrv::core::ArchState& state,
                                         const simrv::core::ArchState& prev_state) const
        -> std::optional<BreakpointHit>;

    void set_skip_once_pc(std::optional<Address> pc) const { skip_once_pc_ = pc; }
    [[nodiscard]] auto get_skip_once_pc() const -> std::optional<Address> { return skip_once_pc_; }

   private:
    void update_active() { active_ = !pc_breakpoints_.empty() || !watchpoints_.empty(); }
    bool active_ = false;
    mutable std::optional<Address> skip_once_pc_;
    std::set<Address> pc_breakpoints_;
    std::vector<Watchpoint> watchpoints_;
};

}  // namespace simrv::debug
