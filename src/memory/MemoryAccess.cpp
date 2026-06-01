/**
 * @file MemoryAccess.cpp
 * @brief Typed memory access implementations.
 */
#include "simrv/memory/MemoryAccess.hpp"

#include <cstring>

#include "simrv/core/Cpu.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/memory/Mmu.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Helpers.hpp"

namespace simrv::memory {

auto MemoryAccess::target_read(MemorySubsystem& mem, core::CPU& cpu, Address v_addr,
                               Instruction funct3) -> Word {
    if (simrv::compiler::unlikely(cpu.pipeline_context.pending_exception.has_value())) {
        return 0;
    }

    const unsigned size_bytes = 1u << (funct3 & 0x3u); // unchanged
    const bool is_amo = (static_cast<Opcode>(cpu.pipeline_context.opcode) == Opcode::Amo);
    const bool is_lr =
        is_amo && (static_cast<Funct5Amo>(cpu.pipeline_context.funct5) == Funct5Amo::Lr);

    const bool crosses_page = ((v_addr & simrv::memory::kPageMask) + size_bytes) > (1u << simrv::memory::kPageShift);
    if (simrv::compiler::unlikely((v_addr & (size_bytes - 1u)) != 0)) {
        if (is_amo || crosses_page) {
            cpu.pipeline_context.pending_exception = (is_amo && !is_lr)
                                                         ? ExceptionCode::MisalignedStore
                                                         : ExceptionCode::MisalignedLoad;
            cpu.pipeline_context.pending_tval = v_addr;
            return 0;
        }
    }

    const PrivilegeLevel eff_priv = cpu.effective_data_privilege();

    if constexpr (simrv::xlen::kIsXLen64) {
        if (eff_priv != kPrivMachine && simrv::xlen::satp_translation_enabled(cpu.state().satp)) {
            if (simrv::compiler::unlikely(!simrv::Mmu::is_canonical(v_addr, cpu.state().satp))) {
                cpu.pipeline_context.pending_exception =
                    is_amo ? ExceptionCode::StorePageFault
                           : ExceptionCode::LoadPageFault;
                cpu.pipeline_context.pending_tval = v_addr;
                return 0;
            }
        }
    }

    auto issue_read = [&](Address addr) -> Word {
        if (!simrv::memory::is_dram_addr(addr)) {
            TlChannelA req{};
            req.opcode = TlOpcodeA::Get;
            req.size = static_cast<uint8_t>(funct3 & 0x3);
            req.source = 0;  // CPU Data Bus source ID
            req.address = addr;
            mem.system_bus().send_request(req);

            TlChannelD resp{};
            if (mem.system_bus().get_response(0, resp)) {
                    if (simrv::compiler::unlikely(resp.error)) {
                        cpu.pipeline_context.pending_exception = ExceptionCode::FaultLoad;
                        cpu.pipeline_context.pending_tval = v_addr;
                    }
                    Word rdata = resp.data;
                    const unsigned req_size_bytes = 1u << (funct3 & 0x3u);
                    const unsigned bits = 8 * req_size_bytes;
                    if (bits < simrv::xlen::kXLenBits) {
                        const Word mask = (static_cast<Word>(1) << bits) - 1;
                        rdata &= mask;
                        constexpr auto kSignExtendBit = 0x4u;
                        if ((funct3 & kSignExtendBit) == 0) {
                            const Word sign_bit = static_cast<Word>(1) << (bits - 1);
                            if ((rdata & sign_bit) != 0) {
                                rdata |= ~mask;
                            }
                        }
                    }
                    return static_cast<Word>(rdata & simrv::xlen::kXLenMask);
                }
                return 0;
        }

        Word cached_data = 0;
        if (cpu.dcache.read(addr, cached_data, funct3)) {
            return cached_data;
        }

        const Address line_base =
            addr & ~(static_cast<Address>(simrv::cache::DCache::kLineBytes - 1u));
        std::array<Byte, simrv::cache::DCache::kLineBytes> line_data{};

        constexpr unsigned fetch_size = xlen::kFetchSize;
        const Instruction fetch_funct3 = static_cast<Instruction>(
            xlen::kIsXLen64 ? ::Funct3::Sd : ::Funct3::Sw);

        for (uint32_t i = 0; i < simrv::cache::DCache::kLineBytes; i += fetch_size) {
            TlChannelA req{};
            req.opcode = TlOpcodeA::Get;
            req.size = static_cast<uint8_t>(fetch_funct3 & 0x3);
            req.source = 0;  // CPU Data Bus source ID
            req.address = line_base + i;
            mem.system_bus().send_request(req);

            TlChannelD resp{};
            if (mem.system_bus().get_response(0, resp)) {
                if (simrv::compiler::unlikely(resp.error)) {
                    cpu.pipeline_context.pending_exception = ExceptionCode::FaultLoad;
                    cpu.pipeline_context.pending_tval = v_addr;
                }
                std::memcpy(line_data.data() + i, &resp.data, fetch_size);
            }
        }

        cpu.dcache.insert(line_base, line_data.data());
        if (cpu.dcache.read(addr, cached_data, funct3)) {
            return cached_data;
        }
        return 0;
    };

    if (simrv::compiler::likely(!cpu.pipeline_context.pending_exception.has_value()) &&
        simrv::compiler::likely(eff_priv == kPrivMachine ||
                                !simrv::xlen::satp_translation_enabled(cpu.state().satp)) &&
        simrv::compiler::likely(simrv::memory::is_dram_addr(v_addr))) {
        return issue_read(v_addr);
    }

    Word rdata = 0;
    Address p_addr = 0;
    core::TLBEntry* entry =
        &cpu.tlb.data_r.at((v_addr >> simrv::memory::kPageShift) & (simrv::memory::kTlbSize - 1));
    const Word current_asid = simrv::xlen::satp_asid(cpu.state().satp);

    if (eff_priv == kPrivMachine || !simrv::xlen::satp_translation_enabled(cpu.state().satp)) {
        p_addr = v_addr;
    } else if (entry->valid && entry->asid == current_asid &&
               entry->v_addr == (v_addr & ~simrv::memory::kPageMask)) {
        p_addr = entry->p_addr + (v_addr & simrv::memory::kPageMask);
    } else {
        auto translate_result = mem.mmu()->translate(v_addr, PteAccess::Read, eff_priv,
                                                     cpu.state().mstatus, cpu.state().satp);
        if (translate_result.has_value()) {
            p_addr = translate_result.value();
            entry->v_addr = v_addr & ~simrv::memory::kPageMask;
            entry->p_addr = p_addr & ~simrv::memory::kPageMask;
            entry->asid = current_asid;
            entry->valid = true;
        } else {
            cpu.pipeline_context.pending_exception = static_cast<ExceptionCode>(translate_result.error());
            cpu.pipeline_context.pending_tval = v_addr;
        }
    }

    if (!cpu.pipeline_context.pending_exception.has_value()) {
        return issue_read(p_addr);
    }
    return rdata;
}

void MemoryAccess::target_write(MemorySubsystem& mem, core::CPU& cpu, Address v_addr, Word wdata,
                                Instruction funct3) {
    if (simrv::compiler::unlikely(cpu.pipeline_context.pending_exception.has_value())) {
        return;
    }

    const unsigned size_bytes = 1u << (funct3 & 0x3u);

    const bool crosses_page = ((v_addr & simrv::memory::kPageMask) + size_bytes) > (1u << simrv::memory::kPageShift);
    if (simrv::compiler::unlikely((v_addr & (size_bytes - 1u)) != 0)) {
        if (crosses_page) {
            cpu.pipeline_context.pending_exception = ExceptionCode::MisalignedStore;
            cpu.pipeline_context.pending_tval = v_addr;
            return;
        }
    }

    const PrivilegeLevel eff_priv = cpu.effective_data_privilege();

    if constexpr (simrv::xlen::kIsXLen64) {
        if (eff_priv != kPrivMachine && simrv::xlen::satp_translation_enabled(cpu.state().satp)) {
            if (simrv::compiler::unlikely(!simrv::Mmu::is_canonical(v_addr, cpu.state().satp))) {
                cpu.pipeline_context.pending_exception = ExceptionCode::StorePageFault;
                cpu.pipeline_context.pending_tval = v_addr;
                return;
            }
        }
    }

    auto issue_write = [&](Address addr, Word data) {
        if (simrv::memory::is_dram_addr(addr)) {
            cpu.dcache.write(addr, data, funct3);
        }

        TlChannelA req{};
        req.opcode = TlOpcodeA::PutFullData;
        req.size = static_cast<uint8_t>(funct3 & 0x3);
        req.source = 0;  // CPU Data Bus source ID
        req.address = addr;
        const unsigned req_size_bytes = 1u << (funct3 & 0x3u);
        const unsigned bits = 8 * req_size_bytes;
        req.data = (bits < simrv::xlen::kXLenBits) ? (data & ((static_cast<Word>(1) << bits) - 1)) : data;
        mem.system_bus().send_request(req);

        TlChannelD resp{};
        if (mem.system_bus().get_response(0, resp)) {
            if (simrv::compiler::unlikely(resp.error)) {
                cpu.pipeline_context.pending_exception = ExceptionCode::FaultStore;
                cpu.pipeline_context.pending_tval = v_addr;
            }
        }
    };

    if (simrv::compiler::likely(!cpu.pipeline_context.pending_exception.has_value()) &&
        simrv::compiler::likely(eff_priv == kPrivMachine ||
                                !simrv::xlen::satp_translation_enabled(cpu.state().satp)) &&
        simrv::compiler::likely(simrv::memory::is_dram_addr(v_addr))) {
        issue_write(v_addr, wdata);
        return;
    }

    Address p_addr = 0;
    core::TLBEntry* entry =
        &cpu.tlb.data_w.at((v_addr >> simrv::memory::kPageShift) & (simrv::memory::kTlbSize - 1));
    const Word current_asid = simrv::xlen::satp_asid(cpu.state().satp);

    if (eff_priv == kPrivMachine || !simrv::xlen::satp_translation_enabled(cpu.state().satp)) {
        p_addr = v_addr;
    } else if (entry->valid && entry->asid == current_asid &&
               entry->v_addr == (v_addr & ~simrv::memory::kPageMask)) {
        p_addr = entry->p_addr + (v_addr & simrv::memory::kPageMask);
    } else {
        auto translate_result = mem.mmu()->translate(v_addr, PteAccess::Write, eff_priv,
                                                     cpu.state().mstatus, cpu.state().satp);
        if (translate_result.has_value()) {
            p_addr = translate_result.value();
            entry->v_addr = v_addr & ~simrv::memory::kPageMask;
            entry->p_addr = p_addr & ~simrv::memory::kPageMask;
            entry->asid = current_asid;
            entry->valid = true;
        } else {
            cpu.pipeline_context.pending_exception = static_cast<ExceptionCode>(translate_result.error());
            cpu.pipeline_context.pending_tval = v_addr;
        }
    }

    if (!cpu.pipeline_context.pending_exception.has_value()) {
        issue_write(p_addr, wdata);
    }
}

auto MemoryAccess::loadInt(MemorySubsystem& mem, core::CPU& cpu, Address addr, Funct3 funct3)
    -> Word {
    return target_read(mem, cpu, addr, enum_mask(funct3));
}

auto MemoryAccess::loadFp(MemorySubsystem& mem, core::CPU& cpu, Address addr, Funct3 funct3)
    -> FloatingRegister {
    const auto f3 = funct3;
    if (f3 == Funct3::Flw) {
        const Word lo = target_read(mem, cpu, addr, static_cast<Instruction>(Funct3::Lw));
        return static_cast<uint64_t>(kF32BoxerBits) | static_cast<uint64_t>(lo & kLower32Mask);
    } else if (f3 == Funct3::Fld) {
        if constexpr (simrv::xlen::kIsXLen64) {
            return static_cast<FloatingRegister>(
                target_read(mem, cpu, addr, static_cast<Instruction>(Funct3::Ld)));
        } else {
            if (simrv::compiler::unlikely((addr & 7) != 0)) {
                cpu.pipeline_context.pending_exception = ExceptionCode::MisalignedLoad;
                cpu.pipeline_context.pending_tval = addr;
                return 0;
            }
            const Word lo = target_read(mem, cpu, addr, static_cast<Instruction>(Funct3::Lw));
            const Word hi = target_read(mem, cpu, addr + 4, static_cast<Instruction>(Funct3::Lw));
            return static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
        }
    }
    return 0;
}

void MemoryAccess::storeInt(MemorySubsystem& mem, core::CPU& cpu, Address addr, Word data,
                            Funct3 funct3) {
    target_write(mem, cpu, addr, data, enum_mask(funct3));
}

void MemoryAccess::storeFp(MemorySubsystem& mem, core::CPU& cpu, Address addr,
                           FloatingRegister data, Funct3 funct3) {
    const auto f3 = funct3;
    if (f3 == Funct3::Fsw) {
        target_write(mem, cpu, addr,
                     static_cast<Word>(data & static_cast<FloatingRegister>(kLower32Mask)),
                     static_cast<Instruction>(Funct3::Sw));
    } else if (f3 == Funct3::Fsd) {
        if constexpr (simrv::xlen::kIsXLen64) {
            target_write(mem, cpu, addr, static_cast<Word>(data),
                         static_cast<Instruction>(Funct3::Sd));
        } else {
            if (simrv::compiler::unlikely((addr & 7) != 0)) {
                cpu.pipeline_context.pending_exception = ExceptionCode::MisalignedStore;
                cpu.pipeline_context.pending_tval = addr;
                return;
            }
            target_write(mem, cpu, addr,
                         static_cast<Word>(data & static_cast<FloatingRegister>(kLower32Mask)),
                         static_cast<Instruction>(Funct3::Sw));
            target_write(
                mem, cpu, addr + 4,
                static_cast<Word>((data >> 32) & static_cast<FloatingRegister>(kLower32Mask)),
                static_cast<Instruction>(Funct3::Sw));
        }
    }
}

}  // namespace simrv::memory