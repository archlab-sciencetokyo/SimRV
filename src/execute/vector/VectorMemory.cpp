#include <cstring>

#include "VectorHelpers.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/Pmp.hpp"
#include "simrv/execute/ExecuteUnit.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/memory/MemorySubsystem.hpp"

namespace simrv::execute {

namespace {

[[nodiscard]] inline auto is_tohost_addr_range(core::CPU& cpu, Address paddr,
                                               uint32_t size) noexcept -> bool {
    const Address th = (cpu.machine_ != nullptr) ? cpu.machine_->isa_test_tohost() : 0;
    return (th != 0 && paddr < th + 8 && paddr + size > th) ||
           (paddr < 0x80001008ULL && paddr + size > 0x80001000ULL) ||
           (paddr < 0x40008008ULL && paddr + size > 0x40008000ULL);
}

[[nodiscard]] auto get_direct_host_memory_for_range(core::CPU& cpu, Address base_addr,
                                                    uint32_t total_bytes, bool is_write) -> Byte* {
    if (total_bytes == 0 || cpu.machine_ == nullptr) return nullptr;
    auto& machine = *cpu.machine_;
    if (simrv::compiler::unlikely(machine.breakpoint_manager().has_any())) {
        return nullptr;
    }
    const bool single_page = ((base_addr >> 12) == ((base_addr + total_bytes - 1) >> 12));
    if (!single_page) return nullptr;

    const bool m_mode_flat = (cpu.state().priv == kPrivMachine &&
                              (cpu.state().mstatus & enum_mask(core::MstatusBit::Mprv)) == 0);
    if (m_mode_flat) {
        if (simrv::compiler::likely(machine.memory_geometry().contains(base_addr, total_bytes) &&
                                    machine.ram_view().contains(base_addr, total_bytes))) {
            const auto acc_type = is_write ? core::PmpAccessType::Write : core::PmpAccessType::Read;
            if (simrv::compiler::likely(core::pmp::check_access(cpu.state(), base_addr, total_bytes,
                                                                acc_type, kPrivMachine))) {
                if (is_write &&
                    simrv::compiler::unlikely(is_tohost_addr_range(cpu, base_addr, total_bytes))) {
                    return nullptr;
                }
                return machine.ram_view().unchecked_ptr(base_addr);
            }
        }
        return nullptr;
    }

    const unsigned active_xlen = cpu.effective_data_xlen();
    Address lookup_addr = base_addr;
    if (active_xlen == 32) lookup_addr &= 0xFFFFFFFFULL;
    const unsigned page_offset = lookup_addr & 0xFFFu;
    if (page_offset + total_bytes > 4096u) return nullptr;

    if (simrv::compiler::likely(
            simrv::xlen::satp_translation_enabled(cpu.state().satp, active_xlen))) {
        const PrivilegeLevel eff_priv = cpu.effective_data_privilege();
        const Word current_asid = simrv::xlen::satp_asid(cpu.state().satp, active_xlen);
        const Address vpn = lookup_addr >> 12;
        const size_t tlb_idx = core::CPU::soft_tlb_index(vpn);
        if (is_write) {
            const auto& entry = cpu.soft_tlb_write[tlb_idx];
            if (simrv::compiler::likely(
                    entry.matches(vpn, current_asid, eff_priv, cpu.soft_tlb_epoch))) {
                if (simrv::compiler::likely(entry.host_ptr_base != nullptr)) {
                    Address paddr = entry.paddr_base + page_offset;
                    if (simrv::compiler::unlikely(is_tohost_addr_range(cpu, paddr, total_bytes))) {
                        return nullptr;
                    }
                    return entry.host_ptr_base + page_offset;
                }
            }
        } else {
            const auto& entry = cpu.soft_tlb_read[tlb_idx];
            if (simrv::compiler::likely(
                    entry.matches(vpn, current_asid, eff_priv, cpu.soft_tlb_epoch))) {
                if (simrv::compiler::likely(entry.host_ptr_base != nullptr)) {
                    return entry.host_ptr_base + page_offset;
                }
            }
        }
    } else {
        if (simrv::compiler::likely(machine.memory_geometry().contains(lookup_addr, total_bytes) &&
                                    machine.ram_view().contains(lookup_addr, total_bytes))) {
            const auto acc_type = is_write ? core::PmpAccessType::Write : core::PmpAccessType::Read;
            if (simrv::compiler::likely(core::pmp::check_access(cpu.state(), lookup_addr,
                                                                total_bytes, acc_type,
                                                                cpu.effective_data_privilege()))) {
                if (is_write && simrv::compiler::unlikely(
                                    is_tohost_addr_range(cpu, lookup_addr, total_bytes))) {
                    return nullptr;
                }
                return machine.ram_view().unchecked_ptr(lookup_addr);
            }
        }
    }

    return nullptr;
}

// Whole vector load helper
void execute_vl_whole(core::CPU& cpu, simrv::memory::MemorySubsystem& mem, RegId rd,
                      Register base_addr, uint32_t nr, uint32_t element_bytes) {
    const uint32_t vlen_bytes = cpu.state().regs.vlen_bytes();
    const uint32_t total_bytes = nr * vlen_bytes;
    if (cpu.state().vstart == 0 && total_bytes > 0) {
        if (element_bytes == 8 && !simrv::xlen::kIsXLen64) {
            if ((base_addr & 7) != 0) {
                cpu.active_context().pending_exception = ExceptionCode::MisalignedLoad;
                cpu.active_context().pending_tval = base_addr;
                cpu.state().vstart = 0;
                return;
            }
        }
        Byte* host_src = get_direct_host_memory_for_range(cpu, base_addr, total_bytes, false);
        if (host_src != nullptr) {
            for (uint32_t r = 0; r < nr; ++r) {
                const auto reg_idx = static_cast<RegId>((static_cast<uint32_t>(rd) + r) % 32);
                auto& vreg = cpu.state().regs.read_vector(reg_idx);
                std::memcpy(vreg.u8.data(), host_src + r * vlen_bytes, vlen_bytes);
            }
            return;
        }
    }

    const uint32_t elements = nr * vlen_bytes / element_bytes;
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < elements; ++i) {
        const Address addr = base_addr + i * element_bytes;
        uint64_t val = 0;
        if (element_bytes == 8 && !simrv::xlen::kIsXLen64) {
            if ((addr & 7) != 0) {
                cpu.active_context().pending_exception = ExceptionCode::MisalignedLoad;
                cpu.active_context().pending_tval = addr;
                cpu.state().vstart = i;
                return;
            }
            const Word lo = simrv::memory::MemoryAccess::loadInt(mem, cpu, addr, isa::Funct3::Lw);
            if (cpu.active_context().pending_exception.has_value()) {
                cpu.state().vstart = i;
                return;
            }
            const Word hi =
                simrv::memory::MemoryAccess::loadInt(mem, cpu, addr + 4, isa::Funct3::Lw);
            val = static_cast<uint32_t>(lo) | (static_cast<uint64_t>(hi) << 32);
        } else {
            const isa::Funct3 width = element_bytes == 1   ? isa::Funct3::Lbu
                                      : element_bytes == 2 ? isa::Funct3::Lhu
                                      : element_bytes == 4 ? isa::Funct3::Lw
                                                           : isa::Funct3::Ld;
            val = simrv::memory::MemoryAccess::loadInt(mem, cpu, addr, width);
        }
        if (cpu.active_context().pending_exception.has_value()) {
            cpu.state().vstart = i;
            return;
        }
        for (uint32_t byte = 0; byte < element_bytes; ++byte) {
            const uint32_t offset = i * element_bytes + byte;
            const auto reg_idx =
                static_cast<RegId>(static_cast<uint32_t>(rd) + offset / vlen_bytes);
            cpu.state().regs.read_vector(reg_idx).u8[offset % vlen_bytes] =
                static_cast<uint8_t>(val >> (byte * 8));
        }
    }
}

// Whole vector store helper
void execute_vs_whole(core::CPU& cpu, simrv::memory::MemorySubsystem& mem, RegId vs3,
                      Register base_addr, uint32_t nr) {
    const uint32_t vlen_bytes = cpu.state().regs.vlen_bytes();
    const uint32_t total_bytes = nr * vlen_bytes;
    if (cpu.state().vstart == 0 && total_bytes > 0) {
        Byte* host_dst = get_direct_host_memory_for_range(cpu, base_addr, total_bytes, true);
        if (host_dst != nullptr) {
            for (uint32_t r = 0; r < nr; ++r) {
                const auto reg_idx = static_cast<RegId>((static_cast<uint32_t>(vs3) + r) % 32);
                const auto& vreg = cpu.state().regs.read_vector(reg_idx);
                std::memcpy(host_dst + r * vlen_bytes, vreg.u8.data(), vlen_bytes);
            }
            if (simrv::compiler::unlikely(cpu.machine_ != nullptr &&
                                          cpu.machine_->num_harts() > 1 &&
                                          mem.reservation_table().may_have_reservations())) {
                mem.reservation_table().invalidate_matching(
                    base_addr, static_cast<HartId>(cpu.state().mhartid));
            }
            return;
        }
    }

    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < total_bytes; i++) {
        Address addr = base_addr + i;
        uint32_t reg_idx = (static_cast<uint32_t>(vs3) + (i / cpu.state().regs.vlen_bytes())) % 32;
        uint8_t val = cpu.state()
                          .regs.read_vector(static_cast<RegId>(reg_idx))
                          .u8[i % cpu.state().regs.vlen_bytes()];
        simrv::memory::MemoryAccess::storeInt(mem, cpu, addr, static_cast<Word>(val),
                                              isa::Funct3::Sb);
        if (cpu.active_context().pending_exception.has_value()) {
            cpu.state().vstart = i;
            return;
        }
    }
}

// Vector load helper (supports unit-stride load vle and unit-stride segment load vlseg)
template <typename T>
void execute_vle(core::CPU& cpu, simrv::memory::MemorySubsystem& mem, RegId rd, Register base_addr,
                 bool vm, uint32_t vl, uint32_t nfields, uint32_t field_registers,
                 isa::Funct3 mem_f3, bool fault_only_first = false) {
    if (vm && nfields == 1 && !fault_only_first && cpu.state().vstart == 0 && vl > 0) {
        if constexpr (sizeof(T) == 8 && !simrv::xlen::kIsXLen64) {
            if ((base_addr & 7) != 0) {
                cpu.active_context().pending_exception = ExceptionCode::MisalignedLoad;
                cpu.active_context().pending_tval = base_addr;
                cpu.state().vstart = 0;
                return;
            }
        }
        const uint32_t total_bytes = vl * sizeof(T);
        Byte* host_src = get_direct_host_memory_for_range(cpu, base_addr, total_bytes, false);
        if (host_src != nullptr) {
            uint32_t remaining = vl;
            uint32_t elem_idx = 0;
            uint32_t byte_offset = 0;
            const uint32_t elems_per_reg = cpu.state().regs.vlen_bytes() / sizeof(T);
            while (remaining > 0) {
                const uint32_t reg_offset = elem_idx / elems_per_reg;
                const uint32_t in_reg = elem_idx % elems_per_reg;
                const uint32_t chunk = std::min(remaining, elems_per_reg - in_reg);
                const auto actual_reg =
                    static_cast<RegId>((std::to_underlying(rd) + reg_offset) & 0x1F);
                auto& vreg = cpu.state().regs.read_vector(actual_reg);
                std::memcpy(&vreg.u8[in_reg * sizeof(T)], host_src + byte_offset,
                            chunk * sizeof(T));
                elem_idx += chunk;
                byte_offset += chunk * sizeof(T);
                remaining -= chunk;
            }
            return;
        }
    }

    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);

    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        for (uint32_t f = 0; f < nfields; f++) {
            Address addr = base_addr + (i * nfields + f) * sizeof(T);
            uint64_t val = 0;
            if constexpr (sizeof(T) == 8) {
                if constexpr (simrv::xlen::kIsXLen64) {
                    val = simrv::memory::MemoryAccess::loadInt(mem, cpu, addr, mem_f3);
                } else {
                    if (simrv::compiler::unlikely((addr & 7) != 0)) {
                        cpu.active_context().pending_exception = ExceptionCode::MisalignedLoad;
                        cpu.active_context().pending_tval = addr;
                        cpu.state().vstart = i;
                        return;
                    }
                    Word lo = simrv::memory::MemoryAccess::loadInt(mem, cpu, addr, isa::Funct3::Lw);
                    if (cpu.active_context().pending_exception.has_value()) {
                        if (fault_only_first && i != 0) {
                            cpu.active_context().pending_exception.reset();
                            cpu.active_context().pending_tval = 0;
                            cpu.state().vl = i;
                        } else {
                            cpu.state().vstart = i;
                        }
                        return;
                    }
                    Word hi =
                        simrv::memory::MemoryAccess::loadInt(mem, cpu, addr + 4, isa::Funct3::Lw);
                    val = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
                }
            } else {
                val = simrv::memory::MemoryAccess::loadInt(mem, cpu, addr, mem_f3);
            }
            if (cpu.active_context().pending_exception.has_value()) {
                if (fault_only_first && i != 0) {
                    cpu.active_context().pending_exception.reset();
                    cpu.active_context().pending_tval = 0;
                    cpu.state().vl = i;
                    return;
                }
                cpu.state().vstart = i;
                return;
            }
            auto target_reg = static_cast<RegId>(static_cast<uint32_t>(rd) + f * field_registers);
            vector::set_group_element<T>(cpu.state().regs, target_reg, i, static_cast<T>(val));
        }
    }
}

void execute_vlm(core::CPU& cpu, simrv::memory::MemorySubsystem& mem, RegId rd, Register base_addr,
                 uint32_t vl) {
    const uint32_t evl = (vl + 7) / 8;
    if (cpu.state().vstart == 0 && evl > 0) {
        Byte* host_src = get_direct_host_memory_for_range(cpu, base_addr, evl, false);
        if (host_src != nullptr) {
            std::memcpy(cpu.state().regs.read_vector(rd).u8.data(), host_src, evl);
            return;
        }
    }
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < evl; ++i) {
        const auto value =
            simrv::memory::MemoryAccess::loadInt(mem, cpu, base_addr + i, isa::Funct3::Lbu);
        if (cpu.active_context().pending_exception.has_value()) {
            cpu.state().vstart = i;
            return;
        }
        vector::set_group_element<uint8_t>(cpu.state().regs, rd, i, static_cast<uint8_t>(value));
    }
}

void execute_vsm(core::CPU& cpu, simrv::memory::MemorySubsystem& mem, RegId vs3, Register base_addr,
                 uint32_t vl) {
    const uint32_t evl = (vl + 7) / 8;
    if (cpu.state().vstart == 0 && evl > 0) {
        Byte* host_dst = get_direct_host_memory_for_range(cpu, base_addr, evl, true);
        if (host_dst != nullptr) {
            std::memcpy(host_dst, cpu.state().regs.read_vector(vs3).u8.data(), evl);
            if (simrv::compiler::unlikely(cpu.machine_ != nullptr &&
                                          cpu.machine_->num_harts() > 1 &&
                                          mem.reservation_table().may_have_reservations())) {
                mem.reservation_table().invalidate_matching(
                    base_addr, static_cast<HartId>(cpu.state().mhartid));
            }
            return;
        }
    }
    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < evl; ++i) {
        const auto value = vector::get_group_element<uint8_t>(cpu.state().regs, vs3, i);
        simrv::memory::MemoryAccess::storeInt(mem, cpu, base_addr + i, value, isa::Funct3::Sb);
        if (cpu.active_context().pending_exception.has_value()) {
            cpu.state().vstart = i;
            return;
        }
    }
}

// Vector store helper (supports unit-stride store vse and unit-stride segment store vsseg)
template <typename T>
void execute_vse(core::CPU& cpu, simrv::memory::MemorySubsystem& mem, RegId vs3, Register base_addr,
                 bool vm, uint32_t vl, uint32_t nfields, uint32_t field_registers,
                 isa::Funct3 mem_f3) {
    if (vm && nfields == 1 && cpu.state().vstart == 0 && vl > 0) {
        if constexpr (sizeof(T) == 8 && !simrv::xlen::kIsXLen64) {
            if ((base_addr & 7) != 0) {
                cpu.active_context().pending_exception = ExceptionCode::MisalignedStore;
                cpu.active_context().pending_tval = base_addr;
                cpu.state().vstart = 0;
                return;
            }
        }
        const uint32_t total_bytes = vl * sizeof(T);
        Byte* host_dst = get_direct_host_memory_for_range(cpu, base_addr, total_bytes, true);
        if (host_dst != nullptr) {
            uint32_t remaining = vl;
            uint32_t elem_idx = 0;
            uint32_t byte_offset = 0;
            const uint32_t elems_per_reg = cpu.state().regs.vlen_bytes() / sizeof(T);
            while (remaining > 0) {
                const uint32_t reg_offset = elem_idx / elems_per_reg;
                const uint32_t in_reg = elem_idx % elems_per_reg;
                const uint32_t chunk = std::min(remaining, elems_per_reg - in_reg);
                const auto source_reg =
                    static_cast<RegId>((std::to_underlying(vs3) + reg_offset) & 0x1F);
                const auto& vreg = cpu.state().regs.read_vector(source_reg);
                std::memcpy(host_dst + byte_offset, &vreg.u8[in_reg * sizeof(T)],
                            chunk * sizeof(T));
                elem_idx += chunk;
                byte_offset += chunk * sizeof(T);
                remaining -= chunk;
            }
            if (simrv::compiler::unlikely(cpu.machine_ != nullptr &&
                                          cpu.machine_->num_harts() > 1 &&
                                          mem.reservation_table().may_have_reservations())) {
                mem.reservation_table().invalidate_matching(
                    base_addr, static_cast<HartId>(cpu.state().mhartid));
            }
            return;
        }
    }

    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);

    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        for (uint32_t f = 0; f < nfields; f++) {
            Address addr = base_addr + (i * nfields + f) * sizeof(T);
            auto source_reg = static_cast<RegId>(static_cast<uint32_t>(vs3) + f * field_registers);
            T val = vector::get_group_element<T>(cpu.state().regs, source_reg, i);
            if constexpr (sizeof(T) == 8) {
                if constexpr (simrv::xlen::kIsXLen64) {
                    simrv::memory::MemoryAccess::storeInt(mem, cpu, addr, static_cast<Word>(val),
                                                          mem_f3);
                } else {
                    if (simrv::compiler::unlikely((addr & 7) != 0)) {
                        cpu.active_context().pending_exception = ExceptionCode::MisalignedStore;
                        cpu.active_context().pending_tval = addr;
                        cpu.state().vstart = i;
                        return;
                    }
                    simrv::memory::MemoryAccess::storeInt(
                        mem, cpu, addr, static_cast<Word>(val & 0xFFFFFFFFULL), isa::Funct3::Sw);
                    if (cpu.active_context().pending_exception.has_value()) {
                        cpu.state().vstart = i;
                        return;
                    }
                    simrv::memory::MemoryAccess::storeInt(
                        mem, cpu, addr + 4, static_cast<Word>((val >> 32) & 0xFFFFFFFFULL),
                        isa::Funct3::Sw);
                }
            } else {
                simrv::memory::MemoryAccess::storeInt(mem, cpu, addr, static_cast<Word>(val),
                                                      mem_f3);
            }
            if (cpu.active_context().pending_exception.has_value()) {
                cpu.state().vstart = i;
                return;
            }
        }
    }
}

// Vector strided load helper
template <typename T>
void execute_vlse(core::CPU& cpu, simrv::memory::MemorySubsystem& mem, RegId rd, Register base_addr,
                  Register stride_reg_val, bool vm, uint32_t vl, uint32_t nfields,
                  uint32_t field_registers, isa::Funct3 mem_f3) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    auto stride = static_cast<int64_t>(static_cast<std::make_signed_t<Register>>(stride_reg_val));

    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        for (uint32_t f = 0; f < nfields; ++f) {
            Address addr = base_addr + i * stride + f * sizeof(T);
            uint64_t val = 0;
            if constexpr (sizeof(T) == 8) {
                if constexpr (simrv::xlen::kIsXLen64) {
                    val = simrv::memory::MemoryAccess::loadInt(mem, cpu, addr, mem_f3);
                } else {
                    if (simrv::compiler::unlikely((addr & 7) != 0)) {
                        cpu.active_context().pending_exception = ExceptionCode::MisalignedLoad;
                        cpu.active_context().pending_tval = addr;
                        cpu.state().vstart = i;
                        return;
                    }
                    Word lo = simrv::memory::MemoryAccess::loadInt(mem, cpu, addr, isa::Funct3::Lw);
                    if (cpu.active_context().pending_exception.has_value()) {
                        cpu.state().vstart = i;
                        return;
                    }
                    Word hi =
                        simrv::memory::MemoryAccess::loadInt(mem, cpu, addr + 4, isa::Funct3::Lw);
                    val = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
                }
            } else {
                val = simrv::memory::MemoryAccess::loadInt(mem, cpu, addr, mem_f3);
            }
            if (cpu.active_context().pending_exception.has_value()) {
                cpu.state().vstart = i;
                return;
            }
            const auto target = static_cast<RegId>(static_cast<uint32_t>(rd) + f * field_registers);
            vector::set_group_element<T>(cpu.state().regs, target, i, static_cast<T>(val));
        }
    }
}

// Vector strided store helper
template <typename T>
void execute_vsse(core::CPU& cpu, simrv::memory::MemorySubsystem& mem, RegId vs3,
                  Register base_addr, Register stride_reg_val, bool vm, uint32_t vl,
                  uint32_t nfields, uint32_t field_registers, isa::Funct3 mem_f3) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);
    auto stride = static_cast<int64_t>(static_cast<std::make_signed_t<Register>>(stride_reg_val));

    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        for (uint32_t f = 0; f < nfields; ++f) {
            Address addr = base_addr + i * stride + f * sizeof(T);
            const auto source =
                static_cast<RegId>(static_cast<uint32_t>(vs3) + f * field_registers);
            T val = vector::get_group_element<T>(cpu.state().regs, source, i);
            if constexpr (sizeof(T) == 8) {
                if constexpr (simrv::xlen::kIsXLen64) {
                    simrv::memory::MemoryAccess::storeInt(mem, cpu, addr, static_cast<Word>(val),
                                                          mem_f3);
                } else {
                    if (simrv::compiler::unlikely((addr & 7) != 0)) {
                        cpu.active_context().pending_exception = ExceptionCode::MisalignedStore;
                        cpu.active_context().pending_tval = addr;
                        cpu.state().vstart = i;
                        return;
                    }
                    simrv::memory::MemoryAccess::storeInt(
                        mem, cpu, addr, static_cast<Word>(val & 0xFFFFFFFFULL), isa::Funct3::Sw);
                    if (cpu.active_context().pending_exception.has_value()) {
                        cpu.state().vstart = i;
                        return;
                    }
                    simrv::memory::MemoryAccess::storeInt(
                        mem, cpu, addr + 4, static_cast<Word>((val >> 32) & 0xFFFFFFFFULL),
                        isa::Funct3::Sw);
                }
            } else {
                simrv::memory::MemoryAccess::storeInt(mem, cpu, addr, static_cast<Word>(val),
                                                      mem_f3);
            }
            if (cpu.active_context().pending_exception.has_value()) {
                cpu.state().vstart = i;
                return;
            }
        }
    }
}

// Vector indexed load helper
template <typename T_data, typename T_idx>
void execute_vluxei(core::CPU& cpu, simrv::memory::MemorySubsystem& mem, RegId rd,
                    Register base_addr, RegId vs2, bool vm, uint32_t vl, uint32_t nfields,
                    uint32_t field_registers, isa::Funct3 mem_f3) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);

    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        auto offset = vector::get_group_element<T_idx>(cpu.state().regs, vs2, i);
        for (uint32_t f = 0; f < nfields; ++f) {
            Address addr = base_addr + static_cast<int64_t>(offset) + f * sizeof(T_data);

            uint64_t val = 0;
            if constexpr (sizeof(T_data) == 8) {
                if constexpr (simrv::xlen::kIsXLen64) {
                    val = simrv::memory::MemoryAccess::loadInt(mem, cpu, addr, mem_f3);
                } else {
                    if (simrv::compiler::unlikely((addr & 7) != 0)) {
                        cpu.active_context().pending_exception = ExceptionCode::MisalignedLoad;
                        cpu.active_context().pending_tval = addr;
                        cpu.state().vstart = i;
                        return;
                    }
                    Word lo = simrv::memory::MemoryAccess::loadInt(mem, cpu, addr, isa::Funct3::Lw);
                    if (cpu.active_context().pending_exception.has_value()) {
                        cpu.state().vstart = i;
                        return;
                    }
                    Word hi =
                        simrv::memory::MemoryAccess::loadInt(mem, cpu, addr + 4, isa::Funct3::Lw);
                    val = static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
                }
            } else {
                val = simrv::memory::MemoryAccess::loadInt(mem, cpu, addr, mem_f3);
            }
            if (cpu.active_context().pending_exception.has_value()) {
                cpu.state().vstart = i;
                return;
            }
            const auto target = static_cast<RegId>(static_cast<uint32_t>(rd) + f * field_registers);
            vector::set_group_element<T_data>(cpu.state().regs, target, i,
                                              static_cast<T_data>(val));
        }
    }
}

template <typename T_idx>
void dispatch_vluxei(core::CPU& cpu, simrv::memory::MemorySubsystem& mem, RegId rd,
                     Register base_addr, RegId vs2, bool vm, uint32_t vl, uint32_t sew,
                     uint32_t nfields, uint32_t field_registers) {
    if (sew == 8) {
        execute_vluxei<uint8_t, T_idx>(cpu, mem, rd, base_addr, vs2, vm, vl, nfields,
                                       field_registers, isa::Funct3::Lbu);
    } else if (sew == 16) {
        execute_vluxei<uint16_t, T_idx>(cpu, mem, rd, base_addr, vs2, vm, vl, nfields,
                                        field_registers, isa::Funct3::Lhu);
    } else if (sew == 32) {
        execute_vluxei<uint32_t, T_idx>(cpu, mem, rd, base_addr, vs2, vm, vl, nfields,
                                        field_registers, isa::Funct3::Lw);
    } else {
        execute_vluxei<uint64_t, T_idx>(cpu, mem, rd, base_addr, vs2, vm, vl, nfields,
                                        field_registers, isa::Funct3::Ld);
    }
}

// Vector indexed store helper
template <typename T_data, typename T_idx>
void execute_vsuxei(core::CPU& cpu, simrv::memory::MemorySubsystem& mem, RegId vs3,
                    Register base_addr, RegId vs2, bool vm, uint32_t vl, uint32_t nfields,
                    uint32_t field_registers, isa::Funct3 mem_f3) {
    const auto& mask_reg = cpu.state().regs.read_vector(RegId::Zero);

    for (uint32_t i = static_cast<uint32_t>(cpu.state().vstart); i < vl; i++) {
        if (!vector::is_element_active(mask_reg, i, vm)) continue;
        auto offset = vector::get_group_element<T_idx>(cpu.state().regs, vs2, i);
        for (uint32_t f = 0; f < nfields; ++f) {
            Address addr = base_addr + static_cast<int64_t>(offset) + f * sizeof(T_data);
            const auto source =
                static_cast<RegId>(static_cast<uint32_t>(vs3) + f * field_registers);
            auto val = vector::get_group_element<T_data>(cpu.state().regs, source, i);
            if constexpr (sizeof(T_data) == 8) {
                if constexpr (simrv::xlen::kIsXLen64) {
                    simrv::memory::MemoryAccess::storeInt(mem, cpu, addr, static_cast<Word>(val),
                                                          mem_f3);
                } else {
                    if (simrv::compiler::unlikely((addr & 7) != 0)) {
                        cpu.active_context().pending_exception = ExceptionCode::MisalignedStore;
                        cpu.active_context().pending_tval = addr;
                        cpu.state().vstart = i;
                        return;
                    }
                    simrv::memory::MemoryAccess::storeInt(
                        mem, cpu, addr, static_cast<Word>(val & 0xFFFFFFFFULL), isa::Funct3::Sw);
                    if (cpu.active_context().pending_exception.has_value()) {
                        cpu.state().vstart = i;
                        return;
                    }
                    simrv::memory::MemoryAccess::storeInt(
                        mem, cpu, addr + 4, static_cast<Word>((val >> 32) & 0xFFFFFFFFULL),
                        isa::Funct3::Sw);
                }
            } else {
                simrv::memory::MemoryAccess::storeInt(mem, cpu, addr, static_cast<Word>(val),
                                                      mem_f3);
            }
            if (cpu.active_context().pending_exception.has_value()) {
                cpu.state().vstart = i;
                return;
            }
        }
    }
}

template <typename T_idx>
void dispatch_vsuxei(core::CPU& cpu, simrv::memory::MemorySubsystem& mem, RegId vs3,
                     Register base_addr, RegId vs2, bool vm, uint32_t vl, uint32_t sew,
                     uint32_t nfields, uint32_t field_registers) {
    if (sew == 8) {
        execute_vsuxei<uint8_t, T_idx>(cpu, mem, vs3, base_addr, vs2, vm, vl, nfields,
                                       field_registers, isa::Funct3::Sb);
    } else if (sew == 16) {
        execute_vsuxei<uint16_t, T_idx>(cpu, mem, vs3, base_addr, vs2, vm, vl, nfields,
                                        field_registers, isa::Funct3::Sh);
    } else if (sew == 32) {
        execute_vsuxei<uint32_t, T_idx>(cpu, mem, vs3, base_addr, vs2, vm, vl, nfields,
                                        field_registers, isa::Funct3::Sw);
    } else {
        execute_vsuxei<uint64_t, T_idx>(cpu, mem, vs3, base_addr, vs2, vm, vl, nfields,
                                        field_registers, isa::Funct3::Sd);
    }
}

}  // namespace

void ExecuteUnit::execute_vector_memory(core::CPU& cpu, memory::MemorySubsystem& mem,
                                        isa::OperationId op_id, RegId rd, RegId rs1, RegId rs2,
                                        bool vm, uint32_t vl, uint32_t sew, uint32_t nfields,
                                        uint32_t field_registers) {
    switch (op_id) {
        case isa::OperationId::VLE8_V:
            execute_vle<uint8_t>(cpu, mem, rd, cpu.state().regs.read(rs1), vm, vl, nfields,
                                 field_registers, isa::Funct3::Lbu);
            break;
        case isa::OperationId::VLE16_V:
            execute_vle<uint16_t>(cpu, mem, rd, cpu.state().regs.read(rs1), vm, vl, nfields,
                                  field_registers, isa::Funct3::Lhu);
            break;
        case isa::OperationId::VLE32_V:
            execute_vle<uint32_t>(cpu, mem, rd, cpu.state().regs.read(rs1), vm, vl, nfields,
                                  field_registers, isa::Funct3::Lw);
            break;
        case isa::OperationId::VLE64_V:
            execute_vle<uint64_t>(cpu, mem, rd, cpu.state().regs.read(rs1), vm, vl, nfields,
                                  field_registers, isa::Funct3::Ld);
            break;
        case isa::OperationId::VLE8FF_V:
            execute_vle<uint8_t>(cpu, mem, rd, cpu.state().regs.read(rs1), vm, vl, nfields,
                                 field_registers, isa::Funct3::Lbu, true);
            break;
        case isa::OperationId::VLE16FF_V:
            execute_vle<uint16_t>(cpu, mem, rd, cpu.state().regs.read(rs1), vm, vl, nfields,
                                  field_registers, isa::Funct3::Lhu, true);
            break;
        case isa::OperationId::VLE32FF_V:
            execute_vle<uint32_t>(cpu, mem, rd, cpu.state().regs.read(rs1), vm, vl, nfields,
                                  field_registers, isa::Funct3::Lw, true);
            break;
        case isa::OperationId::VLE64FF_V:
            execute_vle<uint64_t>(cpu, mem, rd, cpu.state().regs.read(rs1), vm, vl, nfields,
                                  field_registers, isa::Funct3::Ld, true);
            break;
        case isa::OperationId::VLM_V:
            execute_vlm(cpu, mem, rd, cpu.state().regs.read(rs1), vl);
            break;
        case isa::OperationId::VSE8_V:
            execute_vse<uint8_t>(cpu, mem, rd, cpu.state().regs.read(rs1), vm, vl, nfields,
                                 field_registers, isa::Funct3::Sb);
            break;
        case isa::OperationId::VSE16_V:
            execute_vse<uint16_t>(cpu, mem, rd, cpu.state().regs.read(rs1), vm, vl, nfields,
                                  field_registers, isa::Funct3::Sh);
            break;
        case isa::OperationId::VSE32_V:
            execute_vse<uint32_t>(cpu, mem, rd, cpu.state().regs.read(rs1), vm, vl, nfields,
                                  field_registers, isa::Funct3::Sw);
            break;
        case isa::OperationId::VSE64_V:
            execute_vse<uint64_t>(cpu, mem, rd, cpu.state().regs.read(rs1), vm, vl, nfields,
                                  field_registers, isa::Funct3::Sd);
            break;
        case isa::OperationId::VSM_V:
            execute_vsm(cpu, mem, rd, cpu.state().regs.read(rs1), vl);
            break;

        case isa::OperationId::VLSE8_V:
            execute_vlse<uint8_t>(cpu, mem, rd, cpu.state().regs.read(rs1),
                                  cpu.state().regs.read(rs2), vm, vl, nfields, field_registers,
                                  isa::Funct3::Lbu);
            break;
        case isa::OperationId::VLSE16_V:
            execute_vlse<uint16_t>(cpu, mem, rd, cpu.state().regs.read(rs1),
                                   cpu.state().regs.read(rs2), vm, vl, nfields, field_registers,
                                   isa::Funct3::Lhu);
            break;
        case isa::OperationId::VLSE32_V:
            execute_vlse<uint32_t>(cpu, mem, rd, cpu.state().regs.read(rs1),
                                   cpu.state().regs.read(rs2), vm, vl, nfields, field_registers,
                                   isa::Funct3::Lw);
            break;
        case isa::OperationId::VLSE64_V:
            execute_vlse<uint64_t>(cpu, mem, rd, cpu.state().regs.read(rs1),
                                   cpu.state().regs.read(rs2), vm, vl, nfields, field_registers,
                                   isa::Funct3::Ld);
            break;

        case isa::OperationId::VSSE8_V:
            execute_vsse<uint8_t>(cpu, mem, rd, cpu.state().regs.read(rs1),
                                  cpu.state().regs.read(rs2), vm, vl, nfields, field_registers,
                                  isa::Funct3::Sb);
            break;
        case isa::OperationId::VSSE16_V:
            execute_vsse<uint16_t>(cpu, mem, rd, cpu.state().regs.read(rs1),
                                   cpu.state().regs.read(rs2), vm, vl, nfields, field_registers,
                                   isa::Funct3::Sh);
            break;
        case isa::OperationId::VSSE32_V:
            execute_vsse<uint32_t>(cpu, mem, rd, cpu.state().regs.read(rs1),
                                   cpu.state().regs.read(rs2), vm, vl, nfields, field_registers,
                                   isa::Funct3::Sw);
            break;
        case isa::OperationId::VSSE64_V:
            execute_vsse<uint64_t>(cpu, mem, rd, cpu.state().regs.read(rs1),
                                   cpu.state().regs.read(rs2), vm, vl, nfields, field_registers,
                                   isa::Funct3::Sd);
            break;

        case isa::OperationId::VLUXEI8_V:
        case isa::OperationId::VLOXEI8_V:
            dispatch_vluxei<uint8_t>(cpu, mem, rd, cpu.state().regs.read(rs1), rs2, vm, vl, sew,
                                     nfields, field_registers);
            break;
        case isa::OperationId::VLUXEI16_V:
        case isa::OperationId::VLOXEI16_V:
            dispatch_vluxei<uint16_t>(cpu, mem, rd, cpu.state().regs.read(rs1), rs2, vm, vl, sew,
                                      nfields, field_registers);
            break;
        case isa::OperationId::VLUXEI32_V:
        case isa::OperationId::VLOXEI32_V:
            dispatch_vluxei<uint32_t>(cpu, mem, rd, cpu.state().regs.read(rs1), rs2, vm, vl, sew,
                                      nfields, field_registers);
            break;
        case isa::OperationId::VLUXEI64_V:
        case isa::OperationId::VLOXEI64_V:
            dispatch_vluxei<uint64_t>(cpu, mem, rd, cpu.state().regs.read(rs1), rs2, vm, vl, sew,
                                      nfields, field_registers);
            break;

        case isa::OperationId::VSUXEI8_V:
        case isa::OperationId::VSOXEI8_V:
            dispatch_vsuxei<uint8_t>(cpu, mem, rd, cpu.state().regs.read(rs1), rs2, vm, vl, sew,
                                     nfields, field_registers);
            break;
        case isa::OperationId::VSUXEI16_V:
        case isa::OperationId::VSOXEI16_V:
            dispatch_vsuxei<uint16_t>(cpu, mem, rd, cpu.state().regs.read(rs1), rs2, vm, vl, sew,
                                      nfields, field_registers);
            break;
        case isa::OperationId::VSUXEI32_V:
        case isa::OperationId::VSOXEI32_V:
            dispatch_vsuxei<uint32_t>(cpu, mem, rd, cpu.state().regs.read(rs1), rs2, vm, vl, sew,
                                      nfields, field_registers);
            break;
        case isa::OperationId::VSUXEI64_V:
        case isa::OperationId::VSOXEI64_V:
            dispatch_vsuxei<uint64_t>(cpu, mem, rd, cpu.state().regs.read(rs1), rs2, vm, vl, sew,
                                      nfields, field_registers);
            break;

        // Whole loads
        case isa::OperationId::VL1RE8_V:
            execute_vl_whole(cpu, mem, rd, cpu.state().regs.read(rs1), 1, 1);
            break;
        case isa::OperationId::VL1RE16_V:
            execute_vl_whole(cpu, mem, rd, cpu.state().regs.read(rs1), 1, 2);
            break;
        case isa::OperationId::VL1RE32_V:
            execute_vl_whole(cpu, mem, rd, cpu.state().regs.read(rs1), 1, 4);
            break;
        case isa::OperationId::VL1RE64_V:
            execute_vl_whole(cpu, mem, rd, cpu.state().regs.read(rs1), 1, 8);
            break;
        case isa::OperationId::VL2RE8_V:
            execute_vl_whole(cpu, mem, rd, cpu.state().regs.read(rs1), 2, 1);
            break;
        case isa::OperationId::VL2RE16_V:
            execute_vl_whole(cpu, mem, rd, cpu.state().regs.read(rs1), 2, 2);
            break;
        case isa::OperationId::VL2RE32_V:
            execute_vl_whole(cpu, mem, rd, cpu.state().regs.read(rs1), 2, 4);
            break;
        case isa::OperationId::VL2RE64_V:
            execute_vl_whole(cpu, mem, rd, cpu.state().regs.read(rs1), 2, 8);
            break;
        case isa::OperationId::VL4RE8_V:
            execute_vl_whole(cpu, mem, rd, cpu.state().regs.read(rs1), 4, 1);
            break;
        case isa::OperationId::VL4RE16_V:
            execute_vl_whole(cpu, mem, rd, cpu.state().regs.read(rs1), 4, 2);
            break;
        case isa::OperationId::VL4RE32_V:
            execute_vl_whole(cpu, mem, rd, cpu.state().regs.read(rs1), 4, 4);
            break;
        case isa::OperationId::VL4RE64_V:
            execute_vl_whole(cpu, mem, rd, cpu.state().regs.read(rs1), 4, 8);
            break;
        case isa::OperationId::VL8RE8_V:
            execute_vl_whole(cpu, mem, rd, cpu.state().regs.read(rs1), 8, 1);
            break;
        case isa::OperationId::VL8RE16_V:
            execute_vl_whole(cpu, mem, rd, cpu.state().regs.read(rs1), 8, 2);
            break;
        case isa::OperationId::VL8RE32_V:
            execute_vl_whole(cpu, mem, rd, cpu.state().regs.read(rs1), 8, 4);
            break;
        case isa::OperationId::VL8RE64_V:
            execute_vl_whole(cpu, mem, rd, cpu.state().regs.read(rs1), 8, 8);
            break;

        // Whole stores
        case isa::OperationId::VS1R_V:
            execute_vs_whole(cpu, mem, rd, cpu.state().regs.read(rs1), 1);
            break;
        case isa::OperationId::VS2R_V:
            execute_vs_whole(cpu, mem, rd, cpu.state().regs.read(rs1), 2);
            break;
        case isa::OperationId::VS4R_V:
            execute_vs_whole(cpu, mem, rd, cpu.state().regs.read(rs1), 4);
            break;
        case isa::OperationId::VS8R_V:
            execute_vs_whole(cpu, mem, rd, cpu.state().regs.read(rs1), 8);
            break;

        default:
            break;
    }
}

}  // namespace simrv::execute
