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

}  // namespace

auto main() -> int {
    test_spec_provenance_and_formulas();
    test_static_and_compressed_explanation();
    test_state_grounded_alu_and_branch();
    test_state_grounded_memory_and_csr();
    return 0;
}
