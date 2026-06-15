/**
 * @file StageIF.cpp
 * @brief IF stage implementation for Machine.
 */
#include "simrv/Define.hpp"
#include "simrv/xlen/Helpers.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/memory/Mmu.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

void CPU::run_fetch_stage(Machine& machine) {
    if (state_.regs.xlen == 32) {
        state_.pc = static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(state_.pc)));
    }
    auto& ctx = pipeline_context;
    ctx.tlb_miss = false;
    const bool split_page =
        ((state_.pc & ~simrv::memory::kPageMask) != ((state_.pc + 2) & ~simrv::memory::kPageMask));
    const bool translation_enabled =
        state_.priv != kPrivMachine && simrv::xlen::satp_translation_enabled(state_.satp, state_.regs.xlen);

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
    decode_and_normalize_instruction(machine);
}

/* IF_(Instruction Fetch) stages                                                          */
void CPU::fetch_address_translate(Machine& /*machine*/) { /* address translation */
    auto& ctx = pipeline_context;
    Word w_padr1 = kWordAllOnes;
    Word w_padr2 = kWordAllOnes;
    Word const w_vadr1 = state_.pc;
    Word const w_vadr2 = state_.pc + 2;

    ctx.cpc = state_.pc;

    if (state_.priv == kPrivMachine ||
        !simrv::xlen::satp_translation_enabled(state_.satp, state_.regs.xlen)) { /** No translation or protection **/
        w_padr1 = (state_.regs.xlen == 32) ? (w_vadr1 & 0xFFFFFFFFULL) : w_vadr1;
        w_padr2 = (state_.regs.xlen == 32) ? (w_vadr2 & 0xFFFFFFFFULL) : w_vadr2;
    } else {
        const bool split_page =
            ((w_vadr1 & ~simrv::memory::kPageMask) != (w_vadr2 & ~simrv::memory::kPageMask));
        const Word current_asid = simrv::xlen::satp_asid(state_.satp, state_.regs.xlen);

        TLBEntry* tlb_e1 =
            &tlb.inst_r.at((w_vadr1 >> simrv::memory::kPageShift) & (simrv::memory::kTlbSize - 1));
        if (tlb_e1->valid && tlb_e1->asid == current_asid &&
            tlb_e1->v_addr == (w_vadr1 & ~simrv::memory::kPageMask)) {  ///// TLB hit for w_vadr1
            w_padr1 = tlb_e1->p_addr + (w_vadr1 & simrv::memory::kPageMask);
        }

        if (simrv::compiler::likely(!split_page)) {
            if (w_padr1 != kWordAllOnes) {
                w_padr2 = w_padr1 + 2;
            }
        } else {
            TLBEntry* tlb_e2 = &tlb.inst_r.at((w_vadr2 >> simrv::memory::kPageShift) &
                                              (simrv::memory::kTlbSize - 1));
            if (tlb_e2->valid && tlb_e2->asid == current_asid &&
                tlb_e2->v_addr == (w_vadr2 & ~simrv::memory::kPageMask)) {
                w_padr2 = tlb_e2->p_addr + (w_vadr2 & simrv::memory::kPageMask);
            }
        }
    }
    ctx.padr1 = w_padr1;
    ctx.padr2 = w_padr2;
}

void CPU::fetch_resolve_page_walk(Machine& machine, int state) { /* page walk and TLB update */
    auto& ctx = pipeline_context;
    if (ctx.pending_exception.has_value()) {
        return;
    }

    Word w_padr = (state == 1) ? ctx.padr1 : ctx.padr2;
    Word* r_padr = (state == 1) ? &ctx.padr1 : &ctx.padr2;
    Word const w_vadr = (state == 1) ? state_.pc : state_.pc + 2;
    if (w_padr == kWordAllOnes) {
        ctx.tlb_miss = true;
        if constexpr (simrv::xlen::kIsXLen64) {
            if (simrv::compiler::unlikely(!simrv::Mmu::is_canonical(w_vadr, state_.satp))) {
                ctx.pending_exception = ExceptionCode::FetchPageFault;
                ctx.pending_tval = w_vadr;
                return;
            }
        }

        auto* mmu = machine.memory_.mmu();
        auto translate_res =
            mmu->translate(w_vadr, PteAccess::Code, state_.priv, state_.mstatus, state_.satp, state_.regs.xlen);
        auto chain_res = translate_res
            .and_then([&](Address phys) -> std::expected<void, TrapCause> {
                w_padr = phys;
                TLBEntry* tlb_e1 = &tlb.inst_r.at((w_vadr >> simrv::memory::kPageShift) &
                                                  (simrv::memory::kTlbSize - 1));
                tlb_e1->v_addr = w_vadr & ~simrv::memory::kPageMask;  // update TLB entry
                tlb_e1->p_addr = w_padr & ~simrv::memory::kPageMask;  // update TLB entry
                tlb_e1->asid = simrv::xlen::satp_asid(state_.satp, state_.regs.xlen);
                tlb_e1->valid = true;
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

void CPU::fetch_read_instruction_word(Machine& machine) {
    auto& ctx = pipeline_context;
    if (ctx.pending_exception.has_value()) {
        return;
    }

    if (machine.s_high_performance) {
        if (simrv::compiler::likely(ctx.padr2 == ctx.padr1 + 2 &&
                                    simrv::memory::is_dram_addr(ctx.padr1))) {
            const Address masked = ctx.padr1 & simrv::memory::kDramMask;
            if (simrv::compiler::likely(masked <= (simrv::memory::kDramMask - 3))) {
                uint32_t val = 0;
                std::memcpy(&val, machine.mmem + masked, 4);
                if ((val & 0x3) != 0x3) {
                    ctx.ir_org = val & 0xFFFF;
                } else {
                    ctx.ir_org = val;
                }
                return;
            }
            const uint16_t h1 = simrv::memory::ram_read_fast(ctx.padr1, static_cast<Instruction>(Funct3::Lhu), machine.mmem);
            if ((h1 & 0x3) != 0x3) {
                ctx.ir_org = h1;
            } else {
                const uint16_t h2 = simrv::memory::ram_read_fast(ctx.padr2, static_cast<Instruction>(Funct3::Lhu), machine.mmem);
                ctx.ir_org = (static_cast<uint32_t>(h2) << 16) | h1;
            }
            return;
        }
    }

    if (simrv::compiler::likely(ctx.padr2 == ctx.padr1 + 2 &&
                                simrv::memory::is_dram_addr(ctx.padr1))) {
        // Try L1 instruction cache using 16-bit halfword accesses to avoid boundary crossing issues
        auto fetch_halfword = [&](Address paddr) -> uint16_t {
            uint16_t h_data = 0;
            if (icache.read16(paddr, h_data)) {
                return h_data;
            }
            // Cache miss: fetch whole cache line from memory and insert
            const Address line_base =
                paddr & ~(static_cast<Address>(simrv::cache::ICache::kLineBytes - 1u));

            std::array<Byte, simrv::cache::ICache::kLineBytes> line_data{};
            // Determine fetch size based on XLEN
            const unsigned fetch_size = xlen::kFetchSize;
            // Select appropriate Funct3 for memory access
            const auto fetch_funct3 = static_cast<Instruction>(
                xlen::kIsXLen64 ? ::Funct3::Sd : ::Funct3::Sw);

            for (uint32_t i = 0; i < simrv::cache::ICache::kLineBytes; i += fetch_size) {
                simrv::memory::TlChannelA req{};
                req.opcode = simrv::memory::TlOpcodeA::Get;
                req.size = static_cast<uint8_t>(fetch_funct3 & 0x3);
                req.source = 1;  // IFetch source ID
                req.address = line_base + i;
                machine.memory_.system_bus().send_request(req);

                simrv::memory::TlChannelD resp{};
                if (machine.memory_.system_bus().get_response(1, resp)) {
                    std::memcpy(line_data.data() + i, &resp.data, fetch_size);
                }
            }

            icache.insert(line_base, line_data.data());

            (void)icache.read16(paddr, h_data);
            return h_data;
        };

        uint16_t const h1 = fetch_halfword(ctx.padr1);
        if ((h1 & 0x3) != 0x3) {
            // Compressed 16-bit instruction
            ctx.ir_org = h1;
        } else {
            // Standard 32-bit instruction
            uint16_t const h2 = fetch_halfword(ctx.padr2);
            ctx.ir_org = (static_cast<uint32_t>(h2) << 16) | h1;
        }
    } else {
        // Cross-page fetch without cache
        Word ir_l = 0;
        Word ir_h = 0;

        simrv::memory::TlChannelA req_l{};
        req_l.opcode = simrv::memory::TlOpcodeA::Get;
        req_l.size = static_cast<uint8_t>(Funct3::Lhu) & 0x3;
        req_l.source = 1;
        req_l.address = ctx.padr1;
        machine.memory_.system_bus().send_request(req_l);
        simrv::memory::TlChannelD resp_l{};
        if (machine.memory_.system_bus().get_response(1, resp_l)) ir_l = resp_l.data;

        simrv::pipeline::Decoder dec_temp(ir_l);
        if (!dec_temp.is_compressed()) {
            const bool translation_enabled =
                state_.priv != kPrivMachine && simrv::xlen::satp_translation_enabled(state_.satp, state_.regs.xlen);
            if (translation_enabled && ctx.padr2 == kWordAllOnes) {
                fetch_resolve_page_walk(machine, 2);
            }

            if (!ctx.pending_exception.has_value()) {
                simrv::memory::TlChannelA req_h{};
                req_h.opcode = simrv::memory::TlOpcodeA::Get;
                req_h.size = static_cast<uint8_t>(Funct3::Lhu) & 0x3;
                req_h.source = 1;
                req_h.address = ctx.padr2;
                machine.memory_.system_bus().send_request(req_h);
                simrv::memory::TlChannelD resp_h{};
                if (machine.memory_.system_bus().get_response(1, resp_h)) ir_h = resp_h.data;
            }
        }

        ctx.ir_org = (ir_h << 16) | (ir_l & 0xFFFF);
    }
}

/* decode_and_normalize_instruction(Convert) stage, OK */
void CPU::decode_and_normalize_instruction(Machine& machine) {
    auto& ctx = pipeline_context;
    if (simrv::compiler::unlikely(ctx.pending_exception.has_value())) {
        ctx.ir = RV32_NOP;
        return;
    }

    simrv::pipeline::Decoder dec_org(ctx.ir_org);
    bool const w_compressed = dec_org.is_compressed();
    Instruction const w_ir_tmp =
        w_compressed ? simrv::pipeline::decompressInstruction(ctx.ir_org, state_.current_xlen() == 64) : ctx.ir_org;

    bool is_valid = true;
    if (simrv::compiler::unlikely(machine.s_misa_profile != kMisaDefault)) {
        is_valid = instruction_enabled_by_misa(state_.misa, w_ir_tmp, w_compressed);
    }

    const ::OperationId op_id = simrv::pipeline::decoder(w_ir_tmp);
    if (simrv::compiler::unlikely(op_id == ::UNKNOWN)) {
        is_valid = false;
    }

    if (is_valid) {
        const auto op = opcode_of(w_ir_tmp);
        const bool is_fp_op = (op == Opcode::LoadFp) || (op == Opcode::StoreFp) ||
                              (op == Opcode::OpFp) || (op == Opcode::MAdd) ||
                              (op == Opcode::MSub) || (op == Opcode::NMAdd) ||
                              (op == Opcode::NMSub);
        if (simrv::compiler::unlikely(is_fp_op &&
                                      (state_.mstatus & enum_mask(MstatusBit::Fs)) == 0)) {
            is_valid = false;
        }
    }

    if (simrv::compiler::likely(is_valid)) {
        ctx.ir = w_ir_tmp;
        ctx.op_id = op_id;
    } else {
        ctx.pending_exception = ExceptionCode::IllegalInstruction;
        ctx.pending_tval = ctx.ir_org;
        ctx.ir = RV32_NOP;
        ctx.op_id = ::UNKNOWN;
    }

    ctx.cinsn = w_compressed ? 1U : 0U;
    e_instmix.at(static_cast<std::size_t>(ctx.op_id))++;
}

void CPU::run_fetch_stage_baremetal(Machine& machine) {
    if (state_.regs.xlen == 32) {
        state_.pc = static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(state_.pc)));
    }
    auto& ctx = pipeline_context;
    ctx.tlb_miss = false;
    ctx.cpc = state_.pc;
    ctx.padr1 = (state_.regs.xlen == 32) ? (state_.pc & 0xFFFFFFFFULL) : state_.pc;
    ctx.padr2 = (state_.regs.xlen == 32) ? ((state_.pc + 2) & 0xFFFFFFFFULL) : (state_.pc + 2);

    if (simrv::compiler::likely(ctx.padr1 < simrv::memory::kDramSize)) {
        const uint16_t h1 = simrv::memory::ram_read_fast(ctx.padr1, static_cast<Instruction>(Funct3::Lhu), machine.mmem);
        if ((h1 & 0x3) != 0x3) {
            ctx.ir_org = h1;
        } else {
            const uint16_t h2 = simrv::memory::ram_read_fast(ctx.padr2, static_cast<Instruction>(Funct3::Lhu), machine.mmem);
            ctx.ir_org = (static_cast<uint32_t>(h2) << 16) | h1;
        }
    } else {
        fetch_address_translate(machine);
        fetch_read_instruction_word(machine);
    }
    decode_and_normalize_instruction(machine);
}

}  // namespace simrv::core
