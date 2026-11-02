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
    if (ctx.opcode != static_cast<Opcode>(0) || ctx.op_id != isa::ILLEGAL) {
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

    if (ctx.op_id == isa::MOP) {
        // MOPs have no source dependency until redefined by another extension.
        ctx.rrs1 = 0;
        ctx.rrs2 = 0;
        ctx.rcsr = 0;
        return;
    }

    if (isa::is_hypervisor_memory_op(ctx.op_id)) {
        if (const auto exc =
                TrapController::check_privileged_instruction(state_, ctx.op_id, ctx.ir)) {
            raise_decode_exception(ctx, *exc);
            return;
        }
        ctx.rcsr = 0;
        return;
    }

    if (simrv::compiler::likely(opcode != Opcode::System)) {
        ctx.rcsr = 0;
        return;
    }

    simrv::xlen::CsrNumber const w_csr_addr =
        (funct3 != Funct3::Priv)          ? simrv::xlen::CsrNumber{funct12}
        : (funct12 == Funct12Priv::Ecall) ? csr_num(Csr::Mtvec)
        : (funct12 == Funct12Priv::Uret)  ? csr_num(Csr::Uepc)
        : (funct12 == Funct12Priv::Sret)  ? csr_num(Csr::Sepc)
        : (funct12 == Funct12Priv::Mret)  ? csr_num(Csr::Mepc)
                                          : simrv::xlen::CsrNumber{0};

    if (funct3 == Funct3::Priv) {
        if (const auto exc =
                TrapController::check_privileged_instruction(state_, ctx.op_id, ctx.ir)) {
            raise_decode_exception(ctx, *exc);
            return;
        }
    } else {
        const bool is_write =
            ((static_cast<uint8_t>(funct3) & 0x3u) == 0x1u) || (std::to_underlying(ctx.rs1) != 0);
        if (const auto exc = TrapController::check_csr_access(state_, w_csr_addr, is_write)) {
            raise_decode_exception(ctx, *exc);
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
