/**
 * @file StateControl.cpp
 * @brief SimRV implementation unit.
 */
#include "simrv/core/Logger.hpp"
#include "simrv/core/StateControl.hpp"

#include <cstdint>
#include <print>
#include <ranges>
#include <ostream>

#include "simrv/xlen/Types.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/Sbi.hpp"
#include "simrv/core/Tlb.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/xlen/Constants.hpp"

namespace simrv::core {

constexpr Counter kWord32Mask = static_cast<Counter>(kLower32Mask);
constexpr Counter kWord32Shift = 32;

constexpr int kLogHexWidth = static_cast<int>(kXLenHexDigits);

constexpr Address kClintMtimecmpOffset = 0x4000;
constexpr Address kClintMtimeOffset = 0xbff8;

void InterruptController::updateMip(PlicMmio& plic, ArchState& state) {
    // Evaluate if any pending and enabled interrupt exists for Context 0 (M-mode) and Context 1 (S-mode)
    bool m_ext = false;
    bool s_ext = false;

    // Evaluate Context 0 (M-mode)
    Word m_max_prio = 0;
    for (int i : std::views::iota(1, 32)) {
        const auto idx = static_cast<std::size_t>(i);
        if ((plic.plic_pending.at(0) & (1u << i)) != 0 && (plic.plic_enables.at(0).at(0) & (1u << i)) != 0) {
            if (plic.plic_priorities.at(idx) > m_max_prio) {
                m_max_prio = plic.plic_priorities.at(idx);
            }
        }
    }
    if (m_max_prio > plic.plic_threshold.at(0)) {
        m_ext = true;
    }

    // Evaluate Context 1 (S-mode)
    Word s_max_prio = 0;
    for (int i : std::views::iota(1, 32)) {
        const auto idx = static_cast<std::size_t>(i);
        if ((plic.plic_pending.at(0) & (1u << i)) != 0 && (plic.plic_enables.at(1).at(0) & (1u << i)) != 0) {
            if (plic.plic_priorities.at(idx) > s_max_prio) {
                s_max_prio = plic.plic_priorities.at(idx);
            }
        }
    }
    if (s_max_prio > plic.plic_threshold.at(1)) {
        s_ext = true;
    }

    if (m_ext) state.mip |= enum_mask(MipBit::Meip);
    else state.mip &= ~enum_mask(MipBit::Meip);

    if (s_ext) state.mip |= enum_mask(MipBit::Seip);
    else state.mip &= ~enum_mask(MipBit::Seip);
}

void InterruptController::setIrq(PlicMmio& plic, int irq_num, int state_val) {
    if (irq_num <= 0 || irq_num >= 32) return;
    const Word mask = static_cast<Word>(1) << irq_num;
    if (state_val != 0) {
        plic.plic_pending.at(0) |= mask;
    } else {
        plic.plic_pending.at(0) &= ~mask;
    }
    plic.cpu_.plic_update_mip();
}

auto PlicMmio::handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool {
    if (req.opcode == memory::TlOpcodeA::Get) {
        resp.data = mmio_read(offset(req.address));
    } else {
        mmio_write(offset(req.address), req.data);
    }
    return true;
}

auto PlicMmio::get_context_for_offset(Address offset) const -> int {
    if (offset >= 0x200000 && offset < 0x201000) return 0; // Context 0 (M-mode)
    if (offset >= 0x201000 && offset < 0x202000) return 1; // Context 1 (S-mode)
    return -1;
}

auto PlicMmio::mmio_read(Address offset) -> Word {
    if (offset < 0x1000) {
        return plic_priorities.at(offset / 4);
    }
    if (offset >= 0x1000 && offset < 0x1080) {
        return plic_pending.at((offset - 0x1000) / 4);
    }
    if (offset >= 0x2000 && offset < 0x2080) {
        return plic_enables.at(0).at((offset - 0x2000) / 4);
    }
    if (offset >= 0x2080 && offset < 0x2100) {
        return plic_enables.at(1).at((offset - 0x2080) / 4);
    }
    
    int context = get_context_for_offset(offset);
    if (context >= 0) {
        const auto ctx_idx = static_cast<std::size_t>(context);
        Address ctx_base = 0x200000 + (context * 0x1000);
        if (offset == ctx_base) {
            return plic_threshold.at(ctx_idx);
        }
        if (offset == ctx_base + 4) {
            // Claim: evaluate highest priority pending & enabled for this context
            Word max_prio = 0;
            int claim_id = 0;
            for (int i : std::views::iota(1, 32)) {
                const auto idx = static_cast<std::size_t>(i);
                if ((plic_pending.at(0) & (1u << i)) != 0 && (plic_enables.at(ctx_idx).at(0) & (1u << i)) != 0) {
                    if (plic_priorities.at(idx) > max_prio) {
                        max_prio = plic_priorities.at(idx);
                        claim_id = i;
                    }
                }
            }
            if (claim_id > 0) {
                // Clear the pending bit on claim
                plic_pending.at(0) &= ~(1u << claim_id);
                plic_claim.at(ctx_idx) = static_cast<Word>(claim_id);
                cpu_.plic_update_mip();
            }
            return static_cast<Word>(claim_id);
        }
    }
    return 0;
}

void PlicMmio::mmio_write(Address offset, Word wdata) {
    if (offset < 0x1000) {
        plic_priorities.at(offset / 4) = wdata;
    } else if (offset >= 0x2000 && offset < 0x2080) {
        plic_enables.at(0).at((offset - 0x2000) / 4) = wdata;
    } else if (offset >= 0x2080 && offset < 0x2100) {
        plic_enables.at(1).at((offset - 0x2080) / 4) = wdata;
    } else {
        int context = get_context_for_offset(offset);
        if (context >= 0) {
            const auto ctx_idx = static_cast<std::size_t>(context);
            Address ctx_base = 0x200000 + (context * 0x1000);
            if (offset == ctx_base) {
                plic_threshold.at(ctx_idx) = wdata;
            } else if (offset == ctx_base + 4) {
                // Complete: indicates the handler has finished with the IRQ
                if (plic_claim.at(ctx_idx) == wdata && wdata != 0) {
                    plic_claim.at(ctx_idx) = 0;
                }
            }
        }
    }
    cpu_.plic_update_mip();
}

auto ClintMmio::handle_request(const memory::TlChannelA& req, memory::TlChannelD& resp) -> bool {
    const Address off = offset(req.address);
    const int req_bytes = 1 << (req.size & 3);
    
    if (req.opcode == memory::TlOpcodeA::Get) {
        if (req_bytes == 8) {
            if (off == kClintMtimeOffset) {
                resp.data = static_cast<Word>(mtime);
            } else if (off == kClintMtimecmpOffset) {
                resp.data = static_cast<Word>(mtimecmp);
            } else {
                resp.data = mmio_read(off);
            }
        } else {
            // For 32-bit reads, if offset aligns with 64-bit register halves
            if (off == kClintMtimeOffset) {
                resp.data = static_cast<Word>(mtime & 0xFFFFFFFF);
            } else if (off == kClintMtimeOffset + 4) {
                resp.data = static_cast<Word>(mtime >> 32);
            } else if (off == kClintMtimecmpOffset) {
                resp.data = static_cast<Word>(mtimecmp & 0xFFFFFFFF);
            } else if (off == kClintMtimecmpOffset + 4) {
                resp.data = static_cast<Word>(mtimecmp >> 32);
            } else {
                resp.data = mmio_read(off);
            }
        }
    } else {
        const Word wdata = req.data;
        if (req_bytes == 8) {
            if (off == kClintMtimecmpOffset) {
                mtimecmp = static_cast<Counter>(wdata);
                cpu_.evaluate_timer_interrupt();
            } else if (off == kClintMtimeOffset) {
                mtime = static_cast<Counter>(wdata);
                cpu_.evaluate_timer_interrupt();
            } else {
                mmio_write(off, wdata);
            }
        } else {
            if (off == kClintMtimecmpOffset) {
                mtimecmp = (mtimecmp & ~static_cast<Counter>(0xFFFFFFFFull)) | (wdata & 0xFFFFFFFFull);
                cpu_.evaluate_timer_interrupt();
            } else if (off == kClintMtimecmpOffset + 4) {
                mtimecmp = (mtimecmp & 0xFFFFFFFFull) | (static_cast<Counter>(wdata) << 32);
                cpu_.evaluate_timer_interrupt();
            } else if (off == kClintMtimeOffset) {
                mtime = (mtime & ~static_cast<Counter>(0xFFFFFFFFull)) | (wdata & 0xFFFFFFFFull);
                cpu_.evaluate_timer_interrupt();
            } else if (off == kClintMtimeOffset + 4) {
                mtime = (mtime & 0xFFFFFFFFull) | (static_cast<Counter>(wdata) << 32);
                cpu_.evaluate_timer_interrupt();
            } else {
                mmio_write(off, wdata);
            }
        }
    }
    return true;
}

auto ClintMmio::mmio_read(Address offset) const -> Word {
    switch (offset) {
        case 0x0000: // msip for hart 0
            return (cpu_.state().mip & enum_mask(MipBit::Msip)) != 0 ? 1 : 0;
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
    if (offset == 0x0000) { // msip for hart 0
        if ((wdata & 1) != 0) {
            cpu_.state().mip |= enum_mask(MipBit::Msip);
        } else {
            cpu_.state().mip &= ~enum_mask(MipBit::Msip);
        }
    } else if (offset == kClintMtimecmpOffset) {
        mtimecmp = (mtimecmp & ~kWord32Mask) | wdata_64;
        cpu_.state().mip &= ~enum_mask(MipBit::Mtip);
        cpu_.evaluate_timer_interrupt();
    } else if (offset == kClintMtimecmpOffset + 4) {
        mtimecmp = (mtimecmp & kWord32Mask) | (wdata_64 << kWord32Shift);
        cpu_.state().mip &= ~enum_mask(MipBit::Mtip);
        cpu_.evaluate_timer_interrupt();
    }
}

void TrapController::mret(ArchState& state, Tlb& tlb) {
    CSRValue mstatus = state.mstatus;
    const CSRValue mpp = (mstatus & enum_mask(MstatusBit::Mpp)) >> 11;
    const CSRValue mpie = (mstatus & enum_mask(MstatusBit::Mpie)) >> 7;

    // Restore MIE (bit 3) from MPIE (bit 7)
    if (mpie != 0) {
        mstatus |= enum_mask(MstatusBit::Mie);
    } else {
        mstatus &= ~enum_mask(MstatusBit::Mie);
    }
    // Set MPIE (bit 7) to 1
    mstatus |= enum_mask(MstatusBit::Mpie);
    // Clear MPP (bits [12:11]) to U-mode (0)
    mstatus &= ~enum_mask(MstatusBit::Mpp);

    if (static_cast<PrivilegeLevel>(mpp) < kPrivMachine) {
        mstatus &= ~enum_mask(MstatusBit::Mprv);
    }

    state.mstatus = mstatus;
    state.priv = static_cast<PrivilegeLevel>(mpp);
    state.update_xlen();
    state.pc = state.mepc;
    if (state.regs.xlen == 32) {
        state.pc = static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(state.pc)));
    }
    state.reserved = 0;
}

void TrapController::sret(ArchState& state, Tlb& tlb) {
    CSRValue mstatus = state.mstatus;
    const CSRValue spp = (mstatus & enum_mask(MstatusBit::Spp)) >> 8;
    const CSRValue spie = (mstatus & enum_mask(MstatusBit::Spie)) >> 5;

    // Restore SIE (bit 1) from SPIE (bit 5)
    if (spie != 0) {
        mstatus |= enum_mask(MstatusBit::Sie);
    } else {
        mstatus &= ~enum_mask(MstatusBit::Sie);
    }
    // Set SPIE (bit 5) to 1
    mstatus |= enum_mask(MstatusBit::Spie);
    // Clear SPP (bit 8) to U-mode (0)
    mstatus &= ~enum_mask(MstatusBit::Spp);

    // Returning to a less privileged mode than M-mode always clears MPRV
    mstatus &= ~enum_mask(MstatusBit::Mprv);

    state.mstatus = mstatus;
    state.priv = static_cast<PrivilegeLevel>(spp);
    state.update_xlen();
    state.pc = state.sepc;
    if (state.regs.xlen == 32) {
        state.pc = static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(state.pc)));
    }
    state.reserved = 0;
}

namespace {
auto trap_cause_name(TrapCause cause) -> std::string {
    const bool is_interrupt = (cause & (1ULL << 63)) != 0;
    const uint64_t code = cause & ~(1ULL << 63);
    if (is_interrupt) {
        switch (code) {
            case 1:  return "Supervisor software interrupt";
            case 3:  return "Machine software interrupt";
            case 5:  return "Supervisor timer interrupt";
            case 7:  return "Machine timer interrupt";
            case 9:  return "Supervisor external interrupt";
            case 11: return "Machine external interrupt";
            default: return "Unknown interrupt " + std::to_string(code);
        }
    } else {
        switch (code) {
            case 0:  return "Instruction address misaligned";
            case 1:  return "Instruction access fault";
            case 2:  return "Illegal instruction";
            case 3:  return "Breakpoint";
            case 4:  return "Load address misaligned";
            case 5:  return "Load access fault";
            case 6:  return "Store/AMO address misaligned";
            case 7:  return "Store/AMO access fault";
            case 8:  return "Environment call from U-mode";
            case 9:  return "Environment call from S-mode";
            case 11: return "Environment call from M-mode";
            case 12: return "Instruction page fault";
            case 13: return "Load page fault";
            case 15: return "Store/AMO page fault";
            default: return "Unknown exception " + std::to_string(code);
        }
    }
}
} // namespace

void TrapController::raiseException(CPU& cpu, TrapCause cause, CSRValue tval) {
    ArchState& state = cpu.state();
    const Address trap_pc = state.pc;

    if (cpu.trap_log_stream != nullptr && cpu.trap_log_stream->is_open()) {
        std::println(
            *cpu.trap_log_stream,
            "TRAP mtime={} cause={:0{}x} ({}) pc={:0{}x} priv={} ra={:0{}x} sp={:0{}x} tp={:0{}x} a0={:0{}x} "
            "a1={:0{}x} mtvec={:0{}x} stvec={:0{}x} mepc={:0{}x} sepc={:0{}x} satp={:0{}x} "
            "tval={:0{}x}",
            cpu.clint_mmio.mtime,
            static_cast<uint64_t>(cause), kLogHexWidth,
            trap_cause_name(cause),
            static_cast<uint64_t>(trap_pc),
            kLogHexWidth,            static_cast<unsigned>(state.priv),
            static_cast<uint64_t>(state.regs.read(static_cast<RegId>(1))), kLogHexWidth,
            static_cast<uint64_t>(state.regs.read(static_cast<RegId>(2))), kLogHexWidth,
            static_cast<uint64_t>(state.regs.read(static_cast<RegId>(4))), kLogHexWidth,
            static_cast<uint64_t>(state.regs.read(static_cast<RegId>(10))), kLogHexWidth,
            static_cast<uint64_t>(state.regs.read(static_cast<RegId>(11))), kLogHexWidth,
            static_cast<uint64_t>(state.mtvec), kLogHexWidth, static_cast<uint64_t>(state.stvec),
            kLogHexWidth, static_cast<uint64_t>(state.mepc), kLogHexWidth,
            static_cast<uint64_t>(state.sepc), kLogHexWidth, static_cast<uint64_t>(state.satp),
            kLogHexWidth, static_cast<uint64_t>(tval), kLogHexWidth);
        cpu.trap_log_stream->flush();
    }

    if (cpu.sbi.handle_ecall(cause)) {
        state.reserved = 0;
        cpu.pipeline_context.pending_exception = std::nullopt;
        cpu.pipeline_context.pending_tval = 0;
        return;
    }

    CSRValue deleg = 0;
    if (state.priv <= kPrivSupervisor && misa_has_extension(state.misa, IsaExtension::S)) {
        const auto cause_code = static_cast<CSRValue>(trap_exception_code(cause));
        if (cause_code < xlen::kXLenBits) {
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
                        (((state.mstatus & enum_mask(MstatusBit::Sie)) >> 1) << 5);
        state.mstatus = (state.mstatus & ~enum_mask(MstatusBit::Spp)) |
                        (static_cast<CSRValue>(std::to_underlying(state.priv)) << 8);
        state.mstatus &= ~enum_mask(MstatusBit::Sie);
        state.priv = kPrivSupervisor;
        state.update_xlen();

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
                        (((state.mstatus & enum_mask(MstatusBit::Mie)) >> 3) << 7);
        state.mstatus = (state.mstatus & ~enum_mask(MstatusBit::Mpp)) |
                        (static_cast<CSRValue>(std::to_underlying(state.priv)) << 11);
        state.mstatus &= ~enum_mask(MstatusBit::Mie);
        state.priv = kPrivMachine;
        state.update_xlen();

        const Address tvec_base = state.mtvec & ~Address{3};
        const Word tvec_mode = state.mtvec & 3;
        if (tvec_mode == 1 && trap_is_interrupt(cause)) {
            state.pc = tvec_base + 4 * trap_exception_code(cause);
        } else {
            state.pc = tvec_base;
        }
    }
    if (state.regs.xlen == 32) {
        state.pc = static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(state.pc)));
    }
    state.reserved = 0;
    cpu.pipeline_context.pending_exception = std::nullopt;
    cpu.pipeline_context.pending_tval = 0;

    if (cpu.machine_ && cpu.machine_->s_tuimode && cpu.machine_->uart && cause == static_cast<TrapCause>(ExceptionCode::Breakpoint)) {
        if (cpu.machine_->uart->tui()) {
            cpu.machine_->uart->tui()->set_status_override("\033[1;38;5;234;48;5;210m TRAPPED \033[0m");
        }
        if constexpr (simrv::xlen::kIsXLen64) {
            simrv::log::warn(
                "Breakpoint: cause=0x{:016x} pc=0x{:016x} tval=0x{:016x}",
                static_cast<uint64_t>(cause),
                static_cast<uint64_t>(trap_pc),
                static_cast<uint64_t>(tval));
        } else {
            simrv::log::warn(
                "Breakpoint: cause=0x{:08x} pc=0x{:08x} tval=0x{:08x}",
                static_cast<uint64_t>(cause),
                static_cast<uint64_t>(trap_pc),
                static_cast<uint64_t>(tval));
        }
        cpu.machine_->uart->tui_update();
        cpu.machine_->uart->tui_pause_loop();
    }
}

auto TrapController::canExecutePrivilegedInstruction(PrivilegeLevel current_priv, CSRValue misa,
                                                    CSRValue mstatus, Instruction funct12,
                                                    Word funct7) -> bool {
    if (funct12 == static_cast<Instruction>(Funct12Priv::Mret)) {
        return current_priv >= kPrivMachine;
    }
    if (funct12 == static_cast<Instruction>(Funct12Priv::Sret)) {
        return misa_has_extension(misa, IsaExtension::S) &&
               current_priv >= kPrivSupervisor &&
               !(current_priv == kPrivSupervisor && (mstatus & enum_mask(MstatusBit::Tsr)) != 0);
    }
    if (funct7 == static_cast<Instruction>(Funct7Priv::SfenceVma)) {
        return misa_has_extension(misa, IsaExtension::S) &&
               current_priv >= kPrivSupervisor &&
               !(current_priv == kPrivSupervisor && (mstatus & enum_mask(MstatusBit::Tvm)) != 0);
    }
    return true;
}

auto TrapController::canAccessCsr(PrivilegeLevel current_priv, CSRAddress csr_addr, bool is_write) -> bool {
    const Word csr_priv = (csr_addr >> 8) & 0x3u;
    const bool is_read_only = ((csr_addr >> 10) & 0x3u) == 0x3u;

    if (current_priv < static_cast<PrivilegeLevel>(csr_priv)) {
        return false;
    }
    if (is_write && is_read_only) {
        return false;
    }
    return true;
}

}  // namespace simrv::core
