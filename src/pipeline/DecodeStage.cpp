/**
 * @file DecodeStage.cpp
 * @brief Instruction Decode (ID) stage implementation.
 */
#include <cstdint>
#include <utility>

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "simrv/pipeline/OperationTraits.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Helpers.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

using namespace simrv::isa;

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
    if (ctx.opcode != static_cast<Opcode>(0) || ctx.op_id != isa::UNKNOWN) {
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

    ctx.traits = pipeline::operation::make_dependency_traits(ctx.op_id, ctx.opcode, ctx.rd,
                                                             std::to_underlying(ctx.funct5));
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

}  // namespace simrv::core
