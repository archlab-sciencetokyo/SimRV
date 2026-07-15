#include "simrv/tui/RegisterPane.hpp"
#include "simrv/tui/TuiTheme.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/memory/Mmu.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include <format>
#include <optional>

namespace simrv::tui {

auto RegisterPane::translate_safe(const simrv::core::CPU& cpu, Register vaddr) const -> std::optional<Register> {
    const auto eff_priv = cpu.effective_data_privilege();
    const bool translation_enabled = (eff_priv != PrivilegeLevel::Machine &&
                                      simrv::xlen::satp_translation_enabled(cpu.state().satp));
    if (!translation_enabled) {
        return (cpu.state().regs.xlen == 32) ? (vaddr & 0xFFFFFFFFULL) : vaddr;
    }
    auto* mmu = const_cast<Mmu*>(machine_.memory_.mmu());
    auto res = mmu->translate(vaddr, PteAccess::Read, eff_priv,
                              cpu.state().mstatus, cpu.state().satp, cpu.state().regs.xlen);
    if (res.has_value()) {
        return res.value();
    }
    return std::nullopt;
}

auto RegisterPane::render_stack_frame(const simrv::core::CPU& cpu, int logical_row, int col_width, int right_width) -> std::string {
    int const width = col_width + right_width;
    
    Register sp = cpu.state().regs.read(RegId::Sp);
    unsigned int xlen = cpu.state().regs.xlen;
    int word_size = static_cast<int>(xlen) / 8;
    
    Register aligned_sp = sp & ~(static_cast<Register>(word_size) - 1);
    
    if (logical_row == 0) {
        return section_line("Live Guest Stack Watch (sp-aligned)", width);
    }
    if (logical_row == 11) {
        return section_line("Occupancy: sp (Mint) | scratch/free (Peach)", width);
    }
    if (logical_row > 11) {
        return format_to_width("", width);
    }
    
    int word_offset = logical_row - 5;
    Register target_vaddr = aligned_sp + static_cast<Register>(word_offset * word_size);
    if (xlen == 32) {
        target_vaddr &= 0xFFFFFFFFULL;
    }
    
    std::string addr_str = std::format("0x{:08x}", target_vaddr);
    if (xlen == 64) {
        addr_str = std::format("0x{:016x}", target_vaddr);
    }
    
    std::string offset_str = std::format("sp{:+d}", word_offset * word_size);
    if (word_offset == 0) {
        offset_str = "sp";
    }
    
    std::string val_str = "????";
    std::string dec_str = "";
    
    auto paddr_opt = translate_safe(cpu, target_vaddr);
    if (paddr_opt) {
        Register paddr = *paddr_opt;
        if (simrv::memory::is_dram_addr(paddr)) {
            if (xlen == 64) {
                uint64_t data = simrv::memory::ram_read_fast(paddr, static_cast<Instruction>(isa::Funct3::Sd), machine_.memory_.mmu()->mmem());
                val_str = std::format("0x{:016x}", data);
                dec_str = std::format("{}", static_cast<int64_t>(data));
            } else {
                uint32_t data = simrv::memory::ram_read_fast(paddr, static_cast<Instruction>(isa::Funct3::Sw), machine_.memory_.mmu()->mmem());
                val_str = std::format("0x{:08x}", data);
                dec_str = std::format("{}", static_cast<int32_t>(data));
            }
        } else {
            val_str = "device_mmio";
        }
    } else {
        val_str = "unmapped";
    }
    
    const char* label_color = kThemeText;
    const char* val_color = kThemeVal;
    if (word_offset == 0) {
        label_color = kThemeMint;
        val_color = kThemeMint;
    } else if (word_offset < 0) {
        label_color = kThemePeach;
    }
    
    std::string left_col = std::format("  \033[38;5;244m{}\033[0m  {}{:<8}\033[0m: {}{}\033[0m", addr_str, label_color, offset_str, val_color, val_str);
    std::string right_col = std::format(" {}{}\033[0m", kThemeText, dec_str);
    
    return format_to_width(left_col, col_width) + format_to_width(right_col, right_width);
}

} // namespace simrv::tui
