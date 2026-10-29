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

[[nodiscard]] constexpr auto is_implemented_supervisor_csr(uint16_t address) noexcept -> bool {
    switch (address) {
        case csr_addr(Csr::Sstatus):
        case csr_addr(Csr::Sie):
        case csr_addr(Csr::Stvec):
        case csr_addr(Csr::Scounteren):
        case csr_addr(Csr::Sscratch):
        case csr_addr(Csr::Sepc):
        case csr_addr(Csr::Scause):
        case csr_addr(Csr::Stval):
        case csr_addr(Csr::Sip):
        case csr_addr(Csr::Satp):
        case csr_addr(Csr::Senvcfg):
        case csr_addr(Csr::Sstateen0):
        case csr_addr(Csr::Sstateen1):
        case csr_addr(Csr::Sstateen2):
        case csr_addr(Csr::Sstateen3):
        case csr_addr(Csr::Scountovf):
            return true;
        default:
            return false;
    }
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
        constexpr simrv::xlen::CsrValue kHstatusHu = simrv::xlen::CsrValue{1} << 9U;
        if (!isa::misa_has_extension(state_.misa, isa::IsaExtension::H)) {
            raise_decode_exception(ctx, ExceptionCode::IllegalInstruction);
            return;
        }
        if (state_.virt) {
            raise_decode_exception(ctx, ExceptionCode::VirtualInstruction);
            return;
        }
        if (state_.priv == kPrivUser && (state_.hstatus & kHstatusHu) == 0) {
            raise_decode_exception(ctx, ExceptionCode::IllegalInstruction);
            return;
        }
        if (state_.priv != kPrivMachine && state_.priv != kPrivSupervisor &&
            state_.priv != kPrivUser) {
            raise_decode_exception(ctx, ExceptionCode::IllegalInstruction);
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
        if (!TrapController::can_execute_privileged_instruction(
                state_.priv, state_.misa, state_.mstatus, funct12, ctx.funct7, state_.hstatus,
                state_.virt)) {
            const bool virtual_instruction =
                state_.virt &&
                ((funct12 == Funct12Priv::Sret &&
                  (state_.priv == kPrivUser ||
                   (state_.hstatus & (simrv::xlen::CsrValue{1} << 22U)) != 0)) ||
                 (funct12 == Funct12Priv::Wfi &&
                  (state_.mstatus & MstatusBit::Tw) == 0 &&
                  (state_.priv == kPrivUser ||
                   (state_.hstatus & (simrv::xlen::CsrValue{1} << 21U)) != 0)) ||
                 ctx.funct7 == simrv::isa::Funct7Priv::HfenceVvma ||
                 ctx.funct7 == simrv::isa::Funct7Priv::HinvalVvma ||
                 ctx.funct7 == simrv::isa::Funct7Priv::HfenceGvma ||
                 ctx.funct7 == simrv::isa::Funct7Priv::HinvalGvma ||
                 ((funct12 == Funct12Priv::SfenceWInval ||
                   funct12 == Funct12Priv::SfenceInvalIr) &&
                  state_.priv == kPrivUser) ||
                 ((ctx.funct7 == simrv::isa::Funct7Priv::SfenceVma ||
                   ctx.funct7 == simrv::isa::Funct7Priv::SinvalVma) &&
                  (state_.priv == kPrivUser ||
                   (state_.hstatus & (simrv::xlen::CsrValue{1} << 20U)) != 0)));
            raise_decode_exception(ctx, virtual_instruction ? ExceptionCode::VirtualInstruction
                                                            : ExceptionCode::IllegalInstruction);
            return;
        }
    } else {
        const bool is_write =
            ((static_cast<uint8_t>(funct3) & 0x3u) == 0x1u) || (std::to_underlying(ctx.rs1) != 0);
        if (!TrapController::can_access_csr(state_.priv, state_.misa, w_csr_addr, is_write,
                                            state_.virt)) {
            const auto csr_address = w_csr_addr.value();
            const bool virtual_supervisor_csr =
                csr_address == csr_addr(Csr::Vsstatus) || csr_address == csr_addr(Csr::Vsie) ||
                csr_address == csr_addr(Csr::Vstvec) ||
                csr_address == csr_addr(Csr::Vsscratch) ||
                csr_address == csr_addr(Csr::Vsepc) || csr_address == csr_addr(Csr::Vscause) ||
                csr_address == csr_addr(Csr::Vstval) || csr_address == csr_addr(Csr::Vsip) ||
                csr_address == csr_addr(Csr::Vsatp) ||
                ((csr_address == csr_addr(Csr::Vstimecmp) ||
                  (csr_address == csr_addr(Csr::Vstimecmph) && state_.regs.xlen == 32)) &&
                 state_.priv == kPrivSupervisor);
            const bool virtual_hypervisor_csr =
                csr_address == csr_addr(Csr::Hstatus) || csr_address == csr_addr(Csr::Hedeleg) ||
                csr_address == csr_addr(Csr::Hideleg) || csr_address == csr_addr(Csr::Hie) ||
                csr_address == csr_addr(Csr::Hcounteren) ||
                csr_address == csr_addr(Csr::Htimedelta) ||
                csr_address == csr_addr(Csr::Hgeie) ||
                (csr_address == csr_addr(Csr::Hgeip) && !is_write) ||
                csr_address == csr_addr(Csr::Henvcfg) || csr_address == csr_addr(Csr::Htval) ||
                csr_address == csr_addr(Csr::Hip) || csr_address == csr_addr(Csr::Hvip) ||
                csr_address == csr_addr(Csr::Htinst) || csr_address == csr_addr(Csr::Hgatp) ||
                (csr_address >= 0x60CU && csr_address <= 0x60FU);
            const bool virtualization_fault =
                state_.virt &&
                ((state_.priv == kPrivUser &&
                  isa::misa_has_extension(state_.misa, isa::IsaExtension::S) &&
                  is_implemented_supervisor_csr(csr_address)) ||
                 (csr_address == csr_addr(Csr::Satp) &&
                  (state_.priv == kPrivUser ||
                   (state_.hstatus & (simrv::xlen::CsrValue{1} << 20U)) != 0)) ||
                 (state_.priv < kPrivMachine &&
                  (virtual_hypervisor_csr || virtual_supervisor_csr)));
            raise_decode_exception(ctx, virtualization_fault ? ExceptionCode::VirtualInstruction
                                                              : ExceptionCode::IllegalInstruction);
            return;
        }
        if (w_csr_addr == csr_addr(Csr::Satp)) {
            if (state_.virt && (state_.hstatus & (simrv::xlen::CsrValue{1} << 20U)) != 0) {
                raise_decode_exception(ctx, ExceptionCode::VirtualInstruction);
                return;
            }
            if (!state_.virt && state_.priv == kPrivSupervisor &&
                (state_.mstatus & MstatusBit::Tvm) != 0) {
                raise_decode_exception(ctx, ExceptionCode::IllegalInstruction);
                return;
            }
        }

        if (state_.priv < kPrivMachine) {
            if ((w_csr_addr >= 0xC00 && w_csr_addr <= 0xC1F) ||
                (w_csr_addr >= 0xC80 && w_csr_addr <= 0xC9F)) {
                const simrv::xlen::Word counter_bit = 1u << (w_csr_addr & 0x1Fu);
                bool access_denied = (state_.mcounteren & counter_bit) == 0;
                if (access_denied) {
                    raise_decode_exception(ctx, ExceptionCode::IllegalInstruction);
                    return;
                }
                if (state_.virt && (state_.hcounteren & counter_bit) == 0) {
                    raise_decode_exception(ctx, ExceptionCode::VirtualInstruction);
                    return;
                }
                if (state_.priv == kPrivUser && (state_.scounteren & counter_bit) == 0) {
                    raise_decode_exception(ctx, state_.virt ? ExceptionCode::VirtualInstruction
                                                            : ExceptionCode::IllegalInstruction);
                    return;
                }
            }
        }
    }

    if (simrv::compiler::unlikely(w_csr_addr == csr_addr(Csr::Fflags) ||
                                  w_csr_addr == csr_addr(Csr::Frm) ||
                                  w_csr_addr == csr_addr(Csr::Fcsr))) {
        if (!state_.extension_state_enabled(MstatusBit::Fs)) {
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
