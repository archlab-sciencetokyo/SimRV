/**
 * @file FetchStage.cpp
 * @brief Instruction Fetch (IF) stage implementation.
 */
#include <bit>
#include <cstdint>
#include <optional>
#include <utility>

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/debug/GdbStub.hpp"
#include "simrv/debug/SpikeLockstep.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/execute/ExecuteUnit.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/memory/Mmu.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "simrv/pipeline/OperationTraits.hpp"
#include "simrv/pipeline/RetirementEffects.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Helpers.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

using namespace simrv::isa;

namespace {
void release_instruction_eviction(Machine& machine, CPU& cpu) {
    const auto evicted = cpu.icache.take_last_eviction();
    if (!evicted.has_value()) return;
    simrv::memory::TlChannelC release{};
    release.opcode = simrv::memory::TlOpcodeC::Release;
    release.report = simrv::memory::report_for(evicted->state, simrv::memory::TlCap::ToN);
    release.size = simrv::memory::kTlBlockSize;
    release.source = simrv::memory::make_tl_source(static_cast<HartId>(cpu.state().mhartid),
                                                   simrv::memory::TlPort::Instruction);
    release.hart = static_cast<HartId>(cpu.state().mhartid);
    release.address = evicted->address;
    simrv::memory::TlChannelD acknowledgement{};
    (void)machine.memory().system_bus().release_line(release, acknowledgement);
}

[[nodiscard]] inline auto check_fetch_alignment(ArchState& state,
                                                simrv::pipeline::PipelineContext& ctx) noexcept
    -> bool {
    if (state.regs.xlen == 32) {
        state.pc = static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(state.pc)));
    }
    ctx.tlb_miss = false;
    const bool has_c = misa_has_extension(state.misa, isa::IsaExtension::C);
    const Word alignment_mask = has_c ? 1u : 3u;
    if ((state.pc & alignment_mask) != 0) {
        ctx.pending_exception = ExceptionCode::MisalignedFetch;
        ctx.pending_tval = state.pc;
        ctx.ir = isa::kNop32;
        ctx.op_id = isa::UNKNOWN;
        return false;
    }
    return true;
}

}  // namespace

// ==========================================
// IF (Instruction Fetch) Stage
// ==========================================

void CPU::run_fetch_stage(Machine& machine) {
    auto& ctx = active_context();
    if (!check_fetch_alignment(state_, ctx)) return;

    const bool split_page =
        ((state_.pc & ~simrv::memory::kPageMask) != ((state_.pc + 2) & ~simrv::memory::kPageMask));
    const bool translation_enabled =
        state_.priv != kPrivMachine &&
        simrv::xlen::satp_translation_enabled(state_.satp, state_.regs.xlen);

    fetch_address_translate(machine);

    if (simrv::compiler::unlikely(translation_enabled)) {
        fetch_resolve_page_walk(machine, 1);
        if (ca_state.waiting_for_interconnect) return;
        if (simrv::compiler::likely(!split_page)) {
            if (!ctx.pending_exception.has_value() && ctx.padr1 != kWordAllOnes) {
                ctx.padr2 = ctx.padr1 + 2;
            }
        } else {
            fetch_resolve_page_walk(machine, 2);
            if (ca_state.waiting_for_interconnect) return;
        }
    }

    fetch_read_instruction_word(machine);
    if (ca_state.waiting_for_interconnect) return;

    // Decode cache hit: skip decompression and decode dispatch for instructions fetched
    // from this virtual PC before in cycle-accurate pipeline mode. The cache holds the
    // fully-decoded result including the decompressed ir, op_id, imm, and all register fields.
    if (machine.runtime_profile.is_cycle_mode()) {
        if (auto* cached = decode_cache.lookup(ctx.cpc);
            simrv::compiler::likely(cached != nullptr)) {
            cached->copy_to(ctx);
            return;
        }
    }

    decode_and_normalize_instruction(machine);

    // Insert successful decode results into the cache for future fetches of this PC.
    // Instructions that raised exceptions are not cached; they re-decode each time,
    // which is correct since the exception path must re-evaluate validity in context.
    if (machine.runtime_profile.is_cycle_mode()) {
        if (simrv::compiler::likely(!ctx.pending_exception.has_value())) {
            CachedOp op;
            op.copy_from(ctx);
            decode_cache.insert(ctx.cpc, op);
        }
    }
}

void CPU::fetch_address_translate(Machine& machine) {
    auto& ctx = active_context();
    Word w_padr1 = kWordAllOnes;
    Word w_padr2 = kWordAllOnes;
    Word const w_vadr1 = state_.pc;
    Word const w_vadr2 = state_.pc + 2;

    ctx.cpc = state_.pc;

    if (state_.priv == kPrivMachine ||
        !simrv::xlen::satp_translation_enabled(state_.satp, state_.regs.xlen)) {
        w_padr1 = (state_.regs.xlen == 32) ? (w_vadr1 & 0xFFFFFFFFULL) : w_vadr1;
        w_padr2 = (state_.regs.xlen == 32) ? (w_vadr2 & 0xFFFFFFFFULL) : w_vadr2;
    } else {
        const bool split_page =
            ((w_vadr1 & ~simrv::memory::kPageMask) != (w_vadr2 & ~simrv::memory::kPageMask));
        const Word current_asid = simrv::xlen::satp_asid(state_.satp, state_.regs.xlen);

        const Address vpn1 = w_vadr1 >> 12;
        const size_t tlb_idx1 = core::CPU::soft_tlb_index(vpn1);
        const auto& se1 = soft_tlb_inst[tlb_idx1];
        if (simrv::compiler::likely(se1.matches(vpn1, current_asid, state_.priv, soft_tlb_epoch))) {
            w_padr1 = se1.paddr_base + (w_vadr1 & simrv::memory::kPageMask);
        } else {
            TLBEntry* tlb_e1 = tlb.lookup_inst_r(w_vadr1, current_asid, state_.priv);
            if (tlb_e1) {
                w_padr1 = tlb_e1->p_addr + (w_vadr1 & simrv::memory::kPageMask);
                Byte* const host_base = machine.ram_view().contains(tlb_e1->p_addr, 4096)
                                            ? machine.ram_view().unchecked_ptr(tlb_e1->p_addr)
                                            : nullptr;
                soft_tlb_inst[tlb_idx1].set(vpn1, current_asid, state_.priv, soft_tlb_epoch,
                                            tlb_e1->p_addr, host_base);
            }
        }

        if (simrv::compiler::likely(!split_page)) {
            if (w_padr1 != kWordAllOnes) {
                w_padr2 = w_padr1 + 2;
            }
        } else {
            const Address vpn2 = w_vadr2 >> 12;
            const size_t tlb_idx2 = core::CPU::soft_tlb_index(vpn2);
            const auto& se2 = soft_tlb_inst[tlb_idx2];
            if (simrv::compiler::likely(
                    se2.matches(vpn2, current_asid, state_.priv, soft_tlb_epoch))) {
                w_padr2 = se2.paddr_base + (w_vadr2 & simrv::memory::kPageMask);
            } else {
                TLBEntry* tlb_e2 = tlb.lookup_inst_r(w_vadr2, current_asid, state_.priv);
                if (tlb_e2) {
                    w_padr2 = tlb_e2->p_addr + (w_vadr2 & simrv::memory::kPageMask);
                    Byte* const host_base = machine.ram_view().contains(tlb_e2->p_addr, 4096)
                                                ? machine.ram_view().unchecked_ptr(tlb_e2->p_addr)
                                                : nullptr;
                    soft_tlb_inst[tlb_idx2].set(vpn2, current_asid, state_.priv, soft_tlb_epoch,
                                                tlb_e2->p_addr, host_base);
                }
            }
        }
    }
    ctx.padr1 = w_padr1;
    ctx.padr2 = w_padr2;
}

void CPU::fetch_resolve_page_walk(Machine& machine, int state) {
    auto& ctx = active_context();
    if (ctx.pending_exception.has_value()) {
        return;
    }

    Word w_padr = (state == 1) ? ctx.padr1 : ctx.padr2;
    Word* r_padr = (state == 1) ? &ctx.padr1 : &ctx.padr2;
    Word const w_vadr = (state == 1) ? state_.pc : state_.pc + 2;
    if (w_padr == kWordAllOnes) {
        ctx.tlb_miss = true;
        if constexpr (simrv::xlen::kIsXLen64) {
            if (simrv::compiler::unlikely(
                    !simrv::Mmu::is_canonical(w_vadr, state_.satp, state_.regs.xlen))) {
                ctx.pending_exception = ExceptionCode::FetchPageFault;
                ctx.pending_tval = w_vadr;
                return;
            }
        }

        auto translate_res = translate_stage_address(
            machine, VirtAddr{w_vadr}, PteAccess::Code, state_.priv, state_.regs.xlen,
            simrv::memory::TlPort::Instruction, ca_state.instruction_walk);
        if (!translate_res.has_value()) return;
        auto chain_res =
            (*translate_res)
                .and_then([&](PhysAddr phys) -> std::expected<void, TrapCause> {
                    w_padr = phys.raw();
                    const Word asid = simrv::xlen::satp_asid(state_.satp, state_.regs.xlen);
                    tlb.insert_inst_r(w_vadr, w_padr, asid, state_.priv);
                    const Address vpn = w_vadr >> 12;
                    const Address page_base = w_padr & ~simrv::memory::kPageMask;
                    Byte* const host_base = machine.ram_view().contains(page_base, 4096)
                                                ? machine.ram_view().unchecked_ptr(page_base)
                                                : nullptr;
                    soft_tlb_inst[core::CPU::soft_tlb_index(vpn)].set(
                        vpn, asid, state_.priv, soft_tlb_epoch, page_base, host_base);
                    return {};
                })
                .or_else([&](TrapCause error) -> std::expected<void, TrapCause> {
                    ctx.pending_exception = static_cast<ExceptionCode>(error);
                    ctx.pending_tval = w_vadr;
                    return {};
                });
    }
    *r_padr = w_padr;
}

auto CPU::translate_stage_address(Machine& machine, VirtAddr virtual_address, PteAccess access,
                                  PrivilegeLevel privilege, unsigned active_xlen,
                                  simrv::memory::TlPort port,
                                  simrv::pipeline::TimedPageWalkState& timed_walk)
    -> std::optional<std::expected<PhysAddr, TrapCause>> {
    auto* mmu = machine.memory_.mmu();
    if (machine.runtime_profile.is_instruction_mode()) {
        return mmu->translate(virtual_address, access, privilege, state_.mstatus, state_.satp,
                              active_xlen, true, &state_);
    }

    if (!timed_walk.active || timed_walk.walk.virtual_address != virtual_address ||
        timed_walk.walk.access != access || timed_walk.walk.privilege != privilege) {
        if (timed_walk.active && timed_walk.request_pending) {
            machine.memory_.system_bus().cancel_source(timed_walk.source);
        }
        timed_walk.walk = mmu->begin_page_walk(virtual_address, access, privilege, state_.mstatus,
                                               state_.satp, active_xlen, true, &state_);
        timed_walk.source =
            simrv::memory::make_tl_source(static_cast<HartId>(state_.mhartid), port);
        timed_walk.active = true;
        timed_walk.request_pending = false;
    }

    auto& walk = timed_walk.walk;
    if (timed_walk.request_pending) {
        simrv::memory::TileLinkBus::TimedResponse response{};
        if (!machine.memory_.system_bus().try_get_timed_response(timed_walk.source, response)) {
            ca_state.waiting_for_interconnect = true;
            return std::nullopt;
        }
        timed_walk.request_pending = false;
        if (response.payload.failed()) {
            Mmu::fail_page_walk_access(walk);
        } else if (walk.status == PageWalkStatus::ReadPte) {
            mmu->accept_page_walk_pte(walk, response.payload.data);
        } else if (walk.status == PageWalkStatus::WritePte) {
            Mmu::accept_page_walk_write(walk);
        }
    }

    if (walk.status == PageWalkStatus::Complete) {
        const PhysAddr result = walk.physical_address;
        timed_walk.reset();
        return std::expected<PhysAddr, TrapCause>{result};
    }
    if (walk.status == PageWalkStatus::Fault) {
        const TrapCause fault = walk.fault;
        timed_walk.reset();
        return std::expected<PhysAddr, TrapCause>{std::unexpected(fault)};
    }

    simrv::memory::TlChannelA request{};
    request.opcode = walk.status == PageWalkStatus::ReadPte ? simrv::memory::TlOpcodeA::Get
                                                            : simrv::memory::TlOpcodeA::LogicalData;
    request.size = walk.pte_size == 4 ? 2 : 3;
    request.source = timed_walk.source;
    request.hart = static_cast<HartId>(state_.mhartid);
    request.address = walk.pte_address;
    request.data = walk.status == PageWalkStatus::WritePte ? walk.pte_update_mask : walk.pte;
    machine.memory_.system_bus().send_request(request);
    timed_walk.request_pending = true;
    ca_state.waiting_for_interconnect = true;
    return std::nullopt;
}

void CPU::fetch_read_instruction_word(Machine& machine) {
    auto& ctx = active_context();
    if (ctx.pending_exception.has_value()) {
        return;
    }

    if (machine.runtime_profile.is_instruction_mode()) {
        if (simrv::compiler::likely(ctx.padr2 == ctx.padr1 + 2 &&
                                    machine.ram_view().contains(ctx.padr1, sizeof(uint32_t)))) {
            uint32_t val = 0;
            std::memcpy(&val, machine.ram_view().unchecked_ptr(ctx.padr1), 4);
            if ((val & 0x3) != 0x3) {
                ctx.ir_org = val & 0xFFFF;
            } else {
                ctx.ir_org = val;
            }
            return;
        }
    }

    if (simrv::compiler::likely(machine.memory_geometry().contains(ctx.padr1, sizeof(uint16_t)) &&
                                machine.memory_geometry().contains(ctx.padr2, sizeof(uint16_t)))) {
        const Address line_base =
            ctx.padr1 & ~(static_cast<Address>(simrv::cache::ICache::kLineBytes - 1u));
        const bool refill_in_progress = machine.runtime_profile.is_cycle_mode() &&
                                        ca_state.instruction_fill.active &&
                                        ca_state.instruction_fill.line_base == line_base;
        if (machine.runtime_profile.is_cycle_mode() && ca_state.instruction_prefetch.active &&
            ca_state.instruction_prefetch.request_pending) {
            simrv::memory::TileLinkBus::TimedResponse pf_timed{};
            if (machine.memory_.system_bus().try_get_timed_response(
                    ca_state.instruction_prefetch.source, pf_timed)) {
                ca_state.instruction_prefetch.request_pending = false;
                ca_state.instruction_prefetch.reset();
            }
        }

        auto trigger_prefetch = [&](Address base) {
            if (machine.runtime_profile.is_cycle_mode() &&
                pipeline_sim.config.enable_instruction_prefetch &&
                !ca_state.instruction_fill.active && !ca_state.instruction_prefetch.active) {
                const Address next_line = base + simrv::cache::ICache::kLineBytes;
                if (machine.memory_geometry().contains(next_line,
                                                       simrv::cache::ICache::kLineBytes) &&
                    icache.line_state(next_line) == simrv::memory::MesiState::Invalid) {
                    simrv::memory::TlChannelA pf_req{};
                    pf_req.opcode = simrv::memory::TlOpcodeA::Intent;
                    pf_req.intent = simrv::memory::TlIntent::PrefetchRead;
                    pf_req.size = simrv::memory::kTlBeatSize;
                    pf_req.hart = static_cast<HartId>(state_.mhartid);
                    pf_req.source = simrv::memory::make_tl_source(
                        pf_req.hart, simrv::memory::TlPort::Instruction);
                    pf_req.address = next_line;
                    if (machine.memory_.system_bus().send_request(pf_req)) {
                        ca_state.instruction_prefetch.active = true;
                        ca_state.instruction_prefetch.request_pending = true;
                        ca_state.instruction_prefetch.line_base = next_line;
                        ca_state.instruction_prefetch.source = pf_req.source;
                    }
                }
            }
        };

        const bool single_line = (ctx.padr2 == ctx.padr1 + 2) &&
                                 ((ctx.padr1 & (simrv::cache::ICache::kLineBytes - 1u)) <=
                                  (simrv::cache::ICache::kLineBytes - 4u));
        if (single_line) {
            uint32_t w_data = 0;
            if (!refill_in_progress && icache.read(ctx.padr1, w_data)) {
                trigger_prefetch(line_base);
                if ((w_data & 0x3) != 0x3) {
                    ctx.ir_org = w_data & 0xFFFF;
                } else {
                    ctx.ir_org = w_data;
                }
                return;
            }

            if (machine.runtime_profile.is_cycle_mode()) {
                static_assert(simrv::cache::ICache::kLineBytes ==
                              pipeline::InstructionFillState::kLineBytes);
                auto& fill = ca_state.instruction_fill;
                if (fill.active && fill.line_base != line_base) fill.reset();
                if (!fill.active) {
                    fill.active = true;
                    fill.line_base = line_base;
                    fill.source = simrv::memory::make_tl_source(static_cast<HartId>(state_.mhartid),
                                                                simrv::memory::TlPort::Instruction);
                }

                if (fill.request_pending) {
                    simrv::memory::TileLinkBus::TimedResponse timed{};
                    if (!machine.memory_.system_bus().try_get_timed_response(fill.source, timed)) {
                        ca_state.waiting_for_interconnect = true;
                        return;
                    }
                    fill.request_pending = false;
                    if (timed.payload.failed() || !timed.has_line_data) {
                        ctx.pending_exception = ExceptionCode::FaultFetch;
                        ctx.pending_tval = state_.pc;
                        fill.reset();
                        return;
                    }
                    icache.insert(line_base, timed.line_data.data(),
                                  simrv::memory::mesi_for(timed.payload.cap));
                    release_instruction_eviction(machine, *this);
                    machine.memory_.system_bus().grant_ack(
                        simrv::memory::TlChannelE{.sink = timed.payload.sink});
                    fill.reset();
                    trigger_prefetch(line_base);

                    const auto byte_offset = static_cast<size_t>(ctx.padr1 - line_base);
                    std::memcpy(&w_data, timed.line_data.data() + byte_offset, sizeof(w_data));
                    if ((w_data & 0x3) != 0x3) {
                        ctx.ir_org = w_data & 0xFFFF;
                    } else {
                        ctx.ir_org = w_data;
                    }
                    return;
                }

                simrv::memory::TlChannelA req{};
                req.opcode = simrv::memory::TlOpcodeA::AcquireBlock;
                req.grow = simrv::memory::TlGrow::NtoB;
                req.size = simrv::memory::kTlBlockSize;
                req.hart = static_cast<HartId>(state_.mhartid);
                req.source = fill.source;
                req.address = line_base;
                machine.memory_.system_bus().send_request(req);
                fill.request_pending = true;
                ca_state.waiting_for_interconnect = true;
                return;
            }

            std::array<Byte, simrv::cache::ICache::kLineBytes> line_data{};
            simrv::memory::TlChannelA req{};
            req.opcode = simrv::memory::TlOpcodeA::AcquireBlock;
            req.grow = simrv::memory::TlGrow::NtoB;
            req.size = simrv::memory::kTlBlockSize;
            req.hart = static_cast<HartId>(state_.mhartid);
            req.source =
                simrv::memory::make_tl_source(req.hart, simrv::memory::TlPort::Instruction);
            req.address = line_base;
            simrv::memory::TlChannelD resp{};
            if (!machine.memory_.system_bus().acquire_block(req, resp, line_data) ||
                resp.failed()) {
                ctx.pending_exception = ExceptionCode::FaultFetch;
                ctx.pending_tval = state_.pc;
                return;
            }
            icache.insert(line_base, line_data.data(), simrv::memory::mesi_for(resp.cap));
            release_instruction_eviction(machine, *this);
            machine.memory_.system_bus().grant_ack(simrv::memory::TlChannelE{.sink = resp.sink});
            const auto byte_offset = static_cast<size_t>(ctx.padr1 - line_base);
            std::memcpy(&w_data, line_data.data() + byte_offset, sizeof(w_data));
            if ((w_data & 0x3) != 0x3) {
                ctx.ir_org = w_data & 0xFFFF;
            } else {
                ctx.ir_org = w_data;
            }
            return;
        }

        auto fetch_halfword = [&](Address paddr, Address vaddr) -> std::optional<uint16_t> {
            uint16_t h_data = 0;
            const Address h_line_base =
                paddr & ~(static_cast<Address>(simrv::cache::ICache::kLineBytes - 1u));
            const bool h_refill_in_progress = machine.runtime_profile.is_cycle_mode() &&
                                              ca_state.instruction_fill.active &&
                                              ca_state.instruction_fill.line_base == h_line_base;

            if (!h_refill_in_progress && icache.read16(paddr, h_data)) {
                trigger_prefetch(h_line_base);
                return h_data;
            }

            std::array<Byte, simrv::cache::ICache::kLineBytes> line_data{};
            if (machine.runtime_profile.is_cycle_mode()) {
                static_assert(simrv::cache::ICache::kLineBytes ==
                              pipeline::InstructionFillState::kLineBytes);
                auto& fill = ca_state.instruction_fill;
                if (fill.active && fill.line_base != h_line_base) fill.reset();
                if (!fill.active) {
                    fill.active = true;
                    fill.line_base = h_line_base;
                    fill.source = simrv::memory::make_tl_source(static_cast<HartId>(state_.mhartid),
                                                                simrv::memory::TlPort::Instruction);
                }

                if (fill.request_pending) {
                    simrv::memory::TileLinkBus::TimedResponse timed{};
                    if (!machine.memory_.system_bus().try_get_timed_response(fill.source, timed)) {
                        ca_state.waiting_for_interconnect = true;
                        return std::nullopt;
                    }
                    fill.request_pending = false;
                    if (timed.payload.failed() || !timed.has_line_data) {
                        ctx.pending_exception = ExceptionCode::FaultFetch;
                        ctx.pending_tval = vaddr;
                        fill.reset();
                        return std::nullopt;
                    }
                    icache.insert(h_line_base, timed.line_data.data(),
                                  simrv::memory::mesi_for(timed.payload.cap));
                    release_instruction_eviction(machine, *this);
                    machine.memory_.system_bus().grant_ack(
                        simrv::memory::TlChannelE{.sink = timed.payload.sink});
                    fill.reset();
                    trigger_prefetch(h_line_base);

                    const auto byte_offset = static_cast<size_t>(paddr - h_line_base);
                    std::memcpy(&h_data, timed.line_data.data() + byte_offset, sizeof(h_data));
                    return h_data;
                }

                simrv::memory::TlChannelA req{};
                req.opcode = simrv::memory::TlOpcodeA::AcquireBlock;
                req.grow = simrv::memory::TlGrow::NtoB;
                req.size = simrv::memory::kTlBlockSize;
                req.hart = static_cast<HartId>(state_.mhartid);
                req.source = fill.source;
                req.address = h_line_base;
                machine.memory_.system_bus().send_request(req);
                fill.request_pending = true;
                ca_state.waiting_for_interconnect = true;
                return std::nullopt;
            }

            simrv::memory::TlChannelA req{};
            req.opcode = simrv::memory::TlOpcodeA::AcquireBlock;
            req.grow = simrv::memory::TlGrow::NtoB;
            req.size = simrv::memory::kTlBlockSize;
            req.hart = static_cast<HartId>(state_.mhartid);
            req.source =
                simrv::memory::make_tl_source(req.hart, simrv::memory::TlPort::Instruction);
            req.address = h_line_base;
            simrv::memory::TlChannelD resp{};
            if (!machine.memory_.system_bus().acquire_block(req, resp, line_data) ||
                resp.failed()) {
                ctx.pending_exception = ExceptionCode::FaultFetch;
                ctx.pending_tval = vaddr;
                return std::nullopt;
            }
            icache.insert(h_line_base, line_data.data(), simrv::memory::mesi_for(resp.cap));
            release_instruction_eviction(machine, *this);
            machine.memory_.system_bus().grant_ack(simrv::memory::TlChannelE{.sink = resp.sink});
            const auto byte_offset = static_cast<size_t>(paddr - h_line_base);
            std::memcpy(&h_data, line_data.data() + byte_offset, sizeof(h_data));
            return h_data;
        };

        const auto h1 = fetch_halfword(ctx.padr1, state_.pc);
        if (!h1.has_value()) {
            return;
        }
        if ((*h1 & 0x3) != 0x3) {
            ctx.ir_org = *h1;
        } else {
            const auto h2 = fetch_halfword(ctx.padr2, state_.pc + 2);
            if (!h2.has_value()) {
                return;
            }
            ctx.ir_org = (static_cast<uint32_t>(*h2) << 16) | *h1;
        }
    } else {
        Word ir_l = 0;
        Word ir_h = 0;

        simrv::memory::TlChannelA req_l{};
        req_l.opcode = simrv::memory::TlOpcodeA::Get;
        req_l.size = static_cast<uint8_t>(Funct3::Lhu) & 0x3;
        req_l.hart = static_cast<HartId>(state_.mhartid);
        req_l.source =
            simrv::memory::make_tl_source(req_l.hart, simrv::memory::TlPort::Instruction);
        req_l.address = ctx.padr1;
        machine.memory_.system_bus().send_request(req_l);
        simrv::memory::TlChannelD resp_l{};
        if (!machine.memory_.system_bus().get_response(req_l.source, resp_l) || resp_l.failed()) {
            ctx.pending_exception = ExceptionCode::FaultFetch;
            ctx.pending_tval = state_.pc;
            return;
        }
        ir_l = resp_l.data;

        simrv::pipeline::Decoder dec_temp(ir_l);
        if (!dec_temp.is_compressed()) {
            const bool translation_enabled =
                state_.priv != kPrivMachine &&
                simrv::xlen::satp_translation_enabled(state_.satp, state_.regs.xlen);
            if (translation_enabled && ctx.padr2 == kWordAllOnes) {
                fetch_resolve_page_walk(machine, 2);
            }

            if (!ctx.pending_exception.has_value()) {
                simrv::memory::TlChannelA req_h{};
                req_h.opcode = simrv::memory::TlOpcodeA::Get;
                req_h.size = static_cast<uint8_t>(Funct3::Lhu) & 0x3;
                req_h.hart = static_cast<HartId>(state_.mhartid);
                req_h.source =
                    simrv::memory::make_tl_source(req_h.hart, simrv::memory::TlPort::Instruction);
                req_h.address = ctx.padr2;
                machine.memory_.system_bus().send_request(req_h);
                simrv::memory::TlChannelD resp_h{};
                if (!machine.memory_.system_bus().get_response(req_h.source, resp_h) ||
                    resp_h.failed()) {
                    ctx.pending_exception = ExceptionCode::FaultFetch;
                    ctx.pending_tval = state_.pc + 2;
                    return;
                }
                ir_h = resp_h.data;
            }
        }

        ctx.ir_org = (ir_h << 16) | (ir_l & 0xFFFF);
    }
}

void CPU::decode_and_normalize_instruction(Machine& machine) {
    auto& ctx = active_context();
    if (simrv::compiler::unlikely(ctx.pending_exception.has_value())) {
        ctx.ir = isa::kNop32;
        return;
    }

    simrv::pipeline::Decoder dec_org(ctx.ir_org);
    bool const w_compressed = dec_org.is_compressed();
    Instruction const w_ir_tmp = w_compressed ? simrv::pipeline::decompressInstruction(
                                                    ctx.ir_org, state_.current_xlen() == 64)
                                              : ctx.ir_org;

    bool is_valid = true;
    if (simrv::compiler::unlikely(machine.configuration().isa.misa_profile != kMisaDefault)) {
        is_valid = instruction_enabled_by_misa(state_.misa, w_ir_tmp, w_compressed);
    }

    const isa::OperationId op_id = simrv::pipeline::decoder(w_ir_tmp);
    if (simrv::compiler::unlikely(op_id == isa::UNKNOWN)) {
        if (!machine.tui_enabled()) {
            simrv::log::warn("[DECODER] Unknown instruction: PC=0x{:x}, HEX=0x{:x}", state_.pc,
                             w_ir_tmp);
        }
        is_valid = false;
    }

    if (is_valid && state_.regs.xlen == 32 && requires_rv64(op_id)) {
        is_valid = false;
    }

    if (is_valid && state_.regs.xlen == 32) {
        if (op_id == OperationId::SLLI || op_id == OperationId::SRLI ||
            op_id == OperationId::SRAI) {
            if ((funct7_of(w_ir_tmp) & 0x01) != 0) {
                is_valid = false;
            }
        }
    }

    if (is_valid) {
        const auto op = opcode_of(w_ir_tmp);
        if (op == Opcode::Amo) {
            const auto f3 = std::to_underlying(funct3_of(w_ir_tmp));
            if (state_.regs.xlen == 32) {
                if (f3 != 2) {
                    is_valid = false;
                }
            } else {
                if (f3 != 2 && f3 != 3) {
                    is_valid = false;
                }
            }
        }
    }

    if (is_valid) {
        const auto op = opcode_of(w_ir_tmp);
        const bool is_vector =
            (op == Opcode::OpV) ||
            ((op == Opcode::LoadFp || op == Opcode::StoreFp) &&
             (funct3_of(w_ir_tmp) != Funct3::Fld && funct3_of(w_ir_tmp) != Funct3::Fsd &&
              static_cast<uint8_t>(funct3_of(w_ir_tmp)) != 2));
        const bool is_fp_op =
            !is_vector && ((op == Opcode::LoadFp) || (op == Opcode::StoreFp) ||
                           (op == Opcode::OpFp) || (op == Opcode::MAdd) || (op == Opcode::MSub) ||
                           (op == Opcode::NMAdd) || (op == Opcode::NMSub));
        if (is_vector) {
            if (simrv::compiler::unlikely((state_.mstatus & enum_mask(MstatusBit::Vs)) == 0)) {
                if (!machine.tui_enabled()) {
                    simrv::log::warn("[VS CHECK] VS is 0! mstatus=0x{:x}, Vs mask=0x{:x}",
                                     state_.mstatus, enum_mask(MstatusBit::Vs));
                }
                is_valid = false;
            }
        } else if (is_fp_op) {
            if (simrv::compiler::unlikely((state_.mstatus & enum_mask(MstatusBit::Fs)) == 0)) {
                is_valid = false;
            }
        }
    }

    if (simrv::compiler::likely(is_valid)) {
        ctx.ir = w_ir_tmp;
        ctx.op_id = op_id;
        ctx.cinsn = w_compressed ? 1U : 0U;

        simrv::pipeline::Decoder dec(w_ir_tmp);
        const auto op = dec.opcode();

        ctx.opcode = static_cast<Opcode>(op);
        ctx.rd = dec.rd();
        ctx.rs1 = dec.rs1();
        ctx.rs2 = dec.rs2();
        ctx.funct3 = static_cast<Funct3>(dec.funct3());
        ctx.funct5 = static_cast<Funct5Amo>((w_ir_tmp >> 27) & 0x1F);
        ctx.funct7 = dec.funct7();
        ctx.funct12 = (w_ir_tmp >> 20);

        switch (op) {
            case Opcode::Lui:
            case Opcode::Auipc:
                ctx.imm = dec.imm_u();
                break;
            case Opcode::Jal:
                ctx.imm = dec.imm_j();
                break;
            case Opcode::Branch:
                ctx.imm = dec.imm_b();
                break;
            case Opcode::Store:
            case Opcode::StoreFp:
                ctx.imm = dec.imm_s();
                break;
            default:
                ctx.imm = dec.imm_i();
                break;
        }

        ctx.traits = pipeline::operation::make_dependency_traits(op_id, ctx.opcode, ctx.rd,
                                                                 std::to_underlying(ctx.funct5));
    } else {
        ctx.pending_exception = ExceptionCode::IllegalInstruction;
        ctx.pending_tval = ctx.ir_org;
        ctx.ir = isa::kNop32;
        ctx.op_id = isa::UNKNOWN;
        ctx.cinsn = w_compressed ? 1U : 0U;
        ctx.opcode = static_cast<Opcode>(0);
        ctx.rd = RegId::Zero;
        ctx.rs1 = RegId::Zero;
        ctx.rs2 = RegId::Zero;
        ctx.traits = {};
    }
}

void CPU::run_fetch_stage_baremetal(Machine& machine) {
    auto& ctx = active_context();
    if (!check_fetch_alignment(state_, ctx)) return;
    ctx.cpc = state_.pc;

    ctx.padr1 = (state_.regs.xlen == 32) ? (state_.pc & 0xFFFFFFFFULL) : state_.pc;
    ctx.padr2 = (state_.regs.xlen == 32) ? ((state_.pc + 2) & 0xFFFFFFFFULL) : (state_.pc + 2);

    // Fast path: DRAM physical fetch — valid only while the MMU has never been
    // enabled.  The latch is set once on the first satp write that activates
    // translation, so the branch predictor sees this as "not taken" for nearly
    // all cycles of a physical-only run and switches to "always taken" after
    // the OS enables virtual memory.
    if (simrv::compiler::likely(machine.memory_geometry().contains(ctx.padr1, sizeof(uint16_t)) &&
                                !machine.s_mmu_ever_used)) {
        const uint16_t h1 = simrv::memory::ram_read_fast(
            ctx.padr1, static_cast<Instruction>(Funct3::Lhu), machine.ram_view());
        if ((h1 & 0x3) != 0x3) {
            ctx.ir_org = h1;
        } else {
            if (simrv::compiler::unlikely(
                    !machine.memory_geometry().contains(ctx.padr2, sizeof(uint16_t)))) {
                ctx.pending_exception = ExceptionCode::FaultFetch;
                ctx.pending_tval = state_.pc + 2;
                ctx.ir = isa::kNop32;
                ctx.op_id = isa::UNKNOWN;
                return;
            }
            const uint16_t h2 = simrv::memory::ram_read_fast(
                ctx.padr2, static_cast<Instruction>(Funct3::Lhu), machine.ram_view());
            ctx.ir_org = (static_cast<uint32_t>(h2) << 16) | h1;
        }
    } else {
        // Slow path: MMU may be active.  Compute translation_enabled here (not
        // on every cycle in the fast path above).
        const bool split_page = ((state_.pc & ~simrv::memory::kPageMask) !=
                                 ((state_.pc + 2) & ~simrv::memory::kPageMask));
        const bool translation_enabled =
            state_.priv != kPrivMachine &&
            simrv::xlen::satp_translation_enabled(state_.satp, state_.regs.xlen);

        fetch_address_translate(machine);

        if (simrv::compiler::unlikely(translation_enabled)) {
            fetch_resolve_page_walk(machine, 1);
            if (simrv::compiler::likely(!split_page)) {
                if (!ctx.pending_exception.has_value() && ctx.padr1 != kWordAllOnes) {
                    ctx.padr2 = ctx.padr1 + 2;
                }
            }
        }

        fetch_read_instruction_word(machine);
    }
    decode_and_normalize_instruction(machine);
}

}  // namespace simrv::core
