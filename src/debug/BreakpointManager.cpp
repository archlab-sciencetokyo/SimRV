/**
 * @file BreakpointManager.cpp
 * @brief Implementation of breakpoint and watchpoint management.
 */
#include "simrv/debug/BreakpointManager.hpp"
#include <format>
#include <vector>

namespace simrv::debug {

void BreakpointManager::add_pc_breakpoint(Address addr) {
    pc_breakpoints_.insert(addr);
}

void BreakpointManager::remove_pc_breakpoint(Address addr) {
    pc_breakpoints_.erase(addr);
}

void BreakpointManager::clear_pc_breakpoints() {
    pc_breakpoints_.clear();
}

auto BreakpointManager::has_pc_breakpoint(Address addr) const -> bool {
    return pc_breakpoints_.contains(addr);
}

void BreakpointManager::add_watchpoint(Address addr, size_t size, WatchType type, const std::string& label) {
    remove_watchpoint(addr);
    watchpoints_.push_back(Watchpoint{
        .addr = addr,
        .size = size,
        .type = type,
        .label = label
    });
}

void BreakpointManager::remove_watchpoint(Address addr) {
    std::erase_if(watchpoints_, [addr](const Watchpoint& wp) { return wp.addr == addr; });
}

void BreakpointManager::clear_watchpoints() {
    watchpoints_.clear();
}

auto BreakpointManager::check_pc(Address pc) const -> std::optional<BreakpointHit> {
    if (pc_breakpoints_.contains(pc)) {
        return BreakpointHit{
            .reason = BreakpointHit::Reason::Breakpoint,
            .addr = pc,
            .description = std::format("Breakpoint hit at 0x{:08x}", pc)
        };
    }
    return std::nullopt;
}

static auto ranges_overlap(Address a_start, size_t a_size, Address b_start, size_t b_size) -> bool {
    Address a_end = a_start + static_cast<Address>(a_size);
    Address b_end = b_start + static_cast<Address>(b_size);
    return a_start < b_end && b_start < a_end;
}

auto BreakpointManager::check_mem_write(Address paddr, size_t size) const -> std::optional<BreakpointHit> {
    for (const auto& wp : watchpoints_) {
        if ((wp.type == WatchType::Write || wp.type == WatchType::Access) &&
            ranges_overlap(paddr, size, wp.addr, wp.size)) {
            return BreakpointHit{
                .reason = BreakpointHit::Reason::Watchpoint,
                .addr = paddr,
                .description = std::format("Watchpoint (Write) hit at 0x{:08x} ({})", paddr, wp.label.empty() ? "write" : wp.label)
            };
        }
    }
    return std::nullopt;
}

auto BreakpointManager::check_mem_read(Address paddr, size_t size) const -> std::optional<BreakpointHit> {
    for (const auto& wp : watchpoints_) {
        if ((wp.type == WatchType::Read || wp.type == WatchType::Access) &&
            ranges_overlap(paddr, size, wp.addr, wp.size)) {
            return BreakpointHit{
                .reason = BreakpointHit::Reason::Watchpoint,
                .addr = paddr,
                .description = std::format("Watchpoint (Read) hit at 0x{:08x} ({})", paddr, wp.label.empty() ? "read" : wp.label)
            };
        }
    }
    return std::nullopt;
}

}  // namespace simrv::debug
