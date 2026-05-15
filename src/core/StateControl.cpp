/**
 * @file StateControl.cpp
 * @brief SimRV implementation unit.
 */
#include "simrv/core/StateControl.hpp"

#include <algorithm>
#include <cstdint>
#include <print>

#include "simrv/Define.hpp"
#include "simrv/core/BootHacks.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Sbi.hpp"
#include "simrv/core/Tlb.hpp"
#include "simrv/xlen/Constants.hpp"

namespace simrv::core {

constexpr Counter kWord32Mask = static_cast<Counter>(kLower32Mask);
constexpr Counter kWord32Shift = 32;

constexpr int kLogHexWidth = static_cast<int>(kXLenHexDigits);

constexpr Address kPlicClaimCompleteOffset = simrv::mmio::kPlicHartBase + 4;
constexpr Address kClintMtimecmpOffset = 0x4000;
constexpr Address kClintMtimeOffset = 0xbff8;

void InterruptController::updateMip(PlicMmio& plic, ArchState& state) {
    const CSRValue mask = plic.pending_irq & ~plic.served_irq;
    const CSRValue ext_irq = enum_mask(MipBit::Meip) | enum_mask(MipBit::Seip);
    if (mask != 0u) {
        state.mip |= ext_irq;
    } else {
        state.mip &= ~ext_irq;
    }
}

void InterruptController::setIrq(PlicMmio& plic, ArchState& state, int irq_num, int state_val) {
    const CSRValue mask = static_cast<CSRValue>(1U) << (irq_num - 1);
    if (state_val != 0) {
        plic.pending_irq |= mask;
    } else {
        plic.pending_irq &= ~mask;
    }
    updateMip(plic, state);
}

auto PlicMmio::handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool {
    if (req.opcode == memory::TlOpcodeA::Get) {
        resp.data = mmio_read(offset(req.address));
    } else {
        mmio_write(offset(req.address), req.data);
    }
    return true;
}

auto PlicMmio::mmio_read(Address offset) -> Word {
    if (offset == kPlicClaimCompleteOffset) {
        const CSRValue mask = pending_irq & ~served_irq;
        if (mask != 0) {
            served_irq |= mask;
            cpu_.plic_update_mip();
            return mask;
        }
    }
    return 0;
}

void PlicMmio::mmio_write(Address offset, Word wdata) {
    if (offset == kPlicClaimCompleteOffset) {
        served_irq &= ~(1U << (wdata - 1));
        cpu_.plic_update_mip();
    }
}

auto ClintMmio::handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool {
    if (req.opcode == memory::TlOpcodeA::Get) {
        resp.data = mmio_read(offset(req.address));
    } else {
        mmio_write(offset(req.address), req.data);
    }
    return true;
}

auto ClintMmio::mmio_read(Address offset) const -> Word {
    switch (offset) {
        case kClintMtimeOffset:
            return static_cast<Word>(mtime);
        case kClintMtimeOffset + 4:
            return static_cast<Word>(mtime >> 32);
        case kClintMtimecmpOffset:
            return static_cast<Word>(mtimecmp);
        case kClintMtimecmpOffset + 4:
            return static_cast<Word>(mtimecmp >> 32);
        default:
            return 0;
    }
}

void ClintMmio::mmio_write(Address offset, Word wdata) {
    const Counter wdata_64 = static_cast<Counter>(wdata) & kWord32Mask;
    if (offset == kClintMtimecmpOffset) {
        mtimecmp = (mtimecmp & ~kWord32Mask) | wdata_64;
        cpu_.state().mip &= ~enum_mask(MipBit::Mtip);
    } else if (offset == kClintMtimecmpOffset + 4) {
        mtimecmp = (mtimecmp & kWord32Mask) | (wdata_64 << kWord32Shift);
        cpu_.state().mip &= ~enum_mask(MipBit::Mtip);
    }
}

void TrapController::mret(ArchState& state, Tlb& tlb) {
    CSRValue mstatus = state.mstatus;
    const CSRValue mpp = (mstatus & enum_mask(MstatusBit::Mpp)) >> 11;
    const CSRValue mpie = (mstatus & enum_mask(MstatusBit::Mpie)) >> 7;

    mstatus = (mstatus & ~(static_cast<CSRValue>(1) << mpp)) | (mpie << mpp);
    mstatus |= enum_mask(MstatusBit::Mpie);
    mstatus &= ~enum_mask(MstatusBit::Mpp);
    if (mpp < kPrivMachine) {
        mstatus &= ~enum_mask(MstatusBit::Mprv);
    }

    state.mstatus = mstatus;
    state.priv = static_cast<PrivilegeLevel>(mpp);
    state.pc = state.mepc;
    tlb.flush();
}

void TrapController::sret(ArchState& state, Tlb& tlb) {
    CSRValue mstatus = state.mstatus;
    const CSRValue spp = (mstatus & enum_mask(MstatusBit::Spp)) >> 8;
    const CSRValue spie = (mstatus & enum_mask(MstatusBit::Spie)) >> 5;

    mstatus = (mstatus & ~(static_cast<CSRValue>(1) << spp)) | (spie << spp);
    mstatus |= enum_mask(MstatusBit::Spie);
    mstatus &= ~enum_mask(MstatusBit::Spp);
    mstatus &= ~enum_mask(MstatusBit::Mprv);

    state.mstatus = mstatus;
    state.priv = static_cast<PrivilegeLevel>(spp);
    state.pc = state.sepc;
    tlb.flush();
}

void TrapController::raiseException(CPU& cpu, TrapCause cause, CSRValue tval) {
    ArchState& state = cpu.state();
    const Address trap_pc = simrv::boot::normalize_legacy_trap_pc(state);

    if (cpu.trap_log_stream != nullptr && cpu.trap_log_stream->is_open()) {
        std::println(
            *cpu.trap_log_stream,
            "__ TRAP cause={:0{}x} pc={:0{}x} priv={} ra={:0{}x} sp={:0{}x} tp={:0{}x} a0={:0{}x} "
            "a1={:0{}x} mtvec={:0{}x} stvec={:0{}x} mepc={:0{}x} sepc={:0{}x} satp={:0{}x} "
            "tval={:0{}x}",
            static_cast<uint64_t>(cause), kLogHexWidth, static_cast<uint64_t>(trap_pc),
            kLogHexWidth, static_cast<unsigned>(state.priv),
            static_cast<uint64_t>(state.regs.read(1)), kLogHexWidth,
            static_cast<uint64_t>(state.regs.read(2)), kLogHexWidth,
            static_cast<uint64_t>(state.regs.read(4)), kLogHexWidth,
            static_cast<uint64_t>(state.regs.read(10)), kLogHexWidth,
            static_cast<uint64_t>(state.regs.read(11)), kLogHexWidth,
            static_cast<uint64_t>(state.mtvec), kLogHexWidth, static_cast<uint64_t>(state.stvec),
            kLogHexWidth, static_cast<uint64_t>(state.mepc), kLogHexWidth,
            static_cast<uint64_t>(state.sepc), kLogHexWidth, static_cast<uint64_t>(state.satp),
            kLogHexWidth, static_cast<uint64_t>(tval), kLogHexWidth);
        cpu.trap_log_stream->flush();
    }

    if (simrv::sbi::handle_sbi_ecall(cpu, cause)) {
        cpu.TLB_flush();
        cpu.pipeline_context.pending_exception = kWordAllOnes;
        cpu.pipeline_context.pending_tval = 0;
        return;
    }

    CSRValue deleg = 0;
    if (state.priv <= kPrivSupervisor) {
        const auto cause_code = static_cast<CSRValue>(trap_exception_code(cause));
        if (cause_code < kXLenBits) {
            if (trap_is_interrupt(cause)) {
                deleg = (state.mideleg >> cause_code) & 1;
            } else {
                deleg = (state.medeleg >> cause_code) & 1;
            }
        }
    } else {
        deleg = 0;
    }

    if (deleg != 0u) {
        state.scause = cause;
        state.sepc = trap_pc;
        state.stval = tval;
        state.mstatus = (state.mstatus & ~enum_mask(MstatusBit::Spie)) |
                        (((state.mstatus >> state.priv) & 1) << 5);
        state.mstatus = (state.mstatus & ~enum_mask(MstatusBit::Spp)) |
                        (static_cast<CSRValue>(state.priv) << 8);
        state.mstatus &= ~enum_mask(MstatusBit::Sie);
        state.priv = kPrivSupervisor;

        const Address tvec_base = state.stvec & ~Address{3};
        const Word tvec_mode = state.stvec & 3;
        if (tvec_mode == 1 && trap_is_interrupt(cause)) {
            state.pc = tvec_base + 4 * trap_exception_code(cause);
        } else {
            state.pc = tvec_base;
        }
    } else {
        state.mcause = cause;
        state.mepc = trap_pc;
        state.mtval = tval;
        state.mstatus = (state.mstatus & ~enum_mask(MstatusBit::Mpie)) |
                        (((state.mstatus >> state.priv) & 1) << 7);
        state.mstatus = (state.mstatus & ~enum_mask(MstatusBit::Mpp)) |
                        (static_cast<CSRValue>(state.priv) << 11);
        state.mstatus &= ~enum_mask(MstatusBit::Mie);
        state.priv = kPrivMachine;

        const Address tvec_base = state.mtvec & ~Address{3};
        const Word tvec_mode = state.mtvec & 3;
        if (tvec_mode == 1 && trap_is_interrupt(cause)) {
            state.pc = tvec_base + 4 * trap_exception_code(cause);
        } else {
            state.pc = tvec_base;
        }
    }
    cpu.TLB_flush();
    cpu.pipeline_context.pending_exception = kWordAllOnes;
    cpu.pipeline_context.pending_tval = 0;
}
}  // namespace simrv::core
