/**
 * @file MemoryModal.cpp
 * @brief Interactive Full-Screen Hex Dump and Memory Inspector Modal implementation.
 */
#include "simrv/tui/modals/MemoryModal.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <format>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/debug/SymbolTable.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/RamView.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/tui/modals/ModalComponents.hpp"
#include "simrv/tui/panels/InspectorPane.hpp"

namespace simrv::tui::modals {

namespace {

auto translate_vaddr(const simrv::core::CPU& cpu, const simrv::core::Machine& machine,
                     simrv::xlen::Address vaddr) -> std::optional<simrv::xlen::Address> {
    const auto eff_priv = cpu.effective_data_privilege();
    const bool translation_enabled =
        (eff_priv != simrv::xlen::PrivilegeLevel::Machine &&
         simrv::xlen::satp_translation_enabled(cpu.state().satp, cpu.state().regs.xlen));
    if (!translation_enabled) {
        return (cpu.state().regs.xlen == 32) ? (vaddr & 0xFFFFFFFFULL) : vaddr;
    }
    auto* mmu = const_cast<Mmu*>(machine.memory().mmu());
    auto res =
        mmu->translate(vaddr, PteAccess::Read, eff_priv, cpu.state().mstatus, cpu.state().satp,
                       cpu.state().regs.xlen, /*update_access_bits=*/false);
    if (res.has_value()) {
        return res.value().raw();
    }
    return std::nullopt;
}

}  // namespace

void MemoryModal::open(simrv::xlen::Address initial_addr, simrv::core::Machine& machine,
                       InspectorPane* inspector_pane) {
    if (initial_addr == 0) {
        if (inspector_pane && inspector_pane->get_inspect_addr() != 0) {
            initial_addr = inspector_pane->get_inspect_addr();
        } else if (machine.memory_geometry().dram_size > 0) {
            initial_addr = machine.memory_geometry().dram_base;
        } else {
            initial_addr = 0x80000000ULL;
        }
    }
    base_address_ = initial_addr & ~0xFULL;
    cursor_byte_ = static_cast<int>(initial_addr & 0xFULL);
    cursor_row_ = 0;
    physical_mode_ = false;
    mode_ = InputMode::Normal;
}

void MemoryModal::move_cursor(int delta_byte, int delta_row,
                              const simrv::core::Machine& /*machine*/) {
    if (delta_byte != 0) {
        cursor_byte_ += delta_byte;
        while (cursor_byte_ < 0) {
            cursor_byte_ += 16;
            cursor_row_ -= 1;
        }
        while (cursor_byte_ >= 16) {
            cursor_byte_ -= 16;
            cursor_row_ += 1;
        }
    }
    if (delta_row != 0) {
        cursor_row_ += delta_row;
    }

    constexpr int kMaxRows = 10;
    while (cursor_row_ < 0) {
        if (base_address_ >= 16) {
            base_address_ -= 16;
        }
        cursor_row_ = 0;
    }
    while (cursor_row_ >= kMaxRows) {
        base_address_ += 16;
        cursor_row_ = kMaxRows - 1;
    }
}

void MemoryModal::scroll_page(int pages, const simrv::core::Machine& /*machine*/) {
    constexpr int kPageBytes = 16 * 10;
    if (pages < 0) {
        auto diff = static_cast<simrv::xlen::Address>(-pages * kPageBytes);
        base_address_ = (base_address_ > diff) ? (base_address_ - diff) : 0;
    } else {
        base_address_ += static_cast<simrv::xlen::Address>(pages * kPageBytes);
    }
}

void MemoryModal::toggle_addressing_mode() { physical_mode_ = !physical_mode_; }

void MemoryModal::set_input_mode(InputMode mode) { mode_ = mode; }

auto MemoryModal::input_mode() -> MemoryModal::InputMode { return mode_; }

auto MemoryModal::current_address() -> simrv::xlen::Address {
    return base_address_ + static_cast<simrv::xlen::Address>(cursor_row_ * 16 + cursor_byte_);
}

auto MemoryModal::submit_input(
    const std::string& input, simrv::core::Machine& machine, InspectorPane* inspector_pane,
    const std::function<void(const std::string&)>& set_status_override_cb) -> bool {
    if (mode_ == InputMode::GotoAddress) {
        if (input.empty()) {
            mode_ = InputMode::Normal;
            return false;
        }
        simrv::xlen::Address addr = 0;
        bool ok = false;
        if (input == "sp" || input == "SP") {
            addr = machine.primary_hart().state().regs.read(simrv::xlen::RegId::Sp);
            ok = true;
        } else if (input == "pc" || input == "PC") {
            addr = machine.primary_hart().state().pc;
            ok = true;
        } else if (input.starts_with("0x") || input.starts_with("0X")) {
            auto result = std::from_chars(input.data() + 2, input.data() + input.size(), addr, 16);
            ok = (result.ec == std::errc{});
        } else if (std::all_of(input.begin(), input.end(), ::isxdigit)) {
            auto result = std::from_chars(input.data(), input.data() + input.size(), addr, 16);
            ok = (result.ec == std::errc{});
        } else {
            auto sym_opt = machine.symbol_table().lookup_name(input);
            if (sym_opt.has_value()) {
                addr = *sym_opt;
                ok = true;
            }
        }
        if (ok) {
            base_address_ = addr & ~0xFULL;
            cursor_byte_ = static_cast<int>(addr & 0xFULL);
            cursor_row_ = 0;
            if (inspector_pane) {
                inspector_pane->set_inspect_addr(addr);
            }
            set_status_override_cb(std::format("Memory Inspector jumped to 0x{:08x}", addr));
            mode_ = InputMode::Normal;
            return true;
        }
        set_status_override_cb(std::format("Symbol/address not found: {}", input));
        mode_ = InputMode::Normal;
        return false;
    }

    if (mode_ == InputMode::EditByte) {
        if (input.empty()) {
            mode_ = InputMode::Normal;
            return false;
        }
        uint32_t byte_val = 0;
        std::string_view val_str = input;
        if (val_str.starts_with("0x") || val_str.starts_with("0X")) {
            val_str.remove_prefix(2);
        }
        auto result =
            std::from_chars(val_str.data(), val_str.data() + val_str.size(), byte_val, 16);
        if (result.ec != std::errc{} || byte_val > 0xFF) {
            set_status_override_cb("Invalid byte value (must be 00-FF hex)");
            mode_ = InputMode::Normal;
            return false;
        }

        const auto target_addr = current_address();
        std::optional<simrv::xlen::Address> paddr_opt;
        if (physical_mode_) {
            paddr_opt = target_addr;
        } else {
            paddr_opt = translate_vaddr(machine.primary_hart(), machine, target_addr);
        }

        if (!paddr_opt || !machine.memory_geometry().contains(*paddr_opt)) {
            set_status_override_cb(
                std::format("Cannot write: 0x{:08x} unmapped or not in DRAM", target_addr));
            mode_ = InputMode::Normal;
            return false;
        }

        auto* ptr = machine.ram_view().try_get_ptr(*paddr_opt, 1);
        if (ptr != nullptr) {
            *ptr = static_cast<std::byte>(byte_val);
            set_status_override_cb(
                std::format("Wrote byte 0x{:02x} to 0x{:08x}", byte_val, target_addr));
            mode_ = InputMode::Normal;
            return true;
        }

        set_status_override_cb("Failed to access memory pointer");
        mode_ = InputMode::Normal;
        return false;
    }

    return true;
}

void MemoryModal::render([[maybe_unused]] std::vector<std::string>& content_rows,
                         const std::function<void(const std::string&)>& add_row,
                         const std::string& input, simrv::core::Machine& machine,
                         InspectorPane* inspector_pane, [[maybe_unused]] int term_height,
                         [[maybe_unused]] int term_width) {
    const auto& cpu = inspector_pane ? inspector_pane->current_cpu() : machine.primary_hart();
    const unsigned int xlen = cpu.state().regs.xlen;

    const std::string mode_tag = physical_mode_ ? std::format("\033[1;33m[PHYSICAL DRAM]\033[0m")
                                                : std::format("\033[1;36m[VIRTUAL MMU]\033[0m");

    const auto target_addr = current_address();
    std::string sym = machine.symbol_table().lookup(target_addr);
    std::string sym_str = sym.empty() ? "" : std::format(" <{}>", sym);

    std::string title_line =
        (xlen == 64)
            ? std::format(" {} Focus: \033[1m0x{:016x}\033[0m{}", mode_tag, target_addr, sym_str)
            : std::format(" {} Focus: \033[1m0x{:08x}\033[0m{}", mode_tag,
                          static_cast<uint32_t>(target_addr), sym_str);
    add_row(title_line);
    add_row("");

    // Render hex dump table
    constexpr int kRows = 10;
    for (int r = 0; r < kRows; ++r) {
        simrv::xlen::Address row_vaddr = base_address_ + static_cast<simrv::xlen::Address>(r * 16);
        std::optional<simrv::xlen::Address> paddr_opt;
        if (physical_mode_) {
            paddr_opt = row_vaddr;
        } else {
            paddr_opt = translate_vaddr(cpu, machine, row_vaddr);
        }

        std::string addr_prefix =
            (xlen == 64) ? std::format(" 0x{:010x}:  ", row_vaddr)
                         : std::format(" 0x{:08x}:  ", static_cast<uint32_t>(row_vaddr));

        std::string hex_part;
        std::string ascii_part;

        if (paddr_opt.has_value() && machine.memory_geometry().contains(*paddr_opt)) {
            auto* dram_ptr = machine.ram_view().try_get_ptr(*paddr_opt, 16);
            std::array<uint8_t, 16> row_bytes{};
            if (dram_ptr != nullptr) {
                std::memcpy(row_bytes.data(), dram_ptr, 16);
            }

            for (int b = 0; b < 16; ++b) {
                if (b == 8) hex_part += " ";
                const uint8_t byte_val = row_bytes[static_cast<size_t>(b)];
                const bool is_cursor = (r == cursor_row_ && b == cursor_byte_);

                if (is_cursor) {
                    hex_part += std::format("\033[1;7;32m{:02x}\033[0m ", byte_val);
                } else {
                    hex_part += std::format("{:02x} ", byte_val);
                }

                const char c = static_cast<char>(byte_val);
                if (is_cursor) {
                    ascii_part +=
                        std::format("\033[1;7;32m{}\033[0m", (c >= 32 && c <= 126) ? c : '.');
                } else {
                    ascii_part += (c >= 32 && c <= 126) ? c : '.';
                }
            }
        } else {
            hex_part = "?? ?? ?? ?? ?? ?? ?? ??  ?? ?? ?? ?? ?? ?? ?? ?? ";
            ascii_part = "????????????????";
        }

        add_row(std::format("{}{} │{}│", addr_prefix, hex_part, ascii_part));
    }

    add_row("");

    // Multi-format word interpretation of focused address
    std::optional<simrv::xlen::Address> cur_paddr;
    if (physical_mode_) {
        cur_paddr = target_addr;
    } else {
        cur_paddr = translate_vaddr(cpu, machine, target_addr);
    }

    uint64_t val64 = 0;
    if (cur_paddr && machine.memory_geometry().contains(*cur_paddr)) {
        auto* ptr = machine.ram_view().try_get_ptr(*cur_paddr, 8);
        if (ptr != nullptr) {
            std::memcpy(&val64, ptr, 8);
        }
    }

    uint8_t u8_val = static_cast<uint8_t>(val64 & 0xFF);
    int8_t s8_val = static_cast<int8_t>(u8_val);
    uint16_t u16_val = static_cast<uint16_t>(val64 & 0xFFFF);
    int16_t s16_val = static_cast<int16_t>(u16_val);
    uint32_t u32_val = static_cast<uint32_t>(val64 & 0xFFFFFFFF);
    int32_t s32_val = static_cast<int32_t>(u32_val);

    float f32_val = 0.0f;
    std::memcpy(&f32_val, &u32_val, 4);
    double f64_val = 0.0;
    std::memcpy(&f64_val, &val64, 8);

    add_row(std::format(
        " \033[1mByte:\033[0m 0x{:02x} ({:>3}, {:>4d})  │ \033[1mWord16:\033[0m 0x{:04x} "
        "({:>5}, {:>6d})",
        u8_val, u8_val, s8_val, u16_val, u16_val, s16_val));
    add_row(std::format(" \033[1mWord32:\033[0m 0x{:08x} ({:>10}, {:>11d})  float: {:<12.6g}",
                        u32_val, u32_val, s32_val, f32_val));
    add_row(std::format(" \033[1mWord64:\033[0m 0x{:016x}  double: {:<16.8g}", val64, f64_val));

    add_row("");

    // Bottom prompt or action footer
    if (mode_ == InputMode::GotoAddress) {
        add_row(std::format(" \033[1;36mGoto Address/Symbol:\033[0m {}█", input));
        add_row(build_modal_footer(
            {{.key = "[Enter]", .action = "Jump"}, {.key = "[Esc]", .action = "Cancel"}}));
    } else if (mode_ == InputMode::EditByte) {
        add_row(std::format(" \033[1;33mEdit Byte at 0x{:08x} (Hex 00-FF):\033[0m {}█", target_addr,
                            input));
        add_row(build_modal_footer(
            {{.key = "[Enter]", .action = "Write"}, {.key = "[Esc]", .action = "Cancel"}}));
    } else {
        add_row(build_modal_footer({{.key = "[Arrows]", .action = "Navigate"},
                                    {.key = "[PgUp/PgDn]", .action = "Page"},
                                    {.key = "[g]", .action = "Goto"},
                                    {.key = "[v]", .action = "Virt/Phys"},
                                    {.key = "[e]", .action = "Edit"},
                                    {.key = "[Esc/q]", .action = "Close"}}));
    }
}

}  // namespace simrv::tui::modals
