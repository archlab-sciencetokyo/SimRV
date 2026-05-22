/**
 * @file Sbi.cpp
 * @brief SBI handling implementation.
 */
#include "simrv/core/Sbi.hpp"

#include <unistd.h>

#include <cstdint>
#include <format>
#include <ostream>

#include "simrv/Define.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::sbi {
namespace {

enum class LegacyId : Word {
    SetTimer = 0x00,
    ConsolePutchar = 0x01,
    ConsoleGetchar = 0x02,
    ClearIpi = 0x03,
    SendIpi = 0x04,
    RemoteFenceI = 0x05,
    RemoteSfenceVma = 0x06,
    RemoteSfenceVmaAsid = 0x07,
    Shutdown = 0x08,
};

enum class ExtId : Word {
    Base = 0x10,
    Time = 0x54494D45,
    Rfence = 0x52464E43,
    Hsm = 0x48534D,
};

enum class BaseFid : Word {
    GetSpecVersion = 0x0,
    GetImplId = 0x1,
    GetImplVersion = 0x2,
    ProbeExtension = 0x3,
    GetMvendorId = 0x4,
    GetMarchId = 0x5,
    GetMimpId = 0x6,
};

enum class TimeFid : Word {
    SetTimer = 0x0,
};

enum class RfenceFid : Word {
    RemoteFenceI = 0x0,
    RemoteSfenceVma = 0x1,
    RemoteSfenceVmaAsid = 0x2,
    RemoteHfenceGvmaVmid = 0x3,
    RemoteHfenceGvma = 0x4,
    RemoteHfenceVvmaAsid = 0x5,
};

enum class HsmFid : Word {
    HartStart = 0x0,
    HartStop = 0x1,
    HartStatus = 0x2,
    HartSuspend = 0x3,
};

enum class SbiError : SignedWord {
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
constexpr Word kImplId_SimRV = 0x1U;
constexpr Word kImplVersion = 0x1U;
constexpr unsigned kWord32Shift = 32u;
constexpr auto kLogHexWidth = static_cast<int>(kXLenHexDigits);

}  // namespace

Sbi::Sbi(core::CPU& cpu) : cpu_(cpu) {}

auto Sbi::timer_value() const -> Counter {
    const auto a0 = static_cast<RegId>(10);
    const auto a1 = static_cast<RegId>(11);
    if constexpr (xlen::kIsXLen64) {
        return static_cast<Counter>(cpu_.state().regs.read(a0));
    } else {
        return static_cast<Counter>(cpu_.state().regs.read(a0)) |
               (static_cast<Counter>(cpu_.state().regs.read(a1)) << kWord32Shift);
    }
}

void Sbi::sbi_return(SignedWord error, Word value) {
    const auto a0 = static_cast<RegId>(10);
    const auto a1 = static_cast<RegId>(11);
    cpu_.state().regs.write(a0, static_cast<Word>(error));
    cpu_.state().regs.write(a1, value);
    cpu_.state().pc += 4;
}

auto Sbi::handle_legacy(Word ext_id, Word func_id) -> bool {
    switch (static_cast<LegacyId>(ext_id)) {
        case LegacyId::SetTimer: {
            cpu_.clint_mmio.mtimecmp = timer_value();
            cpu_.state().mip &= ~enum_mask(MipBit::Mtip);
            sbi_return(static_cast<SignedWord>(SbiError::Success), 0);
            return true;
        }
        case LegacyId::ConsolePutchar: {
            const auto ch = static_cast<uint8_t>(cpu_.state().regs.read(static_cast<RegId>(10)) & 0xffU);
            (void)(::write(STDOUT_FILENO, &ch, 1) == 0);
            sbi_return(static_cast<SignedWord>(SbiError::Success), 0);
            return true;
        }
        case LegacyId::ConsoleGetchar: {
            sbi_return(static_cast<SignedWord>(-1), 0);
            return true;
        }
        case LegacyId::ClearIpi:
        case LegacyId::SendIpi:
        case LegacyId::RemoteFenceI:
        case LegacyId::RemoteSfenceVma:
        case LegacyId::RemoteSfenceVmaAsid:
        case LegacyId::Shutdown:
            sbi_return(static_cast<SignedWord>(SbiError::Success), 0);
            return true;
    }
    return false;
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
            const Word probed = cpu_.state().regs.read(static_cast<RegId>(10));
            const bool supported =
                (probed == enum_mask(ExtId::Base)) || (probed == enum_mask(ExtId::Time)) ||
                (probed == enum_mask(ExtId::Rfence)) || (probed == enum_mask(ExtId::Hsm));
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
        cpu_.clint_mmio.mtimecmp = timer_value();
        cpu_.state().mip &= ~enum_mask(MipBit::Mtip);
        sbi_return(static_cast<SignedWord>(SbiError::Success), 0);
    } else {
        sbi_return(static_cast<SignedWord>(SbiError::NotSupported), 0);
    }
    return true;
}

auto Sbi::handle_rfence(Word func_id) -> bool {
    if (func_id <= enum_mask(RfenceFid::RemoteHfenceVvmaAsid)) {
        sbi_return(static_cast<SignedWord>(SbiError::Success), 0);
    } else {
        sbi_return(static_cast<SignedWord>(SbiError::NotSupported), 0);
    }
    return true;
}

auto Sbi::handle_hsm(Word func_id) -> bool {
    switch (static_cast<HsmFid>(func_id)) {
        case HsmFid::HartStart: {
            const Word hartid = cpu_.state().regs.read(static_cast<RegId>(10));
            sbi_return(
                static_cast<SignedWord>(hartid == cpu_.state().mhartid ? SbiError::Success
                                                                       : SbiError::NotSupported),
                0);
            return true;
        }
        case HsmFid::HartStop:
            sbi_return(static_cast<SignedWord>(SbiError::NotSupported), 0);
            return true;
        case HsmFid::HartStatus: {
            const Word hartid = cpu_.state().regs.read(static_cast<RegId>(10));
            sbi_return(static_cast<SignedWord>(SbiError::Success),
                       hartid == cpu_.state().mhartid ? 0U : 1U);
            return true;
        }
        case HsmFid::HartSuspend:
            sbi_return(static_cast<SignedWord>(SbiError::Success), 0);
            return true;
        default:
            sbi_return(static_cast<SignedWord>(SbiError::NotSupported), 0);
            return true;
    }
}

auto Sbi::handle_ecall(TrapCause cause) -> bool {
    if (cause != enum_mask(ExceptionCode::SupervisorEcall) &&
        cause != enum_mask(ExceptionCode::MachineEcall)) {
        return false;
    }

    const auto a6 = static_cast<RegId>(16);
    const auto a7 = static_cast<RegId>(17);
    const Word ext_id = cpu_.state().regs.read(a7);
    const Word func_id = cpu_.state().regs.read(a6);

    if (cpu_.trap_log_stream != nullptr && cpu_.trap_log_stream->is_open()) {
        *cpu_.trap_log_stream << std::format(
            "__ SBI ecall cause={} ext={:0{}x} fid={:0{}x} a0={:0{}x} a1={:0{}x} pc={:0{}x}\n",
            static_cast<unsigned>(cause), static_cast<uint64_t>(ext_id), kLogHexWidth,
            static_cast<uint64_t>(func_id), kLogHexWidth,
            static_cast<uint64_t>(cpu_.state().regs.read(static_cast<RegId>(10))), kLogHexWidth,
            static_cast<uint64_t>(cpu_.state().regs.read(static_cast<RegId>(11))), kLogHexWidth,
            static_cast<uint64_t>(cpu_.state().pc), kLogHexWidth);
        cpu_.trap_log_stream->flush();
    }

    if (ext_id <= enum_mask(LegacyId::Shutdown)) {
        return handle_legacy(ext_id, func_id);
    }

    switch (static_cast<ExtId>(ext_id)) {
        case ExtId::Base:
            return handle_base(func_id);
        case ExtId::Time:
            return handle_time(func_id);
        case ExtId::Rfence:
            return handle_rfence(func_id);
        case ExtId::Hsm:
            return handle_hsm(func_id);
        default:
            sbi_return(static_cast<SignedWord>(SbiError::NotSupported), 0);
            return true;
    }
}

}  // namespace simrv::sbi