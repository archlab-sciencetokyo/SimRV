/**
 * @file StageIF.cpp
 * @brief IF stage implementation for Machine.
 */
#include "Machine.hpp"

namespace simrv::machine_detail {
auto page_walk(Address v_addr, Address* p_addr, PteAccess access, CPU* cpu, Byte* mmem) -> bool;
}  // namespace simrv::machine_detail

void CPU::run_fetch_stage(Machine& machine) {
    const bool split_page =
        ((pc & ~simrv::memory::kPageMask) != ((pc + 2) & ~simrv::memory::kPageMask));
    const bool translation_enabled = !(priv == kPrivMachine || (satp >> 31) == 0);

    fetch_address_translate(machine);

    if (simrv::compiler::unlikely(translation_enabled)) {
        fetch_resolve_page_walk(machine, 1);
        if (simrv::compiler::likely(!split_page)) {
            if (pending_exception == ~0u && pipeline_context.padr1 != ~0u) {
                pipeline_context.padr2 = pipeline_context.padr1 + 2;
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
    Word w_padr1 = ~0u;
    Word w_padr2 = ~0u;
    Word w_vadr1 = pc;
    Word w_vadr2 = pc + 2;

    pipeline_context.cpc = pc;

    if (priv == kPrivMachine || (satp >> 31) == 0) { /** No translation or protection **/
        w_padr1 = w_vadr1;
        w_padr2 = w_vadr2;
    } else {
        const bool split_page =
            ((w_vadr1 & ~simrv::memory::kPageMask) != (w_vadr2 & ~simrv::memory::kPageMask));

        TLBEntry* tlb_e1 =
            &TLB_inst_r[(w_vadr1 >> simrv::memory::kPageShift) & (simrv::memory::kTlbSize - 1)];
        if (tlb_e1->v_addr == (w_vadr1 & ~simrv::memory::kPageMask)) {  ///// TLB hit for w_vadr1
            w_padr1 = tlb_e1->p_addr + (w_vadr1 & simrv::memory::kPageMask);
        }

        if (simrv::compiler::likely(!split_page)) {
            if (w_padr1 != ~0u) {
                w_padr2 = w_padr1 + 2;
            }
        } else {
            TLBEntry* tlb_e2 =
                &TLB_inst_r[(w_vadr2 >> simrv::memory::kPageShift) & (simrv::memory::kTlbSize - 1)];
            if (tlb_e2->v_addr == (w_vadr2 & ~simrv::memory::kPageMask)) {
                w_padr2 = tlb_e2->p_addr + (w_vadr2 & simrv::memory::kPageMask);
            }
        }
    }
    pipeline_context.padr1 = w_padr1;
    pipeline_context.padr2 = w_padr2;
}

void CPU::fetch_resolve_page_walk(Machine& machine, int state) { /* page walk and TLB update */
    if (pending_exception != ~0u) return;

    Word w_padr = (state == 1) ? pipeline_context.padr1 : pipeline_context.padr2;
    Word* r_padr = (state == 1) ? &pipeline_context.padr1 : &pipeline_context.padr2;
    Word w_vadr = (state == 1) ? pc : pc + 2;
    if (w_padr == ~0u) {
        const bool pf =
            simrv::machine_detail::page_walk(w_vadr, &w_padr, PteAccess::Code, this, machine.mmem);
        if (pf) {
            pending_exception = enum_mask(ExceptionCode::FetchPageFault);
            pending_tval = w_vadr;
        } else {
            TLBEntry* tlb_e1 =
                &TLB_inst_r[(w_vadr >> simrv::memory::kPageShift) & (simrv::memory::kTlbSize - 1)];
            tlb_e1->v_addr = w_vadr & ~simrv::memory::kPageMask;  // update TLB entry
            tlb_e1->p_addr = w_padr & ~simrv::memory::kPageMask;  // update TLB entry
        }
    }
    *r_padr = w_padr;
}

void CPU::fetch_read_instruction_word(Machine& machine) {
    if (pending_exception != ~0u) return;

    if (simrv::compiler::likely(pipeline_context.padr2 == pipeline_context.padr1 + 2)) {
        pipeline_context.ir_org = simrv::memory_detail::ram_read_fast(
            pipeline_context.padr1, static_cast<Instruction>(Funct3::Lw), machine.mmem);
    } else {
        Word ir_l = simrv::memory_detail::ram_read_fast(
            pipeline_context.padr1, static_cast<Instruction>(Funct3::Lhu), machine.mmem);
        Word ir_h = simrv::memory_detail::ram_read_fast(
            pipeline_context.padr2, static_cast<Instruction>(Funct3::Lhu), machine.mmem);
        pipeline_context.ir_org = (ir_h << 16) | (ir_l & 0xFFFF);
    }
}

/* decode_and_normalize_instruction(Convert) stage, OK                                                                 */
void CPU::decode_and_normalize_instruction(Machine& machine) {
    bool w_compressed = decode_unit.isCompressedInstruction(pipeline_context.ir_org);
    Instruction w_ir_tmp = w_compressed ? decode_unit.decompressInstruction(pipeline_context.ir_org)
                                        : pipeline_context.ir_org;

    // Check if decoded instruction is enabled by current MISA
    if (simrv::compiler::likely(machine.s_misa_profile == kMisaDefault) ||
        instruction_enabled_by_misa(misa, w_ir_tmp, w_compressed)) {
        pipeline_context.ir = w_ir_tmp;
    } else {
        // Raise illegal instruction exception
        raise_exception(enum_mask(ExceptionCode::IllegalInstruction), pipeline_context.ir_org);
        pipeline_context.ir = RV32_NOP;
    }

    pipeline_context.cinsn = w_compressed ? 1u : 0u;
    if (machine.s_use_mix) machine.e_instmix[decode_unit.decodeOperation(pipeline_context.ir)]++;
}
