/**
 * @file InstructionExplainerTests.cpp
 * @brief Semantic unit tests for the formal instruction explainer (simrv::explain).
 */
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string_view>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/PmpTypes.hpp"
#include "simrv/explain/InstructionExplainer.hpp"
#include "simrv/isa/TrapTypes.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/xlen/IdentifierTypes.hpp"

namespace {

void expect(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        std::exit(1);
    }
}

void test_spec_provenance_and_formulas() {
    using namespace simrv::explain;
    using enum simrv::isa::OperationId;

    const auto add_spec = get_operation_spec(ADD);
    expect(add_spec.mnemonic == "ADD", "ADD mnemonic matches");
    expect(add_spec.reference.document == kUnprivSpecDoc,
           "ADD cites Unprivileged Manual V20260120");
    expect(add_spec.reference.document.contains("V20260120"), "Spec citation includes V20260120");
    expect(add_spec.reference.section.contains("Ch. 1"), "ADD cites Chapter 1");
    expect(add_spec.reference.extension == "RV32I", "ADD extension is RV32I");
    expect(add_spec.reference.formula == "x[rd] = x[rs1] + x[rs2]", "ADD operational formula");

    const auto csrrs_spec = get_operation_spec(CSRRS);
    expect(csrrs_spec.reference.document == kUnprivSpecDoc, "CSRRS cites Unprivileged Manual");
    expect(csrrs_spec.reference.section.contains("Ch. 5"), "CSRRS cites Chapter 5 (Zicsr)");
    expect(csrrs_spec.reference.extension == "Zicsr", "CSRRS extension is Zicsr");
    expect(!csrrs_spec.reference.formula.empty(), "CSRRS has operational formula");
    expect(csrrs_spec.reference.formula.contains("rs1 != x0"),
           "CSRRS formula preserves the no-write-on-zero-source rule");

    const auto csrrw_spec = get_operation_spec(CSRRW);
    expect(csrrw_spec.reference.formula.contains("rd != x0"),
           "CSRRW formula preserves the no-read-on-zero-destination rule");

    const auto mret_spec = get_operation_spec(MRET);
    expect(mret_spec.reference.document == kPrivSpecDoc, "MRET cites Privileged Manual V20260120");
    expect(mret_spec.reference.extension == "Privileged", "MRET extension is Privileged");
    expect(mret_spec.reference.formula.contains("PC = mepc"), "MRET formula references mepc");

    const auto sh1add_spec = get_operation_spec(SH1ADD);
    expect(sh1add_spec.reference.extension == "Zba", "SH1ADD extension is Zba");
    expect(sh1add_spec.reference.section.contains("29.1.2"), "SH1ADD cites Zba section");
    expect(sh1add_spec.reference.formula == "x[rd] = x[rs2] + (x[rs1] << 1)", "SH1ADD formula");

    const auto pack_spec = get_operation_spec(PACK);
    expect(pack_spec.reference.extension == "Zbkb", "PACK is classified as Zbkb");
    expect(pack_spec.reference.section.contains("29.1.6"), "PACK cites the Zbkb section");

    const auto fli_spec = get_operation_spec(FLI_S);
    expect(fli_spec.mnemonic == "FLI.S", "FLI.S has a normative explainer entry");
    expect(fli_spec.reference.extension == "Zfa", "FLI.S is classified as Zfa");
    expect(fli_spec.reference.section.contains("Ch. 25"), "FLI.S cites the Zfa chapter");

    const auto mop_spec = get_operation_spec(MOP);
    expect(mop_spec.description.contains("writes zero to x[rd]"), "MOP describes its zero result");
    expect(mop_spec.reference.extension == "Zimop", "MOP is classified as Zimop");

    const auto wrs_spec = get_operation_spec(WRS_NTO);
    expect(wrs_spec.description.contains("reservation set"), "WRS describes its wait condition");
    expect(wrs_spec.reference.extension == "Zawrs", "WRS is classified as Zawrs");

    const auto hlv_spec = get_operation_spec(HLVX_HU);
    expect(hlv_spec.mnemonic == "HLVX.HU", "HLVX.HU has a normative explainer entry");
    expect(hlv_spec.reference.extension == "H", "HLVX.HU is classified as H");
    expect(hlv_spec.reference.section.contains("Ch. 14"), "HLVX.HU cites the H chapter");

    const auto hfence_spec = get_operation_spec(HFENCE_GVMA);
    expect(hfence_spec.mnemonic == "HFENCE.GVMA", "HFENCE.GVMA has a normative explainer entry");
    expect(hfence_spec.reference.extension == "H", "HFENCE.GVMA is classified as H");

    const auto unknown_spec = get_operation_spec(ILLEGAL);
    expect(unknown_spec.mnemonic == "UNKNOWN", "Undescribed operations retain UNKNOWN metadata");

    const auto hstateen_details = get_csr_details(0x60C);
    expect(hstateen_details.has_value(), "hstateen0 has CSR metadata");
    expect(hstateen_details->name == "hstateen0", "hstateen0 metadata names the CSR");
    expect(hstateen_details->extension == "Ssstateen", "hstateen0 is classified as Ssstateen");

    const auto hgatp_details = get_csr_details(0x680);
    expect(hgatp_details.has_value(), "hgatp has CSR metadata");
    expect(hgatp_details->extension == "Shgatpa", "hgatp is classified as Shgatpa");

    const auto fence_i_spec = get_operation_spec(FENCE_I);
    expect(fence_i_spec.reference.extension == "Zifencei", "FENCE.I is classified as Zifencei");
    expect(fence_i_spec.description.contains("current hart"),
           "FENCE.I describes same-hart instruction-fetch synchronization");
    expect(!fence_i_spec.description.contains("address-translation"),
           "FENCE.I is not described as SFENCE.VMA");
    expect(fence_i_spec.reference.formula.contains("subsequent_instruction_fetches"),
           "FENCE.I formula describes instruction-fetch ordering");

    const auto czero_spec = get_operation_spec(CZERO_EQZ);
    expect(czero_spec.reference.extension == "Zicond", "CZERO.EQZ extension is Zicond");
    expect(czero_spec.reference.section.contains("Ch. 10"), "CZERO.EQZ cites Chapter 10");
}

void test_static_and_compressed_explanation() {
    using namespace simrv::explain;

    // 1. Standard 32-bit ADDI x1, x2, 42 -> 0x02a10093
    const auto addi_expl = explain_static(0x02A10093U, 0x80000000ULL);
    expect(!addi_expl.is_compressed, "32-bit ADDI is not compressed");
    expect(!addi_expl.state_grounded, "explain_static marks state_grounded=false");
    expect(addi_expl.op_id == simrv::isa::OperationId::ADDI, "Decoded OperationId is ADDI");
    expect(addi_expl.format == simrv::isa::InstFormat::I, "Decoded format is I-Type");
    expect(addi_expl.imm == 42, "Decoded I-immediate is 42");
    expect(std::to_underlying(addi_expl.rd.reg_id) == 1, "rd is x1");
    expect(std::to_underlying(addi_expl.rs1.reg_id) == 2, "rs1 is x2");
    expect(addi_expl.predicted_effect.next_pc == 0x80000004ULL, "Static next_pc is PC + 4");

    // 2. Compressed C.ADDI x1, 5 -> 0x0095 (000 0 00001 00101 01)
    const auto c_addi_expl = explain_static(0x0095U, 0x80000010ULL);
    expect(c_addi_expl.is_compressed, "C.ADDI is marked compressed");
    expect(c_addi_expl.op_id == simrv::isa::OperationId::ADDI, "C.ADDI decompresses to ADDI");
    expect(c_addi_expl.imm == 5, "C.ADDI immediate is 5");
    expect(c_addi_expl.predicted_effect.next_pc == 0x80000012ULL, "Compressed next_pc is PC + 2");
}

void test_state_grounded_alu_and_branch() {
    using namespace simrv::explain;

    simrv::core::Machine machine;
    std::array<simrv::xlen::Byte, 65536> backing{};
    machine.set_ram_for_testing(backing.data(), backing.size());
    machine.memory().initialize_mmu();

    auto& cpu = machine.primary_hart();
    cpu.machine_ = &machine;
    cpu.reset();
    auto& st = cpu.state();

    constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
    st.pc = pc;
    st.regs.write(static_cast<simrv::xlen::RegId>(1), 100);
    st.regs.write(static_cast<simrv::xlen::RegId>(2), 25);

    // ADD x3, x1, x2 -> 0x002081b3
    constexpr uint32_t add_inst = 0x002081B3U;
    const auto add_expl = explain_in_state(st, pc, add_inst, &cpu, &machine);
    expect(add_expl.state_grounded, "explain_in_state is state_grounded");
    expect(add_expl.rs1.raw_value == 100, "rs1 captures live value 100");
    expect(add_expl.rs2.raw_value == 25, "rs2 captures live value 25");
    expect(add_expl.predicted_effect.predicted_reg_value.has_value() &&
               *add_expl.predicted_effect.predicted_reg_value == 125,
           "ADD predicts destination value 125");
    expect(add_expl.predicted_effect.next_pc == pc + 4, "ADD predicts sequential next_pc");

    // BEQ x1, x2, +16 -> 0x00208863 (not taken since 100 != 25)
    constexpr uint32_t beq_inst = 0x00208863U;
    const auto beq_nt = explain_in_state(st, pc, beq_inst, &cpu, &machine);
    expect(beq_nt.control.has_value(), "BEQ populates ControlDataflow");
    expect(beq_nt.control->is_conditional, "BEQ is conditional");
    expect(!beq_nt.control->condition_met, "BEQ condition is false when x1 != x2");
    expect(beq_nt.control->target_pc == pc + 16, "BEQ target_pc is PC + 16");
    expect(beq_nt.predicted_effect.next_pc == pc + 4, "Not-taken BEQ predicts PC + 4");

    // Set x2 = 100 so BEQ is taken
    st.regs.write(static_cast<simrv::xlen::RegId>(2), 100);
    const auto beq_taken = explain_in_state(st, pc, beq_inst, &cpu, &machine);
    expect(beq_taken.control->condition_met, "BEQ condition is true when x1 == x2");
    expect(beq_taken.predicted_effect.next_pc == pc + 16, "Taken BEQ predicts target_pc");
}

void test_state_grounded_memory_and_csr() {
    using namespace simrv::explain;

    simrv::core::Machine machine;
    std::array<simrv::xlen::Byte, 65536> backing{};
    machine.set_ram_for_testing(backing.data(), backing.size());
    machine.memory().initialize_mmu();

    auto& cpu = machine.primary_hart();
    cpu.machine_ = &machine;
    cpu.reset();
    auto& st = cpu.state();

    constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
    st.pc = pc;

    // Store a known 32-bit word at DRAM + 0x100
    constexpr uint32_t mem_word = 0xCAFEBABEU;
    std::memcpy(backing.data() + 0x100, &mem_word, sizeof(mem_word));
    st.regs.write(static_cast<simrv::xlen::RegId>(1), pc + 0xF0);

    // LW x4, 16(x1) -> imm=16 (0x010), rs1=1, funct3=2, rd=4, opcode=0x03 -> 0x0100a203
    constexpr uint32_t lw_inst = 0x0100A203U;
    std::memcpy(backing.data(), &lw_inst, sizeof(lw_inst));

    const auto lw_expl = explain_at_pc(machine, 0, pc);
    expect(lw_expl.has_value(), "explain_at_pc succeeds for mapped DRAM instruction");
    expect(lw_expl->memory.has_value(), "LW populates MemoryDataflow");
    expect(lw_expl->memory->effective_vaddr == pc + 0x100, "LW effective_vaddr is base + 16");
    expect(lw_expl->memory->is_aligned, "LW at +0x100 is 4-byte aligned");
    expect(lw_expl->memory->pmp_permitted, "LW is permitted by default PMP");
    expect(lw_expl->memory->memory_preview_value.has_value() &&
               static_cast<uint32_t>(*lw_expl->memory->memory_preview_value) == 0xCAFEBABEU,
           "LW previews memory value from RAM without side effects");
    const auto expected_sext =
        static_cast<simrv::xlen::Register>(static_cast<int64_t>(static_cast<int32_t>(0xCAFEBABEU)));
    expect(lw_expl->predicted_effect.predicted_reg_value.has_value() &&
               *lw_expl->predicted_effect.predicted_reg_value == expected_sext,
           "LW predicts sign-extended destination register value");

    // CSRRS x5, mstatus (0x300), x0 -> 0x300022f3
    constexpr uint32_t csrrs_mstatus = 0x300022F3U;
    st.mstatus = 0x1800U;
    st.priv = simrv::xlen::PrivilegeLevel::Machine;
    const auto csr_ok = explain_in_state(st, pc, csrrs_mstatus, &cpu, &machine);
    expect(csr_ok.csr.has_value(), "CSRRS populates CsrDataflow");
    expect(csr_ok.csr->privilege_permitted, "M-mode is permitted to read mstatus");
    expect(!csr_ok.csr->attempts_write, "CSRRS with rs1=x0 does not attempt write");
    expect(csr_ok.predicted_effect.predicted_reg_value.has_value() &&
               *csr_ok.predicted_effect.predicted_reg_value == 0x1800U,
           "CSRRS predicts old mstatus value in rd");
    expect(!csr_ok.predicted_effect.predicted_trap.has_value(), "M-mode CSRRS does not trap");

    // Drop to User mode -> reading mstatus should predict IllegalInstruction trap
    st.priv = simrv::xlen::PrivilegeLevel::User;
    const auto csr_trap = explain_in_state(st, pc, csrrs_mstatus, &cpu, &machine);
    expect(!csr_trap.csr->privilege_permitted, "U-mode is not permitted to access mstatus");
    expect(csr_trap.predicted_effect.predicted_trap.has_value() &&
               *csr_trap.predicted_effect.predicted_trap ==
                   simrv::isa::to_trap_cause(simrv::isa::ExceptionCode::IllegalInstruction),
           "U-mode mstatus access predicts IllegalInstruction trap");
}

void test_virtualization_privilege_and_extension_gating() {
    using namespace simrv::explain;

    simrv::core::Machine machine;
    std::array<simrv::xlen::Byte, 65536> backing{};
    machine.set_ram_for_testing(backing.data(), backing.size());
    machine.memory().initialize_mmu();

    auto& cpu = machine.primary_hart();
    cpu.machine_ = &machine;
    cpu.reset();
    auto& st = cpu.state();
    constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
    st.pc = pc;
    st.set_misa(st.misa | (simrv::xlen::CsrValue{1} << 7U));  // Enable H extension

    // 1. SSTATUS read masks out M-mode-only bits (e.g. MPP=0x1800) and aliases vsstatus in VS-mode
    constexpr uint32_t csrrs_sstatus = 0x100022F3U;  // csrrs x5, sstatus, x0
    st.priv = simrv::xlen::PrivilegeLevel::Supervisor;
    st.virt = false;
    st.mstatus = 0x1800U | 0x0002U;  // MPP=3 (hidden in sstatus), SIE=1 (visible in sstatus)
    st.vsstatus = 0x0020U;           // SPIE=1
    const auto hs_sstatus = explain_in_state(st, pc, csrrs_sstatus, &cpu, &machine);
    expect(hs_sstatus.csr.has_value() && hs_sstatus.csr->current_value.has_value(),
           "HS-mode sstatus read succeeds");
    expect((*hs_sstatus.csr->current_value & 0x1800U) == 0 &&
               (*hs_sstatus.csr->current_value & 0x0002U) == 0x0002U,
           "HS-mode sstatus masks out MPP while preserving SIE");

    st.virt = true;
    const auto vs_sstatus = explain_in_state(st, pc, csrrs_sstatus, &cpu, &machine);
    expect(vs_sstatus.active_virtualization,
           "InstructionExplanation records active_virtualization");
    expect(vs_sstatus.csr.has_value() && vs_sstatus.csr->current_value.has_value() &&
               (*vs_sstatus.csr->current_value & 0xFFU) == 0x20U,
           "VS-mode sstatus read aliases vsstatus");

    // 2. VS-mode ECALL predicts HypervisorEcall (cause 10)
    constexpr uint32_t ecall_inst = 0x00000073U;
    const auto vs_ecall = explain_in_state(st, pc, ecall_inst, &cpu, &machine);
    expect(vs_ecall.predicted_effect.predicted_trap.has_value() &&
               *vs_ecall.predicted_effect.predicted_trap ==
                   simrv::isa::to_trap_cause(simrv::isa::ExceptionCode::HypervisorEcall),
           "VS-mode ECALL predicts VirtualSupervisorEcall (cause 10)");

    // 3. VS-mode SRET targets vsepc and traps with VirtualInstruction when hstatus.VTSR=1
    constexpr uint32_t sret_inst = 0x10200073U;
    st.sepc = pc + 0x200;
    st.vsepc = pc + 0x400;
    st.hstatus = 0;
    const auto vs_sret_ok = explain_in_state(st, pc, sret_inst, &cpu, &machine);
    expect(!vs_sret_ok.predicted_effect.predicted_trap.has_value(),
           "VS-mode SRET with VTSR=0 does not trap");
    expect(vs_sret_ok.predicted_effect.next_pc == pc + 0x400,
           "VS-mode SRET predicts return to vsepc");

    st.hstatus = simrv::xlen::CsrValue{1} << 22U;  // VTSR=1
    const auto vs_sret_trap = explain_in_state(st, pc, sret_inst, &cpu, &machine);
    expect(vs_sret_trap.predicted_effect.predicted_trap.has_value() &&
               *vs_sret_trap.predicted_effect.predicted_trap ==
                   simrv::isa::to_trap_cause(simrv::isa::ExceptionCode::VirtualInstruction),
           "VS-mode SRET with hstatus.VTSR=1 predicts VirtualInstruction trap");

    // 4. Extension context state gating: FP instruction when mstatus.FS == Off traps
    constexpr uint32_t fadd_s_inst = 0x0020F1D3U;  // fadd.s f3, f1, f2
    st.virt = false;
    st.mstatus &= ~static_cast<simrv::xlen::CsrValue>(simrv::core::MstatusBit::Fs);
    const auto fp_off = explain_in_state(st, pc, fadd_s_inst, &cpu, &machine);
    expect(fp_off.predicted_effect.predicted_trap.has_value() &&
               *fp_off.predicted_effect.predicted_trap ==
                   simrv::isa::to_trap_cause(simrv::isa::ExceptionCode::IllegalInstruction),
           "FADD.S when mstatus.FS=Off predicts IllegalInstruction");

    st.mstatus |= static_cast<simrv::xlen::CsrValue>(1U << 13U);  // FS=Initial
    const auto fp_on = explain_in_state(st, pc, fadd_s_inst, &cpu, &machine);
    expect(!fp_on.predicted_effect.predicted_trap.has_value(),
           "FADD.S when mstatus.FS=Initial does not trap");

    // 5. Zimop MOP.R.0 writes 0 to rd and HLV.W formats canonical assembly cleanly
    constexpr uint32_t mop_r_0 = 0x81C0C2F3U;  // mop.r.0 x5, x1
    const auto mop_expl = explain_in_state(st, pc, mop_r_0, &cpu, &machine);
    expect(mop_expl.op_id == simrv::isa::OperationId::MOP, "Decodes Zimop MOP");
    expect(mop_expl.predicted_effect.predicted_reg_value.has_value() &&
               *mop_expl.predicted_effect.predicted_reg_value == 0,
           "MOP predicts 0 written to destination register");

    constexpr uint32_t hlv_w_inst = 0x6800C2F3U;  // hlv.w x5, (x1)
    const auto hlv_expl = explain_in_state(st, pc, hlv_w_inst, &cpu, &machine);
    expect(hlv_expl.op_id == simrv::isa::OperationId::HLV_W, "Decodes HLV.W");
    expect(hlv_expl.assembly.find("HLV.W") != std::string::npos &&
               hlv_expl.assembly.find('(') != std::string::npos,
           "HLV.W formats canonical memory operand syntax");
    expect(hlv_expl.memory.has_value() && hlv_expl.memory->access_bytes == 4U &&
               hlv_expl.memory->offset == 0,
           "HLV.W MemoryDataflow uses 4-byte access size and 0 offset");
}

void test_sstc_and_stateen_csr_parity() {
    using namespace simrv::explain;

    simrv::core::Machine machine;
    std::array<simrv::xlen::Byte, 65536> backing{};
    machine.set_ram_for_testing(backing.data(), backing.size());
    machine.memory().initialize_mmu();

    auto& cpu = machine.primary_hart();
    cpu.machine_ = &machine;
    cpu.reset();
    auto& st = cpu.state();
    constexpr simrv::xlen::Address pc = simrv::memory::kDramBaseAddress;
    st.pc = pc;
    st.set_misa(st.misa | (simrv::xlen::CsrValue{1} << 7U));  // Enable H extension

    // 1. Sstc: stimecmp (0x14D) in S-mode requires menvcfg.STCE (bit 63) and mcounteren.TM (bit 1)
    constexpr uint32_t csrrs_stimecmp = 0x14D022F3U;  // csrrs x5, stimecmp, x0
    st.priv = simrv::xlen::PrivilegeLevel::Supervisor;
    st.virt = false;
    st.menvcfg = 0;
    st.mcounteren = 0;
    st.stimecmp = 0x12345678ULL;
    const auto stimecmp_blocked = explain_in_state(st, pc, csrrs_stimecmp, &cpu, &machine);
    expect(stimecmp_blocked.predicted_effect.predicted_trap.has_value() &&
               *stimecmp_blocked.predicted_effect.predicted_trap ==
                   simrv::isa::to_trap_cause(simrv::isa::ExceptionCode::IllegalInstruction),
           "S-mode stimecmp read without menvcfg.STCE/mcounteren.TM predicts IllegalInstruction");

    if constexpr (simrv::xlen::kIsXLen64) {
        st.menvcfg = simrv::xlen::CsrValue{1} << 63U;
    } else {
        st.menvcfgh = simrv::xlen::CsrValue{1} << 31U;
    }
    st.mcounteren = 0x2U;
    const auto stimecmp_ok = explain_in_state(st, pc, csrrs_stimecmp, &cpu, &machine);
    expect(!stimecmp_ok.predicted_effect.predicted_trap.has_value(),
           "S-mode stimecmp read with STCE=1 and mcounteren.TM=1 succeeds");
    expect(stimecmp_ok.csr.has_value() && stimecmp_ok.csr->current_value.has_value() &&
               *stimecmp_ok.csr->current_value == 0x12345678ULL,
           "stimecmp read_from_state returns live stimecmp value");

    // In VS-mode, stimecmp aliases vstimecmp and requires henvcfg.STCE + hcounteren.TM on RV64
    if constexpr (simrv::xlen::kIsXLen64) {
        st.virt = true;
        st.henvcfg = 0;
        st.hcounteren = 0;
        st.vstimecmp = 0x87654321ULL;
        const auto vs_stimecmp_blocked = explain_in_state(st, pc, csrrs_stimecmp, &cpu, &machine);
        expect(
            vs_stimecmp_blocked.predicted_effect.predicted_trap.has_value() &&
                *vs_stimecmp_blocked.predicted_effect.predicted_trap ==
                    simrv::isa::to_trap_cause(simrv::isa::ExceptionCode::VirtualInstruction),
            "VS-mode stimecmp read without henvcfg.STCE/hcounteren.TM predicts VirtualInstruction");

        st.henvcfg = simrv::xlen::CsrValue{1} << 63U;
        st.hcounteren = 0x2U;
        const auto vs_stimecmp_ok = explain_in_state(st, pc, csrrs_stimecmp, &cpu, &machine);
        expect(!vs_stimecmp_ok.predicted_effect.predicted_trap.has_value(),
               "VS-mode stimecmp read with henvcfg.STCE=1 and hcounteren.TM=1 succeeds");
        expect(vs_stimecmp_ok.csr.has_value() && vs_stimecmp_ok.csr->current_value.has_value() &&
                   *vs_stimecmp_ok.csr->current_value == 0x87654321ULL,
               "VS-mode stimecmp aliases vstimecmp");
    }

    // 2. Smstateen/Ssstateen: sstateen0 (0x10C) and senvcfg (0x10A) gated by mstateen0 / hstateen0
    constexpr uint32_t csrrs_sstateen0 = 0x10C022F3U;  // csrrs x5, sstateen0, x0
    constexpr uint32_t csrrs_senvcfg = 0x10A022F3U;    // csrrs x5, senvcfg, x0
    st.virt = false;
    st.mstateen[0] = 0;
    const auto hs_sstateen_blocked = explain_in_state(st, pc, csrrs_sstateen0, &cpu, &machine);
    expect(hs_sstateen_blocked.predicted_effect.predicted_trap.has_value() &&
               *hs_sstateen_blocked.predicted_effect.predicted_trap ==
                   simrv::isa::to_trap_cause(simrv::isa::ExceptionCode::IllegalInstruction),
           "HS-mode sstateen0 read when mstateen0.SE0=0 predicts IllegalInstruction");

    const auto hs_senvcfg_blocked = explain_in_state(st, pc, csrrs_senvcfg, &cpu, &machine);
    expect(hs_senvcfg_blocked.predicted_effect.predicted_trap.has_value() &&
               *hs_senvcfg_blocked.predicted_effect.predicted_trap ==
                   simrv::isa::to_trap_cause(simrv::isa::ExceptionCode::IllegalInstruction),
           "HS-mode senvcfg read when mstateen0.ENVCFG=0 predicts IllegalInstruction");

    st.mstateen[0] = (uint64_t{1} << 63U) | (uint64_t{1} << 62U);
    st.hstateen[0] = 0;
    st.virt = true;
    const auto vs_sstateen_blocked = explain_in_state(st, pc, csrrs_sstateen0, &cpu, &machine);
    expect(vs_sstateen_blocked.predicted_effect.predicted_trap.has_value() &&
               *vs_sstateen_blocked.predicted_effect.predicted_trap ==
                   simrv::isa::to_trap_cause(simrv::isa::ExceptionCode::VirtualInstruction),
           "VS-mode sstateen0 read when hstateen0.SE0=0 predicts VirtualInstruction");

    // 3. format_instruction_assembly helper matches explain_static assembly
    expect(format_instruction_assembly(0x02A10093U, pc) == explain_static(0x02A10093U, pc).assembly,
           "format_instruction_assembly matches explain_static");
}

}  // namespace

auto main() -> int {
    test_spec_provenance_and_formulas();
    test_static_and_compressed_explanation();
    test_state_grounded_alu_and_branch();
    test_state_grounded_memory_and_csr();
    test_virtualization_privilege_and_extension_gating();
    test_sstc_and_stateen_csr_parity();
    return 0;
}
