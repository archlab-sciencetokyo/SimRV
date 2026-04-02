/**
 * @file StageIF.cpp
 * @brief IF stage implementation for Machine.
 */
#include "Machine.hpp"

namespace simrv::machine_detail {
Word ram_read(Address addr, Instruction funct3, Byte* ram);
int page_walk(Address v_addr, Address* p_addr, PteAccess access, CPU* cpu, Byte* mmem);
}  // namespace simrv::machine_detail

void CPU::run_fetch_stage(Machine& machine) {
    fetch_address_translate(machine);
    fetch_resolve_page_walk(machine, 1);
    fetch_resolve_page_walk(machine, 2);
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
        TLBEntry* tlb_e1 =
            &TLB_inst_r[(w_vadr1 >> simrv::memory::kPageShift) & (simrv::memory::kTlbSize - 1)];
        TLBEntry* tlb_e2 =
            &TLB_inst_r[(w_vadr2 >> simrv::memory::kPageShift) & (simrv::memory::kTlbSize - 1)];
        if (tlb_e1->v_addr == (w_vadr1 & ~simrv::memory::kPageMask)) {  ///// TLB hit for w_vadr1
            w_padr1 = tlb_e1->p_addr + (w_vadr1 & simrv::memory::kPageMask);
        }
        if (tlb_e2->v_addr == (w_vadr2 & ~simrv::memory::kPageMask)) {  ///// TLB hit for w_vadr2
            w_padr2 = tlb_e2->p_addr + (w_vadr2 & simrv::memory::kPageMask);
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
        int pf =
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

    Word ir_l = simrv::machine_detail::ram_read(
        pipeline_context.padr1, static_cast<Instruction>(Funct3::Lhu), machine.mmem);
    Word ir_h = simrv::machine_detail::ram_read(
        pipeline_context.padr2, static_cast<Instruction>(Funct3::Lhu), machine.mmem);
    pipeline_context.ir_org = (ir_h << 16) | (ir_l & 0xFFFF);
}

/* decode_and_normalize_instruction(Convert) stage, OK                                                                 */
void CPU::decode_and_normalize_instruction(Machine& machine) {
    Instruction w_ir_tmp = decode_unit.decompress(pipeline_context.ir_org);
    bool w_compressed = decode_unit.isCompressed(pipeline_context.ir_org);

    // Check if decoded instruction is enabled by current MISA
    if (!instruction_enabled_by_misa(misa, w_ir_tmp, w_compressed)) {
        // Raise illegal instruction exception
        raise_exception(enum_mask(ExceptionCode::IllegalInstruction), pipeline_context.ir_org);
        pipeline_context.ir = RV32_NOP;
    } else {
        pipeline_context.ir = w_ir_tmp;
    }

    pipeline_context.cinsn = w_compressed ? 1u : 0u;
    if (machine.s_use_mix) machine.e_instmix[decode_unit.decodeOp(pipeline_context.ir)]++;
}
