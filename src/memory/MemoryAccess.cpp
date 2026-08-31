/**
 * @file MemoryAccess.cpp
 * @brief Typed memory access implementations.
 */
#include "simrv/memory/MemoryAccess.hpp"

#include <cstring>
#include <utility>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/Pmp.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/memory/Mmio.hpp"
#include "simrv/memory/Mmu.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Helpers.hpp"

namespace simrv::memory {

namespace {
template <typename Cache>
void release_evicted_line(TileLinkBus& bus, Cache& cache, HartId hart, TlPort port) {
    const auto evicted = cache.take_last_eviction();
    if (!evicted.has_value()) return;
    TlChannelC release{};
    release.opcode =
        evicted->state == MesiState::Modified ? TlOpcodeC::ReleaseData : TlOpcodeC::Release;
    release.report = report_for(evicted->state, TlCap::ToN);
    release.size = kTlBlockSize;
    release.source = make_tl_source(hart, port);
    release.hart = hart;
    release.address = evicted->address;
    TlChannelD acknowledgement{};
    const auto* data = evicted->state == MesiState::Modified ? &evicted->data : nullptr;
    (void)bus.release_line(release, acknowledgement, data);
}
}  // namespace

using simrv::isa::Funct3;
using simrv::isa::Funct5Amo;
using simrv::isa::Opcode;

auto MemorySubsystem::memory_geometry() const noexcept -> const simrv::core::MemoryGeometry& {
    return machine_.configuration().memory;
}

auto MemoryAccess::target_read(MemorySubsystem& mem, core::CPU& cpu, Address v_addr,
                               Instruction funct3) -> Word {
    const auto& geometry = mem.memory_geometry();
    const unsigned active_xlen = cpu.effective_data_xlen();
    if (active_xlen == 32) v_addr &= 0xFFFFFFFFULL;
    if (simrv::compiler::unlikely(cpu.active_context().pending_exception.has_value())) {
        return 0;
    }

    const unsigned size_bytes = 1u << (funct3 & 0x3u);  // unchanged
    const bool is_amo = (static_cast<Opcode>(cpu.active_context().opcode) == Opcode::Amo);
    const bool is_lr =
        is_amo && (static_cast<Funct5Amo>(cpu.active_context().funct5) == Funct5Amo::Lr);

    const bool crosses_page =
        ((v_addr & simrv::memory::kPageMask) + size_bytes) > (1u << simrv::memory::kPageShift);
    const bool crosses_cache_line = ((v_addr & (simrv::cache::DCache::kLineBytes - 1u)) +
                                     size_bytes) > simrv::cache::DCache::kLineBytes;
    if (simrv::compiler::unlikely((v_addr & (size_bytes - 1u)) != 0)) {
        if (is_amo) {
            cpu.active_context().pending_exception =
                (!is_lr) ? ExceptionCode::MisalignedStore : ExceptionCode::MisalignedLoad;
            cpu.active_context().pending_tval = v_addr;
            return 0;
        }
    }
    if (simrv::compiler::unlikely(crosses_page || crosses_cache_line)) {
        Word result = 0;
        for (unsigned b = 0; b < size_bytes; ++b) {
            Address byte_vaddr = v_addr + b;
            Word byte_val =
                target_read(mem, cpu, byte_vaddr, static_cast<Instruction>(isa::Funct3::Lbu));
            if (cpu.ca_state.waiting_for_interconnect) return 0;
            if (cpu.active_context().pending_exception.has_value()) {
                cpu.active_context().pending_tval = v_addr;
                return 0;
            }
            result |= (byte_val & 0xFFULL) << (8 * b);
        }
        const unsigned bits = 8 * size_bytes;
        if (bits < simrv::xlen::kXLenBits) {
            const Word mask = (static_cast<Word>(1) << bits) - 1;
            result &= mask;
            constexpr auto kSignExtendBit = 0x4u;
            if ((funct3 & kSignExtendBit) == 0) {
                const Word sign_bit = static_cast<Word>(1) << (bits - 1);
                if ((result & sign_bit) != 0) {
                    result |= ~mask;
                }
            }
        }
        return static_cast<Word>(result & simrv::xlen::kXLenMask);
    }

    const PrivilegeLevel eff_priv = cpu.effective_data_privilege();
    const Word current_asid = simrv::xlen::satp_asid(cpu.state().satp, active_xlen);
    const bool translation_enabled =
        (eff_priv != kPrivMachine &&
         simrv::xlen::satp_translation_enabled(cpu.state().satp, active_xlen));

    if constexpr (simrv::xlen::kIsXLen64) {
        if (translation_enabled) {
            if (simrv::compiler::unlikely(
                    !simrv::Mmu::is_canonical(v_addr, cpu.state().satp, active_xlen))) {
                cpu.active_context().pending_exception =
                    is_amo ? ExceptionCode::StorePageFault : ExceptionCode::LoadPageFault;
                cpu.active_context().pending_tval = v_addr;
                return 0;
            }
        }
    }

    if (cpu.machine_->runtime_profile.is_instruction_mode() && !crosses_page) {
        if (!translation_enabled) {
            // Bypass soft TLB lookup if translation is disabled (direct DRAM access)
            const Address eff_vaddr = (active_xlen == 32) ? (v_addr & 0xFFFFFFFFULL) : v_addr;
            if (simrv::compiler::likely(geometry.contains(eff_vaddr, size_bytes))) {
                if (simrv::compiler::unlikely(!core::pmp::check_access(
                        cpu.state(), eff_vaddr, size_bytes, core::PmpAccessType::Read, eff_priv))) {
                    cpu.active_context().pending_exception =
                        is_amo ? ExceptionCode::FaultStore : ExceptionCode::FaultLoad;
                    cpu.active_context().pending_tval = v_addr;
                    return 0;
                }
                return simrv::memory::ram_read_fast(eff_vaddr, funct3, cpu.machine_->ram_view());
            }
        } else {
            const Address vpn = v_addr >> 12;
            const size_t tlb_idx = static_cast<size_t>(vpn) & 2047u;
            const auto& entry = cpu.soft_tlb_read[tlb_idx];
            if (simrv::compiler::likely(
                    entry.matches(vpn, current_asid, eff_priv, cpu.soft_tlb_epoch))) {
                if (simrv::compiler::likely(entry.host_ptr_base != nullptr)) {
                    return simrv::memory::host_read_fast(entry.host_ptr_base + (v_addr & 0xFFF),
                                                         funct3);
                }
                return simrv::memory::ram_read_fast(entry.paddr_base + (v_addr & 0xFFF), funct3,
                                                    cpu.machine_->ram_view());
            }
        }
    }

    auto issue_read = [&](Address addr) -> Word {
        if (cpu.machine_->runtime_profile.is_instruction_mode() &&
            geometry.contains(addr, size_bytes)) {
            return simrv::memory::ram_read_fast(addr, funct3, cpu.machine_->ram_view());
        }
        if (!geometry.contains(addr, size_bytes)) {
            auto& transfer = cpu.ca_state.data_transfer;
            if (cpu.machine_->runtime_profile.is_cycle_mode() && transfer.active) {
                TileLinkBus::TimedResponse timed{};
                if (!mem.system_bus().try_get_timed_response(transfer.source, timed)) {
                    cpu.ca_state.waiting_for_interconnect = true;
                    return 0;
                }
                transfer.reset();
                if (simrv::compiler::unlikely(timed.payload.failed())) {
                    cpu.active_context().pending_exception = ExceptionCode::FaultLoad;
                    cpu.active_context().pending_tval = v_addr;
                    return 0;
                }
                Word rdata = timed.payload.data;
                const unsigned req_size_bytes = 1u << (funct3 & 0x3u);
                const unsigned bits = 8 * req_size_bytes;
                if (bits < simrv::xlen::kXLenBits) {
                    const Word mask = (static_cast<Word>(1) << bits) - 1;
                    rdata &= mask;
                    constexpr auto kSignExtendBit = 0x4u;
                    if ((funct3 & kSignExtendBit) == 0) {
                        const Word sign_bit = static_cast<Word>(1) << (bits - 1);
                        if ((rdata & sign_bit) != 0) rdata |= ~mask;
                    }
                }
                return static_cast<Word>(rdata & simrv::xlen::kXLenMask);
            }
            TlChannelA req{};
            req.opcode = TlOpcodeA::Get;
            const unsigned req_size = static_cast<uint8_t>(funct3 & 0x3);
            const unsigned req_size_bytes = 1u << req_size;
            const bool is_aligned = (addr & (req_size_bytes - 1u)) == 0;

            req.hart = static_cast<HartId>(cpu.state().mhartid);
            req.source = make_tl_source(req.hart, TlPort::Data);
            if (is_aligned) {
                req.size = static_cast<uint8_t>(req_size);
                req.address = addr;
            } else {
                req.size = kTlBeatSize;
                req.address = addr & ~(static_cast<Address>(kTlBeatBytes - 1u));
            }
            mem.system_bus().send_request(req);

            if (cpu.machine_->runtime_profile.is_cycle_mode()) {
                transfer = {
                    .address = addr, .source = req.source, .active = true, .is_write = false};
                cpu.ca_state.waiting_for_interconnect = true;
                return 0;
            }

            TlChannelD resp{};
            if (mem.system_bus().get_response(req.source, resp)) {
                if (simrv::compiler::unlikely(resp.failed())) {
                    cpu.active_context().pending_exception = ExceptionCode::FaultLoad;
                    cpu.active_context().pending_tval = v_addr;
                }
                Word rdata = resp.data;
                if (!is_aligned) {
                    const unsigned lane_offset = static_cast<unsigned>(addr & (kTlBeatBytes - 1u));
                    rdata >>= (lane_offset * 8u);
                }
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
            cpu.active_context().pending_exception = ExceptionCode::FaultLoad;
            cpu.active_context().pending_tval = v_addr;
            return 0;
        }

        auto& transfer = cpu.ca_state.data_transfer;
        Word cached_data = 0;
        const auto read_refill_data = [&](const auto& data, Address line_base) -> Word {
            const auto byte_offset = static_cast<size_t>(addr - line_base);
            Word raw = 0;
            std::memcpy(&raw, data.data() + byte_offset, size_bytes);
            return simrv::memory::extend_loaded_value(raw, static_cast<uint8_t>(funct3));
        };
        // A resumable line fill represents one cache miss. Do not probe the cache again while
        // that request is in flight, otherwise both miss counters and modeled penalties grow
        // once per waiting cycle.
        if (cpu.machine_->runtime_profile.is_cycle_mode() && transfer.active &&
            transfer.line_fill) {
            TileLinkBus::TimedResponse timed{};
            if (!mem.system_bus().try_get_timed_response(transfer.source, timed)) {
                cpu.ca_state.waiting_for_interconnect = true;
                return 0;
            }
            const Address completed_line = transfer.address;
            transfer.reset();
            if (timed.payload.failed() || !timed.has_line_data) {
                cpu.active_context().pending_exception = ExceptionCode::FaultLoad;
                cpu.active_context().pending_tval = v_addr;
                return 0;
            }
            cpu.dcache.insert(completed_line, timed.line_data.data(), mesi_for(timed.payload.cap));
            release_evicted_line(mem.system_bus(), cpu.dcache,
                                 static_cast<HartId>(cpu.state().mhartid), TlPort::Data);
            TlChannelE ack{};
            ack.sink = timed.payload.sink;
            mem.system_bus().grant_ack(ack);
            const Address completed_offset = addr - completed_line;
            if (addr >= completed_line && completed_offset < simrv::cache::DCache::kLineBytes &&
                size_bytes <= simrv::cache::DCache::kLineBytes - completed_offset) {
                return read_refill_data(timed.line_data, completed_line);
            }
            // A prior split or write transaction can complete before this access resumes. Its
            // insertion is not this access's refill, so continue with a normal lookup/request.
        }

        if (cpu.dcache.read(addr, cached_data, funct3)) {
            return cached_data;
        }

        const Address line_base =
            addr & ~(static_cast<Address>(simrv::cache::DCache::kLineBytes - 1u));
        std::array<Byte, simrv::cache::DCache::kLineBytes> line_data{};

        TlChannelA req{};
        req.opcode = TlOpcodeA::AcquireBlock;
        req.grow = TlGrow::NtoB;
        req.size = simrv::cache::DCache::kLineShift;
        req.hart = static_cast<HartId>(cpu.state().mhartid);
        req.source = make_tl_source(req.hart, TlPort::Data);
        req.address = line_base;

        if (cpu.machine_->runtime_profile.is_cycle_mode()) {
            mem.system_bus().send_request(req);
            transfer = {.address = line_base,
                        .source = req.source,
                        .active = true,
                        .is_write = false,
                        .line_fill = true};
            cpu.ca_state.waiting_for_interconnect = true;
            return 0;
        }

        TlChannelD resp{};
        if (mem.system_bus().acquire_block(req, resp, line_data)) {
            cpu.dcache.insert(line_base, line_data.data(), mesi_for(resp.cap));
            release_evicted_line(mem.system_bus(), cpu.dcache,
                                 static_cast<HartId>(cpu.state().mhartid), TlPort::Data);
            TlChannelE ack{};
            ack.sink = resp.sink;
            mem.system_bus().grant_ack(ack);
            return read_refill_data(line_data, line_base);
        }
        return simrv::memory::ram_read_fast(addr, funct3, cpu.machine_->ram_view());
    };

    const Address eff_vaddr = (active_xlen == 32) ? (v_addr & 0xFFFFFFFFULL) : v_addr;

    if (simrv::compiler::likely(!cpu.active_context().pending_exception.has_value()) &&
        simrv::compiler::likely(eff_priv == kPrivMachine || !simrv::xlen::satp_translation_enabled(
                                                                cpu.state().satp, active_xlen)) &&
        simrv::compiler::likely(geometry.contains(eff_vaddr, size_bytes))) {
        return issue_read(eff_vaddr);
    }

    Word rdata = 0;
    Address p_addr = 0;
    const PteAccess access_type = (is_amo && !is_lr) ? PteAccess::Write : PteAccess::Read;
    core::TLBEntry* entry = (is_amo && !is_lr)
                                ? cpu.tlb.lookup_data_w(v_addr, current_asid, eff_priv)
                                : cpu.tlb.lookup_data_r(v_addr, current_asid, eff_priv);

    if (eff_priv == kPrivMachine ||
        !simrv::xlen::satp_translation_enabled(cpu.state().satp, active_xlen)) {
        p_addr = eff_vaddr;
    } else if (entry) {
        p_addr = entry->p_addr + (v_addr & simrv::memory::kPageMask);
    } else {
        cpu.active_context().tlb_miss = true;
        auto translate_res =
            cpu.translate_stage_address(*cpu.machine_, v_addr, access_type, eff_priv, active_xlen,
                                        TlPort::Data, cpu.ca_state.data_walk);
        if (!translate_res.has_value()) return 0;
        auto chain_res = (*translate_res)
                             .and_then([&](PhysAddr phys) -> std::expected<void, TrapCause> {
                                 p_addr = phys.raw();
                                 cpu.tlb.insert_data_r(v_addr, p_addr, current_asid, eff_priv);
                                 if (is_amo && !is_lr) {
                                     cpu.tlb.insert_data_w(v_addr, p_addr, current_asid, eff_priv);
                                 }
                                 return {};
                             })
                             .or_else([&](TrapCause error) -> std::expected<void, TrapCause> {
                                 cpu.active_context().pending_exception =
                                     static_cast<ExceptionCode>(error);
                                 cpu.active_context().pending_tval = v_addr;
                                 return {};
                             });
        (void)chain_res;
    }

    if (!cpu.active_context().pending_exception.has_value()) {
        if (simrv::compiler::unlikely(!core::pmp::check_access(
                cpu.state(), p_addr, size_bytes,
                (is_amo && !is_lr) ? core::PmpAccessType::Write : core::PmpAccessType::Read,
                eff_priv))) {
            cpu.active_context().pending_exception =
                is_amo ? ExceptionCode::FaultStore : ExceptionCode::FaultLoad;
            cpu.active_context().pending_tval = v_addr;
            return 0;
        }
        if (cpu.machine_->runtime_profile.is_instruction_mode() && geometry.contains(p_addr)) {
            const size_t tlb_idx = (v_addr >> 12) & 2047;
            const Address vpn = v_addr >> 12;
            Byte* host_base = cpu.machine_->ram_view().unchecked_ptr(p_addr & ~0xFFFULL);
            cpu.soft_tlb_read[tlb_idx].set(
                vpn, translation_enabled ? static_cast<uint64_t>(current_asid) : ~uint64_t{0},
                eff_priv, cpu.soft_tlb_epoch, p_addr & ~0xFFFULL, host_base);
            if (cpu.soft_tlb_write[tlb_idx].paddr_base != (p_addr & ~0xFFFULL)) {
                cpu.soft_tlb_write[tlb_idx].invalidate();
            }
        }
        return issue_read(p_addr);
    }
    return rdata;
}

void MemoryAccess::target_write(MemorySubsystem& mem, core::CPU& cpu, Address v_addr, Word wdata,
                                Instruction funct3) {
    const auto& geometry = mem.memory_geometry();
    const unsigned active_xlen = cpu.effective_data_xlen();
    if (active_xlen == 32) v_addr &= 0xFFFFFFFFULL;
    if (simrv::compiler::unlikely(cpu.active_context().pending_exception.has_value())) {
        return;
    }

    const unsigned size_bytes = 1u << (funct3 & 0x3u);

    const bool crosses_page =
        ((v_addr & simrv::memory::kPageMask) + size_bytes) > (1u << simrv::memory::kPageShift);
    const bool crosses_cache_line = ((v_addr & (simrv::cache::DCache::kLineBytes - 1u)) +
                                     size_bytes) > simrv::cache::DCache::kLineBytes;
    const bool is_amo = (static_cast<Opcode>(cpu.active_context().opcode) == Opcode::Amo);
    if (simrv::compiler::unlikely((v_addr & (size_bytes - 1u)) != 0)) {
        if (is_amo) {
            cpu.active_context().pending_exception = ExceptionCode::MisalignedStore;
            cpu.active_context().pending_tval = v_addr;
            return;
        }
    }
    if (simrv::compiler::unlikely(crosses_page || crosses_cache_line)) {
        for (unsigned b = 0; b < size_bytes; ++b) {
            Address byte_vaddr = v_addr + b;
            Word byte_val = (wdata >> (8 * b)) & 0xFFULL;
            target_write(mem, cpu, byte_vaddr, byte_val, static_cast<Instruction>(isa::Funct3::Sb));
            if (cpu.ca_state.waiting_for_interconnect) return;
            if (cpu.active_context().pending_exception.has_value()) {
                cpu.active_context().pending_tval = v_addr;
                return;
            }
        }
        return;
    }

    const PrivilegeLevel eff_priv = cpu.effective_data_privilege();
    const Word current_asid = simrv::xlen::satp_asid(cpu.state().satp, active_xlen);
    const bool translation_enabled =
        (eff_priv != kPrivMachine &&
         simrv::xlen::satp_translation_enabled(cpu.state().satp, active_xlen));

    if constexpr (simrv::xlen::kIsXLen64) {
        if (translation_enabled) {
            if (simrv::compiler::unlikely(
                    !simrv::Mmu::is_canonical(v_addr, cpu.state().satp, active_xlen))) {
                cpu.active_context().pending_exception = ExceptionCode::StorePageFault;
                cpu.active_context().pending_tval = v_addr;
                return;
            }
        }
    }

    auto issue_write = [&](Address addr, Word data) -> void {
        auto& transfer = cpu.ca_state.data_transfer;
        if (cpu.machine_->runtime_profile.is_cycle_mode() && transfer.active &&
            transfer.line_fill) {
            TileLinkBus::TimedResponse timed{};
            if (!mem.system_bus().try_get_timed_response(transfer.source, timed)) {
                cpu.ca_state.waiting_for_interconnect = true;
                return;
            }
            const Address completed_line = transfer.address;
            transfer.reset();
            if (timed.payload.failed() || !timed.has_line_data) {
                cpu.active_context().pending_exception = ExceptionCode::FaultStore;
                cpu.active_context().pending_tval = v_addr;
                return;
            }
            cpu.dcache.insert(completed_line, timed.line_data.data(), mesi_for(timed.payload.cap));
            release_evicted_line(mem.system_bus(), cpu.dcache,
                                 static_cast<HartId>(cpu.state().mhartid), TlPort::Data);
            TlChannelE ack{};
            ack.sink = timed.payload.sink;
            mem.system_bus().grant_ack(ack);
        }
        const bool is_tohost_write = simrv::xlen::kIsXLen64
                                         ? (funct3 == static_cast<Instruction>(Funct3::Sw) ||
                                            funct3 == static_cast<Instruction>(Funct3::Sd))
                                         : (funct3 == static_cast<Instruction>(Funct3::Sw));
        if (simrv::compiler::unlikely(is_tohost_write)) {
            if (addr == cpu.machine_->configuration().isa.isatest_tohost || addr == 0x80001000 ||
                addr == 0x40008000) {
                cpu.machine_->tohost =
                    simrv::xlen::kIsXLen64
                        ? data
                        : ((cpu.machine_->tohost & 0xFFFFFFFF00000000ULL) | data);
            } else if (!simrv::xlen::kIsXLen64 &&
                       (addr == cpu.machine_->configuration().isa.isatest_tohost + 4 ||
                        addr == 0x80001004 || addr == 0x40008004)) {
                cpu.machine_->tohost = (cpu.machine_->tohost & 0x00000000FFFFFFFFULL) |
                                       (static_cast<uint64_t>(data) << 32);
            }
        }
        if (cpu.machine_->runtime_profile.is_instruction_mode() &&
            geometry.contains(addr, size_bytes)) {
            simrv::memory::ram_write_fast(addr, data, funct3, cpu.machine_->ram_view());
            return;
        }
        if (geometry.contains(addr, size_bytes)) {
            bool cache_write_completed = cpu.dcache.write(addr, data, funct3);
            if (!cache_write_completed) {
                // Not in Trunk state; acquire Trunk ownership via TL-C
                const Address line_base =
                    addr & ~(static_cast<Address>(simrv::cache::DCache::kLineBytes - 1u));
                std::array<Byte, simrv::cache::DCache::kLineBytes> line_data{};
                TlChannelA req{};
                req.opcode = TlOpcodeA::AcquireBlock;
                req.grow = TlGrow::NtoT;
                req.size = simrv::cache::DCache::kLineShift;
                req.hart = static_cast<HartId>(cpu.state().mhartid);
                req.source = make_tl_source(req.hart, TlPort::Data);
                req.address = line_base;

                if (cpu.machine_->runtime_profile.is_cycle_mode()) {
                    mem.system_bus().send_request(req);
                    transfer = {.address = line_base,
                                .source = req.source,
                                .active = true,
                                .is_write = true,
                                .line_fill = true};
                    cpu.ca_state.waiting_for_interconnect = true;
                    return;
                }

                TlChannelD resp{};
                if (mem.system_bus().acquire_block(req, resp, line_data)) {
                    cpu.dcache.insert(line_base, line_data.data(), mesi_for(resp.cap));
                    release_evicted_line(mem.system_bus(), cpu.dcache,
                                         static_cast<HartId>(cpu.state().mhartid), TlPort::Data);
                    TlChannelE ack{};
                    ack.sink = resp.sink;
                    mem.system_bus().grant_ack(ack);
                    cache_write_completed = cpu.dcache.write(addr, data, funct3);
                }
            }
            if (cache_write_completed) {
                mem.system_bus().mark_modified(addr, static_cast<HartId>(cpu.state().mhartid));
            }
            simrv::memory::ram_write_fast(addr, data, funct3, cpu.machine_->ram_view());
            return;
        }

        TlChannelA req{};
        const unsigned req_size = static_cast<uint8_t>(funct3 & 0x3);
        const unsigned req_size_bytes = 1u << req_size;
        const bool is_aligned = (addr & (req_size_bytes - 1u)) == 0;

        req.hart = static_cast<HartId>(cpu.state().mhartid);
        req.source = make_tl_source(req.hart, TlPort::Data);
        if (is_aligned) {
            req.opcode = TlOpcodeA::PutFullData;
            req.size = static_cast<uint8_t>(req_size);
            req.address = addr;
            req.mask = TlChannelA::compute_mask(req.size, req.address);
        } else {
            req.opcode = TlOpcodeA::PutPartialData;
            req.size = kTlBeatSize;
            req.address = addr & ~(static_cast<Address>(kTlBeatBytes - 1u));
            const unsigned lane_offset = static_cast<unsigned>(addr & (kTlBeatBytes - 1u));
            req.mask = ((TlMask{1} << req_size_bytes) - 1u) << lane_offset;
        }
        const unsigned bits = 8 * req_size_bytes;
        req.data =
            (bits < simrv::xlen::kXLenBits) ? (data & ((static_cast<Word>(1) << bits) - 1)) : data;
        if (!is_aligned) {
            const unsigned lane_offset = static_cast<unsigned>(addr & (kTlBeatBytes - 1u));
            req.data <<= (lane_offset * 8u);
        }
        if (cpu.machine_->runtime_profile.is_cycle_mode() && transfer.active) {
            TileLinkBus::TimedResponse timed{};
            if (!mem.system_bus().try_get_timed_response(transfer.source, timed)) {
                cpu.ca_state.waiting_for_interconnect = true;
                return;
            }
            transfer.reset();
            if (simrv::compiler::unlikely(timed.payload.failed())) {
                cpu.active_context().pending_exception = ExceptionCode::FaultStore;
                cpu.active_context().pending_tval = v_addr;
            }
            return;
        }
        mem.system_bus().send_request(req);
        if (cpu.machine_->runtime_profile.is_cycle_mode()) {
            transfer = {.address = addr, .source = req.source, .active = true, .is_write = true};
            cpu.ca_state.waiting_for_interconnect = true;
            return;
        }

        TlChannelD resp{};
        if (mem.system_bus().get_response(req.source, resp)) {
            if (simrv::compiler::unlikely(resp.failed())) {
                cpu.active_context().pending_exception = ExceptionCode::FaultStore;
                cpu.active_context().pending_tval = v_addr;
            }
        } else {
            cpu.active_context().pending_exception = ExceptionCode::FaultStore;
            cpu.active_context().pending_tval = v_addr;
        }
    };

    if (cpu.machine_->runtime_profile.is_instruction_mode() && !crosses_page) {
        if (!translation_enabled) {
            // Bypass soft TLB lookup if translation is disabled (direct DRAM access)
            const Address eff_vaddr = (active_xlen == 32) ? (v_addr & 0xFFFFFFFFULL) : v_addr;
            if (simrv::compiler::likely(geometry.contains(eff_vaddr, size_bytes))) {
                issue_write(eff_vaddr, wdata);
                return;
            }
        } else {
            const Address vpn = v_addr >> 12;
            const size_t tlb_idx = static_cast<size_t>(vpn) & 2047u;
            const auto& entry = cpu.soft_tlb_write[tlb_idx];
            if (simrv::compiler::likely(
                    entry.matches(vpn, current_asid, eff_priv, cpu.soft_tlb_epoch))) {
                if (simrv::compiler::likely(entry.host_ptr_base != nullptr)) {
                    simrv::memory::host_write_fast(entry.host_ptr_base + (v_addr & 0xFFF), wdata,
                                                   funct3);
                    return;
                }
                issue_write(entry.paddr_base + (v_addr & 0xFFF), wdata);
                return;
            }
        }
    }

    const Address eff_vaddr = (active_xlen == 32) ? (v_addr & 0xFFFFFFFFULL) : v_addr;

    if (simrv::compiler::likely(!cpu.active_context().pending_exception.has_value()) &&
        simrv::compiler::likely(eff_priv == kPrivMachine || !simrv::xlen::satp_translation_enabled(
                                                                cpu.state().satp, active_xlen)) &&
        simrv::compiler::likely(geometry.contains(eff_vaddr, size_bytes))) {
        if (simrv::compiler::unlikely(!core::pmp::check_access(
                cpu.state(), eff_vaddr, size_bytes, core::PmpAccessType::Write, eff_priv))) {
            cpu.active_context().pending_exception = ExceptionCode::FaultStore;
            cpu.active_context().pending_tval = v_addr;
            return;
        }
        issue_write(eff_vaddr, wdata);
        return;
    }

    Address p_addr = 0;
    core::TLBEntry* entry = cpu.tlb.lookup_data_w(v_addr, current_asid, eff_priv);

    if (eff_priv == kPrivMachine ||
        !simrv::xlen::satp_translation_enabled(cpu.state().satp, active_xlen)) {
        p_addr = eff_vaddr;
    } else if (entry) {
        p_addr = entry->p_addr + (v_addr & simrv::memory::kPageMask);
    } else {
        cpu.active_context().tlb_miss = true;
        auto translate_res =
            cpu.translate_stage_address(*cpu.machine_, v_addr, PteAccess::Write, eff_priv,
                                        active_xlen, TlPort::Data, cpu.ca_state.data_walk);
        if (!translate_res.has_value()) return;
        auto chain_res = (*translate_res)
                             .and_then([&](PhysAddr phys) -> std::expected<void, TrapCause> {
                                 p_addr = phys.raw();
                                 cpu.tlb.insert_data_w(v_addr, p_addr, current_asid, eff_priv);
                                 return {};
                             })
                             .or_else([&](TrapCause error) -> std::expected<void, TrapCause> {
                                 cpu.active_context().pending_exception =
                                     static_cast<ExceptionCode>(error);
                                 cpu.active_context().pending_tval = v_addr;
                                 return {};
                             });
        (void)chain_res;
    }

    if (!cpu.active_context().pending_exception.has_value()) {
        if (simrv::compiler::unlikely(!core::pmp::check_access(
                cpu.state(), p_addr, size_bytes, core::PmpAccessType::Write, eff_priv))) {
            cpu.active_context().pending_exception = ExceptionCode::FaultStore;
            cpu.active_context().pending_tval = v_addr;
            return;
        }
        if (cpu.machine_->runtime_profile.is_instruction_mode() && geometry.contains(p_addr)) {
            const size_t tlb_idx = (v_addr >> 12) & 2047;
            const Address vpn = v_addr >> 12;
            Byte* host_base = cpu.machine_->ram_view().unchecked_ptr(p_addr & ~0xFFFULL);
            cpu.soft_tlb_write[tlb_idx].set(
                vpn, translation_enabled ? static_cast<uint64_t>(current_asid) : ~uint64_t{0},
                eff_priv, cpu.soft_tlb_epoch, p_addr & ~0xFFFULL, host_base);
            if (cpu.soft_tlb_read[tlb_idx].paddr_base != (p_addr & ~0xFFFULL)) {
                cpu.soft_tlb_read[tlb_idx].invalidate();
            }
        }
        issue_write(p_addr, wdata);
    }
}

auto MemoryAccess::loadInt(MemorySubsystem& mem, core::CPU& cpu, Address addr, isa::Funct3 funct3)
    -> Word {
    return target_read(mem, cpu, addr, enum_mask(funct3));
}

auto MemoryAccess::loadFp(MemorySubsystem& mem, core::CPU& cpu, Address addr, isa::Funct3 funct3)
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
            const Word lo = target_read(mem, cpu, addr, static_cast<Instruction>(Funct3::Lw));
            const Word hi = target_read(mem, cpu, addr + 4, static_cast<Instruction>(Funct3::Lw));
            return static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
        }
    }
    return 0;
}

void MemoryAccess::storeInt(MemorySubsystem& mem, core::CPU& cpu, Address addr, Word data,
                            isa::Funct3 funct3) {
    target_write(mem, cpu, addr, data, enum_mask(funct3));
}

void MemoryAccess::storeFp(MemorySubsystem& mem, core::CPU& cpu, Address addr,
                           FloatingRegister data, isa::Funct3 funct3) {
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
