/**
 * @file MemoryAccess.cpp
 * @brief Typed memory access implementations.
 */
#include "simrv/memory/MemoryAccess.hpp"

#include <cstring>

#include "simrv/core/Cpu.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/Constants.hpp"

namespace simrv::memory {

auto MemoryAccess::target_read(MemorySubsystem& mem, core::CPU& cpu, Address v_addr,
                               Instruction funct3) -> Word {
    const unsigned size_bytes = 1u << (funct3 & 0x3u);
    const bool is_amo = (static_cast<Opcode>(cpu.pipeline_context.opcode) == Opcode::Amo);

    if (simrv::compiler::unlikely((v_addr & (size_bytes - 1u)) != 0)) {
        cpu.pipeline_context.pending_exception = is_amo ? enum_mask(ExceptionCode::MisalignedStore)
                                                        : enum_mask(ExceptionCode::MisalignedLoad);
        cpu.pipeline_context.pending_tval = v_addr;
        return 0;
    }

    const PrivilegeLevel eff_priv = cpu.effective_data_privilege();

    if constexpr (simrv::xlen::kIsXLen64) {
        if (eff_priv != kPrivMachine && simrv::xlen::satp_translation_enabled(cpu.state().satp)) {
            const Word mode = simrv::xlen::satp_mode(cpu.state().satp);
            bool canonical = true;
            if (mode == 8) {  // SV39
                const Word shift = 64 - 39;
                canonical = (static_cast<SignedWord>(v_addr << shift) >> shift) ==
                            static_cast<SignedWord>(v_addr);
            } else if (mode == 9) {  // SV48
                const Word shift = 64 - 48;
                canonical = (static_cast<SignedWord>(v_addr << shift) >> shift) ==
                            static_cast<SignedWord>(v_addr);
            }
            if (simrv::compiler::unlikely(!canonical)) {
                cpu.pipeline_context.pending_exception =
                    is_amo ? enum_mask(ExceptionCode::StorePageFault)
                           : enum_mask(ExceptionCode::LoadPageFault);
                cpu.pipeline_context.pending_tval = v_addr;
                return 0;
            }
        }
    }

    auto issue_read = [&](Address addr) -> Word {
        Word cached_data = 0;
        if (cpu.dcache.read(addr, cached_data, funct3)) {
            return cached_data;
        }

        const Address line_base =
            addr & ~(static_cast<Address>(simrv::cache::DCache::kLineBytes - 1u));
        std::array<Byte, simrv::cache::DCache::kLineBytes> line_data{};

        const unsigned fetch_size = (kXLenBits == 64) ? 8 : 4;
        const Instruction fetch_funct3 = (kXLenBits == 64) ? static_cast<Instruction>(Funct3::Sd)
                                                           : static_cast<Instruction>(Funct3::Sw);

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
                    cpu.pipeline_context.pending_exception = enum_mask(ExceptionCode::FaultLoad);
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

    if (simrv::compiler::likely(cpu.pipeline_context.pending_exception == kWordAllOnes) &&
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
            cpu.pipeline_context.pending_exception = translate_result.error();
            cpu.pipeline_context.pending_tval = v_addr;
        }
    }

    if (cpu.pipeline_context.pending_exception == kWordAllOnes) {
        return issue_read(p_addr);
    }
    return rdata;
}

void MemoryAccess::target_write(MemorySubsystem& mem, core::CPU& cpu, Address v_addr, Word wdata,
                                Instruction funct3) {
    const unsigned size_bytes = 1u << (funct3 & 0x3u);

    if (simrv::compiler::unlikely((v_addr & (size_bytes - 1u)) != 0)) {
        cpu.pipeline_context.pending_exception = enum_mask(ExceptionCode::MisalignedStore);
        cpu.pipeline_context.pending_tval = v_addr;
        return;
    }

    const PrivilegeLevel eff_priv = cpu.effective_data_privilege();

    if constexpr (simrv::xlen::kIsXLen64) {
        if (eff_priv != kPrivMachine && simrv::xlen::satp_translation_enabled(cpu.state().satp)) {
            const Word mode = simrv::xlen::satp_mode(cpu.state().satp);
            bool canonical = true;
            if (mode == 8) {  // SV39
                const Word shift = 64 - 39;
                canonical = (static_cast<SignedWord>(v_addr << shift) >> shift) ==
                            static_cast<SignedWord>(v_addr);
            } else if (mode == 9) {  // SV48
                const Word shift = 64 - 48;
                canonical = (static_cast<SignedWord>(v_addr << shift) >> shift) ==
                            static_cast<SignedWord>(v_addr);
            }
            if (simrv::compiler::unlikely(!canonical)) {
                cpu.pipeline_context.pending_exception = enum_mask(ExceptionCode::StorePageFault);
                cpu.pipeline_context.pending_tval = v_addr;
                return;
            }
        }
    }

    auto issue_write = [&](Address addr, Word data) {
        cpu.dcache.write(addr, data, funct3);

        TlChannelA req{};
        req.opcode = TlOpcodeA::PutFullData;
        req.size = static_cast<uint8_t>(funct3 & 0x3);
        req.source = 0;  // CPU Data Bus source ID
        req.address = addr;
        req.data = data;
        mem.system_bus().send_request(req);

        TlChannelD resp{};
        if (mem.system_bus().get_response(0, resp)) {
            if (simrv::compiler::unlikely(resp.error)) {
                cpu.pipeline_context.pending_exception = enum_mask(ExceptionCode::FaultStore);
                cpu.pipeline_context.pending_tval = v_addr;
            }
        }
    };

    if (simrv::compiler::likely(cpu.pipeline_context.pending_exception == kWordAllOnes) &&
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
            cpu.pipeline_context.pending_exception = translate_result.error();
            cpu.pipeline_context.pending_tval = v_addr;
        }
    }

    if (cpu.pipeline_context.pending_exception == kWordAllOnes) {
        issue_write(p_addr, wdata);
    }
}

auto MemoryAccess::loadInt(MemorySubsystem& mem, core::CPU& cpu, Address addr, Instruction funct3)
    -> Word {
    return target_read(mem, cpu, addr, funct3);
}

auto MemoryAccess::loadFp(MemorySubsystem& mem, core::CPU& cpu, Address addr, Instruction funct3)
    -> FloatingRegister {
    const auto f3 = static_cast<Funct3>(funct3);
    if (f3 == Funct3::Flw) {
        const Word lo = target_read(mem, cpu, addr, static_cast<Instruction>(Funct3::Lw));
        return static_cast<uint64_t>(kF32BoxerBits) | static_cast<uint64_t>(lo & kLower32Mask);
    } else if (f3 == Funct3::Fld) {
        if constexpr (simrv::xlen::kIsXLen64) {
            return static_cast<FloatingRegister>(
                target_read(mem, cpu, addr, static_cast<Instruction>(Funct3::Ld)));
        } else {
            const Word lo = target_read(mem, cpu, addr, static_cast<Instruction>(Funct3::Lw));
            const Word hi = target_read(mem, cpu, addr + 4, static_cast<Instruction>(Funct3::Lw));
            return static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
        }
    }
    return 0;
}

void MemoryAccess::storeInt(MemorySubsystem& mem, core::CPU& cpu, Address addr, Word data,
                            Instruction funct3) {
    target_write(mem, cpu, addr, data, funct3);
}

void MemoryAccess::storeFp(MemorySubsystem& mem, core::CPU& cpu, Address addr,
                           FloatingRegister data, Instruction funct3) {
    const auto f3 = static_cast<Funct3>(funct3);
    if (f3 == Funct3::Fsw) {
        target_write(mem, cpu, addr,
                     static_cast<Word>(data & static_cast<FloatingRegister>(kLower32Mask)),
                     static_cast<Instruction>(Funct3::Sw));
    } else if (f3 == Funct3::Fsd) {
        if constexpr (simrv::xlen::kIsXLen64) {
            target_write(mem, cpu, addr, static_cast<Word>(data),
                         static_cast<Instruction>(Funct3::Sd));
        } else {
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