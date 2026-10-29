/**
 * @file MemoryAccess.cpp
 * @brief Typed memory access implementations.
 */
#include "simrv/memory/MemoryAccess.hpp"

#include <cstring>
#include <format>
#include <utility>

#include "simrv/CompilerHints.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/Pmp.hpp"
#include "simrv/debug/BreakpointManager.hpp"
#include "simrv/debug/GdbStub.hpp"
#include "simrv/debug/SpikeLockstep.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/memory/Mmu.hpp"
#include "simrv/xlen/Constants.hpp"

namespace simrv::memory {

namespace {
class HypervisorMemoryAccessScope {
   public:
    explicit HypervisorMemoryAccessScope(core::CPU& cpu) noexcept
        : cpu_(cpu), previous_(cpu.hypervisor_memory_access_active()),
          hypervisor_operation_(isa::is_hypervisor_memory_op(cpu.active_context().op_id)) {
        if (hypervisor_operation_) {
            if (!previous_) cpu_.active_context().hypervisor_memory_access_fault = false;
            cpu_.set_hypervisor_memory_access_active(true);
        }
    }
    ~HypervisorMemoryAccessScope() {
        if (hypervisor_operation_ && cpu_.active_context().pending_exception.has_value()) {
            cpu_.active_context().hypervisor_memory_access_fault = true;
        }
        cpu_.set_hypervisor_memory_access_active(previous_);
    }

   private:
    core::CPU& cpu_;
    bool previous_;
    bool hypervisor_operation_;
};

[[nodiscard]] auto apply_user_pointer_mask(core::CPU& cpu, simrv::xlen::VirtAddr address,
                                           unsigned active_xlen) -> simrv::xlen::VirtAddr {
    const auto op = cpu.active_context().op_id;
    const bool hypervisor_access = isa::is_hypervisor_memory_op(op);
    if (active_xlen != 64 || isa::is_hlvx_op(op)) return address;
    constexpr uint64_t kPmmMask = uint64_t{0x3} << 32U;
    uint64_t pmm = 0;
    if (hypervisor_access) {
        constexpr simrv::xlen::CsrValue kSpvp = simrv::xlen::CsrValue{1} << 8U;
        constexpr uint64_t kHupmmMask = uint64_t{0x3} << 48U;
        if ((cpu.state().hstatus & kSpvp) != 0) {
            pmm = (static_cast<uint64_t>(cpu.state().henvcfg) & kPmmMask) >> 32U;
        } else if (cpu.state().priv == simrv::xlen::PrivilegeLevel::User) {
            pmm = (static_cast<uint64_t>(cpu.state().hstatus) & kHupmmMask) >> 48U;
        } else {
            pmm = (static_cast<uint64_t>(cpu.state().senvcfg) & kPmmMask) >> 32U;
        }
    } else {
        if (cpu.effective_data_privilege() != simrv::xlen::PrivilegeLevel::User) return address;
        pmm = (static_cast<uint64_t>(cpu.state().senvcfg) & kPmmMask) >> 32U;
    }
    if (pmm != 2U) return address;  // RVA23 requires PMLEN=0 and PMLEN=7.

    constexpr uint64_t kLow57Mask = (uint64_t{1} << 57U) - 1U;
    const uint64_t low = address.raw() & kLow57Mask;
    const bool translated = hypervisor_access
                                ? (simrv::xlen::satp_mode(cpu.state().vsatp, active_xlen) != 0 ||
                                   simrv::xlen::satp_mode(cpu.state().hgatp, active_xlen) != 0)
                                : (cpu.effective_data_privilege() !=
                                       simrv::xlen::PrivilegeLevel::Machine &&
                                   simrv::xlen::satp_mode(cpu.active_satp(), active_xlen) != 0);
    const uint64_t upper = translated && ((low >> 56U) & 1U) != 0 ? ~kLow57Mask : 0;
    return simrv::xlen::VirtAddr{static_cast<simrv::xlen::Word>(low | upper)};
}

template <typename Cache>
void release_evicted_line(TileLinkBus& bus, Cache& cache, simrv::xlen::HartId hart, TlPort port,
                          core::CPU& cpu) {
    const auto evicted = cache.take_last_eviction();
    if (!evicted.has_value()) return;
    if (evicted->state == MesiState::Modified) {
        cpu.record_hpm_event(core::HpmEvent::DCacheWriteback);
    }
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

auto MemoryAccess::mask_user_pointer(core::CPU& cpu, simrv::xlen::VirtAddr address)
    -> simrv::xlen::VirtAddr {
    const unsigned active_xlen = cpu.effective_data_xlen();
    if (active_xlen == 32) address &= 0xFFFFFFFFULL;
    return apply_user_pointer_mask(cpu, address, active_xlen);
}

auto MemoryAccess::target_read(MemorySubsystem& mem, core::CPU& cpu, simrv::xlen::VirtAddr v_addr,
                               isa::Funct3 funct3) -> simrv::xlen::Word {
    HypervisorMemoryAccessScope hypervisor_scope(cpu);
    const auto& geometry = mem.memory_geometry();
    const unsigned active_xlen = cpu.effective_data_xlen();
    if (active_xlen == 32) v_addr &= 0xFFFFFFFFULL;
    v_addr = apply_user_pointer_mask(cpu, v_addr, active_xlen);
    if (simrv::compiler::unlikely(cpu.active_context().pending_exception.has_value())) {
        return 0;
    }

    const unsigned size_bytes = 1u << (std::to_underlying(funct3) & 0x3u);  // unchanged
    const bool is_amo = (static_cast<Opcode>(cpu.active_context().opcode) == Opcode::Amo);
    const bool is_lr = is_amo && (cpu.active_context().funct5 == Funct5Amo::Lr);
    const bool hypervisor_access =
        isa::is_hypervisor_memory_op(cpu.active_context().op_id);
    simrv::memory::PtePbmt pbmt = simrv::memory::PtePbmt::Pma;

    const bool crosses_page =
        ((v_addr & simrv::memory::kPageMask) + size_bytes) > (1u << simrv::memory::kPageShift);
    const bool crosses_cache_line = ((v_addr & (simrv::cache::DCache::kLineBytes - 1u)) +
                                     size_bytes) > simrv::cache::DCache::kLineBytes;
    if (simrv::compiler::unlikely((v_addr & (size_bytes - 1u)) != 0)) {
        if (is_amo) {
            cpu.active_context().pending_exception =
                (!is_lr) ? ExceptionCode::FaultStore : ExceptionCode::FaultLoad;
            cpu.active_context().pending_tval = v_addr.raw();
            return 0;
        }
    }
    if (simrv::compiler::unlikely(cpu.machine_->breakpoint_manager().has_any())) {
        if (auto hit =
                cpu.machine_->breakpoint_manager().check_mem_read(v_addr.raw(), size_bytes)) {
            if (cpu.machine_->debugger() && cpu.machine_->debugger()->is_connected()) {
                cpu.machine_->debug_watch_hit(
                    cpu.hart_id(), GdbSignal::SigTrap,
                    std::format("{}:{:x};",
                                hit->watch_type == debug::WatchType::Access ? "awatch" : "rwatch",
                                v_addr.raw()),
                    hit->description);
            } else if (cpu.machine_->tui_enabled()) {
                cpu.machine_->debug_watch_hit(cpu.hart_id(), GdbSignal::SigTrap, {},
                                              hit->description);
            }
        }
    }
    if (simrv::compiler::unlikely(cpu.check_triggers_mem(v_addr.raw(), size_bytes, false))) {
        cpu.active_context().pending_exception = ExceptionCode::Breakpoint;
        cpu.active_context().pending_tval = v_addr.raw();
        return 0;
    }
    if (simrv::compiler::unlikely(crosses_page || crosses_cache_line)) {
        simrv::xlen::Word result = 0;
        for (unsigned b = 0; b < size_bytes; ++b) {
            simrv::xlen::VirtAddr byte_vaddr = v_addr + b;
            simrv::xlen::Word byte_val = target_read(mem, cpu, byte_vaddr, isa::Funct3::Lbu);
            if (cpu.ca_state.waiting_for_interconnect) return 0;
            if (cpu.active_context().pending_exception.has_value()) {
                return 0;
            }
            result |= (byte_val & 0xFFULL) << (8 * b);
        }
        return simrv::memory::extend_loaded_value(result, funct3);
    }

    const simrv::xlen::PrivilegeLevel eff_priv = cpu.effective_data_privilege();
    const simrv::xlen::Word active_satp = cpu.active_satp();
    const simrv::xlen::Word current_asid = simrv::xlen::satp_asid(active_satp, active_xlen);
    const bool translation_enabled =
        (eff_priv != kPrivMachine &&
         (simrv::xlen::satp_translation_enabled(active_satp, active_xlen) || hypervisor_access));

    if constexpr (simrv::xlen::kIsXLen64) {
        if (translation_enabled) {
            if (simrv::compiler::unlikely(
                    !simrv::Mmu::is_canonical(v_addr, active_satp, active_xlen))) {
                cpu.active_context().pending_exception = (is_amo && !is_lr)
                                                             ? ExceptionCode::StorePageFault
                                                             : ExceptionCode::LoadPageFault;
                cpu.active_context().pending_tval = v_addr.raw();
                return 0;
            }
        }
    }

    const auto pmp_req = (is_amo && !is_lr)
                             ? (core::PmpAccessType::Read | core::PmpAccessType::Write)
                             : core::PmpAccessType::Read;

    if (cpu.machine_->runtime_profile.is_instruction_mode() && !crosses_page &&
        !hypervisor_access) {
        if (!translation_enabled) {
            // Bypass soft TLB lookup if translation is disabled (direct DRAM access)
            const simrv::xlen::Address eff_vaddr = (simrv::xlen::kIsXLen64 && active_xlen == 32)
                                                       ? (v_addr.raw() & 0xFFFFFFFFULL)
                                                       : v_addr.raw();
            if (simrv::compiler::likely(geometry.contains(eff_vaddr, size_bytes))) {
                if (simrv::compiler::unlikely(!core::pmp::check_access(
                        cpu.state(), eff_vaddr, size_bytes, pmp_req, eff_priv))) {
                    cpu.active_context().pending_exception =
                        (is_amo && !is_lr) ? ExceptionCode::FaultStore : ExceptionCode::FaultLoad;
                    cpu.active_context().pending_tval = v_addr.raw();
                    return 0;
                }
                cpu.active_context().mem_paddr = eff_vaddr;
                return simrv::memory::ram_read_fast(eff_vaddr, funct3, cpu.machine_->ram_view());
            }
        } else {
            const simrv::xlen::Address vpn = v_addr.raw() >> 12;
            const size_t tlb_idx = core::CPU::soft_tlb_index(vpn);
            const auto& entry = cpu.soft_tlb_read[tlb_idx];
            if (simrv::compiler::likely(
                    entry.matches(vpn, current_asid, eff_priv, cpu.soft_tlb_epoch))) {
                const simrv::xlen::PhysAddr paddr = entry.paddr_base + (v_addr.raw() & 0xFFF);
                cpu.active_context().mem_paddr = paddr;
                if (simrv::compiler::likely(entry.has_host_ptr() && !(is_amo && !is_lr))) {
                    return simrv::memory::host_read_fast(entry.host_ptr(v_addr.raw()), funct3);
                }
                if (simrv::compiler::likely(geometry.contains(paddr, size_bytes))) {
                    if (simrv::compiler::unlikely(!core::pmp::check_access(
                            cpu.state(), paddr, size_bytes, pmp_req, eff_priv))) {
                        cpu.active_context().pending_exception = (is_amo && !is_lr)
                                                                     ? ExceptionCode::FaultStore
                                                                     : ExceptionCode::FaultLoad;
                        cpu.active_context().pending_tval = v_addr.raw();
                        return 0;
                    }
                    return simrv::memory::ram_read_fast(paddr, funct3, cpu.machine_->ram_view());
                }
            }
        }
    }

    const ExceptionCode read_access_fault = (is_amo && !is_lr)
                                                ? ExceptionCode::FaultStore
                                                : ExceptionCode::FaultLoad;
    auto issue_read = [&](simrv::xlen::Address addr) -> simrv::xlen::Word {
        cpu.active_context().mem_paddr = addr;
        if (pbmt == simrv::memory::PtePbmt::Pma &&
            cpu.machine_->runtime_profile.is_instruction_mode() &&
            geometry.contains(addr, size_bytes)) {
            return simrv::memory::ram_read_fast(addr, funct3, cpu.machine_->ram_view());
        }
        if (!geometry.contains(addr, size_bytes) || pbmt != simrv::memory::PtePbmt::Pma) {
            auto& transfer = cpu.ca_state.data_transfer;
            if (cpu.machine_->runtime_profile.is_cycle_mode() && transfer.active) {
                TileLinkBus::TimedResponse timed{};
                if (!mem.system_bus().try_get_timed_response(transfer.source, timed)) {
                    cpu.ca_state.waiting_for_interconnect = true;
                    return 0;
                }
                transfer.reset();
                if (simrv::compiler::unlikely(timed.payload.failed())) {
                    cpu.active_context().pending_exception = read_access_fault;
                    cpu.active_context().pending_tval = v_addr.raw();
                    return 0;
                }
                simrv::xlen::Word rdata = timed.payload.data;
                return simrv::memory::extend_loaded_value(rdata, funct3);
            }
            TlChannelA req{};
            req.opcode = TlOpcodeA::Get;
            const unsigned req_size = static_cast<uint8_t>(std::to_underlying(funct3) & 0x3);
            const unsigned req_size_bytes = 1u << req_size;
            const bool is_aligned = (addr & (req_size_bytes - 1u)) == 0;

            req.hart = cpu.hart_id();
            req.logical_cycle = cpu.clint_mmio.mcycle > 0 ? cpu.clint_mmio.mcycle - 1 : 0;
            req.logical_issue_sequence = cpu.next_logical_issue_sequence();
            req.source = make_tl_source(req.hart, TlPort::Data);
            if (is_aligned) {
                req.size = static_cast<uint8_t>(req_size);
                req.address = addr;
            } else {
                req.size = kTlBeatSize;
                req.address = addr & ~(static_cast<simrv::xlen::Address>(kTlBeatBytes - 1u));
            }
            if (!mem.system_bus().send_request(req)) {
                cpu.ca_state.waiting_for_interconnect = false;
                cpu.active_context().pending_exception = read_access_fault;
                cpu.active_context().pending_tval = v_addr.raw();
                return 0;
            }

            if (cpu.machine_->runtime_profile.is_cycle_mode()) {
                transfer = {
                    .address = addr, .source = req.source, .active = true, .is_write = false};
                cpu.ca_state.waiting_for_interconnect = true;
                return 0;
            }

            TlChannelD resp{};
            if (mem.system_bus().get_response(req.source, resp)) {
                if (simrv::compiler::unlikely(resp.failed())) {
                    cpu.active_context().pending_exception = read_access_fault;
                    cpu.active_context().pending_tval = v_addr.raw();
                }
                simrv::xlen::Word rdata = resp.data;
                if (!is_aligned) {
                    const auto lane_offset = static_cast<unsigned>(addr & (kTlBeatBytes - 1u));
                    rdata >>= (lane_offset * 8u);
                }
                return simrv::memory::extend_loaded_value(rdata, static_cast<uint8_t>(funct3));
            }
            cpu.active_context().pending_exception = read_access_fault;
            cpu.active_context().pending_tval = v_addr.raw();
            return 0;
        }

        auto& transfer = cpu.ca_state.data_transfer;
        simrv::xlen::Word cached_data = 0;
        const auto read_refill_data = [&](const auto& data,
                                          simrv::xlen::Address line_base) -> simrv::xlen::Word {
            const auto byte_offset = static_cast<size_t>(addr - line_base);
            simrv::xlen::Word raw = 0;
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
            const simrv::xlen::Address completed_line = transfer.address;
            transfer.reset();
            if (timed.payload.failed() || !timed.has_line_data) {
                cpu.active_context().pending_exception = read_access_fault;
                cpu.active_context().pending_tval = v_addr.raw();
                return 0;
            }
            cpu.dcache.insert(completed_line, timed.line_data.data(), mesi_for(timed.payload.cap));
            release_evicted_line(mem.system_bus(), cpu.dcache, cpu.hart_id(), TlPort::Data, cpu);
            TlChannelE ack{};
            ack.sink = timed.payload.sink;
            mem.system_bus().grant_ack(ack);
            const simrv::xlen::Address completed_offset = addr - completed_line;
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
        cpu.record_hpm_event(core::HpmEvent::DCacheMiss);

        const simrv::xlen::Address line_base =
            addr & ~(static_cast<simrv::xlen::Address>(simrv::cache::DCache::kLineBytes - 1u));
        std::array<simrv::xlen::Byte, simrv::cache::DCache::kLineBytes> line_data{};

        TlChannelA req{};
        req.opcode = TlOpcodeA::AcquireBlock;
        req.grow = TlGrow::NtoB;
        req.size = simrv::cache::DCache::kLineShift;
        req.hart = cpu.hart_id();
        req.logical_cycle = cpu.clint_mmio.mcycle > 0 ? cpu.clint_mmio.mcycle - 1 : 0;
        req.logical_issue_sequence = cpu.next_logical_issue_sequence();
        req.source = make_tl_source(req.hart, TlPort::Data);
        req.address = line_base;

        if (cpu.machine_->runtime_profile.is_cycle_mode()) {
            if (!mem.system_bus().send_request(req)) {
                cpu.ca_state.waiting_for_interconnect = false;
                cpu.active_context().pending_exception = read_access_fault;
                cpu.active_context().pending_tval = v_addr.raw();
                return 0;
            }
            transfer = {.address = line_base,
                        .source = req.source,
                        .active = true,
                        .is_write = false,
                        .line_fill = true};
            cpu.ca_state.waiting_for_interconnect = true;
            return 0;
        }

        TlChannelD resp{};
        if (mem.system_bus().acquire_block(req, resp, line_data) && !resp.failed()) {
            cpu.dcache.insert(line_base, line_data.data(), mesi_for(resp.cap));
            release_evicted_line(mem.system_bus(), cpu.dcache, cpu.hart_id(), TlPort::Data, cpu);
            TlChannelE ack{};
            ack.sink = resp.sink;
            mem.system_bus().grant_ack(ack);
            return read_refill_data(line_data, line_base);
        }
        cpu.active_context().pending_exception = read_access_fault;
        cpu.active_context().pending_tval = v_addr.raw();
        return 0;
    };

    const simrv::xlen::Address eff_vaddr = (simrv::xlen::kIsXLen64 && active_xlen == 32)
                                               ? (v_addr.raw() & 0xFFFFFFFFULL)
                                               : v_addr.raw();

    if (simrv::compiler::likely(!cpu.active_context().pending_exception.has_value()) &&
        simrv::compiler::likely(eff_priv == kPrivMachine || !translation_enabled) &&
        simrv::compiler::likely(geometry.contains(eff_vaddr, size_bytes))) {
        return issue_read(eff_vaddr);
    }

    simrv::xlen::Word rdata = 0;
    simrv::xlen::Address p_addr = 0;
    bool is_global = false;
    const PteAccess access_type =
        (is_amo && !is_lr) ? PteAccess::Write
        : isa::is_hlvx_op(cpu.active_context().op_id) ? PteAccess::ReadExecute
                                                       : PteAccess::Read;
    core::TLBEntry* entry = hypervisor_access ? nullptr : (is_amo && !is_lr)
                                ? cpu.tlb.lookup_data_w(v_addr.raw(), current_asid, eff_priv)
                                : cpu.tlb.lookup_data_r(v_addr.raw(), current_asid, eff_priv);

    if (eff_priv == kPrivMachine || !translation_enabled) {
        p_addr = eff_vaddr;
    } else if (entry) {
        p_addr = entry->p_addr + (v_addr.raw() & simrv::memory::kPageMask);
        is_global = entry->global;
        pbmt = entry->pbmt;
    } else {
        cpu.active_context().tlb_miss = true;
        cpu.record_hpm_event(core::HpmEvent::DTlbMiss);
        auto translate_res =
            cpu.translate_stage_address(*cpu.machine_, v_addr, access_type, eff_priv, active_xlen,
                                        TlPort::Data, cpu.ca_state.data_walk);
        if (!translate_res.has_value()) return 0;
        auto chain_res =
            (*translate_res)
                .and_then([&](PageWalkResult res) -> std::expected<void, simrv::xlen::TrapCause> {
                    p_addr = res.physical_address.raw();
                    is_global = res.global();
                    pbmt = simrv::memory::PteView{.raw = res.pte}.pbmt();
                    if (!hypervisor_access) {
                        cpu.tlb.insert_data_r(v_addr.raw(), p_addr, current_asid, eff_priv,
                                              is_global, pbmt);
                    }
                    if (is_amo && !is_lr && !hypervisor_access) {
                        cpu.tlb.insert_data_w(v_addr.raw(), p_addr, current_asid, eff_priv,
                                              is_global, pbmt);
                    }
                    return {};
                })
                .or_else([&](simrv::xlen::TrapCause error)
                             -> std::expected<void, simrv::xlen::TrapCause> {
                    cpu.active_context().pending_exception = to_exception_code(error);
                    cpu.active_context().pending_tval = v_addr.raw();
                    return {};
                });
        (void)chain_res;
    }

    if (!cpu.active_context().pending_exception.has_value()) {
        const auto pmp_slow_req = (is_amo && !is_lr)
                                      ? (core::PmpAccessType::Read | core::PmpAccessType::Write)
                                      : core::PmpAccessType::Read;
        if (simrv::compiler::unlikely(!core::pmp::check_access(cpu.state(), p_addr, size_bytes,
                                                               pmp_slow_req, eff_priv))) {
            cpu.active_context().pending_exception =
                (is_amo && !is_lr) ? ExceptionCode::FaultStore : ExceptionCode::FaultLoad;
            cpu.active_context().pending_tval = v_addr.raw();
            return 0;
        }
        if (is_amo && pbmt != simrv::memory::PtePbmt::Pma) {
            cpu.active_context().pending_exception =
                is_lr ? ExceptionCode::FaultLoad : ExceptionCode::FaultStore;
            cpu.active_context().pending_tval = v_addr.raw();
            return 0;
        }
        if (!hypervisor_access && pbmt == simrv::memory::PtePbmt::Pma &&
            cpu.machine_->runtime_profile.is_instruction_mode() && geometry.contains(p_addr)) {
            const simrv::xlen::Address vpn = v_addr.raw() >> 12;
            const size_t tlb_idx = core::CPU::soft_tlb_index(vpn);
            const simrv::xlen::Address ppage = p_addr & ~0xFFFULL;
            simrv::xlen::Byte* host_base =
                (cpu.machine_->ram_view().contains(ppage, 4096) &&
                 (cpu.state().num_active_pmp == 0 ||
                  core::pmp::check_access(cpu.state(), ppage, 4096, core::PmpAccessType::Read,
                                          eff_priv)))
                    ? cpu.machine_->ram_view().unchecked_ptr(ppage)
                    : nullptr;
            cpu.soft_tlb_read[tlb_idx].set(
                vpn, translation_enabled ? static_cast<uint64_t>(current_asid) : ~uint64_t{0},
                eff_priv, cpu.soft_tlb_epoch, ppage, host_base, is_global);
            if (cpu.soft_tlb_write[tlb_idx].paddr_base != ppage) {
                cpu.soft_tlb_write[tlb_idx].invalidate();
            }
        } else if (pbmt != simrv::memory::PtePbmt::Pma && translation_enabled) {
            const size_t tlb_idx = core::CPU::soft_tlb_index(v_addr.raw() >> 12U);
            cpu.soft_tlb_read[tlb_idx].invalidate();
            cpu.soft_tlb_write[tlb_idx].invalidate();
        }
        cpu.active_context().mem_paddr = p_addr;
        return issue_read(p_addr);
    }
    return rdata;
}

void MemoryAccess::target_write(MemorySubsystem& mem, core::CPU& cpu, simrv::xlen::VirtAddr v_addr,
                                simrv::xlen::Word wdata, isa::Funct3 funct3) {
    HypervisorMemoryAccessScope hypervisor_scope(cpu);
    const auto& geometry = mem.memory_geometry();
    const unsigned active_xlen = cpu.effective_data_xlen();
    if (active_xlen == 32) v_addr &= 0xFFFFFFFFULL;
    v_addr = apply_user_pointer_mask(cpu, v_addr, active_xlen);
    if (simrv::compiler::unlikely(cpu.active_context().pending_exception.has_value())) {
        return;
    }

    const unsigned size_bytes = 1u << (std::to_underlying(funct3) & 0x3u);
    auto note_store = [&](simrv::xlen::Address paddr, size_t bytes) {
        cpu.active_context().mem_paddr = paddr;
        cpu.state().lr_reservation_active = false;
        if (mem.reservation_table().may_have_reservations() && cpu.machine_->num_harts() > 1) {
            mem.reservation_table().invalidate_range(paddr, bytes, cpu.hart_id());
        }
    };

    const bool crosses_page =
        ((v_addr & simrv::memory::kPageMask) + size_bytes) > (1u << simrv::memory::kPageShift);
    const bool crosses_cache_line = ((v_addr & (simrv::cache::DCache::kLineBytes - 1u)) +
                                     size_bytes) > simrv::cache::DCache::kLineBytes;
    const bool is_amo = (static_cast<Opcode>(cpu.active_context().opcode) == Opcode::Amo);
    const bool is_sc = is_amo && (cpu.active_context().funct5 == Funct5Amo::Sc);
    const bool hypervisor_access =
        isa::is_hypervisor_memory_op(cpu.active_context().op_id);
    simrv::memory::PtePbmt pbmt = simrv::memory::PtePbmt::Pma;
    if (simrv::compiler::unlikely((v_addr & (size_bytes - 1u)) != 0)) {
        if (is_amo) {
            cpu.active_context().pending_exception = ExceptionCode::FaultStore;
            cpu.active_context().pending_tval = v_addr.raw();
            return;
        }
    }
    if (simrv::compiler::unlikely(cpu.machine_->breakpoint_manager().has_any())) {
        if (auto hit =
                cpu.machine_->breakpoint_manager().check_mem_write(v_addr.raw(), size_bytes)) {
            if (cpu.machine_->debugger() && cpu.machine_->debugger()->is_connected()) {
                cpu.machine_->debug_watch_hit(
                    cpu.hart_id(), GdbSignal::SigTrap,
                    std::format("{}:{:x};",
                                hit->watch_type == debug::WatchType::Access ? "awatch" : "watch",
                                v_addr.raw()),
                    hit->description);
            } else if (cpu.machine_->tui_enabled()) {
                cpu.machine_->debug_watch_hit(cpu.hart_id(), GdbSignal::SigTrap, {},
                                              hit->description);
            }
        }
    }
    if (simrv::compiler::unlikely(cpu.check_triggers_mem(v_addr.raw(), size_bytes, true))) {
        cpu.active_context().pending_exception = ExceptionCode::Breakpoint;
        cpu.active_context().pending_tval = v_addr.raw();
        return;
    }
    if (simrv::compiler::unlikely(crosses_page || crosses_cache_line)) {
        for (unsigned b = 0; b < size_bytes; ++b) {
            simrv::xlen::VirtAddr byte_vaddr = v_addr + b;
            simrv::xlen::Word byte_val = (wdata >> (8 * b)) & 0xFFULL;
            target_write(mem, cpu, byte_vaddr, byte_val, isa::Funct3::Sb);
            if (cpu.ca_state.waiting_for_interconnect) return;
            if (cpu.active_context().pending_exception.has_value()) {
                return;
            }
        }
        return;
    }

    const simrv::xlen::PrivilegeLevel eff_priv = cpu.effective_data_privilege();
    const simrv::xlen::Word active_satp = cpu.active_satp();
    const simrv::xlen::Word current_asid = simrv::xlen::satp_asid(active_satp, active_xlen);
    const bool translation_enabled =
        (eff_priv != kPrivMachine &&
         (simrv::xlen::satp_translation_enabled(active_satp, active_xlen) || hypervisor_access));

    if constexpr (simrv::xlen::kIsXLen64) {
        if (translation_enabled) {
            if (simrv::compiler::unlikely(
                    !simrv::Mmu::is_canonical(v_addr, active_satp, active_xlen))) {
                cpu.active_context().pending_exception = ExceptionCode::StorePageFault;
                cpu.active_context().pending_tval = v_addr.raw();
                return;
            }
        }
    }

    auto check_sc_reservation = [&](simrv::xlen::Address addr) -> bool {
        auto& ctx = cpu.active_context();
        if (!is_sc || ctx.sc_reservation_checked) return true;

        ctx.sc_reservation_checked = true;
        bool success = mem.reservation_table().check_and_clear_reservation(cpu.hart_id(), addr);
        if (success && cpu.machine_->lockstep() && cpu.machine_->lockstep()->is_running()) {
            const auto lockstep_success = cpu.machine_->lockstep()->determine_sc_success();
            if (lockstep_success.has_value() && !lockstep_success.value()) success = false;
        }
        ctx.sc_reservation_succeeded = success;
        ctx.wb_data = success ? 0 : 1;
        cpu.state().lr_reservation_active = success;
        return success;
    };

    auto issue_write = [&](simrv::xlen::Address addr, simrv::xlen::Word data) -> void {
        auto& ctx = cpu.active_context();
        ctx.mem_paddr = addr;
        const auto commit_write = [&]() { note_store(addr, size_bytes); };
        auto& transfer = cpu.ca_state.data_transfer;
        if (cpu.machine_->runtime_profile.is_cycle_mode() && transfer.active &&
            transfer.line_fill) {
            TileLinkBus::TimedResponse timed{};
            if (!mem.system_bus().try_get_timed_response(transfer.source, timed)) {
                cpu.ca_state.waiting_for_interconnect = true;
                return;
            }
            const simrv::xlen::Address completed_line = transfer.address;
            transfer.reset();
            const bool is_perm_grant =
                timed.payload.opcode == TlOpcodeD::Grant && !timed.has_line_data;
            if (timed.payload.failed() || (!timed.has_line_data && !is_perm_grant)) {
                cpu.active_context().pending_exception = ExceptionCode::FaultStore;
                cpu.active_context().pending_tval = v_addr.raw();
                return;
            }
            if (is_perm_grant) {
                cpu.dcache.upgrade_line_state(completed_line, mesi_for(timed.payload.cap));
            } else {
                cpu.dcache.insert(completed_line, timed.line_data.data(),
                                  mesi_for(timed.payload.cap));
                release_evicted_line(mem.system_bus(), cpu.dcache, cpu.hart_id(), TlPort::Data, cpu);
            }
            TlChannelE ack{};
            ack.sink = timed.payload.sink;
            mem.system_bus().grant_ack(ack);
        }
        if (simrv::compiler::unlikely(cpu.cpu_model_config.is_provingground_or_rvproc) &&
            (addr == cpu.machine_->isa_test_tohost() || (addr & 0x80000000ULL) != 0)) {
            if (!check_sc_reservation(addr)) return;
            if (funct3 == Funct3::Sw && (data == 0x00020000ULL || (data & 1ULL) != 0)) {
                cpu.machine_->publish_tohost_write(data, core::Machine::TohostWriteKind::Full);
            } else {
                cpu.machine_->console_write(static_cast<char>(data & 0xFF));
            }
            commit_write();
            return;
        }
        const bool is_tohost_write = simrv::xlen::kIsXLen64
                                         ? (funct3 == Funct3::Sw || funct3 == Funct3::Sd)
                                         : (funct3 == Funct3::Sw);
        if (simrv::compiler::unlikely(is_tohost_write)) {
            auto publish_host_write = [&](uint64_t value, core::Machine::TohostWriteKind kind) {
                if (cpu.machine_->runtime_profile.is_cycle_mode() &&
                    cpu.pipeline_sim.config.host_interface_latency != 0) {
                    cpu.ca_state.pending_host_value = value;
                    cpu.ca_state.pending_host_write_kind = static_cast<uint8_t>(kind);
                    const auto phase_period = cpu.pipeline_sim.config.host_interface_phase_period;
                    const auto phase_delay =
                        phase_period == 0 ? 0 : cpu.pipeline_sim.cycle_count() % phase_period;
                    cpu.ca_state.host_write_remaining =
                        cpu.pipeline_sim.config.host_interface_latency + phase_delay - 1;
                    cpu.ca_state.host_write_pending = true;
                } else {
                    cpu.machine_->publish_tohost_write(value, kind);
                }
            };
            const bool is_guest_xlen32 = (cpu.state().regs.xlen == 32) || !simrv::xlen::kIsXLen64;
            if (addr == cpu.machine_->isa_test_tohost() || addr == 0x80001000 ||
                addr == 0x40008000) {
                if (!check_sc_reservation(addr)) return;
                const bool low_word = funct3 == Funct3::Sw;
                publish_host_write(low_word || is_guest_xlen32
                                       ? ((cpu.machine_->tohost & 0xFFFFFFFF00000000ULL) |
                                          (data & 0xFFFFFFFFULL))
                                       : data,
                                   low_word && !is_guest_xlen32
                                       ? core::Machine::TohostWriteKind::LowWord
                                            : core::Machine::TohostWriteKind::Full);
            } else if (funct3 == Funct3::Sw &&
                       (addr == cpu.machine_->isa_test_tohost() + 4 || addr == 0x80001004 ||
                        addr == 0x40008004)) {
                if (!check_sc_reservation(addr)) return;
                publish_host_write((cpu.machine_->tohost & 0x00000000FFFFFFFFULL) |
                                       (static_cast<uint64_t>(data & 0xFFFFFFFFULL) << 32),
                                   core::Machine::TohostWriteKind::HighWord);
            }
        }
        if (pbmt == simrv::memory::PtePbmt::Pma &&
            cpu.machine_->runtime_profile.is_instruction_mode() &&
            geometry.contains(addr, size_bytes)) {
            if (!check_sc_reservation(addr)) return;
            simrv::memory::ram_write_fast(addr, data, funct3, cpu.machine_->ram_view());
            commit_write();
            return;
        }
        if (geometry.contains(addr, size_bytes) && pbmt == simrv::memory::PtePbmt::Pma) {
            const simrv::xlen::Address line_base =
                addr & ~(static_cast<simrv::xlen::Address>(simrv::cache::DCache::kLineBytes - 1u));
            const auto line_state = cpu.dcache.line_state(line_base);
            const bool has_trunk = line_state == simrv::memory::MesiState::Exclusive ||
                                   line_state == simrv::memory::MesiState::Modified;
            if (is_sc && has_trunk && !check_sc_reservation(addr)) return;

            bool first_write = false;
            const auto misses_before = cpu.dcache.miss_count();
            bool cache_write_completed = cpu.dcache.write(addr, data, funct3, &first_write);
            if (!cache_write_completed) {
                const auto misses_after = cpu.dcache.miss_count();
                if (misses_after != misses_before)
                    cpu.record_hpm_event(core::HpmEvent::DCacheMiss, misses_after - misses_before);
                // Not in Trunk state; acquire Trunk ownership via TL-C
                const bool is_upgrade = (line_state == simrv::memory::MesiState::Shared);
                TlChannelA req{};
                req.opcode = is_upgrade ? TlOpcodeA::AcquirePerm : TlOpcodeA::AcquireBlock;
                req.grow = is_upgrade ? TlGrow::BtoT : TlGrow::NtoT;
                req.size = simrv::cache::DCache::kLineShift;
                req.hart = cpu.hart_id();
                req.logical_cycle = cpu.clint_mmio.mcycle > 0 ? cpu.clint_mmio.mcycle - 1 : 0;
                req.logical_issue_sequence = cpu.next_logical_issue_sequence();
                req.source = make_tl_source(req.hart, TlPort::Data);
                req.address = line_base;

                if (cpu.machine_->runtime_profile.is_cycle_mode()) {
                    if (!mem.system_bus().send_request(req)) {
                        cpu.ca_state.waiting_for_interconnect = false;
                        cpu.active_context().pending_exception = ExceptionCode::FaultStore;
                        cpu.active_context().pending_tval = v_addr.raw();
                        return;
                    }
                    transfer = {.address = line_base,
                                .source = req.source,
                                .active = true,
                                .is_write = true,
                                .line_fill = true};
                    cpu.ca_state.waiting_for_interconnect = true;
                    return;
                }

                TlChannelD resp{};
                if (is_upgrade) {
                    if (mem.system_bus().acquire_perm(req, resp) && !resp.failed()) {
                        cpu.dcache.upgrade_line_state(line_base, mesi_for(resp.cap));
                        TlChannelE ack{};
                        ack.sink = resp.sink;
                        mem.system_bus().grant_ack(ack);
                        if (is_sc && !check_sc_reservation(addr)) return;
                        cache_write_completed = cpu.dcache.write(addr, data, funct3, &first_write);
                    }
                } else {
                    std::array<simrv::xlen::Byte, simrv::cache::DCache::kLineBytes> line_data{};
                    if (mem.system_bus().acquire_block(req, resp, line_data) && !resp.failed()) {
                        cpu.dcache.insert(line_base, line_data.data(), mesi_for(resp.cap));
                        release_evicted_line(mem.system_bus(), cpu.dcache, cpu.hart_id(),
                                             TlPort::Data, cpu);
                        TlChannelE ack{};
                        ack.sink = resp.sink;
                        mem.system_bus().grant_ack(ack);
                        if (is_sc && !check_sc_reservation(addr)) return;
                        cache_write_completed = cpu.dcache.write(addr, data, funct3, &first_write);
                    }
                }
            }
            if (cache_write_completed && first_write) {
                mem.system_bus().mark_modified(addr, cpu.hart_id());
            }
            if (!cache_write_completed) {
                cpu.active_context().pending_exception = ExceptionCode::FaultStore;
                cpu.active_context().pending_tval = v_addr.raw();
                return;
            }
            simrv::memory::ram_write_fast(addr, data, funct3, cpu.machine_->ram_view());
            commit_write();
            return;
        }

        TlChannelA req{};
        const unsigned req_size = static_cast<uint8_t>(std::to_underlying(funct3) & 0x3);
        const unsigned req_size_bytes = 1u << req_size;
        const bool is_aligned = (addr & (req_size_bytes - 1u)) == 0;

        req.hart = cpu.hart_id();
        req.logical_cycle = cpu.clint_mmio.mcycle > 0 ? cpu.clint_mmio.mcycle - 1 : 0;
        req.logical_issue_sequence = cpu.next_logical_issue_sequence();
        req.source = make_tl_source(req.hart, TlPort::Data);
        if (is_aligned) {
            req.opcode = TlOpcodeA::PutFullData;
            req.size = static_cast<uint8_t>(req_size);
            req.address = addr;
            req.mask = TlChannelA::compute_mask(req.size, req.address);
        } else {
            req.opcode = TlOpcodeA::PutPartialData;
            req.size = kTlBeatSize;
            req.address = addr & ~(static_cast<simrv::xlen::Address>(kTlBeatBytes - 1u));
            const auto lane_offset = static_cast<unsigned>(addr & (kTlBeatBytes - 1u));
            req.mask = ((TlMask{1} << req_size_bytes) - 1u) << lane_offset;
        }
        const unsigned bits = 8 * req_size_bytes;
        req.data = (bits < simrv::xlen::kXLenBits)
                       ? (data & ((static_cast<simrv::xlen::Word>(1) << bits) - 1))
                       : data;
        if (!is_aligned) {
            const auto lane_offset = static_cast<unsigned>(addr & (kTlBeatBytes - 1u));
            req.data <<= (lane_offset * 8u);
        }
        if (cpu.machine_->runtime_profile.is_cycle_mode() && transfer.active) {
            TileLinkBus::TimedResponse timed{};
            if (!mem.system_bus().try_get_timed_response(transfer.source, timed)) {
                cpu.ca_state.waiting_for_interconnect = true;
                return;
            }
            const simrv::xlen::Address completed_addr = transfer.address;
            transfer.reset();
            if (simrv::compiler::unlikely(timed.payload.failed())) {
                cpu.active_context().pending_exception = ExceptionCode::FaultStore;
                cpu.active_context().pending_tval = v_addr.raw();
            } else {
                ctx.mem_paddr = completed_addr;
                commit_write();
            }
            return;
        }
        if (!check_sc_reservation(addr)) return;
        if (!mem.system_bus().send_request(req)) {
            cpu.ca_state.waiting_for_interconnect = false;
            cpu.active_context().pending_exception = ExceptionCode::FaultStore;
            cpu.active_context().pending_tval = v_addr.raw();
            return;
        }
        if (cpu.machine_->runtime_profile.is_cycle_mode()) {
            transfer = {.address = addr, .source = req.source, .active = true, .is_write = true};
            cpu.ca_state.waiting_for_interconnect = true;
            return;
        }

        TlChannelD resp{};
        if (mem.system_bus().get_response(req.source, resp)) {
            if (simrv::compiler::unlikely(resp.failed())) {
                cpu.active_context().pending_exception = ExceptionCode::FaultStore;
                cpu.active_context().pending_tval = v_addr.raw();
            } else {
                commit_write();
            }
        } else {
            cpu.active_context().pending_exception = ExceptionCode::FaultStore;
            cpu.active_context().pending_tval = v_addr.raw();
        }
    };

    if (cpu.machine_->runtime_profile.is_instruction_mode() && !crosses_page && !is_sc &&
        !hypervisor_access) {
        if (!translation_enabled) {
            // Bypass soft TLB lookup if translation is disabled (direct DRAM access)
            const simrv::xlen::Address eff_vaddr = (simrv::xlen::kIsXLen64 && active_xlen == 32)
                                                       ? (v_addr.raw() & 0xFFFFFFFFULL)
                                                       : v_addr.raw();
            if (simrv::compiler::likely(geometry.contains(eff_vaddr, size_bytes))) {
                issue_write(eff_vaddr, wdata);
                return;
            }
        } else {
            const simrv::xlen::Address vpn = v_addr.raw() >> 12;
            const size_t tlb_idx = core::CPU::soft_tlb_index(vpn);
            const auto& entry = cpu.soft_tlb_write[tlb_idx];
            if (simrv::compiler::likely(
                    entry.matches(vpn, current_asid, eff_priv, cpu.soft_tlb_epoch))) {
                const simrv::xlen::PhysAddr paddr = entry.paddr_base + (v_addr.raw() & 0xFFF);
                if (simrv::compiler::likely(entry.has_host_ptr())) {
                    simrv::memory::host_write_fast(entry.host_ptr(v_addr.raw()), wdata, funct3);
                    note_store(paddr.raw(), size_bytes);
                    return;
                }
                if (simrv::compiler::unlikely(!core::pmp::check_access(
                        cpu.state(), paddr, size_bytes, core::PmpAccessType::Write, eff_priv))) {
                    cpu.active_context().pending_exception = ExceptionCode::FaultStore;
                    cpu.active_context().pending_tval = v_addr.raw();
                    return;
                }
                if (simrv::compiler::likely(geometry.contains(paddr, size_bytes))) {
                    simrv::memory::ram_write_fast(paddr, wdata, funct3, cpu.machine_->ram_view());
                    note_store(paddr.raw(), size_bytes);
                    return;
                }
                issue_write(paddr.raw(), wdata);
                return;
            }
        }
    }

    const simrv::xlen::Address eff_vaddr = (simrv::xlen::kIsXLen64 && active_xlen == 32)
                                               ? (v_addr.raw() & 0xFFFFFFFFULL)
                                               : v_addr.raw();

    if (simrv::compiler::likely(!cpu.active_context().pending_exception.has_value()) &&
        simrv::compiler::likely(eff_priv == kPrivMachine || !translation_enabled) &&
        simrv::compiler::likely(geometry.contains(eff_vaddr, size_bytes))) {
        if (simrv::compiler::unlikely(!core::pmp::check_access(
                cpu.state(), eff_vaddr, size_bytes, core::PmpAccessType::Write, eff_priv))) {
            cpu.active_context().pending_exception = ExceptionCode::FaultStore;
            cpu.active_context().pending_tval = v_addr.raw();
            return;
        }
        issue_write(eff_vaddr, wdata);
        return;
    }

    simrv::xlen::Address p_addr = 0;
    bool is_global = false;
    core::TLBEntry* entry = hypervisor_access
                                ? nullptr
                                : cpu.tlb.lookup_data_w(v_addr.raw(), current_asid, eff_priv);

    if (eff_priv == kPrivMachine || !translation_enabled) {
        p_addr = eff_vaddr;
    } else if (entry) {
        p_addr = entry->p_addr + (v_addr.raw() & simrv::memory::kPageMask);
        is_global = entry->global;
        pbmt = entry->pbmt;
    } else {
        cpu.active_context().tlb_miss = true;
        cpu.record_hpm_event(core::HpmEvent::DTlbMiss);
        auto translate_res =
            cpu.translate_stage_address(*cpu.machine_, v_addr, PteAccess::Write, eff_priv,
                                        active_xlen, TlPort::Data, cpu.ca_state.data_walk);
        if (!translate_res.has_value()) return;
        auto chain_res =
            (*translate_res)
                .and_then([&](PageWalkResult res) -> std::expected<void, simrv::xlen::TrapCause> {
                    p_addr = res.physical_address.raw();
                    is_global = res.global();
                    pbmt = simrv::memory::PteView{.raw = res.pte}.pbmt();
                    if (!hypervisor_access) {
                        cpu.tlb.insert_data_w(v_addr.raw(), p_addr, current_asid, eff_priv,
                                              is_global, pbmt);
                    }
                    return {};
                })
                .or_else([&](simrv::xlen::TrapCause error)
                             -> std::expected<void, simrv::xlen::TrapCause> {
                    cpu.active_context().pending_exception = to_exception_code(error);
                    cpu.active_context().pending_tval = v_addr.raw();
                    return {};
                });
        (void)chain_res;
    }

    if (!cpu.active_context().pending_exception.has_value()) {
        if (simrv::compiler::unlikely(!core::pmp::check_access(
                cpu.state(), p_addr, size_bytes, core::PmpAccessType::Write, eff_priv))) {
            cpu.active_context().pending_exception = ExceptionCode::FaultStore;
            cpu.active_context().pending_tval = v_addr.raw();
            return;
        }
        // SC must still raise an access fault for an inaccessible or non-memory
        // target even when no LR reservation exists. Check target support before
        // the reservation can turn the operation into an ordinary failed SC.
        if (is_sc && (pbmt != simrv::memory::PtePbmt::Pma ||
                      !geometry.contains(p_addr, size_bytes))) {
            cpu.active_context().pending_exception = ExceptionCode::FaultStore;
            cpu.active_context().pending_tval = v_addr.raw();
            return;
        }
        if (is_amo && pbmt != simrv::memory::PtePbmt::Pma) {
            cpu.active_context().pending_exception = ExceptionCode::FaultStore;
            cpu.active_context().pending_tval = v_addr.raw();
            return;
        }
        if (!hypervisor_access && pbmt == simrv::memory::PtePbmt::Pma &&
            cpu.machine_->runtime_profile.is_instruction_mode() && geometry.contains(p_addr)) {
            const simrv::xlen::Address vpn = v_addr.raw() >> 12;
            const size_t tlb_idx = core::CPU::soft_tlb_index(vpn);
            const simrv::xlen::Address ppage = p_addr & ~0xFFFULL;
            simrv::xlen::Byte* host_base =
                (cpu.machine_->ram_view().contains(ppage, 4096) &&
                 (cpu.state().num_active_pmp == 0 ||
                  core::pmp::check_access(cpu.state(), ppage, 4096, core::PmpAccessType::Write,
                                          eff_priv)))
                    ? cpu.machine_->ram_view().unchecked_ptr(ppage)
                    : nullptr;
            cpu.soft_tlb_write[tlb_idx].set(
                vpn, translation_enabled ? static_cast<uint64_t>(current_asid) : ~uint64_t{0},
                eff_priv, cpu.soft_tlb_epoch, ppage, host_base, is_global);
            if (cpu.soft_tlb_read[tlb_idx].paddr_base != ppage) {
                cpu.soft_tlb_read[tlb_idx].invalidate();
            }
        } else if (pbmt != simrv::memory::PtePbmt::Pma && translation_enabled) {
            const size_t tlb_idx = core::CPU::soft_tlb_index(v_addr.raw() >> 12U);
            cpu.soft_tlb_read[tlb_idx].invalidate();
            cpu.soft_tlb_write[tlb_idx].invalidate();
        }
        issue_write(p_addr, wdata);
    }
}

auto MemoryAccess::loadInt(MemorySubsystem& mem, core::CPU& cpu, simrv::xlen::VirtAddr addr,
                           isa::Funct3 funct3) -> simrv::xlen::Word {
    return target_read(mem, cpu, addr, funct3);
}

auto MemoryAccess::loadFp(MemorySubsystem& mem, core::CPU& cpu, simrv::xlen::VirtAddr addr,
                          isa::Funct3 funct3) -> simrv::xlen::FloatingRegister {
    const auto f3 = funct3;
    if (f3 == Funct3::Flh) {
        const auto half = target_read(mem, cpu, addr, Funct3::Lh);
        return static_cast<simrv::xlen::FloatingRegister>(~uint64_t{0xFFFF}) |
               static_cast<uint16_t>(half);
    } else if (f3 == Funct3::Flw) {
        const simrv::xlen::Word lo = target_read(mem, cpu, addr, Funct3::Lw);
        return static_cast<uint64_t>(simrv::xlen::kF32BoxerBits) |
               static_cast<uint64_t>(lo & simrv::xlen::kLower32Mask);
    } else if (f3 == Funct3::Fld) {
        if constexpr (simrv::xlen::kIsXLen64) {
            return static_cast<simrv::xlen::FloatingRegister>(
                target_read(mem, cpu, addr, Funct3::Ld));
        } else {
            const simrv::xlen::Word lo = target_read(mem, cpu, addr, Funct3::Lw);
            const simrv::xlen::Word hi = target_read(mem, cpu, addr + 4, Funct3::Lw);
            return static_cast<uint64_t>(lo) | (static_cast<uint64_t>(hi) << 32);
        }
    }
    return 0;
}

void MemoryAccess::storeInt(MemorySubsystem& mem, core::CPU& cpu, simrv::xlen::VirtAddr addr,
                            simrv::xlen::Word data, isa::Funct3 funct3) {
    target_write(mem, cpu, addr, data, funct3);
}

void MemoryAccess::storeFp(MemorySubsystem& mem, core::CPU& cpu, simrv::xlen::VirtAddr addr,
                           simrv::xlen::FloatingRegister data, isa::Funct3 funct3) {
    const auto f3 = funct3;
    if (f3 == Funct3::Fsh) {
        target_write(mem, cpu, addr, static_cast<uint16_t>(data), Funct3::Sh);
    } else if (f3 == Funct3::Fsw) {
        target_write(
            mem, cpu, addr,
            static_cast<simrv::xlen::Word>(
                data & static_cast<simrv::xlen::FloatingRegister>(simrv::xlen::kLower32Mask)),
            Funct3::Sw);
    } else if (f3 == Funct3::Fsd) {
        if constexpr (simrv::xlen::kIsXLen64) {
            target_write(mem, cpu, addr, static_cast<simrv::xlen::Word>(data), Funct3::Sd);
        } else {
            target_write(
                mem, cpu, addr,
                static_cast<simrv::xlen::Word>(
                    data & static_cast<simrv::xlen::FloatingRegister>(simrv::xlen::kLower32Mask)),
                Funct3::Sw);
            target_write(mem, cpu, addr + 4,
                         static_cast<simrv::xlen::Word>(
                             (data >> 32) &
                             static_cast<simrv::xlen::FloatingRegister>(simrv::xlen::kLower32Mask)),
                         Funct3::Sw);
        }
    }
}

void MemoryAccess::execute_cmo(MemorySubsystem& mem, core::CPU& cpu, simrv::xlen::VirtAddr addr,
                               isa::OperationId op) {
    const auto eff_priv = cpu.effective_data_privilege();
    const unsigned active_xlen = cpu.effective_data_xlen();
    addr = apply_user_pointer_mask(cpu, addr, active_xlen);

    // 1. Permission checks
    if (op == isa::OperationId::CBO_ZERO) {
        if (eff_priv == kPrivUser) {
            if ((cpu.state().senvcfg & (1ULL << 7)) == 0 ||
                (cpu.state().menvcfg & (1ULL << 7)) == 0) {
                cpu.active_context().pending_exception = ExceptionCode::IllegalInstruction;
                cpu.active_context().pending_tval = cpu.active_context().ir;
                return;
            }
        } else if (eff_priv == kPrivSupervisor) {
            if ((cpu.state().menvcfg & (1ULL << 7)) == 0) {
                cpu.active_context().pending_exception = ExceptionCode::IllegalInstruction;
                cpu.active_context().pending_tval = cpu.active_context().ir;
                return;
            }
        }
    } else if (op == isa::OperationId::CBO_CLEAN || op == isa::OperationId::CBO_FLUSH) {
        if (eff_priv == kPrivUser) {
            if ((cpu.state().senvcfg & (1ULL << 4)) == 0 ||
                (cpu.state().menvcfg & (1ULL << 4)) == 0) {
                cpu.active_context().pending_exception = ExceptionCode::IllegalInstruction;
                cpu.active_context().pending_tval = cpu.active_context().ir;
                return;
            }
        } else if (eff_priv == kPrivSupervisor) {
            if ((cpu.state().menvcfg & (1ULL << 4)) == 0) {
                cpu.active_context().pending_exception = ExceptionCode::IllegalInstruction;
                cpu.active_context().pending_tval = cpu.active_context().ir;
                return;
            }
        }
    } else if (op == isa::OperationId::CBO_INVAL) {
        if (eff_priv == kPrivUser) {
            if ((cpu.state().senvcfg & (3ULL << 4)) == 0 ||
                (cpu.state().menvcfg & (3ULL << 4)) == 0) {
                cpu.active_context().pending_exception = ExceptionCode::IllegalInstruction;
                cpu.active_context().pending_tval = cpu.active_context().ir;
                return;
            }
        } else if (eff_priv == kPrivSupervisor) {
            if ((cpu.state().menvcfg & (3ULL << 4)) == 0) {
                cpu.active_context().pending_exception = ExceptionCode::IllegalInstruction;
                cpu.active_context().pending_tval = cpu.active_context().ir;
                return;
            }
        }
    }

    constexpr simrv::xlen::Address kLineSize = simrv::cache::DCache::kLineBytes;
    const simrv::xlen::Address vaddr = addr.raw() & ~(kLineSize - 1u);
    const simrv::xlen::Word active_satp = cpu.active_satp();
    const simrv::xlen::Word current_asid = simrv::xlen::satp_asid(active_satp, active_xlen);
    const bool translation_enabled =
        (eff_priv != kPrivMachine &&
         simrv::xlen::satp_translation_enabled(active_satp, active_xlen));

    simrv::xlen::Address paddr = vaddr;
    if (translation_enabled) {
        if constexpr (simrv::xlen::kIsXLen64) {
            if (!simrv::Mmu::is_canonical(simrv::xlen::VirtAddr{vaddr}, active_satp,
                                          active_xlen)) {
                cpu.active_context().pending_exception =
                    (op == isa::OperationId::CBO_ZERO || op == isa::OperationId::CBO_FLUSH)
                        ? ExceptionCode::StorePageFault
                        : ExceptionCode::LoadPageFault;
                cpu.active_context().pending_tval = vaddr;
                return;
            }
        }
        const PteAccess access_type =
            (op == isa::OperationId::CBO_ZERO || op == isa::OperationId::CBO_FLUSH)
                ? PteAccess::Write
                : PteAccess::Read;
        auto* entry = (access_type == PteAccess::Write)
                          ? cpu.tlb.lookup_data_w(vaddr, current_asid, eff_priv)
                          : cpu.tlb.lookup_data_r(vaddr, current_asid, eff_priv);
        if (entry != nullptr) {
            paddr = entry->p_addr + (vaddr & simrv::memory::kPageMask);
        } else {
            auto translate_res = cpu.translate_stage_address(
                *cpu.machine_, simrv::xlen::VirtAddr{vaddr}, access_type, eff_priv, active_xlen,
                TlPort::Data, cpu.ca_state.data_walk);
            if (!translate_res.has_value()) return;
            (void)(*translate_res)
                .and_then([&](PageWalkResult res) -> std::expected<void, simrv::xlen::TrapCause> {
                    paddr = res.physical_address.raw();
                    if (access_type == PteAccess::Write) {
                        cpu.tlb.insert_data_w(vaddr, paddr, current_asid, eff_priv, res.global());
                    } else {
                        cpu.tlb.insert_data_r(vaddr, paddr, current_asid, eff_priv, res.global());
                    }
                    return {};
                })
                .or_else([&](simrv::xlen::TrapCause error)
                             -> std::expected<void, simrv::xlen::TrapCause> {
                    cpu.active_context().pending_exception = to_exception_code(error);
                    cpu.active_context().pending_tval = vaddr;
                    return {};
                });
            if (cpu.active_context().pending_exception.has_value()) return;
        }
    }

    // PMP verification
    const core::PmpAccessType pmp_access =
        (op == isa::OperationId::CBO_ZERO || op == isa::OperationId::CBO_FLUSH)
            ? core::PmpAccessType::Write
            : core::PmpAccessType::Read;
    if (!core::pmp::check_access(cpu.state(), paddr, kLineSize, pmp_access, eff_priv)) {
        cpu.active_context().pending_exception = (pmp_access == core::PmpAccessType::Write)
                                                     ? ExceptionCode::FaultStore
                                                     : ExceptionCode::FaultLoad;
        cpu.active_context().pending_tval = vaddr;
        return;
    }

    const auto& geometry = mem.memory_geometry();
    std::array<simrv::xlen::Byte, kLineSize> dirty_line{};

    if (op == isa::OperationId::CBO_ZERO) {
        if (geometry.contains(paddr, kLineSize)) {
            std::memset(cpu.machine_->ram_view().unchecked_ptr(paddr), 0, kLineSize);
        }
        cpu.dcache.zero_line(paddr);
        mem.system_bus().coherence_hub().invalidate_line_broadcast(paddr, cpu.hart_id());
    } else if (op == isa::OperationId::CBO_CLEAN) {
        if (cpu.dcache.probe_line(paddr, simrv::memory::MesiState::Exclusive, &dirty_line)) {
            if (geometry.contains(paddr, kLineSize)) {
                std::memcpy(cpu.machine_->ram_view().unchecked_ptr(paddr), dirty_line.data(),
                            kLineSize);
            }
        }
    } else if (op == isa::OperationId::CBO_FLUSH) {
        if (cpu.dcache.probe_line(paddr, simrv::memory::MesiState::Invalid, &dirty_line)) {
            if (geometry.contains(paddr, kLineSize)) {
                std::memcpy(cpu.machine_->ram_view().unchecked_ptr(paddr), dirty_line.data(),
                            kLineSize);
            }
        }
    } else if (op == isa::OperationId::CBO_INVAL) {
        (void)cpu.dcache.probe_line(paddr, simrv::memory::MesiState::Invalid, nullptr);
    }
}

}  // namespace simrv::memory
