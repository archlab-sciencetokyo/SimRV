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

auto sbi_timer_value(const core::CPU& cpu) -> Counter {
    if constexpr (xlen::kIsXLen64) {
        return static_cast<Counter>(cpu.state().regs.read(10));
    } else {
        return static_cast<Counter>(cpu.state().regs.read(10)) |
               (static_cast<Counter>(cpu.state().regs.read(11)) << kWord32Shift);
    }
}

void sbi_return(core::CPU& cpu, SbiError error, Word value) {
    cpu.state().regs.write(10, static_cast<Word>(error));
    cpu.state().regs.write(11, value);
    cpu.state().pc += 4;
}

}  // namespace

auto handle_sbi_ecall(core::CPU& cpu, TrapCause cause) -> bool {
    if (cause != enum_mask(ExceptionCode::SupervisorEcall) &&
        cause != enum_mask(ExceptionCode::MachineEcall)) {
        return false;
    }

    const Word ext_id = cpu.state().regs.read(17);
    const Word func_id = cpu.state().regs.read(16);

    if (cpu.trap_log_stream != nullptr && cpu.trap_log_stream->is_open()) {
        *cpu.trap_log_stream << std::format(
            "__ SBI ecall cause={} ext={:0{}x} fid={:0{}x} a0={:0{}x} a1={:0{}x} pc={:0{}x}\n",
            static_cast<unsigned>(cause), static_cast<uint64_t>(ext_id), kLogHexWidth,
            static_cast<uint64_t>(func_id), kLogHexWidth,
            static_cast<uint64_t>(cpu.state().regs.read(10)), kLogHexWidth,
            static_cast<uint64_t>(cpu.state().regs.read(11)), kLogHexWidth,
            static_cast<uint64_t>(cpu.state().pc), kLogHexWidth);
        cpu.trap_log_stream->flush();
    }

    if (ext_id <= enum_mask(LegacyId::Shutdown)) {
        switch (static_cast<LegacyId>(ext_id)) {
            case LegacyId::SetTimer: {
                cpu.clint_mmio.mtimecmp = sbi_timer_value(cpu);
                cpu.state().mip &= ~enum_mask(MipBit::Mtip);
                sbi_return(cpu, SbiError::Success, 0);
                return true;
            }
            case LegacyId::ConsolePutchar: {
                const auto ch = static_cast<uint8_t>(cpu.state().regs.read(10) & 0xffU);
                (void)(::write(STDOUT_FILENO, &ch, 1) == 0);
                sbi_return(cpu, SbiError::Success, 0);
                return true;
            }
            case LegacyId::ConsoleGetchar: {
                sbi_return(cpu, static_cast<SbiError>(static_cast<Word>(-1)), 0);
                return true;
            }
            case LegacyId::ClearIpi:
            case LegacyId::SendIpi:
            case LegacyId::RemoteFenceI:
            case LegacyId::RemoteSfenceVma:
            case LegacyId::RemoteSfenceVmaAsid:
            case LegacyId::Shutdown:
                sbi_return(cpu, SbiError::Success, 0);
                return true;
        }
    }

    switch (static_cast<ExtId>(ext_id)) {
        case ExtId::Base:
            switch (static_cast<BaseFid>(func_id)) {
                case BaseFid::GetSpecVersion:
                    sbi_return(cpu, SbiError::Success, kSpecVersion_0_3);
                    return true;
                case BaseFid::GetImplId:
                    sbi_return(cpu, SbiError::Success, kImplId_SimRV);
                    return true;
                case BaseFid::GetImplVersion:
                    sbi_return(cpu, SbiError::Success, kImplVersion);
                    return true;
                case BaseFid::ProbeExtension: {
                    const Word probed = cpu.state().regs.read(10);
                    const bool supported =
                        (probed == enum_mask(ExtId::Base)) || (probed == enum_mask(ExtId::Time)) ||
                        (probed == enum_mask(ExtId::Rfence)) || (probed == enum_mask(ExtId::Hsm));
                    sbi_return(cpu, SbiError::Success, supported ? 1U : 0U);
                    return true;
                }
                case BaseFid::GetMvendorId:
                case BaseFid::GetMarchId:
                case BaseFid::GetMimpId:
                    sbi_return(cpu, SbiError::Success, 0);
                    return true;
                default:
                    sbi_return(cpu, SbiError::NotSupported, 0);
                    return true;
            }
        case ExtId::Time:
            if (static_cast<TimeFid>(func_id) == TimeFid::SetTimer) {
                cpu.clint_mmio.mtimecmp = sbi_timer_value(cpu);
                cpu.state().mip &= ~enum_mask(MipBit::Mtip);
                sbi_return(cpu, SbiError::Success, 0);
            } else {
                sbi_return(cpu, SbiError::NotSupported, 0);
            }
            return true;
        case ExtId::Rfence:
            if (func_id <= enum_mask(RfenceFid::RemoteHfenceVvmaAsid)) {
                sbi_return(cpu, SbiError::Success, 0);
            } else {
                sbi_return(cpu, SbiError::NotSupported, 0);
            }
            return true;
        case ExtId::Hsm:
            switch (static_cast<HsmFid>(func_id)) {
                case HsmFid::HartStart: {
                    const Word hartid = cpu.state().regs.read(10);
                    sbi_return(
                        cpu,
                        hartid == cpu.state().mhartid ? SbiError::Success : SbiError::NotSupported,
                        0);
                    return true;
                }
                case HsmFid::HartStop:
                    sbi_return(cpu, SbiError::NotSupported, 0);
                    return true;
                case HsmFid::HartStatus: {
                    const Word hartid = cpu.state().regs.read(10);
                    sbi_return(cpu, SbiError::Success, hartid == cpu.state().mhartid ? 0U : 1U);
                    return true;
                }
                case HsmFid::HartSuspend:
                    sbi_return(cpu, SbiError::Success, 0);
                    return true;
                default:
                    sbi_return(cpu, SbiError::NotSupported, 0);
                    return true;
            }
        default:
            sbi_return(cpu, SbiError::NotSupported, 0);
            return true;
    }
}

}  // namespace simrv::sbi