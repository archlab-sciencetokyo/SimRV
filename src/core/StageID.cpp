/**
 * @file StageID.cpp
 * @brief ID stage implementation for Machine.
 */
#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/decode/Decoder.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

void CPU::run_decode_stage(Machine& machine) {
    decode_fields(machine);
    fetch_operands(machine);
}

/* decode_fields(Instruction Decode) stage, OK */
void CPU::decode_fields(Machine& /*machine*/) {
    auto& ctx = pipeline_context;
    if (simrv::compiler::unlikely(ctx.pending_exception != kWordAllOnes)) {
        return;
    }

    simrv::decode::Decoder dec(ctx.ir);

    ctx.opcode = static_cast<Word>(dec.opcode());
    ctx.rd = dec.rd();
    ctx.rs1 = dec.rs1();
    ctx.rs2 = dec.rs2();
    ctx.funct3 = dec.funct3();
    ctx.funct5 = (ctx.ir >> 27) & 0x1F;
    ctx.funct7 = dec.funct7();
    ctx.funct12 = (ctx.ir >> 20);

    switch (dec.opcode()) {
        case simrv::decode::Opcode::kLui:
        case simrv::decode::Opcode::kAuipc:
            ctx.imm = dec.imm_u();
            break;
        case simrv::decode::Opcode::kJal:
            ctx.imm = dec.imm_j();
            break;
        case simrv::decode::Opcode::kBranch:
            ctx.imm = dec.imm_b();
            break;
        case simrv::decode::Opcode::kStore:
        case simrv::decode::Opcode::kStoreFp:
            ctx.imm = dec.imm_s();
            break;
        default:
            ctx.imm = dec.imm_i();
            break;
    }
}

/* fetch_operands(Operand Fetch) stage */
void CPU::fetch_operands(Machine& /*machine*/) {
    auto& ctx = pipeline_context;
    if (simrv::compiler::unlikely(ctx.pending_exception != simrv::xlen::kWordAllOnes)) {
        return;
    }

    const auto opcode = static_cast<Opcode>(ctx.opcode);
    const auto funct3 = static_cast<Funct3>(ctx.funct3);
    const Instruction funct12 = ctx.funct12;

    ctx.rrs1 = state_.regs.read(ctx.rs1); /* regfile read port 1 */
    ctx.rrs2 = state_.regs.read(ctx.rs2); /* regfile read port 2 */

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
        if ((funct12 == static_cast<Instruction>(Funct12Priv::Mret) &&
             state_.priv < kPrivMachine) ||
            (funct12 == static_cast<Instruction>(Funct12Priv::Sret) &&
             state_.priv < kPrivSupervisor) ||
            (ctx.funct7 == static_cast<Instruction>(Funct7Priv::SfenceVma) &&
             state_.priv < kPrivSupervisor)) {
            ctx.pending_exception = enum_mask(ExceptionCode::IllegalInstruction);
            ctx.pending_tval = ctx.ir_org;
            return;
        }
    } else {
        const bool is_write = ((static_cast<uint8_t>(funct3) & 0x3u) == 0x1u) || (ctx.rs1 != 0);
        const Word csr_priv = (w_csr_addr >> 8) & 0x3u;
        const bool is_read_only = ((w_csr_addr >> 10) & 0x3u) == 0x3u;

        if (state_.priv < csr_priv || (is_write && is_read_only)) {
            ctx.pending_exception = enum_mask(ExceptionCode::IllegalInstruction);
            ctx.pending_tval = ctx.ir_org;
            return;
        }

        if (state_.priv < kPrivMachine) {
            if ((w_csr_addr >= 0xC00 && w_csr_addr <= 0xC1F) ||
                (w_csr_addr >= 0xC80 && w_csr_addr <= 0xC9F)) {
                const Word counter_bit = 1u << (w_csr_addr & 0x1Fu);
                bool access_denied = (state_.mcounteren & counter_bit) == 0;
                if (state_.priv == kPrivUser) {
                    access_denied = access_denied || ((state_.scounteren & counter_bit) == 0);
                }
                if (access_denied) {
                    ctx.pending_exception = enum_mask(ExceptionCode::IllegalInstruction);
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
            ctx.pending_exception = enum_mask(ExceptionCode::IllegalInstruction);
            ctx.pending_tval = ctx.ir_org;
            return;
        }
    }

    ctx.rcsr = read_csr(w_csr_addr);
}

}  // namespace simrv::core