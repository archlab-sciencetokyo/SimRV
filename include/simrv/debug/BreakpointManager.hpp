/**
 * @file BreakpointManager.hpp
 * @brief Manages hardware/software breakpoints and memory/register watchpoints.
 */
#pragma once

#include <optional>
#include <set>
#include <string>
#include <vector>
#include "simrv/xlen/Types.hpp"

namespace simrv::debug {

enum class WatchType : uint8_t {
    Read,
    Write,
    Access
};

struct Watchpoint {
    Address addr = 0;
    size_t size = 4;
    WatchType type = WatchType::Write;
    std::string label;
};

struct BreakpointHit {
    enum class Reason : uint8_t { Breakpoint, Watchpoint } reason;
    Address addr = 0;
    std::string description;
};

class BreakpointManager {
   public:
    BreakpointManager() = default;

    void add_pc_breakpoint(Address addr);
    void remove_pc_breakpoint(Address addr);
    void clear_pc_breakpoints();
    [[nodiscard]] auto has_any() const -> bool { return active_; }
    [[nodiscard]] auto has_pc_breakpoint(Address addr) const -> bool;
    [[nodiscard]] auto get_pc_breakpoints() const -> const std::set<Address>& { return pc_breakpoints_; }

    void add_watchpoint(Address addr, size_t size = 4, WatchType type = WatchType::Write, const std::string& label = "");
    void remove_watchpoint(Address addr);
    void clear_watchpoints();
    [[nodiscard]] auto get_watchpoints() const -> const std::vector<Watchpoint>& { return watchpoints_; }

    [[nodiscard]] auto check_pc(Address pc) const -> std::optional<BreakpointHit>;
    [[nodiscard]] auto check_mem_write(Address paddr, size_t size) const -> std::optional<BreakpointHit>;
    [[nodiscard]] auto check_mem_read(Address paddr, size_t size) const -> std::optional<BreakpointHit>;

   private:
    void update_active() { active_ = !pc_breakpoints_.empty() || !watchpoints_.empty(); }
    bool active_ = false;
    std::set<Address> pc_breakpoints_;
    std::vector<Watchpoint> watchpoints_;
};

}  // namespace simrv::debug
