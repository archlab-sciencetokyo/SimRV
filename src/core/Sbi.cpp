/**
 * @file Sbi.cpp
 * @brief SBI handling implementation.
 */
#include "simrv/core/Sbi.hpp"

#include <unistd.h>

#include <cstdint>
#include <format>

#include "simrv/CompilerHints.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/SbiTypes.hpp"
#include "simrv/core/Tracer.hpp"
#include "simrv/device/Uart.hpp"
#include "simrv/tui/Tui.hpp"
#include "simrv/xlen/RegisterTypes.hpp"

namespace simrv::sbi {

using core::HartStatus;
using core::MipBit;

namespace {

using ExtId = SbiExtId;

enum class BaseFid : std::uint8_t {
    GetSpecVersion = 0x0,
    GetImplId = 0x1,
    GetImplVersion = 0x2,
    ProbeExtension = 0x3,
    GetMvendorId = 0x4,
    GetMarchId = 0x5,
    GetMimpId = 0x6,
};

enum class HsmFid : std::uint8_t {
    HartStart = 0x0,
    HartStop = 0x1,
    HartGetStatus = 0x2,
    HartSuspend = 0x3,
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
    RemoteHfenceVvma = 0x6,
};

constexpr simrv::xlen::Word kSpecVersion_2_0 = 0x02000000U;
// Project-specific ID (ASCII "SIM"). ID 1 is reserved for OpenSBI and must not be reported by
// SimRV's direct execution environment. An official SBI implementation ID has not been assigned.
constexpr simrv::xlen::Word kImplId_SimRV = 0x53494DU;
constexpr simrv::xlen::Word kImplVersion = 0x1U;
constexpr unsigned kWord32Shift = 32u;

}  // namespace

Sbi::Sbi(core::CPU& cpu) : cpu_(cpu) {}

auto Sbi::timer_value() const -> simrv::xlen::Counter {
    const auto a0 = simrv::xlen::RegId::A0;
    const auto a1 = simrv::xlen::RegId::A1;
    if constexpr (xlen::kIsXLen64) {
        return static_cast<simrv::xlen::Counter>(cpu_.state().regs.read(a0));
    } else {
        return static_cast<simrv::xlen::Counter>(cpu_.state().regs.read(a0)) |
               (static_cast<simrv::xlen::Counter>(cpu_.state().regs.read(a1)) << kWord32Shift);
    }
}

void Sbi::sbi_return(simrv::xlen::SignedWord error, simrv::xlen::Word value) {
    const auto a0 = simrv::xlen::RegId::A0;
    const auto a1 = simrv::xlen::RegId::A1;
    cpu_.state().regs.write(a0, static_cast<simrv::xlen::Word>(error));
    cpu_.state().regs.write(a1, value);
    cpu_.state().pc += 4;
}

auto Sbi::handle_base(simrv::xlen::Word func_id) -> bool {
    switch (static_cast<BaseFid>(func_id)) {
        case BaseFid::GetSpecVersion:
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), kSpecVersion_2_0);
            return true;
        case BaseFid::GetImplId:
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), kImplId_SimRV);
            return true;
        case BaseFid::GetImplVersion:
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), kImplVersion);
            return true;
        case BaseFid::ProbeExtension: {
            const simrv::xlen::Word probed = cpu_.state().regs.read(simrv::xlen::RegId::A0);
            const bool supported = (probed == ExtId::Base) || (probed == ExtId::Time) ||
                                   (probed == ExtId::Rfence) || (probed == ExtId::Ipi) ||
                                   (probed == ExtId::Hsm) || (probed == ExtId::SystemReset) ||
                                   (probed == ExtId::Pmu) || (probed == ExtId::DebugConsole) ||
                                   (probed == ExtId::Susp) || (probed == ExtId::Cppc);
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success),
                       supported ? 1U : 0U);
            return true;
        }
        case BaseFid::GetMvendorId:
        case BaseFid::GetMarchId:
        case BaseFid::GetMimpId:
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), 0);
            return true;
        default:
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::NotSupported), 0);
            return true;
    }
}

auto Sbi::handle_time(simrv::xlen::Word func_id) -> bool {
    if (static_cast<TimeFid>(func_id) == TimeFid::SetTimer) {
        cpu_.clint_mmio.supervisor_timer.store(true, std::memory_order_release);
        cpu_.clint_mmio.mtimecmp = timer_value();
        cpu_.state().mip &= ~MipBit::Mtip;
        cpu_.state().stip_timer = false;
        cpu_.state().refresh_supervisor_pending();
        cpu_.evaluate_timer_interrupt();
        sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), 0);
    } else {
        sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::NotSupported), 0);
    }
    return true;
}

auto Sbi::handle_rfence(simrv::xlen::Word func_id) -> bool {
    const simrv::xlen::Word hart_mask = cpu_.state().regs.read(simrv::xlen::RegId::A0);
    const simrv::xlen::Word hart_mask_base = cpu_.state().regs.read(simrv::xlen::RegId::A1);

    const auto fid = static_cast<RfenceFid>(func_id);
    if (fid != RfenceFid::RemoteFenceI && fid != RfenceFid::RemoteSfenceVma &&
        fid != RfenceFid::RemoteSfenceVmaAsid && fid != RfenceFid::RemoteHfenceGvmaVmid &&
        fid != RfenceFid::RemoteHfenceGvma && fid != RfenceFid::RemoteHfenceVvmaAsid &&
        fid != RfenceFid::RemoteHfenceVvma) {
        sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::NotSupported), 0);
        return true;
    }

    if (cpu_.machine_ != nullptr) {
        const size_t n_harts = cpu_.machine_->num_harts();
        for (size_t h = 0; h < n_harts; ++h) {
            if (detail::is_hart_selected(hart_mask, hart_mask_base, h)) {
                auto& target_cpu = cpu_.machine_->hart(h);
                if (fid == RfenceFid::RemoteFenceI) {
                    target_cpu.icache.flush();
                    target_cpu.decode_cache.flush();
                } else if (fid == RfenceFid::RemoteSfenceVma ||
                           fid == RfenceFid::RemoteSfenceVmaAsid) {
                    target_cpu.TLB_flush();
                }
            }
        }
    } else {
        if (detail::is_hart_selected(hart_mask, hart_mask_base, cpu_.state().mhartid)) {
            if (fid == RfenceFid::RemoteFenceI) {
                cpu_.icache.flush();
                cpu_.decode_cache.flush();
            } else if (fid == RfenceFid::RemoteSfenceVma || fid == RfenceFid::RemoteSfenceVmaAsid) {
                cpu_.TLB_flush();
            }
        }
    }
    sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), 0);
    return true;
}

auto Sbi::handle_ipi(simrv::xlen::Word func_id) -> bool {
    if (func_id == 0) {  // send_ipi
        const simrv::xlen::Word hart_mask = cpu_.state().regs.read(simrv::xlen::RegId::A0);
        const simrv::xlen::Word hart_mask_base = cpu_.state().regs.read(simrv::xlen::RegId::A1);
        if (cpu_.machine_ != nullptr) {
            const size_t n_harts = cpu_.machine_->num_harts();
            for (size_t h = 0; h < n_harts; ++h) {
                if (detail::is_hart_selected(hart_mask, hart_mask_base, h)) {
                    auto& target_cpu = cpu_.machine_->hart(h);
                    target_cpu.state().mip |= MipBit::Ssip;
                    target_cpu.hart_status.notify_all();
                }
            }
        } else {
            if (detail::is_hart_selected(hart_mask, hart_mask_base, cpu_.state().mhartid)) {
                cpu_.state().mip |= MipBit::Ssip;
                cpu_.hart_status.notify_all();
            }
        }
        sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), 0);
    } else {
        sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::NotSupported), 0);
    }
    return true;
}

auto Sbi::handle_hsm(simrv::xlen::Word func_id) -> bool {
    if (cpu_.machine_ == nullptr) {
        sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Failed), 0);
        return true;
    }

    const simrv::xlen::Word target_hart = cpu_.state().regs.read(simrv::xlen::RegId::A0);

    switch (static_cast<HsmFid>(func_id)) {
        case HsmFid::HartStart: {
            const simrv::xlen::Word start_addr = cpu_.state().regs.read(simrv::xlen::RegId::A1);
            const simrv::xlen::Word opaque = cpu_.state().regs.read(simrv::xlen::RegId::A2);

            if (target_hart >= cpu_.machine_->num_harts()) {
                sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::InvalidParam), 0);
                return true;
            }
            auto& target_cpu = cpu_.machine_->hart(target_hart);
            auto cur_status = target_cpu.hart_status.load(std::memory_order_relaxed);
            if (cur_status == HartStatus::Started) {
                sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::AlreadyStarted), 0);
                return true;
            }
            target_cpu.state().pc = start_addr;
            if (target_cpu.state().regs.xlen == 32) {
                target_cpu.state().pc = static_cast<simrv::xlen::Register>(
                    static_cast<int64_t>(static_cast<int32_t>(start_addr)));
            }
            target_cpu.state().priv = simrv::xlen::PrivilegeLevel::Supervisor;
            target_cpu.state().regs.write(simrv::xlen::RegId::A0, target_hart);
            target_cpu.state().regs.write(simrv::xlen::RegId::A1, opaque);
            target_cpu.hart_status.store(HartStatus::Started, std::memory_order_release);
            target_cpu.hart_status.notify_all();
            cpu_.machine_->notify_control_event();
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), 0);
            return true;
        }
        case HsmFid::HartStop: {
            cpu_.hart_status.store(HartStatus::StopPending, std::memory_order_release);
            cpu_.machine_->notify_control_event();
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), 0);
            return true;
        }
        case HsmFid::HartGetStatus: {
            if (target_hart >= cpu_.machine_->num_harts()) {
                sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::InvalidParam), 0);
                return true;
            }
            auto& target_cpu = cpu_.machine_->hart(target_hart);
            auto status = target_cpu.hart_status.load(std::memory_order_relaxed);
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success),
                       static_cast<simrv::xlen::Word>(std::to_underlying(status)));
            return true;
        }
        case HsmFid::HartSuspend: {
            cpu_.hart_status.store(HartStatus::SuspendPending, std::memory_order_release);
            cpu_.machine_->notify_control_event();
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), 0);
            return true;
        }
        default:
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::NotSupported), 0);
            return true;
    }
}

auto Sbi::handle_system_reset(simrv::xlen::Word func_id) -> bool {
    if (func_id == 0) {  // sbi_system_reset
        const simrv::xlen::Word reset_type = cpu_.state().regs.read(simrv::xlen::RegId::A0);
        const simrv::xlen::Word reset_reason = cpu_.state().regs.read(simrv::xlen::RegId::A1);

        if (reset_type == 0) {
            simrv::log::info("[SBI] System Reset: Shutdown requested (reason: 0x{:x}).",
                             reset_reason);
            if (cpu_.machine_ != nullptr) {
                cpu_.machine_->exit_code = static_cast<int>(reset_reason);
                cpu_.machine_->stop(core::Machine::StopReason::GuestPoweroff);
            }
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), 0);
        } else if (reset_type == 1 || reset_type == 2) {
            simrv::log::info("[SBI] System Reset: Reboot requested (reason: 0x{:x}).",
                             reset_reason);
            if (cpu_.machine_ != nullptr) {
                cpu_.machine_->request_reboot();
            }
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), 0);
        } else {
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::InvalidParam), 0);
        }
    } else {
        sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::NotSupported), 0);
    }
    return true;
}

auto Sbi::handle_dbcn(simrv::xlen::Word func_id) -> bool {
    switch (func_id) {
        case 0: {  // sbi_debug_console_write
            const auto num_bytes =
                static_cast<size_t>(cpu_.state().regs.read(simrv::xlen::RegId::A0));
            const auto base_addr_lo = cpu_.state().regs.read(simrv::xlen::RegId::A1);
            const auto base_addr_hi = cpu_.state().regs.read(simrv::xlen::RegId::A2);
            const auto paddr = static_cast<simrv::xlen::Address>(
                (static_cast<uint64_t>(base_addr_hi) << 32) | static_cast<uint64_t>(base_addr_lo));

            size_t written = 0;
            for (size_t i = 0; i < num_bytes; ++i) {
                const simrv::xlen::Address byte_addr = paddr + i;
                if (cpu_.machine_ != nullptr &&
                    cpu_.machine_->memory_geometry().contains(byte_addr)) {
                    const auto geometry = cpu_.machine_->memory_geometry();
                    const auto ch = static_cast<char>(
                        cpu_.machine_->ram_data()[byte_addr - geometry.dram_base]);
                    cpu_.machine_->console_write(ch);
                    written++;
                }
            }
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success),
                       static_cast<simrv::xlen::Word>(written));
            return true;
        }
        case 1: {  // sbi_debug_console_read
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), 0);
            return true;
        }
        case 2: {  // sbi_debug_console_write_byte
            const auto ch = static_cast<char>(cpu_.state().regs.read(simrv::xlen::RegId::A0));
            if (cpu_.machine_) {
                cpu_.machine_->console_write(ch);
            } else {
                (void)(::write(STDOUT_FILENO, &ch, 1) == 0);
            }
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), 0);
            return true;
        }
        default:
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::NotSupported), 0);
            return true;
    }
}

auto Sbi::handle_pmu(simrv::xlen::Word func_id) -> bool {
    switch (func_id) {
        case 0:  // sbi_pmu_num_counters
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), 32);
            return true;
        case 1: {  // sbi_pmu_counter_get_info
            const simrv::xlen::Word counter_idx = cpu_.state().regs.read(simrv::xlen::RegId::A0);
            if (counter_idx >= 32) {
                sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::InvalidParam), 0);
                return true;
            }
            // Bit 0..11: CSR index (0xC00 + counter_idx)
            // Bit 12..17: Width - 1 (63 for 64-bit counter)
            // Bit 18..31: Type (0 = hardware)
            const simrv::xlen::Word info = (0xC00U + counter_idx) | (63U << 12U);
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), info);
            return true;
        }
        case 2: {  // sbi_pmu_counter_config_matching
            const simrv::xlen::Word counter_idx_base =
                cpu_.state().regs.read(simrv::xlen::RegId::A0);
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), counter_idx_base);
            return true;
        }
        case 3:  // sbi_pmu_counter_start
        case 4:  // sbi_pmu_counter_stop
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), 0);
            return true;
        case 5: {  // sbi_pmu_counter_fw_read
            const simrv::xlen::Word counter_idx = cpu_.state().regs.read(simrv::xlen::RegId::A0);
            if (counter_idx == 0) {
                sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success),
                           static_cast<simrv::xlen::Word>(cpu_.clint_mmio.mcycle & 0xFFFFFFFFU));
            } else if (counter_idx == 1) {
                sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success),
                           static_cast<simrv::xlen::Word>(cpu_.clint_mmio.mtime & 0xFFFFFFFFU));
            } else if (counter_idx == 2) {
                sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success),
                           static_cast<simrv::xlen::Word>(cpu_.e_icount & 0xFFFFFFFFU));
            } else {
                sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), 0);
            }
            return true;
        }
        case 6: {  // sbi_pmu_counter_fw_read_hi
            const simrv::xlen::Word counter_idx = cpu_.state().regs.read(simrv::xlen::RegId::A0);
            if (counter_idx == 0) {
                sbi_return(
                    static_cast<simrv::xlen::SignedWord>(SbiError::Success),
                    static_cast<simrv::xlen::Word>((cpu_.clint_mmio.mcycle >> 32) & 0xFFFFFFFFU));
            } else if (counter_idx == 1) {
                sbi_return(
                    static_cast<simrv::xlen::SignedWord>(SbiError::Success),
                    static_cast<simrv::xlen::Word>((cpu_.clint_mmio.mtime >> 32) & 0xFFFFFFFFU));
            } else if (counter_idx == 2) {
                sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success),
                           static_cast<simrv::xlen::Word>((cpu_.e_icount >> 32) & 0xFFFFFFFFU));
            } else {
                sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), 0);
            }
            return true;
        }
        case 7:  // sbi_pmu_snapshot_set_shmem
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), 0);
            return true;
        default:
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::NotSupported), 0);
            return true;
    }
}

auto Sbi::handle_cppc(simrv::xlen::Word func_id) -> bool {
    switch (func_id) {
        case 0: {  // sbi_cppc_probe
            const simrv::xlen::Word reg_id = cpu_.state().regs.read(simrv::xlen::RegId::A0);
            if (reg_id <= 0x12) {
                sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success),
                           32);  // 32-bit register width
            } else {
                sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::NotSupported), 0);
            }
            return true;
        }
        case 1: {  // sbi_cppc_read
            const simrv::xlen::Word reg_id = cpu_.state().regs.read(simrv::xlen::RegId::A0);
            simrv::xlen::Word val = 0;
            switch (reg_id) {
                case 0x00:  // HIGHEST_PERF
                    val = 1000;
                    break;
                case 0x01:  // NOMINAL_PERF
                    val = 100;
                    break;
                case 0x02:  // LOW_NON_LINEAR_PERF
                    val = 50;
                    break;
                case 0x03:  // LOWEST_PERF
                    val = 10;
                    break;
                case 0x04:  // GUARANTEED_PERF
                case 0x12:  // REFERENCE_PERF
                    val = 100;
                    break;
                default:
                    val = 0;
                    break;
            }
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), val);
            return true;
        }
        case 2:  // sbi_cppc_write
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), 0);
            return true;
        default:
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::NotSupported), 0);
            return true;
    }
}

auto Sbi::handle_susp(simrv::xlen::Word func_id) -> bool {
    if (func_id == 0) {  // sbi_system_suspend
        sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), 0);
        return true;
    }
    sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::NotSupported), 0);
    return true;
}

auto Sbi::handle_ecall(simrv::xlen::TrapCause cause) -> bool {
    if (cpu_.use_opensbi) {
        return false;
    }

    if (!detail::is_direct_sbi_ecall(cause)) {
        return false;
    }

    const auto a6 = simrv::xlen::RegId::A6;
    const auto a7 = simrv::xlen::RegId::A7;
    const simrv::xlen::Word ext_id = cpu_.state().regs.read(a7);
    const simrv::xlen::Word func_id = cpu_.state().regs.read(a6);

    if (cpu_.machine_ != nullptr &&
        simrv::compiler::unlikely(cpu_.machine_->trace().is_trap_log_enabled() ||
                                  cpu_.machine_->trace().is_architecture_trace_enabled())) {
        cpu_.machine_->trace().log_sbi(cpu_.clint_mmio.mtime.load(std::memory_order_relaxed),
                                       cpu_.clint_mmio.mcycle, static_cast<unsigned>(cause), ext_id,
                                       func_id, cpu_.state().regs.read(simrv::xlen::RegId::A0),
                                       cpu_.state().regs.read(simrv::xlen::RegId::A1),
                                       cpu_.pipeline_context.cpc.raw(), cpu_.hart_id());
    }

    switch (static_cast<ExtId>(ext_id)) {
        // Retained SBI legacy extension IDs for guest firmware that still uses the v0.1 calls.
        case ExtId::LegacySetTimer:
            return handle_time(0);
        case ExtId::LegacyConsolePutchar: {
            const auto ch = static_cast<char>(cpu_.state().regs.read(simrv::xlen::RegId::A0));
            if (cpu_.machine_) {
                cpu_.machine_->console_write(ch);
            } else {
                (void)(::write(STDOUT_FILENO, &ch, 1) == 0);
            }
            sbi_return(0, 0);
            return true;
        }
        case ExtId::LegacyConsoleGetchar:
            sbi_return(static_cast<simrv::xlen::SignedWord>(-1), 0);
            return true;
        case ExtId::LegacyShutdown:
            simrv::log::info("[SBI] Legacy Shutdown requested (ext 0x08).");
            if (cpu_.machine_ != nullptr) {
                cpu_.machine_->exit_code = 0;
                cpu_.machine_->stop(core::Machine::StopReason::GuestPoweroff);
            }
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::Success), 0);
            return true;
        case ExtId::Base:
            return handle_base(func_id);
        case ExtId::Time:
            return handle_time(func_id);
        case ExtId::Rfence:
            return handle_rfence(func_id);
        case ExtId::Ipi:
            return handle_ipi(func_id);
        case ExtId::Hsm:
            return handle_hsm(func_id);
        case ExtId::SystemReset:
            return handle_system_reset(func_id);
        case ExtId::DebugConsole:
            return handle_dbcn(func_id);
        case ExtId::Pmu:
            return handle_pmu(func_id);
        case ExtId::Cppc:
            return handle_cppc(func_id);
        case ExtId::Susp:
            return handle_susp(func_id);
        default:
            sbi_return(static_cast<simrv::xlen::SignedWord>(SbiError::NotSupported), 0);
            return true;
    }
}

}  // namespace simrv::sbi
