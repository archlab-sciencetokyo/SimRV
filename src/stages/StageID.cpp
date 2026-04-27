/**
 * @file StageID.cpp
 * @brief ID stage implementation for Machine.
 */
#include "Cpu.hpp"
#include "Define.hpp"
#include "Machine.hpp"
#include "XLen.hpp"

void CPU::run_decode_stage(Machine& machine) {
    decode_fields(machine);
    fetch_operands(machine);
}

/* decode_fields(Instruction Decode) stage, OK                                                      */
void CPU::decode_fields(Machine& /*machine*/) {
    pipeline_context.opcode = (pipeline_context.ir >> 0) & 0x7F;
    pipeline_context.rd = (pipeline_context.ir >> 7) & 0x1f;
    pipeline_context.rs1 = (pipeline_context.ir >> 15) & 0x1f;
    pipeline_context.rs2 = (pipeline_context.ir >> 20) & 0x1f;
    pipeline_context.funct3 = (pipeline_context.ir >> 12) & 0x7;
    pipeline_context.funct5 = (pipeline_context.ir >> 27) & 0x1F;
    pipeline_context.funct7 = (pipeline_context.ir >> 25);
    pipeline_context.funct12 = (pipeline_context.ir >> 20);
    pipeline_context.imm = decode_unit.decodeImmediate(pipeline_context.ir);
}

/* fetch_operands(Operand Fetch) stage                                                               */
void CPU::fetch_operands(Machine& /*machine*/) {
    const auto opcode = static_cast<Opcode>(pipeline_context.opcode);
    const auto funct3 = static_cast<Funct3>(pipeline_context.funct3);
    const Instruction funct12 = pipeline_context.funct12;

    pipeline_context.rrs1 = reg[pipeline_context.rs1]; /* regfile read port 1 */
    pipeline_context.rrs2 = reg[pipeline_context.rs2]; /* regfile read port 2 */

    if (simrv::compiler::likely(opcode != Opcode::System)) {
        pipeline_context.rcsr = 0;
        return;
    }

    CSRAddress const w_csr_addr =
        (funct3 != Funct3::Priv) ? static_cast<CSRAddress>(funct12)
        : (funct12 == static_cast<Instruction>(Funct12Priv::Ecall)) ? csr_addr(Csr::Mtvec)
        : (funct12 == static_cast<Instruction>(Funct12Priv::Uret))  ? csr_addr(Csr::Uepc)
        : (funct12 == static_cast<Instruction>(Funct12Priv::Sret))  ? csr_addr(Csr::Sepc)
        : (funct12 == static_cast<Instruction>(Funct12Priv::Mret))  ? csr_addr(Csr::Mepc)
                                                                    : 0;
    pipeline_context.rcsr = read_csr(w_csr_addr);
}
