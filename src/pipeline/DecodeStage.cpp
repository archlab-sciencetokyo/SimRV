/**
 * @file DecodeStage.cpp
 * @brief Instruction Decode (ID) stage implementation.
 */
#include <cstdint>
#include <utility>

#include "simrv/CompilerHints.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/isa/Priv.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "simrv/pipeline/OperationTraits.hpp"
#include "simrv/xlen/Helpers.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace simrv::core {

using namespace simrv::isa;

namespace {
inline void raise_decode_exception(pipeline::PipelineContext& ctx, ExceptionCode code) noexcept {
    ctx.pending_exception = code;
    ctx.pending_tval = ctx.ir_org;
}
}  // namespace

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

    pipeline::operation::unpack_instruction(ctx, ctx.ir, ctx.op_id);
}

void CPU::fetch_operands(Machine& /*machine*/) {
    auto& ctx = active_context();
    if (simrv::compiler::unlikely(ctx.pending_exception.has_value())) {
        return;
    }

    const auto opcode = ctx.opcode;
    const auto funct3 = ctx.funct3;
    const simrv::xlen::Instruction funct12 = ctx.funct12;

    ctx.rrs1 = state_.regs.read(ctx.rs1);
    ctx.rrs2 = state_.regs.read(ctx.rs2);

    if (simrv::compiler::likely(opcode != Opcode::System)) {
        ctx.rcsr = 0;
        return;
    }

    simrv::xlen::CSRAddress const w_csr_addr =
        (funct3 != Funct3::Priv)          ? static_cast<simrv::xlen::CSRAddress>(funct12)
        : (funct12 == Funct12Priv::Ecall) ? csr_addr(Csr::Mtvec)
        : (funct12 == Funct12Priv::Uret)  ? csr_addr(Csr::Uepc)
        : (funct12 == Funct12Priv::Sret)  ? csr_addr(Csr::Sepc)
        : (funct12 == Funct12Priv::Mret)  ? csr_addr(Csr::Mepc)
                                          : 0;

    if (funct3 == Funct3::Priv) {
        if (!TrapController::can_execute_privileged_instruction(
                state_.priv, state_.misa, state_.mstatus, funct12, ctx.funct7)) {
            raise_decode_exception(ctx, ExceptionCode::IllegalInstruction);
            return;
        }
    } else {
        const bool is_write =
            ((static_cast<uint8_t>(funct3) & 0x3u) == 0x1u) || (std::to_underlying(ctx.rs1) != 0);
        if (!TrapController::can_access_csr(state_.priv, state_.misa, w_csr_addr, is_write)) {
            raise_decode_exception(ctx, ExceptionCode::IllegalInstruction);
            return;
        }
        if (w_csr_addr == csr_addr(Csr::Satp) && state_.priv == kPrivSupervisor &&
            (state_.mstatus & MstatusBit::Tvm) != 0) {
            raise_decode_exception(ctx, ExceptionCode::IllegalInstruction);
            return;
        }

        if (state_.priv < kPrivMachine) {
            if ((w_csr_addr >= 0xC00 && w_csr_addr <= 0xC1F) ||
                (w_csr_addr >= 0xC80 && w_csr_addr <= 0xC9F)) {
                const simrv::xlen::Word counter_bit = 1u << (w_csr_addr & 0x1Fu);
                bool access_denied = (state_.mcounteren & counter_bit) == 0;
                if (state_.priv == kPrivUser) {
                    access_denied = access_denied || ((state_.scounteren & counter_bit) == 0);
                }
                if (access_denied) {
                    raise_decode_exception(ctx, ExceptionCode::IllegalInstruction);
                    return;
                }
            }
        }
    }

    if (simrv::compiler::unlikely(w_csr_addr == csr_addr(Csr::Fflags) ||
                                  w_csr_addr == csr_addr(Csr::Frm) ||
                                  w_csr_addr == csr_addr(Csr::Fcsr))) {
        if ((state_.mstatus & MstatusBit::Fs) == 0) {
            raise_decode_exception(ctx, ExceptionCode::IllegalInstruction);
            return;
        }
    }

    if (funct3 != Funct3::Priv) {
        auto res = read_csr(w_csr_addr);
        if (!res) {
            raise_decode_exception(ctx, res.error());
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
