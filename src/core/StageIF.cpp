/**
 * @file StageIF.cpp
 * @brief IF stage implementation for Machine.
 */
#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/decode/Decoder.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

void CPU::run_fetch_stage(Machine& machine) {
    auto& ctx = pipeline_context;
    const bool split_page =
        ((state_.pc & ~simrv::memory::kPageMask) != ((state_.pc + 2) & ~simrv::memory::kPageMask));
    const bool translation_enabled =
        state_.priv != kPrivMachine && simrv::xlen::satp_translation_enabled(state_.satp);

    fetch_address_translate(machine);

    if (simrv::compiler::unlikely(translation_enabled)) {
        fetch_resolve_page_walk(machine, 1);
        if (simrv::compiler::likely(!split_page)) {
            if (ctx.pending_exception == kWordAllOnes && ctx.padr1 != kWordAllOnes) {
                ctx.padr2 = ctx.padr1 + 2;
            }
        } else {
            fetch_resolve_page_walk(machine, 2);
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
        !simrv::xlen::satp_translation_enabled(state_.satp)) { /** No translation or protection **/
        w_padr1 = w_vadr1;
        w_padr2 = w_vadr2;
    } else {
        const bool split_page =
            ((w_vadr1 & ~simrv::memory::kPageMask) != (w_vadr2 & ~simrv::memory::kPageMask));
        const Word current_asid = simrv::xlen::satp_asid(state_.satp);

        TLBEntry* tlb_e1 =
            &TLB_inst_r.at((w_vadr1 >> simrv::memory::kPageShift) & (simrv::memory::kTlbSize - 1));
        if (tlb_e1->valid && tlb_e1->asid == current_asid &&
            tlb_e1->v_addr == (w_vadr1 & ~simrv::memory::kPageMask)) {  ///// TLB hit for w_vadr1
            w_padr1 = tlb_e1->p_addr + (w_vadr1 & simrv::memory::kPageMask);
        }

        if (simrv::compiler::likely(!split_page)) {
            if (w_padr1 != kWordAllOnes) {
                w_padr2 = w_padr1 + 2;
            }
        } else {
            TLBEntry* tlb_e2 = &TLB_inst_r.at((w_vadr2 >> simrv::memory::kPageShift) &
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
    if (ctx.pending_exception != kWordAllOnes) {
        return;
    }

    Word w_padr = (state == 1) ? ctx.padr1 : ctx.padr2;
    Word* r_padr = (state == 1) ? &ctx.padr1 : &ctx.padr2;
    Word const w_vadr = (state == 1) ? state_.pc : state_.pc + 2;
    if (w_padr == kWordAllOnes) {
        auto* mmu = machine.memory_.mmu();
        auto translate_res =
            mmu->translate(w_vadr, PteAccess::Code, state_.priv, state_.mstatus, state_.satp);
        if (!translate_res.has_value()) {
            ctx.pending_exception = translate_res.error();
            ctx.pending_tval = w_vadr;
        } else {
            w_padr = translate_res.value();
            TLBEntry* tlb_e1 = &TLB_inst_r.at((w_vadr >> simrv::memory::kPageShift) &
                                              (simrv::memory::kTlbSize - 1));
            tlb_e1->v_addr = w_vadr & ~simrv::memory::kPageMask;  // update TLB entry
            tlb_e1->p_addr = w_padr & ~simrv::memory::kPageMask;  // update TLB entry
            tlb_e1->asid = simrv::xlen::satp_asid(state_.satp);
            tlb_e1->valid = true;
        }
    }
    *r_padr = w_padr;
}

void CPU::fetch_read_instruction_word(Machine& machine) {
    auto& ctx = pipeline_context;
    if (ctx.pending_exception != kWordAllOnes) {
        return;
    }

    if (simrv::compiler::likely(ctx.padr2 == ctx.padr1 + 2)) {
        // Try L1 instruction cache first
        uint32_t cached_data = 0;
        if (icache.read(ctx.padr1, cached_data)) {
            // Cache hit
            ctx.ir_org = static_cast<Instruction>(
                static_cast<SignedWord>(static_cast<int32_t>(cached_data)));
        } else {
            // Cache miss or cross-line fetch: fetch whole cache line from memory and insert
            const Address line_base =
                ctx.padr1 & ~(static_cast<Address>(simrv::cache::ICache::kLineBytes - 1u));

            std::array<Byte, simrv::cache::ICache::kLineBytes> line_data{};
            const unsigned fetch_size = (kXLenBits == 64) ? 8 : 4;
            const Instruction fetch_funct3 = (kXLenBits == 64)
                                                 ? static_cast<Instruction>(Funct3::Sd)
                                                 : static_cast<Instruction>(Funct3::Sw);

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

            icache.read(ctx.padr1, cached_data);
            ctx.ir_org = static_cast<Instruction>(
                static_cast<SignedWord>(static_cast<int32_t>(cached_data)));
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

        simrv::memory::TlChannelA req_h{};
        req_h.opcode = simrv::memory::TlOpcodeA::Get;
        req_h.size = static_cast<uint8_t>(Funct3::Lhu) & 0x3;
        req_h.source = 1;
        req_h.address = ctx.padr2;
        machine.memory_.system_bus().send_request(req_h);
        simrv::memory::TlChannelD resp_h{};
        if (machine.memory_.system_bus().get_response(1, resp_h)) ir_h = resp_h.data;

        ctx.ir_org = (ir_h << 16) | (ir_l & 0xFFFF);
    }
}

/* decode_and_normalize_instruction(Convert) stage, OK */
void CPU::decode_and_normalize_instruction(Machine& machine) {
    auto& ctx = pipeline_context;
    if (simrv::compiler::unlikely(ctx.pending_exception != kWordAllOnes)) {
        ctx.ir = RV32_NOP;
        return;
    }

    simrv::decode::Decoder dec_org(ctx.ir_org);
    bool const w_compressed = dec_org.is_compressed();
    Instruction const w_ir_tmp =
        w_compressed ? simrv::decode::decompressInstruction(ctx.ir_org) : ctx.ir_org;

    bool is_valid = true;
    if (simrv::compiler::unlikely(machine.s_misa_profile != kMisaDefault)) {
        is_valid = instruction_enabled_by_misa(state_.misa, w_ir_tmp, w_compressed);
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
    } else {
        ctx.pending_exception = enum_mask(ExceptionCode::IllegalInstruction);
        ctx.pending_tval = ctx.ir_org;
        ctx.ir = RV32_NOP;
    }

    ctx.cinsn = w_compressed ? 1U : 0U;
    if (machine.s_use_mix) {
        e_instmix.at(simrv::decode::decoder(ctx.ir))++;
    }
}

}  // namespace simrv::core