/**
 * @file Sbi.cpp
 * @brief SBI handling implementation.
 */
#include "simrv/core/Sbi.hpp"

#include <unistd.h>

#include <cstdint>
#include <format>

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::sbi {

using core::MipBit;

namespace {

enum class ExtId : std::uint32_t {
    LegacySetTimer = 0x00,
    LegacyConsolePutchar = 0x01,
    LegacyConsoleGetchar = 0x02,
    LegacyShutdown = 0x08,
    Base = 0x10,
    Time = 0x54494D45,
    Rfence = 0x52464E43,
    Ipi = 0x735049,
    SystemReset = 0x53525354,
};

enum class BaseFid : std::uint8_t {
    GetSpecVersion = 0x0,
    GetImplId = 0x1,
    GetImplVersion = 0x2,
    ProbeExtension = 0x3,
    GetMvendorId = 0x4,
    GetMarchId = 0x5,
    GetMimpId = 0x6,
};

enum class TimeFid : std::uint8_t {
    SetTimer = 0x0,
};

enum class RfenceFid : std::uint8_t {
    RemoteFenceI = 0x0,
    RemoteSfenceVma = 0x1,
    RemoteSfenceVmaAsid = 0x2,
    RemoteHfenceGvmaVmid = 0x3,
    RemoteHfenceGvma = 0x4,
    RemoteHfenceVvmaAsid = 0x5,
};

enum class SbiError : std::int8_t {
    Success = 0,
    Failed = -1,
    NotSupported = -2,
    InvalidParam = -3,
    Denied = -4,
    InvalidAddress = -5,
    AlreadyAvailable = -6,
    AlreadyStarted = -7,
    AlreadyStopped = -8,
};

constexpr Word kSpecVersion_0_3 = 0x00000003U;
// Project-specific ID (ASCII "SIM"). ID 1 is reserved for OpenSBI and must not be reported by
// SimRV's direct execution environment. An official SBI implementation ID has not been assigned.
constexpr Word kImplId_SimRV = 0x53494DU;
constexpr Word kImplVersion = 0x1U;
constexpr unsigned kWord32Shift = 32u;
constexpr auto kLogHexWidth = static_cast<int>(kXLenHexDigits);

}  // namespace

Sbi::Sbi(core::CPU& cpu) : cpu_(cpu) {}

auto Sbi::timer_value() const -> Counter {
    const auto a0 = RegId::A0;
    const auto a1 = RegId::A1;
    if constexpr (xlen::kIsXLen64) {
        return static_cast<Counter>(cpu_.state().regs.read(a0));
    } else {
        return static_cast<Counter>(cpu_.state().regs.read(a0)) |
               (static_cast<Counter>(cpu_.state().regs.read(a1)) << kWord32Shift);
    }
}

void Sbi::sbi_return(SignedWord error, Word value) {
    const auto a0 = RegId::A0;
    const auto a1 = RegId::A1;
    cpu_.state().regs.write(a0, static_cast<Word>(error));
    cpu_.state().regs.write(a1, value);
    cpu_.state().pc += 4;
}

auto Sbi::handle_base(Word func_id) -> bool {
    switch (static_cast<BaseFid>(func_id)) {
        case BaseFid::GetSpecVersion:
            sbi_return(static_cast<SignedWord>(SbiError::Success), kSpecVersion_0_3);
            return true;
        case BaseFid::GetImplId:
            sbi_return(static_cast<SignedWord>(SbiError::Success), kImplId_SimRV);
            return true;
        case BaseFid::GetImplVersion:
            sbi_return(static_cast<SignedWord>(SbiError::Success), kImplVersion);
            return true;
        case BaseFid::ProbeExtension: {
            const Word probed = cpu_.state().regs.read(RegId::A0);
            const bool supported =
                (probed == enum_mask(ExtId::Base)) || (probed == enum_mask(ExtId::Time)) ||
                (probed == enum_mask(ExtId::Rfence)) || (probed == enum_mask(ExtId::Ipi)) ||
                (probed == enum_mask(ExtId::SystemReset));
            sbi_return(static_cast<SignedWord>(SbiError::Success), supported ? 1U : 0U);
            return true;
        }
        case BaseFid::GetMvendorId:
        case BaseFid::GetMarchId:
        case BaseFid::GetMimpId:
            sbi_return(static_cast<SignedWord>(SbiError::Success), 0);
            return true;
        default:
            sbi_return(static_cast<SignedWord>(SbiError::NotSupported), 0);
            return true;
    }
}

auto Sbi::handle_time(Word func_id) -> bool {
    if (static_cast<TimeFid>(func_id) == TimeFid::SetTimer) {
        cpu_.clint_mmio.supervisor_timer.store(true, std::memory_order_release);
        cpu_.clint_mmio.mtimecmp = timer_value();
        cpu_.state().mip &= ~enum_mask(MipBit::Mtip);
        cpu_.state().stip_timer = false;
        cpu_.state().refresh_supervisor_pending();
        cpu_.evaluate_timer_interrupt();
        sbi_return(static_cast<SignedWord>(SbiError::Success), 0);
    } else {
        sbi_return(static_cast<SignedWord>(SbiError::NotSupported), 0);
    }
    return true;
}

auto Sbi::handle_rfence(Word func_id) -> bool {
    const auto selection = detail::select_local_hart(
        cpu_.state().regs.read(RegId::A0), cpu_.state().regs.read(RegId::A1), cpu_.state().mhartid);
    if (selection == detail::HartMaskSelection::Invalid) {
        sbi_return(static_cast<SignedWord>(SbiError::InvalidParam), 0);
        return true;
    }

    switch (static_cast<RfenceFid>(func_id)) {
        case RfenceFid::RemoteFenceI:
            if (selection == detail::HartMaskSelection::Local) {
                cpu_.icache.flush();
                cpu_.dcache.flush();
                cpu_.decode_cache.flush();
            }
            sbi_return(static_cast<SignedWord>(SbiError::Success), 0);
            return true;
        case RfenceFid::RemoteSfenceVma:
        case RfenceFid::RemoteSfenceVmaAsid:
            if (selection == detail::HartMaskSelection::Local) {
                cpu_.TLB_flush();
                cpu_.dcache.flush();
            }
            sbi_return(static_cast<SignedWord>(SbiError::Success), 0);
            return true;
        default:
            sbi_return(static_cast<SignedWord>(SbiError::NotSupported), 0);
            return true;
    }
}

auto Sbi::handle_ipi(Word func_id) -> bool {
    if (func_id == 0) {  // send_ipi
        const Word hart_mask = cpu_.state().regs.read(RegId::A0);
        const Word hart_mask_base = cpu_.state().regs.read(RegId::A1);
        const auto selection =
            detail::select_local_hart(hart_mask, hart_mask_base, cpu_.state().mhartid);
        if (selection == detail::HartMaskSelection::Invalid) {
            sbi_return(static_cast<SignedWord>(SbiError::InvalidParam), 0);
            return true;
        }
        if (selection == detail::HartMaskSelection::Local) {
            cpu_.state().mip |= enum_mask(MipBit::Ssip);
        }
        sbi_return(static_cast<SignedWord>(SbiError::Success), 0);
    } else {
        sbi_return(static_cast<SignedWord>(SbiError::NotSupported), 0);
    }
    return true;
}

auto Sbi::handle_system_reset(Word func_id) -> bool {
    if (func_id == 0) {  // sbi_system_reset
        const Word reset_type = cpu_.state().regs.read(RegId::A0);
        const Word reset_reason = cpu_.state().regs.read(RegId::A1);

        if (reset_type == 0) {
            simrv::log::info("[SBI] System Reset: Shutdown requested (reason: 0x{:x}).",
                             reset_reason);
            if (cpu_.machine_ != nullptr) {
                cpu_.machine_->exit_code = static_cast<int>(reset_reason);
                cpu_.machine_->stop();
            }
            sbi_return(static_cast<SignedWord>(SbiError::Success), 0);
        } else if (reset_type == 1 || reset_type == 2) {
            simrv::log::info("[SBI] System Reset: Reboot requested (reason: 0x{:x}).",
                             reset_reason);
            if (cpu_.machine_ != nullptr) {
                cpu_.machine_->request_reboot();
            }
            sbi_return(static_cast<SignedWord>(SbiError::Success), 0);
        } else {
            sbi_return(static_cast<SignedWord>(SbiError::InvalidParam), 0);
        }
    } else {
        sbi_return(static_cast<SignedWord>(SbiError::NotSupported), 0);
    }
    return true;
}

auto Sbi::handle_ecall(TrapCause cause) -> bool {
    if (cpu_.use_opensbi) {
        return false;
    }

    if (!detail::is_direct_sbi_ecall(cause)) {
        return false;
    }

    const auto a6 = RegId::A6;
    const auto a7 = RegId::A7;
    const Word ext_id = cpu_.state().regs.read(a7);
    const Word func_id = cpu_.state().regs.read(a6);

    if (cpu_.trap_log_stream != nullptr && cpu_.trap_log_stream->is_open()) {
        *cpu_.trap_log_stream << std::format(
            "__ SBI ecall cause={} ext={:0{}x} fid={:0{}x} a0={:0{}x} a1={:0{}x} pc={:0{}x}\n",
            static_cast<unsigned>(cause), static_cast<uint64_t>(ext_id), kLogHexWidth,
            static_cast<uint64_t>(func_id), kLogHexWidth,
            static_cast<uint64_t>(cpu_.state().regs.read(RegId::A0)), kLogHexWidth,
            static_cast<uint64_t>(cpu_.state().regs.read(RegId::A1)), kLogHexWidth,
            static_cast<uint64_t>(cpu_.state().pc), kLogHexWidth);
        cpu_.trap_log_stream->flush();
    }

    switch (static_cast<ExtId>(ext_id)) {
        case ExtId::LegacySetTimer:
            return handle_time(0);
        case ExtId::LegacyConsolePutchar: {
            const auto ch = static_cast<char>(cpu_.state().regs.read(RegId::A0));
            if (cpu_.machine_ && cpu_.machine_->s_tuimode && cpu_.machine_->tui) {
                cpu_.machine_->tui->handle_char_write(ch);
            } else {
                (void)(::write(STDOUT_FILENO, &ch, 1) == 0);
            }
            sbi_return(0, 0);
            return true;
        }
        case ExtId::LegacyConsoleGetchar:
            sbi_return(static_cast<SignedWord>(-1), 0);
            return true;
        case ExtId::LegacyShutdown:
            simrv::log::info("[SBI] Legacy Shutdown requested (ext 0x08).");
            if (cpu_.machine_ != nullptr) {
                cpu_.machine_->exit_code = 0;
                cpu_.machine_->stop();
            }
            sbi_return(static_cast<SignedWord>(SbiError::Success), 0);
            return true;
        case ExtId::Base:
            return handle_base(func_id);
        case ExtId::Time:
            return handle_time(func_id);
        case ExtId::Rfence:
            return handle_rfence(func_id);
        case ExtId::Ipi:
            return handle_ipi(func_id);
        case ExtId::SystemReset:
            return handle_system_reset(func_id);
        default:
            sbi_return(static_cast<SignedWord>(SbiError::NotSupported), 0);
            return true;
    }
}

}  // namespace simrv::sbi
