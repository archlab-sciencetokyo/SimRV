/**
 * @file PipelineStages.cpp
 * @brief Consolidated pipeline stages implementation for Machine.
 */
#include <bit>
#include <cstdint>
#include <optional>
#include <utility>

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/debug/SpikeLockstep.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/execute/ExecuteUnit.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/memory/Mmu.hpp"
#include "simrv/pipeline/Decoder.hpp"
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

void CPU::fetch_address_translate(Machine& /*machine*/) {
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

        TLBEntry* tlb_e1 = tlb.lookup_inst_r(w_vadr1, current_asid, state_.priv);
        if (tlb_e1) {
            w_padr1 = tlb_e1->p_addr + (w_vadr1 & simrv::memory::kPageMask);
        }

        if (simrv::compiler::likely(!split_page)) {
            if (w_padr1 != kWordAllOnes) {
                w_padr2 = w_padr1 + 2;
            }
        } else {
            TLBEntry* tlb_e2 = tlb.lookup_inst_r(w_vadr2, current_asid, state_.priv);
            if (tlb_e2) {
                w_padr2 = tlb_e2->p_addr + (w_vadr2 & simrv::memory::kPageMask);
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
                    tlb.insert_inst_r(w_vadr, w_padr,
                                      simrv::xlen::satp_asid(state_.satp, state_.regs.xlen),
                                      state_.priv);
                    return {};
                })
                .or_else([&](TrapCause error) -> std::expected<void, TrapCause> {
                    ctx.pending_exception = static_cast<ExceptionCode>(error);
                    ctx.pending_tval = w_vadr;
                    return {};
                });
        (void)chain_res;
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
        auto fetch_halfword = [&](Address paddr, Address vaddr) -> std::optional<uint16_t> {
            uint16_t h_data = 0;
            const Address line_base =
                paddr & ~(static_cast<Address>(simrv::cache::ICache::kLineBytes - 1u));
            // A timed refill is one cache miss, not one miss per cycle spent resuming it.
            // Avoid probing (and incrementing miss statistics) while this line is in flight.
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

            if (!refill_in_progress && icache.read16(paddr, h_data)) {
                if (machine.runtime_profile.is_cycle_mode() &&
                    pipeline_sim.config.enable_instruction_prefetch &&
                    !ca_state.instruction_fill.active && !ca_state.instruction_prefetch.active) {
                    const Address next_line = line_base + simrv::cache::ICache::kLineBytes;
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
                return h_data;
            }

            std::array<Byte, simrv::cache::ICache::kLineBytes> line_data{};
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
                        return std::nullopt;
                    }
                    fill.request_pending = false;
                    if (timed.payload.failed()) {
                        ctx.pending_exception = ExceptionCode::FaultFetch;
                        ctx.pending_tval = vaddr;
                        fill.reset();
                        return std::nullopt;
                    }
                    if (!timed.has_line_data) {
                        ctx.pending_exception = ExceptionCode::FaultFetch;
                        ctx.pending_tval = vaddr;
                        fill.reset();
                        return std::nullopt;
                    }
                    icache.insert(line_base, timed.line_data.data(),
                                  simrv::memory::mesi_for(timed.payload.cap));
                    release_instruction_eviction(machine, *this);
                    machine.memory_.system_bus().grant_ack(
                        simrv::memory::TlChannelE{.sink = timed.payload.sink});
                    fill.reset();

                    // Hardware next-line instruction stream prefetcher
                    if (pipeline_sim.config.enable_instruction_prefetch) {
                        const Address next_line = line_base + simrv::cache::ICache::kLineBytes;
                        if (machine.memory_geometry().contains(next_line,
                                                               simrv::cache::ICache::kLineBytes) &&
                            icache.line_state(next_line) == simrv::memory::MesiState::Invalid &&
                            !ca_state.instruction_prefetch.active) {
                            simrv::memory::TlChannelA pf_req{};
                            pf_req.opcode = simrv::memory::TlOpcodeA::Intent;
                            pf_req.intent = simrv::memory::TlIntent::PrefetchRead;
                            pf_req.size = simrv::memory::kTlBeatSize;
                            pf_req.hart = static_cast<HartId>(state_.mhartid);
                            pf_req.source = fill.source;
                            pf_req.address = next_line;
                            if (machine.memory_.system_bus().send_request(pf_req)) {
                                ca_state.instruction_prefetch.active = true;
                                ca_state.instruction_prefetch.request_pending = true;
                                ca_state.instruction_prefetch.line_base = next_line;
                                ca_state.instruction_prefetch.source = pf_req.source;
                            }
                        }
                    }

                    const auto byte_offset = static_cast<size_t>(paddr - line_base);
                    std::memcpy(&h_data, timed.line_data.data() + byte_offset, sizeof(h_data));
                    return h_data;
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
                return std::nullopt;
            }

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
                ctx.pending_tval = vaddr;
                return std::nullopt;
            }
            icache.insert(line_base, line_data.data(), simrv::memory::mesi_for(resp.cap));
            release_instruction_eviction(machine, *this);
            machine.memory_.system_bus().grant_ack(simrv::memory::TlChannelE{.sink = resp.sink});
            const auto byte_offset = static_cast<size_t>(paddr - line_base);
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
    } else {
        ctx.pending_exception = ExceptionCode::IllegalInstruction;
        ctx.pending_tval = ctx.ir_org;
        ctx.ir = isa::kNop32;
        ctx.op_id = isa::UNKNOWN;
    }

    ctx.cinsn = w_compressed ? 1U : 0U;
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

// ==========================================
// ID (Instruction Decode) Stage
// ==========================================

void CPU::run_decode_stage(Machine& machine) {
    decode_fields(machine);
    fetch_operands(machine);
}

void CPU::decode_fields(Machine& /*machine*/) {
    auto& ctx = active_context();
    if (simrv::compiler::unlikely(ctx.pending_exception.has_value())) {
        return;
    }

    simrv::pipeline::Decoder dec(ctx.ir);
    const auto op = dec.opcode();

    ctx.opcode = static_cast<Opcode>(op);
    ctx.rd = dec.rd();
    ctx.rs1 = dec.rs1();
    ctx.rs2 = dec.rs2();
    ctx.funct3 = static_cast<Funct3>(dec.funct3());
    ctx.funct5 = static_cast<Funct5Amo>((ctx.ir >> 27) & 0x1F);
    ctx.funct7 = dec.funct7();
    ctx.funct12 = (ctx.ir >> 20);

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
}

void CPU::fetch_operands(Machine& /*machine*/) {
    auto& ctx = active_context();
    if (simrv::compiler::unlikely(ctx.pending_exception.has_value())) {
        return;
    }

    const auto opcode = ctx.opcode;
    const auto funct3 = ctx.funct3;
    const Instruction funct12 = ctx.funct12;

    ctx.rrs1 = state_.regs.read(ctx.rs1);
    ctx.rrs2 = state_.regs.read(ctx.rs2);

    if (simrv::compiler::likely(opcode != Opcode::System)) {
        ctx.rcsr = 0;
        return;
    }

    CSRAddress const w_csr_addr =
        (funct3 != Funct3::Priv) ? static_cast<CSRAddress>(funct12)
        : (funct12 == static_cast<Instruction>(Funct12Priv::Ecall)) ? csr_addr(Csr::Mtvec)
        : (funct12 == static_cast<Instruction>(Funct12Priv::Uret))  ? csr_addr(Csr::Uepc)
        : (funct12 == static_cast<Instruction>(Funct12Priv::Sret))  ? csr_addr(Csr::Sepc)
        : (funct12 == static_cast<Instruction>(Funct12Priv::Mret))  ? csr_addr(Csr::Mepc)
                                                                    : 0;

    if (funct3 == Funct3::Priv) {
        if (!TrapController::canExecutePrivilegedInstruction(state_.priv, state_.misa,
                                                             state_.mstatus, funct12, ctx.funct7)) {
            ctx.pending_exception = ExceptionCode::IllegalInstruction;
            ctx.pending_tval = ctx.ir_org;
            return;
        }
    } else {
        const bool is_write =
            ((static_cast<uint8_t>(funct3) & 0x3u) == 0x1u) || (std::to_underlying(ctx.rs1) != 0);
        if (!TrapController::canAccessCsr(state_.priv, state_.misa, w_csr_addr, is_write)) {
            ctx.pending_exception = ExceptionCode::IllegalInstruction;
            ctx.pending_tval = ctx.ir_org;
            return;
        }
        if (w_csr_addr == csr_addr(Csr::Satp) && state_.priv == kPrivSupervisor &&
            (state_.mstatus & enum_mask(MstatusBit::Tvm)) != 0) {
            ctx.pending_exception = ExceptionCode::IllegalInstruction;
            ctx.pending_tval = ctx.ir_org;
            return;
        }

        if (static_cast<PrivilegeLevel>(std::to_underlying(state_.priv)) < kPrivMachine) {
            if ((w_csr_addr >= 0xC00 && w_csr_addr <= 0xC1F) ||
                (w_csr_addr >= 0xC80 && w_csr_addr <= 0xC9F)) {
                const Word counter_bit = 1u << (w_csr_addr & 0x1Fu);
                bool access_denied = (state_.mcounteren & counter_bit) == 0;
                if (state_.priv == kPrivUser) {
                    access_denied = access_denied || ((state_.scounteren & counter_bit) == 0);
                }
                if (access_denied) {
                    ctx.pending_exception = ExceptionCode::IllegalInstruction;
                    ctx.pending_tval = ctx.ir_org;
                    return;
                }
            }
        }
    }

    if (simrv::compiler::unlikely(w_csr_addr == csr_addr(Csr::Fflags) ||
                                  w_csr_addr == csr_addr(Csr::Frm) ||
                                  w_csr_addr == csr_addr(Csr::Fcsr))) {
        if ((state_.mstatus & enum_mask(MstatusBit::Fs)) == 0) {
            ctx.pending_exception = ExceptionCode::IllegalInstruction;
            ctx.pending_tval = ctx.ir_org;
            return;
        }
    }

    if (funct3 != Funct3::Priv) {
        auto res = read_csr(w_csr_addr);
        if (!res) {
            ctx.pending_exception = res.error();
            ctx.pending_tval = ctx.ir_org;
            return;
        }
        ctx.rcsr = *res;
        ctx.rcsr_write = ctx.rcsr;
        if (w_csr_addr == csr_addr(Csr::Mip)) {
            // SEIP reads as software || PLIC, but CSRRS/CSRRC operate only on
            // the software-writable component (Privileged ISA 1.13).
            ctx.rcsr_write = mip_rmw_base(ctx.rcsr, state_.seip_software);
        }
    } else {
        if (w_csr_addr != 0) {
            auto res = read_csr(w_csr_addr);
            if (res) ctx.rcsr = *res;
        }
    }
}

// ==========================================
// EX (Execute) Stage
// ==========================================

void CPU::run_execute_stage(Machine& machine) { execute_core(machine); }

void CPU::execute_core(Machine& machine) {
    auto& ctx = active_context();
    if (simrv::compiler::unlikely(ctx.pending_exception.has_value())) {
        return;
    }

    if (ctx.op_id >= isa::OperationId::VSETVLI && ctx.op_id <= isa::OperationId::VWSLL_VI) {
        ctx.tkn = false;
        execute::ExecuteUnit::execute_vector(*this, machine, ctx.op_id, ctx.ir);
        // VS dirty tracking may be imprecise. Conservatively mark it Dirty
        // after dispatch because vector instructions can update registers,
        // vl/vtype/vstart/vxsat, or partial state before a restartable fault.
        state_.mstatus |= enum_mask(MstatusBit::Vs);
        return;
    }

    ctx.fp_wb_enable = false;
    ctx.int_wb_from_fp = false;

    switch (ctx.opcode) {
        case Opcode::Lui:
            ctx.tkn = false;
            ctx.wb_data = ctx.imm;
            break;
        case Opcode::Auipc:
            ctx.tkn = false;
            ctx.wb_data = (ctx.cpc + ctx.imm).raw();
            break;
        case Opcode::Jal:
            ctx.tkn = true;
            ctx.wb_data = (ctx.cpc + ((ctx.cinsn != 0u) ? 2 : 4)).raw();
            ctx.jmp_pc = (ctx.cpc + ctx.imm).raw();
            break;
        case Opcode::Jalr:
            ctx.tkn = true;
            ctx.wb_data = (ctx.cpc + ((ctx.cinsn != 0u) ? 2 : 4)).raw();
            ctx.jmp_pc = (ctx.rrs1 + ctx.imm) & ~static_cast<Register>(1);
            if (state_.regs.xlen == 32) {
                ctx.jmp_pc =
                    static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(ctx.jmp_pc)));
            }
            break;
        case Opcode::Op:
            ctx.tkn = false;
            ctx.wb_data =
                execute::ExecuteUnit::aluInt(ctx.rrs1, ctx.rrs2, ctx.op_id, state_.regs.xlen);
            break;
        case Opcode::OpImm:
            ctx.tkn = false;
            ctx.wb_data =
                execute::ExecuteUnit::aluInt(ctx.rrs1, ctx.imm, ctx.op_id, state_.regs.xlen);
            break;
        case Opcode::OpImm32:
            ctx.tkn = false;
            ctx.wb_data = execute::ExecuteUnit::aluIntW(ctx.rrs1, ctx.imm, ctx.op_id);
            break;
        case Opcode::Op32:
            ctx.tkn = false;
            ctx.wb_data = execute::ExecuteUnit::aluIntW(ctx.rrs1, ctx.rrs2, ctx.op_id);
            break;
        case Opcode::Load:
        case Opcode::LoadFp:
        case Opcode::Store:
            ctx.tkn = false;
            ctx.mem_addr = ctx.rrs1 + ctx.imm;
            break;
        case Opcode::StoreFp:
            ctx.tkn = false;
            ctx.mem_addr = ctx.rrs1 + ctx.imm;
            ctx.fp_mem_wdata = state_.regs.read_fp(ctx.rs2);
            break;
        case Opcode::MiscMem:
            ctx.tkn = false;
            if (ctx.funct3 == Funct3::FenceI) {
                icache.flush();
                dcache.flush();
                decode_cache.flush();
            } else if (ctx.ir == 0x0100000f) {
                // RISC-V PAUSE (Zihintpause / cpu_relax): architectural NOP pipeline hint
            }
            break;
        case Opcode::Branch:
            ctx.tkn =
                execute::ExecuteUnit::branchTaken(ctx.rrs1, ctx.rrs2, ctx.funct3, state_.regs.xlen);
            ctx.jmp_pc = (ctx.cpc + ctx.imm).raw();
            if (state_.regs.xlen == 32) {
                ctx.jmp_pc =
                    static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(ctx.jmp_pc)));
            }
            break;
        case Opcode::Amo:
            ctx.tkn = false;
            ctx.mem_addr = ctx.rrs1;
            if (ctx.funct5 == Funct5Amo::Sc) {
                // Zalrsc requires natural alignment even when the reservation will make SC fail.
                // A failed reservation must not bypass the architecturally required exception.
                if (!amo_address_aligned(ctx.mem_addr, ctx.funct3)) {
                    ctx.pending_exception = ExceptionCode::MisalignedStore;
                    ctx.pending_tval = ctx.mem_addr;
                    break;
                }
                const bool native_success =
                    machine.memory_.reservation_table().check_and_clear_reservation(
                        static_cast<HartId>(state_.mhartid), ctx.rrs1);
                state_.reserved = native_success ? 1 : 0;
                state_.load_res = ctx.rrs1;
                ctx.wb_data = native_success ? 0 : 1;
                if (machine.lockstep() && machine.lockstep()->is_running()) {
                    if (native_success) {
                        auto sc_success_opt = machine.lockstep()->determine_sc_success();
                        if (sc_success_opt.has_value() && !sc_success_opt.value()) {
                            ctx.wb_data = 1;
                        }
                    }
                }
            }
            break;
        case Opcode::System:
            execute_system(machine);
            break;
        case Opcode::Custom0:
            ctx.tkn = false;
            break;
        case Opcode::MAdd:
        case Opcode::MSub:
        case Opcode::NMAdd:
        case Opcode::NMSub:
        case Opcode::OpFp:
            execute_fp(machine);
            break;
        default:
            ctx.tkn = false;
            ctx.pending_exception = ExceptionCode::IllegalInstruction;
            ctx.pending_tval = ctx.ir;
            break;
    }
}

void CPU::execute_system(Machine& machine) {
    auto& ctx = active_context();
    if (ctx.funct3 == Funct3::Priv) {
        switch (static_cast<Funct12Priv>(ctx.funct12)) {
            case Funct12Priv::Ecall:
                ctx.wb_data_csr =
                    enum_mask(ExceptionCode::UserEcall) + std::to_underlying(state_.priv);
                ctx.pending_exception = static_cast<ExceptionCode>(
                    enum_mask(ExceptionCode::UserEcall) + std::to_underlying(state_.priv));
                break;
            case Funct12Priv::Ebreak: {
                bool semihost_handled = false;
                const bool in_dram = machine.memory_geometry().contains(state_.pc - 4) &&
                                     machine.memory_geometry().contains(state_.pc + 4);
                if (in_dram) {
                    const Word inst_prev = simrv::memory::ram_read_fast(
                        state_.pc - 4, static_cast<Instruction>(Funct3::Lw), machine.ram_view());
                    const Word inst_next = simrv::memory::ram_read_fast(
                        state_.pc + 4, static_cast<Instruction>(Funct3::Lw), machine.ram_view());
                    if (inst_prev == 0x01f01013 && inst_next == 0x40705013) {
                        semihost_handled = true;
                        const Word semihost_op = state_.regs.read(RegId::A0);
                        const Address arg_ptr = state_.regs.read(RegId::A1);

                        switch (semihost_op) {
                            case 0x05: {
                                const Instruction load_op =
                                    kIsXLen64 ? static_cast<Instruction>(Funct3::Ld)
                                              : static_cast<Instruction>(Funct3::Lw);
                                const Address fd = simrv::memory::ram_read_fast(arg_ptr, load_op,
                                                                                machine.ram_view());
                                const Address buf_addr = simrv::memory::ram_read_fast(
                                    arg_ptr + (kIsXLen64 ? 8 : 4), load_op, machine.ram_view());
                                const Address len = simrv::memory::ram_read_fast(
                                    arg_ptr + (kIsXLen64 ? 16 : 8), load_op, machine.ram_view());
                                (void)fd;

                                if (machine.memory_geometry().contains(buf_addr)) {
                                    for (Address i = 0; i < len; ++i) {
                                        const auto ch = static_cast<uint8_t>(
                                            simrv::memory::ram_read_fast(
                                                buf_addr + i, static_cast<Instruction>(Funct3::Lb),
                                                machine.ram_view()) &
                                            0xFF);
                                        if (machine.tui_enabled() && machine.tui_controller()) {
                                            machine.tui_controller()->handle_char_write(
                                                static_cast<char>(ch));
                                        } else {
                                            (void)(::write(STDOUT_FILENO, &ch, 1) == 0);
                                        }
                                    }
                                }
                                state_.regs.write(RegId::A0, 0);
                                break;
                            }
                            case 0x03: {
                                if (machine.memory_geometry().contains(arg_ptr)) {
                                    const auto ch = static_cast<uint8_t>(
                                        simrv::memory::ram_read_fast(
                                            arg_ptr, static_cast<Instruction>(Funct3::Lb),
                                            machine.ram_view()) &
                                        0xFF);
                                    if (machine.tui_enabled() && machine.tui_controller()) {
                                        machine.tui_controller()->handle_char_write(
                                            static_cast<char>(ch));
                                    } else {
                                        (void)(::write(STDOUT_FILENO, &ch, 1) == 0);
                                    }
                                }
                                state_.regs.write(RegId::A0, 0);
                                break;
                            }
                            case 0x04: {
                                Address ptr = arg_ptr;
                                if (machine.memory_geometry().contains(ptr)) {
                                    while (true) {
                                        const auto ch = static_cast<uint8_t>(
                                            simrv::memory::ram_read_fast(
                                                ptr, static_cast<Instruction>(Funct3::Lb),
                                                machine.ram_view()) &
                                            0xFF);
                                        if (ch == 0) break;
                                        if (machine.tui_enabled() && machine.tui_controller()) {
                                            machine.tui_controller()->handle_char_write(
                                                static_cast<char>(ch));
                                        } else {
                                            (void)(::write(STDOUT_FILENO, &ch, 1) == 0);
                                        }
                                        ptr++;
                                    }
                                }
                                state_.regs.write(RegId::A0, 0);
                                break;
                            }
                            default:
                                simrv::log::warn("__ Unhandled semihosting op: 0x{:02x}",
                                                 semihost_op);
                                state_.regs.write(RegId::A0, static_cast<Word>(-1));
                                break;
                        }

                        ctx.tkn = true;
                        ctx.jmp_pc = state_.pc + 8;
                    }
                }

                if (!semihost_handled) {
                    ctx.wb_data_csr = enum_mask(ExceptionCode::Breakpoint);
                    ctx.pending_exception = ExceptionCode::Breakpoint;
                    ctx.tkn = false;
                }
                break;
            }
            case Funct12Priv::Uret:
            case Funct12Priv::Sret:
            case Funct12Priv::Mret:
                ctx.tkn = true;
                ctx.jmp_pc = ctx.rcsr;
                break;
            case Funct12Priv::Wfi: {
                ctx.tkn = false;
                if ((state_.mip & state_.mie) == 0) {
                    const Counter cur_cmp = clint_mmio.mtimecmp.load(std::memory_order_relaxed);
                    if (cur_cmp > clint_mmio.mtime.load(std::memory_order_relaxed) &&
                        cur_cmp != std::numeric_limits<Counter>::max()) {
                        clint_mmio.mtime.store(cur_cmp, std::memory_order_relaxed);
                        evaluate_timer_interrupt();
                    }
                }
                break;
            }
            default:
                if (ctx.funct7 == static_cast<Instruction>(Funct7Priv::SfenceVma)) {
                    ctx.tkn = false;
                }
                break;
        }
    } else {
        const auto csr_val_imm = ((std::to_underlying(ctx.funct3) & 4) != 0)
                                     ? static_cast<ImmValue>(std::to_underlying(ctx.rs1))
                                     : ctx.imm;
        auto csr_result =
            execute::ExecuteUnit::csrWriteValue(ctx.rcsr_write, ctx.rrs1, csr_val_imm, ctx.funct3);
        if (csr_result.has_value()) {
            ctx.tkn = false;
            ctx.wb_data_csr = csr_result.value();
        } else {
            ctx.pending_exception = static_cast<ExceptionCode>(csr_result.error());
            ctx.pending_tval = ctx.ir;
        }
    }
}

void CPU::execute_fp(Machine& /*machine*/) {
    auto& ctx = active_context();
    switch (ctx.opcode) {
        case Opcode::MAdd:
        case Opcode::MSub:
        case Opcode::NMAdd:
        case Opcode::NMSub: {
            ctx.tkn = false;
            const Word rm =
                (enum_mask(ctx.funct3) == 7) ? ((state_.fcsr >> 5) & 0x7) : enum_mask(ctx.funct3);
            if (simrv::compiler::unlikely(rm >= 5)) {
                ctx.pending_exception = ExceptionCode::IllegalInstruction;
                ctx.pending_tval = ctx.ir;
                break;
            }
            const Word fmt = ctx.funct7 & 0x3;
            const Word rs3 = (ctx.ir >> 27) & 0x1F;
            const CSRValue old_fcsr = state_.fcsr;
            const auto fp = execute::ExecuteUnit::fusedFp(
                ctx.opcode, fmt, std::to_underlying(ctx.rs1), std::to_underlying(ctx.rs2), rs3,
                enum_mask(ctx.funct3), state_.regs.fp_data_ptr(), state_.fcsr);
            if (state_.fcsr != old_fcsr) {
                state_.mstatus |= enum_mask(MstatusBit::Fs);
            }
            ctx.fp_wb_data = fp.fp_wb_data;
            ctx.fp_wb_enable = fp.fp_wb_enable;
            break;
        }
        case Opcode::OpFp: {
            ctx.tkn = false;
            const Word rm =
                (enum_mask(ctx.funct3) == 7) ? ((state_.fcsr >> 5) & 0x7) : enum_mask(ctx.funct3);
            if (simrv::compiler::unlikely(rm >= 5)) {
                ctx.pending_exception = ExceptionCode::IllegalInstruction;
                ctx.pending_tval = ctx.ir;
                break;
            }
            const CSRValue old_fcsr = state_.fcsr;
            const auto fp = execute::ExecuteUnit::opFp(
                ctx.funct7, ctx.funct3, std::to_underlying(ctx.rs1), std::to_underlying(ctx.rs2),
                ctx.rrs1, state_.regs.fp_data_ptr(), state_.fcsr);
            if (state_.fcsr != old_fcsr) {
                state_.mstatus |= enum_mask(MstatusBit::Fs);
            }
            ctx.wb_data = fp.int_wb_data;
            ctx.int_wb_from_fp = fp.int_wb_enable;
            ctx.fp_wb_data = fp.fp_wb_data;
            ctx.fp_wb_enable = fp.fp_wb_enable;
            break;
        }
        default:
            break;
    }
}

// ==========================================
// MEM (Memory) Stage
// ==========================================

void CPU::run_memory_stage(Machine& machine) {
    if (active_context().op_id >= isa::OperationId::VSETVLI &&
        active_context().op_id <= isa::OperationId::VWSLL_VI) {
        return;
    }
    memory_load_phase(machine);
    memory_prepare_store_data(machine);
    memory_store_phase(machine);
    if (ca_state.waiting_for_interconnect) return;
}

void CPU::memory_load_phase(Machine& machine) {
    auto& ctx = active_context();
    if (ctx.pending_exception.has_value()) {
        return;
    }

    const auto opcode = static_cast<Opcode>(ctx.opcode);
    const auto funct5 = static_cast<Funct5Amo>(ctx.funct5);

    if (opcode == Opcode::Load || (opcode == Opcode::Amo && funct5 != Funct5Amo::Sc)) {
        ctx.mem_rdata =
            simrv::memory::MemoryAccess::loadInt(machine.memory_, *this, ctx.mem_addr, ctx.funct3);
    }

    if (opcode == Opcode::LoadFp) {
        ctx.fp_mem_rdata =
            simrv::memory::MemoryAccess::loadFp(machine.memory_, *this, ctx.mem_addr, ctx.funct3);
    }

    if (opcode == Opcode::Amo && funct5 == Funct5Amo::Lr) {
        state_.load_res = ctx.mem_addr;
        state_.reserved = 1;
        machine.memory_.reservation_table().set_reservation(static_cast<HartId>(state_.mhartid),
                                                            ctx.mem_addr);
    }
}

void CPU::memory_prepare_store_data(Machine& /*machine*/) {
    auto& ctx = active_context();
    const auto opcode = static_cast<Opcode>(ctx.opcode);
    const auto funct5 = static_cast<Funct5Amo>(ctx.funct5);
    ctx.mem_wdata = (opcode != Opcode::Amo || funct5 == Funct5Amo::Sc)
                        ? ctx.rrs2
                        : execute::ExecuteUnit::aluAmo(ctx.rrs2, ctx.mem_rdata, funct5, ctx.funct3);

    if (opcode == Opcode::StoreFp) {
        ctx.mem_wdata =
            static_cast<Register>(ctx.fp_mem_wdata & static_cast<FloatingRegister>(kLower32Mask));
    }
}

void CPU::memory_store_phase(Machine& machine) {
    auto& ctx = active_context();
    if (ctx.pending_exception.has_value()) {
        return;
    }

    const auto opcode = static_cast<Opcode>(ctx.opcode);
    const auto funct5 = static_cast<Funct5Amo>(ctx.funct5);

    if ((opcode == Opcode::Store) ||
        (opcode == Opcode::Amo &&
         (funct5 == Funct5Amo::Sc && (ctx.wb_data == 0u) && (state_.reserved != 0u))) ||
        (opcode == Opcode::Amo && funct5 != Funct5Amo::Lr && funct5 != Funct5Amo::Sc)) {
        simrv::memory::MemoryAccess::storeInt(machine.memory_, *this, ctx.mem_addr, ctx.mem_wdata,
                                              ctx.funct3);
    }

    if (opcode == Opcode::StoreFp) {
        simrv::memory::MemoryAccess::storeFp(machine.memory_, *this, ctx.mem_addr, ctx.fp_mem_wdata,
                                             ctx.funct3);
    }

    if (ca_state.waiting_for_interconnect) return;

    if ((opcode == Opcode::Store) || (opcode == Opcode::StoreFp) ||
        (opcode == Opcode::Amo && funct5 != Funct5Amo::Lr)) {
        if (!ctx.pending_exception.has_value()) {
            state_.reserved = 0;
            machine.memory_.reservation_table().invalidate_matching(
                ctx.mem_addr, static_cast<HartId>(state_.mhartid));
        }
    }
}

// ==========================================
// WB (Writeback) Stage
// ==========================================

void CPU::run_writeback_stage(Machine& machine) { writeback_registers(machine); }

void CPU::writeback_registers(Machine& machine) {
    const auto effects = pipeline::build_writeback_effects(active_context());
    e_icount += effects.increments_instruction_count;
    if (simrv::compiler::unlikely(machine.instruction_mix_enabled()) &&
        effects.increments_instruction_count != 0) {
        e_instmix[static_cast<std::size_t>(active_context().op_id)]++;
    }
    if (effects.floating_write.enabled) {
        state_.regs.write_fp(effects.floating_write.destination, effects.floating_write.value);
    }
    if (effects.marks_floating_point_dirty) state_.mstatus |= enum_mask(MstatusBit::Fs);
    if (effects.integer_write.enabled) {
        state_.regs.write_branchless(effects.integer_write.destination,
                                     effects.integer_write.value);
    }
}

// ==========================================
// COMMIT (Commit) Stage
// ==========================================

void CPU::run_commit_stage(Machine& machine) { commit_control_flow_and_traps(machine); }

void CPU::commit_control_flow_and_traps([[maybe_unused]] Machine& machine) {
    auto& ctx = active_context();
    if (ctx.cinsn != 0u && !ctx.pending_exception.has_value()) {
        e_ccount++;
    }

    const auto opcode = static_cast<Opcode>(ctx.opcode);
    const auto funct3 = static_cast<Funct3>(ctx.funct3);

    if (!ctx.pending_exception.has_value() && opcode == Opcode::System) {
        if (funct3 == Funct3::Priv) {
            switch (static_cast<Funct12Priv>(ctx.funct12)) {
                case Funct12Priv::Uret: {
                    break;
                }
                case Funct12Priv::Sret: {
                    sret();
                    break;
                }
                case Funct12Priv::Mret: {
                    mret();
                    break;
                }
                default:
                    if (ctx.funct7 == static_cast<Instruction>(Funct7Priv::SfenceVma)) {
                        const bool match_all_vaddr = (std::to_underlying(ctx.rs1) == 0);
                        const bool match_all_asid = (std::to_underlying(ctx.rs2) == 0);
                        TLB_flush(match_all_vaddr, ctx.rrs1, match_all_asid,
                                  static_cast<Word>(ctx.rrs2));
                    }
                    break;
            }
        } else {
            const bool is_write = (funct3 == Funct3::Csrrw || funct3 == Funct3::Csrrwi) ||
                                  (std::to_underlying(ctx.rs1) != 0);
            if (is_write) {
                auto res = write_csr(static_cast<CSRAddress>(ctx.funct12), ctx.wb_data_csr);
                if (!res) {
                    ctx.pending_exception = res.error();
                }
            }
        }
    }

    Word const pending_interrupts = state_.mip & state_.mie;
    Word enable_interrupts = 0;
    Word mask = 0;
    Word irq_num = 32;
    if (simrv::compiler::unlikely(pending_interrupts != 0u)) {
        switch (state_.priv) {
            case kPrivMachine: {
                if ((state_.mstatus & enum_mask(MstatusBit::Mie)) != 0u) {
                    enable_interrupts = ~state_.mideleg;
                }
                break;
            }
            case kPrivSupervisor: {
                enable_interrupts = ~state_.mideleg;
                if ((state_.mstatus & enum_mask(MstatusBit::Sie)) != 0u) {
                    enable_interrupts |= state_.mideleg;
                }
                break;
            }
            case kPrivUser: {
                enable_interrupts = ~0;
                break;
            }
            default:
                break;
        }
        mask = pending_interrupts & enable_interrupts;
        if (mask != 0) {
            irq_num = select_highest_priority_interrupt(mask);
        }
    }
    if (ctx.pending_exception.has_value()) {
        state_.pc = ctx.cpc.raw();
        raise_exception(std::to_underlying(*ctx.pending_exception), ctx.pending_tval);
    } else {
        if (ctx.tkn != 0u) {
            const bool has_c = misa_has_extension(state_.misa, isa::IsaExtension::C);
            const Word alignment_mask = has_c ? 1u : 3u;
            if ((ctx.jmp_pc & alignment_mask) != 0) {
                ctx.pending_exception = ExceptionCode::MisalignedFetch;
                ctx.pending_tval = ctx.jmp_pc;
                raise_exception(std::to_underlying(*ctx.pending_exception), ctx.pending_tval);
                return;
            }
            state_.pc = ctx.jmp_pc;
        } else {
            state_.pc = (ctx.cpc + ((ctx.cinsn != 0u) ? 2 : 4)).raw();
        }
        if (state_.regs.xlen == 32) {
            state_.pc =
                static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(state_.pc)));
        }
        if (mask != 0) {
            raise_exception(kInterruptCauseBit | irq_num, 0);
        }
    }
}

void CPU::run_commit_stage_baremetal([[maybe_unused]] Machine& machine) {
    auto& ctx = active_context();
    if (ctx.cinsn != 0u && !ctx.pending_exception.has_value()) {
        e_ccount++;
    }

    const auto opcode = static_cast<Opcode>(ctx.opcode);
    const auto funct3 = static_cast<Funct3>(ctx.funct3);

    if (!ctx.pending_exception.has_value() && opcode == Opcode::System) {
        if (funct3 == Funct3::Priv) {
            switch (static_cast<Funct12Priv>(ctx.funct12)) {
                case Funct12Priv::Uret: {
                    break;
                }
                case Funct12Priv::Sret: {
                    sret();
                    break;
                }
                case Funct12Priv::Mret: {
                    mret();
                    break;
                }
                default:
                    if (ctx.funct7 == static_cast<Instruction>(Funct7Priv::SfenceVma)) {
                        const bool match_all_vaddr = (std::to_underlying(ctx.rs1) == 0);
                        const bool match_all_asid = (std::to_underlying(ctx.rs2) == 0);
                        TLB_flush(match_all_vaddr, ctx.rrs1, match_all_asid,
                                  static_cast<Word>(ctx.rrs2));
                    }
                    break;
            }
        } else {
            const bool is_write = (funct3 == Funct3::Csrrw || funct3 == Funct3::Csrrwi) ||
                                  (std::to_underlying(ctx.rs1) != 0);
            if (is_write) {
                auto res = write_csr(static_cast<CSRAddress>(ctx.funct12), ctx.wb_data_csr);
                if (!res) {
                    ctx.pending_exception = res.error();
                }
            }
        }
    }

    Word const pending_interrupts = state_.mip & state_.mie;
    Word enable_interrupts = 0;
    Word mask = 0;
    Word irq_num = 32;
    if (simrv::compiler::unlikely(pending_interrupts != 0u)) {
        switch (state_.priv) {
            case kPrivMachine: {
                if ((state_.mstatus & enum_mask(MstatusBit::Mie)) != 0u) {
                    enable_interrupts = ~state_.mideleg;
                }
                break;
            }
            case kPrivSupervisor: {
                enable_interrupts = ~state_.mideleg;
                if ((state_.mstatus & enum_mask(MstatusBit::Sie)) != 0u) {
                    enable_interrupts |= state_.mideleg;
                }
                break;
            }
            case kPrivUser: {
                enable_interrupts = ~0;
                break;
            }
            default:
                break;
        }
        mask = pending_interrupts & enable_interrupts;
        if (mask != 0) {
            irq_num = select_highest_priority_interrupt(mask);
        }
    }

    if (ctx.pending_exception.has_value()) {
        state_.pc = ctx.cpc.raw();
        raise_exception(std::to_underlying(*ctx.pending_exception), ctx.pending_tval);
    } else {
        if (ctx.tkn != 0u) {
            const bool has_c = misa_has_extension(state_.misa, isa::IsaExtension::C);
            const Word alignment_mask = has_c ? 1u : 3u;
            if ((ctx.jmp_pc & alignment_mask) != 0) {
                ctx.pending_exception = ExceptionCode::MisalignedFetch;
                ctx.pending_tval = ctx.jmp_pc;
                raise_exception(std::to_underlying(*ctx.pending_exception), ctx.pending_tval);
                return;
            }
            state_.pc = ctx.jmp_pc;
        } else {
            state_.pc = (ctx.cpc + ((ctx.cinsn != 0u) ? 2 : 4)).raw();
        }
        if (state_.regs.xlen == 32) {
            state_.pc =
                static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(state_.pc)));
        }
        if (simrv::compiler::unlikely(mask != 0)) {
            raise_exception(kInterruptCauseBit | irq_num, 0);
        }
    }
}

}  // namespace simrv::core
