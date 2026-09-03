/**
 * @file BreakpointManager.cpp
 * @brief Implementation of breakpoint and watchpoint management.
 */
#include "simrv/debug/BreakpointManager.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <format>
#include <vector>

#include "simrv/core/Cpu.hpp"

namespace simrv::debug {

namespace {

auto parse_gpr_reg(const std::string& str) -> std::optional<ParsedReg> {
    if ((str.starts_with('x') || str.starts_with('r')) && str.size() > 1 &&
        std::all_of(str.begin() + 1, str.end(), ::isdigit)) {
        int idx = std::stoi(str.substr(1));
        if (idx >= 0 && idx < 32) {
            return ParsedReg{.type = RegType::GPR,
                             .index = static_cast<RegId>(idx),
                             .canonical_name = std::format("x{}", idx)};
        }
    }
    static constexpr std::array<const char*, 32> kGprAbi = {
        "zero", "ra", "sp", "gp", "tp",  "t0",  "t1", "t2", "s0", "s1", "a0",
        "a1",   "a2", "a3", "a4", "a5",  "a6",  "a7", "s2", "s3", "s4", "s5",
        "s6",   "s7", "s8", "s9", "s10", "s11", "t3", "t4", "t5", "t6"};
    for (size_t i = 0; i < kGprAbi.size(); ++i) {
        if (str == kGprAbi[i]) {
            return ParsedReg{.type = RegType::GPR,
                             .index = static_cast<RegId>(i),
                             .canonical_name = std::format("x{} ({})", i, kGprAbi[i])};
        }
    }
    if (str == "fp") {
        return ParsedReg{.type = RegType::GPR, .index = RegId::S0, .canonical_name = "x8 (s0/fp)"};
    }
    return std::nullopt;
}

auto parse_fpr_reg(const std::string& str) -> std::optional<ParsedReg> {
    if (str.starts_with('f') && str.size() > 1 &&
        std::all_of(str.begin() + 1, str.end(), ::isdigit)) {
        int idx = std::stoi(str.substr(1));
        if (idx >= 0 && idx < 32) {
            return ParsedReg{.type = RegType::FPR,
                             .index = static_cast<RegId>(idx),
                             .canonical_name = std::format("f{}", idx)};
        }
    }
    static constexpr std::array<const char*, 32> kFprAbi = {
        "ft0", "ft1", "ft2", "ft3", "ft4",  "ft5",  "ft6", "ft7", "fs0",  "fs1", "fa0",
        "fa1", "fa2", "fa3", "fa4", "fa5",  "fa6",  "fa7", "fs2", "fs3",  "fs4", "fs5",
        "fs6", "fs7", "fs8", "fs9", "fs10", "fs11", "ft8", "ft9", "ft10", "ft11"};
    for (size_t i = 0; i < kFprAbi.size(); ++i) {
        if (str == kFprAbi[i]) {
            return ParsedReg{.type = RegType::FPR,
                             .index = static_cast<RegId>(i),
                             .canonical_name = std::format("f{} ({})", i, kFprAbi[i])};
        }
    }
    return std::nullopt;
}

auto parse_vec_reg(const std::string& str) -> std::optional<ParsedReg> {
    if (str.starts_with('v') && str.size() > 1 &&
        std::all_of(str.begin() + 1, str.end(), ::isdigit)) {
        int idx = std::stoi(str.substr(1));
        if (idx >= 0 && idx < 32) {
            return ParsedReg{.type = RegType::VEC,
                             .index = static_cast<RegId>(idx),
                             .canonical_name = std::format("v{}", idx)};
        }
    }
    return std::nullopt;
}

}  // namespace

auto parse_register_name(std::string_view input) -> std::optional<ParsedReg> {
    std::string str;
    for (char c : input) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            str += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    if (str.empty()) return std::nullopt;

    if (str == "pc") {
        return ParsedReg{.type = RegType::PC, .index = RegId::Zero, .canonical_name = "pc"};
    }

    if (auto gpr = parse_gpr_reg(str)) return gpr;
    if (auto fpr = parse_fpr_reg(str)) return fpr;
    if (auto vec = parse_vec_reg(str)) return vec;

    return std::nullopt;
}

void BreakpointManager::add_pc_breakpoint(Address addr) {
    pc_breakpoints_.insert(addr);
    update_active();
}

void BreakpointManager::remove_pc_breakpoint(Address addr) {
    pc_breakpoints_.erase(addr);
    update_active();
}

void BreakpointManager::clear_pc_breakpoints() {
    pc_breakpoints_.clear();
    update_active();
}

auto BreakpointManager::has_pc_breakpoint(Address addr) const -> bool {
    return pc_breakpoints_.contains(addr);
}

void BreakpointManager::add_watchpoint(Address addr, size_t size, WatchType type,
                                       const std::string& label) {
    remove_watchpoint(addr);
    watchpoints_.push_back(Watchpoint{.target = WatchTarget::Memory,
                                      .addr = addr,
                                      .reg_type = RegType::GPR,
                                      .reg_index = RegId::Zero,
                                      .reg_name = "",
                                      .size = size,
                                      .type = type,
                                      .label = label});
    update_active();
}

void BreakpointManager::add_reg_watchpoint(RegType reg_type, RegId reg_index,
                                           const std::string& reg_name) {
    remove_reg_watchpoint(reg_type, reg_index);
    watchpoints_.push_back(Watchpoint{.target = WatchTarget::Register,
                                      .addr = 0,
                                      .reg_type = reg_type,
                                      .reg_index = reg_index,
                                      .reg_name = reg_name,
                                      .size = 4,
                                      .type = WatchType::Write,
                                      .label = reg_name});
    update_active();
}

void BreakpointManager::remove_watchpoint(Address addr) {
    std::erase_if(watchpoints_, [addr](const Watchpoint& wp) -> bool {
        return wp.target == WatchTarget::Memory && wp.addr == addr;
    });
    update_active();
}

void BreakpointManager::remove_watchpoint_by_index(size_t index) {
    if (index < watchpoints_.size()) {
        watchpoints_.erase(watchpoints_.begin() + static_cast<std::ptrdiff_t>(index));
        update_active();
    }
}

void BreakpointManager::remove_reg_watchpoint(RegType reg_type, RegId reg_index) {
    std::erase_if(watchpoints_, [reg_type, reg_index](const Watchpoint& wp) -> bool {
        return wp.target == WatchTarget::Register && wp.reg_type == reg_type &&
               wp.reg_index == reg_index;
    });
    update_active();
}

void BreakpointManager::clear_watchpoints() {
    watchpoints_.clear();
    update_active();
}

auto BreakpointManager::check_pc(Address pc) const -> std::optional<BreakpointHit> {
    if (skip_once_pc_.has_value()) {
        Address skipped_pc = *skip_once_pc_;
        skip_once_pc_.reset();
        if (skipped_pc == pc) {
            return std::nullopt;
        }
    }
    if (pc_breakpoints_.contains(pc)) {
        return BreakpointHit{.reason = BreakpointHit::Reason::Breakpoint,
                             .addr = pc,
                             .description = std::format("Breakpoint hit at 0x{:08x}", pc)};
    }
    return std::nullopt;
}

static auto ranges_overlap(Address a_start, size_t a_size, Address b_start, size_t b_size) -> bool {
    Address a_end = a_start + static_cast<Address>(a_size);
    Address b_end = b_start + static_cast<Address>(b_size);
    return a_start < b_end && b_start < a_end;
}

auto BreakpointManager::check_mem_write(Address paddr, size_t size) const
    -> std::optional<BreakpointHit> {
    for (const auto& wp : watchpoints_) {
        if (wp.target != WatchTarget::Memory) continue;
        if ((wp.type == WatchType::Write || wp.type == WatchType::Access) &&
            ranges_overlap(paddr, size, wp.addr, wp.size)) {
            return BreakpointHit{
                .reason = BreakpointHit::Reason::Watchpoint,
                .addr = paddr,
                .description = std::format("Watchpoint (Write) hit at 0x{:08x} ({})", paddr,
                                           wp.label.empty() ? "write" : wp.label)};
        }
    }
    return std::nullopt;
}

auto BreakpointManager::check_mem_read(Address paddr, size_t size) const
    -> std::optional<BreakpointHit> {
    for (const auto& wp : watchpoints_) {
        if (wp.target != WatchTarget::Memory) continue;
        if ((wp.type == WatchType::Read || wp.type == WatchType::Access) &&
            ranges_overlap(paddr, size, wp.addr, wp.size)) {
            return BreakpointHit{
                .reason = BreakpointHit::Reason::Watchpoint,
                .addr = paddr,
                .description = std::format("Watchpoint (Read) hit at 0x{:08x} ({})", paddr,
                                           wp.label.empty() ? "read" : wp.label)};
        }
    }
    return std::nullopt;
}

auto BreakpointManager::check_reg_changes(const simrv::core::ArchState& state,
                                          const simrv::core::ArchState& prev_state) const
    -> std::optional<BreakpointHit> {
    for (const auto& wp : watchpoints_) {
        if (wp.target != WatchTarget::Register) continue;

        switch (wp.reg_type) {
            case RegType::GPR: {
                auto old_val = prev_state.regs.read(wp.reg_index);
                auto new_val = state.regs.read(wp.reg_index);
                if (old_val != new_val) {
                    return BreakpointHit{
                        .reason = BreakpointHit::Reason::Watchpoint,
                        .addr = static_cast<Address>(wp.reg_index),
                        .description =
                            std::format("Register Watchpoint hit: {} changed from 0x{:x} to 0x{:x}",
                                        wp.reg_name.empty()
                                            ? std::format("x{}", std::to_underlying(wp.reg_index))
                                            : wp.reg_name,
                                        old_val, new_val)};
                }
                break;
            }
            case RegType::FPR: {
                auto old_val = prev_state.regs.read_fp(wp.reg_index);
                auto new_val = state.regs.read_fp(wp.reg_index);
                if (old_val != new_val) {
                    return BreakpointHit{
                        .reason = BreakpointHit::Reason::Watchpoint,
                        .addr = static_cast<Address>(wp.reg_index),
                        .description = std::format(
                            "Register Watchpoint hit: {} changed from 0x{:016x} to 0x{:016x}",
                            wp.reg_name.empty()
                                ? std::format("f{}", std::to_underlying(wp.reg_index))
                                : wp.reg_name,
                            old_val, new_val)};
                }
                break;
            }
            case RegType::VEC: {
                const auto& old_v = prev_state.regs.read_vector(wp.reg_index);
                const auto& new_v = state.regs.read_vector(wp.reg_index);
                bool vec_changed = false;
                for (size_t i = 0; i < std::size(old_v.u64); ++i) {
                    if (old_v.u64[i] != new_v.u64[i]) {
                        vec_changed = true;
                        break;
                    }
                }
                if (vec_changed) {
                    return BreakpointHit{
                        .reason = BreakpointHit::Reason::Watchpoint,
                        .addr = static_cast<Address>(wp.reg_index),
                        .description =
                            std::format("Register Watchpoint hit: {} (Vector) value modified",
                                        wp.reg_name.empty()
                                            ? std::format("v{}", std::to_underlying(wp.reg_index))
                                            : wp.reg_name)};
                }
                break;
            }
            case RegType::PC: {
                if (prev_state.pc != state.pc) {
                    return BreakpointHit{
                        .reason = BreakpointHit::Reason::Watchpoint,
                        .addr = state.pc,
                        .description = std::format(
                            "Register Watchpoint hit: PC changed from 0x{:08x} to 0x{:08x}",
                            prev_state.pc, state.pc)};
                }
                break;
            }
            default:
                break;
        }
    }
    return std::nullopt;
}

}  // namespace simrv::debug
