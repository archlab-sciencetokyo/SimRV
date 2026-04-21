/**
 * @file StateControl.cpp
 * @brief SimRV implementation unit.
 */
#include "StateControl.hpp"

#include <unistd.h>

#include <iomanip>

#include "Cpu.hpp"

namespace {
constexpr Address D_KERNEL_PHYS_BASE = static_cast<Address>(0x80400000u);
constexpr Address D_KERNEL_PHYS_END = static_cast<Address>(0x84000000u);
constexpr Address D_KERNEL_VIRT_BASE = static_cast<Address>(0xC0000000u);

Address normalize_trap_pc(const CPU& cpu) {
    if (cpu.priv != kPrivSupervisor || (cpu.satp >> 31) == 0) {
        return cpu.pc;
    }
    if (cpu.pc >= D_KERNEL_PHYS_BASE && cpu.pc < D_KERNEL_PHYS_END) {
        return cpu.pc - D_KERNEL_PHYS_BASE + D_KERNEL_VIRT_BASE;
    }
    return cpu.pc;
}

constexpr Word SBI_LEGACY_SET_TIMER = 0x00u;
constexpr Word SBI_LEGACY_CONSOLE_PUTCHAR = 0x01u;
constexpr Word SBI_LEGACY_CONSOLE_GETCHAR = 0x02u;
constexpr Word SBI_LEGACY_CLEAR_IPI = 0x03u;
constexpr Word SBI_LEGACY_SEND_IPI = 0x04u;
constexpr Word SBI_LEGACY_REMOTE_FENCE_I = 0x05u;
constexpr Word SBI_LEGACY_REMOTE_SFENCE_VMA = 0x06u;
constexpr Word SBI_LEGACY_REMOTE_SFENCE_VMA_ASID = 0x07u;
constexpr Word SBI_LEGACY_SHUTDOWN = 0x08u;

constexpr Word SBI_EXT_BASE = 0x10u;
constexpr Word SBI_EXT_TIME = 0x54494D45u;  // "TIME"

constexpr Word SBI_BASE_GET_SPEC_VERSION = 0x0u;
constexpr Word SBI_BASE_GET_IMPL_ID = 0x1u;
constexpr Word SBI_BASE_GET_IMPL_VERSION = 0x2u;
constexpr Word SBI_BASE_PROBE_EXTENSION = 0x3u;
constexpr Word SBI_BASE_GET_MVENDORID = 0x4u;
constexpr Word SBI_BASE_GET_MARCHID = 0x5u;
constexpr Word SBI_BASE_GET_MIMPID = 0x6u;

constexpr Word SBI_TIME_SET_TIMER = 0x0u;

constexpr Word SBI_SUCCESS = 0u;
constexpr Word SBI_ERR_NOT_SUPPORTED = static_cast<Word>(-2);
constexpr Word SBI_SPEC_VERSION_0_2 = 0x00000002u;
constexpr Word SBI_IMPL_ID_SIMRV = 0x1u;
constexpr Word SBI_IMPL_VERSION = 0x1u;

void sbi_return(CPU& cpu, Word error, Word value) {
    cpu.reg[10] = error;
    cpu.reg[11] = value;
    cpu.pc += 4;
}

bool handle_sbi_ecall(CPU& cpu, TrapCause cause) {
    if (cause != enum_mask(ExceptionCode::SupervisorEcall) &&
        cause != enum_mask(ExceptionCode::MachineEcall)) {
        return false;
    }

    const Word ext_id = cpu.reg[17];
    const Word func_id = cpu.reg[16];
    if (cpu.trap_log_stream != nullptr && cpu.trap_log_stream->is_open()) {
        (*cpu.trap_log_stream) << "__ SBI ecall cause=" << std::dec << static_cast<unsigned>(cause)
                               << " ext=" << std::hex << std::setw(8) << std::setfill('0')
                               << static_cast<unsigned>(ext_id) << " fid=" << std::setw(8)
                               << static_cast<unsigned>(func_id) << " a0=" << std::setw(8)
                               << static_cast<unsigned>(cpu.reg[10]) << " a1=" << std::setw(8)
                               << static_cast<unsigned>(cpu.reg[11]) << " pc=" << std::setw(8)
                               << static_cast<unsigned>(cpu.pc) << std::dec << '\n';
        cpu.trap_log_stream->flush();
    }

    switch (ext_id) {
        case SBI_LEGACY_SET_TIMER: {
            const Counter timer_value =
                static_cast<Counter>(cpu.reg[10]) | (static_cast<Counter>(cpu.reg[11]) << 32);
            cpu.mtimecmp = timer_value;
            cpu.mip &= ~enum_mask(MipBit::Mtip);
            sbi_return(cpu, SBI_SUCCESS, 0);
            return true;
        }
        case SBI_LEGACY_CONSOLE_PUTCHAR: {
            const uint8_t ch = static_cast<uint8_t>(cpu.reg[10] & 0xffu);
            (void)!::write(STDOUT_FILENO, &ch, 1);
            sbi_return(cpu, SBI_SUCCESS, 0);
            return true;
        }
        case SBI_LEGACY_CONSOLE_GETCHAR: {
            sbi_return(cpu, static_cast<Word>(-1), 0);
            return true;
        }
        case SBI_LEGACY_CLEAR_IPI:
        case SBI_LEGACY_SEND_IPI:
        case SBI_LEGACY_REMOTE_FENCE_I:
        case SBI_LEGACY_REMOTE_SFENCE_VMA:
        case SBI_LEGACY_REMOTE_SFENCE_VMA_ASID:
        case SBI_LEGACY_SHUTDOWN:
            sbi_return(cpu, SBI_SUCCESS, 0);
            return true;
        case SBI_EXT_BASE:
            switch (func_id) {
                case SBI_BASE_GET_SPEC_VERSION:
                    sbi_return(cpu, SBI_SUCCESS, SBI_SPEC_VERSION_0_2);
                    return true;
                case SBI_BASE_GET_IMPL_ID:
                    sbi_return(cpu, SBI_SUCCESS, SBI_IMPL_ID_SIMRV);
                    return true;
                case SBI_BASE_GET_IMPL_VERSION:
                    sbi_return(cpu, SBI_SUCCESS, SBI_IMPL_VERSION);
                    return true;
                case SBI_BASE_PROBE_EXTENSION: {
                    const Word probed_ext = cpu.reg[10];
                    const bool supported =
                        (probed_ext == SBI_EXT_BASE) || (probed_ext == SBI_EXT_TIME);
                    sbi_return(cpu, SBI_SUCCESS, supported ? 1u : 0u);
                    return true;
                }
                case SBI_BASE_GET_MVENDORID:
                case SBI_BASE_GET_MARCHID:
                case SBI_BASE_GET_MIMPID:
                    sbi_return(cpu, SBI_SUCCESS, 0);
                    return true;
                default:
                    sbi_return(cpu, SBI_ERR_NOT_SUPPORTED, 0);
                    return true;
            }
        case SBI_EXT_TIME:
            if (func_id == SBI_TIME_SET_TIMER) {
                const Counter timer_value =
                    static_cast<Counter>(cpu.reg[10]) | (static_cast<Counter>(cpu.reg[11]) << 32);
                cpu.mtimecmp = timer_value;
                cpu.mip &= ~enum_mask(MipBit::Mtip);
                sbi_return(cpu, SBI_SUCCESS, 0);
            } else {
                sbi_return(cpu, SBI_ERR_NOT_SUPPORTED, 0);
            }
            return true;
        default:
            sbi_return(cpu, SBI_ERR_NOT_SUPPORTED, 0);
            return true;
    }
}
}  // namespace

void TlbUnit::flush() {
    for (Word i = 0; i < simrv::memory::kTlbSize; i++) {
        cpu_.TLB_inst_r[i].v_addr = cpu_.TLB_inst_r[i].p_addr = static_cast<Word>(-1);
        cpu_.TLB_data_r[i].v_addr = cpu_.TLB_data_r[i].p_addr = static_cast<Word>(-1);
        cpu_.TLB_data_w[i].v_addr = cpu_.TLB_data_w[i].p_addr = static_cast<Word>(-1);
    }
}

void InterruptController::updateMip() {
    CSRValue mask = cpu_.plic_pending_irq & ~cpu_.plic_served_irq;
    const CSRValue ext_irq = enum_mask(MipBit::Meip) | enum_mask(MipBit::Seip);
    if (mask) {
        cpu_.mip |= ext_irq;
    } else {
        cpu_.mip &= ~ext_irq;
    }
}

void InterruptController::setIrq(int irq_num, int state) {
    CSRValue mask = static_cast<CSRValue>(1u) << (irq_num - 1);
    if (state) {
        cpu_.plic_pending_irq |= mask;
    } else {
        cpu_.plic_pending_irq &= ~mask;
    }
    updateMip();
}

auto PlicMmio::read([[maybe_unused]] Machine& machine, Address p_addr, Word& rdata) -> bool {
    rdata = mmio_read(offset(p_addr));
    return true;
}

auto PlicMmio::write([[maybe_unused]] Machine& machine, Address p_addr, Word wdata) -> bool {
    mmio_write(offset(p_addr), wdata);
    return true;
}

auto PlicMmio::mmio_read(Address offset) -> Word {
    if (offset == simrv::mmio::kPlicHartBase + 4) {
        CSRValue mask = cpu_.plic_pending_irq & ~cpu_.plic_served_irq;
        if (mask != 0) {
            cpu_.plic_served_irq |= mask;
            cpu_.plic_update_mip();
            return mask;
        }
    }
    return 0;
}

void PlicMmio::mmio_write(Address offset, Word wdata) {
    if (offset == simrv::mmio::kPlicHartBase + 4) {
        cpu_.plic_served_irq &= ~(1u << (wdata - 1));
        cpu_.plic_update_mip();
    }
}

auto ClintMmio::read([[maybe_unused]] Machine& machine, Address p_addr, Word& rdata) -> bool {
    rdata = mmio_read(offset(p_addr));
    return true;
}

auto ClintMmio::write([[maybe_unused]] Machine& machine, Address p_addr, Word wdata) -> bool {
    mmio_write(offset(p_addr), wdata);
    return true;
}

auto ClintMmio::mmio_read(Address offset) -> Word {
    if (offset == 0xbff8) return static_cast<Word>(cpu_.mtime);
    if (offset == 0xbffc) return static_cast<Word>(cpu_.mtime >> 32);
    if (offset == 0x4000) return static_cast<Word>(cpu_.mtimecmp);
    if (offset == 0x4004) return static_cast<Word>(cpu_.mtimecmp >> 32);
    return 0;
}

void ClintMmio::mmio_write(Address offset, Word wdata) {
    if (offset == 0x4000) {
        cpu_.mtimecmp = (cpu_.mtimecmp & ~0xffffffffu) | wdata;
        cpu_.mip &= ~enum_mask(MipBit::Mtip);
    }
    if (offset == 0x4004) {
        cpu_.mtimecmp = (cpu_.mtimecmp & 0xffffffffu) | (static_cast<Counter>(wdata) << 32);
        cpu_.mip &= ~enum_mask(MipBit::Mtip);
    }
}

void TrapController::mret() {
    MstatusView mstatus(cpu_.mstatus);
    const CSRValue mpp = mstatus.bits.mpp;
    const CSRValue mpie = mstatus.bits.mpie;

    mstatus.rawValue = (mstatus.rawValue & ~(static_cast<CSRValue>(1) << mpp)) | (mpie << mpp);
    mstatus.bits.mpie = 1;
    mstatus.bits.mpp = 0;
    cpu_.mstatus = mstatus.raw();
    cpu_.priv = mpp;
    cpu_.pc = cpu_.mepc;
    cpu_.TLB_flush();
}

void TrapController::sret() {
    MstatusView mstatus(cpu_.mstatus);
    const CSRValue spp = mstatus.bits.spp;
    const CSRValue spie = mstatus.bits.spie;

    mstatus.rawValue = (mstatus.rawValue & ~(static_cast<CSRValue>(1) << spp)) | (spie << spp);
    mstatus.bits.spie = 1;
    mstatus.bits.spp = 0;
    cpu_.mstatus = mstatus.raw();
    cpu_.priv = spp;
    cpu_.pc = cpu_.sepc;
    cpu_.TLB_flush();
}

void TrapController::raiseException(TrapCause cause, CSRValue tval) {
    const Address trap_pc = normalize_trap_pc(cpu_);

    if (cpu_.trap_log_stream != nullptr && cpu_.trap_log_stream->is_open()) {
        (*cpu_.trap_log_stream) << "__ TRAP cause=" << std::hex << std::setw(8)
                                << std::setfill('0') << static_cast<unsigned>(cause) << " pc="
                                << std::setw(8) << static_cast<unsigned>(trap_pc) << " priv="
                                << std::dec << static_cast<unsigned>(cpu_.priv) << " ra="
                                << std::hex << std::setw(8) << static_cast<unsigned>(cpu_.reg[1])
                                << " sp=" << std::setw(8) << static_cast<unsigned>(cpu_.reg[2])
                                << " tp=" << std::setw(8) << static_cast<unsigned>(cpu_.reg[4])
                                << " a0=" << std::setw(8) << static_cast<unsigned>(cpu_.reg[10])
                                << " a1=" << std::setw(8) << static_cast<unsigned>(cpu_.reg[11])
                                << " mtvec=" << std::setw(8) << static_cast<unsigned>(cpu_.mtvec)
                                << " stvec=" << std::setw(8) << static_cast<unsigned>(cpu_.stvec)
                                << " mepc=" << std::setw(8) << static_cast<unsigned>(cpu_.mepc)
                                << " sepc=" << std::setw(8) << static_cast<unsigned>(cpu_.sepc)
                                << " satp=" << std::setw(8) << static_cast<unsigned>(cpu_.satp)
                                << " tval=" << std::setw(8) << static_cast<unsigned>(tval)
                                << std::dec << '\n';
        cpu_.trap_log_stream->flush();
    }

    if (handle_sbi_ecall(cpu_, cause)) {
        cpu_.TLB_flush();
        cpu_.pending_exception = ~0u;
        cpu_.pending_tval = 0;
        return;
    }

    CSRValue deleg;
    if (cpu_.priv <= kPrivSupervisor) {
        if (cause & kInterruptCauseBit) {
            deleg = (cpu_.mideleg >> (cause & 0x1F)) & 1;
        } else {
            deleg = (cpu_.medeleg >> (cause & 0x1F)) & 1;
        }
    } else {
        deleg = 0;
    }

    if (deleg) {
        cpu_.scause = cause;
        cpu_.sepc = trap_pc;
        cpu_.stval = tval;
        cpu_.mstatus = (cpu_.mstatus & ~enum_mask(MstatusBit::Spie)) |
                       (((cpu_.mstatus >> cpu_.priv) & 1) << 5);
        cpu_.mstatus = (cpu_.mstatus & ~enum_mask(MstatusBit::Spp)) | (cpu_.priv << 8);
        cpu_.mstatus &= ~enum_mask(MstatusBit::Sie);
        cpu_.priv = kPrivSupervisor;
        cpu_.pc = cpu_.stvec;
    } else {
        cpu_.mcause = cause;
        cpu_.mepc = trap_pc;
        cpu_.mtval = tval;
        cpu_.mstatus = (cpu_.mstatus & ~enum_mask(MstatusBit::Mpie)) |
                       (((cpu_.mstatus >> cpu_.priv) & 1) << 7);
        cpu_.mstatus = (cpu_.mstatus & ~enum_mask(MstatusBit::Mpp)) | (cpu_.priv << 11);
        cpu_.mstatus &= ~enum_mask(MstatusBit::Mie);
        cpu_.priv = kPrivMachine;
        cpu_.pc = cpu_.mtvec;
    }
    cpu_.TLB_flush();
    cpu_.pending_exception = ~0u;
    cpu_.pending_tval = 0;
}
