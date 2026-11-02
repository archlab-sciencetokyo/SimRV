/**
 * @file CsrFile.cpp
 * @brief Control and Status Register (CSR) file implementation.
 */
#include "simrv/core/CsrFile.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <string_view>
#include <utility>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/core/Tracer.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#endif

namespace simrv::core {

namespace {
constexpr unsigned kHighWordShift = 32;
constexpr simrv::xlen::CsrValue kCounterEnableMask =
    static_cast<simrv::xlen::CsrValue>(0xFFFFFFFFU);
// This RV64 hart model supports the architectural maximum of 63 guest external
// interrupt files. Bit zero is reserved; on RV32 the CSR width naturally caps
// the implemented guest interrupt IDs at 31.
constexpr simrv::xlen::CsrValue kHgeieWritableMask =
    std::numeric_limits<simrv::xlen::CsrValue>::max() & ~simrv::xlen::CsrValue{1};
constexpr simrv::xlen::CsrValue kHstatusVgeinMask = simrv::xlen::CsrValue{0x3F} << 12U;
constexpr simrv::xlen::CsrValue kVsSoftwareInterrupt = simrv::xlen::CsrValue{1} << 1U;
constexpr simrv::xlen::CsrValue kHenvcfgWritableMask =
    0xF1U | static_cast<simrv::xlen::CsrValue>(uint64_t{0x3} << 32U) |
    static_cast<simrv::xlen::CsrValue>(uint64_t{1} << 63U);
constexpr simrv::xlen::CsrValue kSenvcfgWritableMask =
    0xF1U | static_cast<simrv::xlen::CsrValue>(uint64_t{0x3} << 32U);
constexpr simrv::xlen::CsrValue kVsInterruptBits = (simrv::xlen::CsrValue{1} << 2U) |
                                                   (simrv::xlen::CsrValue{1} << 6U) |
                                                   (simrv::xlen::CsrValue{1} << 10U);
constexpr simrv::xlen::CsrValue kSupervisorInterruptBits = (simrv::xlen::CsrValue{1} << 1U) |
                                                           (simrv::xlen::CsrValue{1} << 5U) |
                                                           (simrv::xlen::CsrValue{1} << 9U);
constexpr simrv::xlen::CsrValue kSupervisorInterruptEnableMask =
    kSupervisorInterruptBits | (simrv::xlen::CsrValue{1} << 13U);

[[nodiscard]] constexpr auto fp_accessible(const ArchState& state) noexcept -> bool {
    return isa::misa_has_extension(state.misa, isa::IsaExtension::F) &&
           state.extension_state_enabled(MstatusBit::Fs);
}

[[nodiscard]] constexpr auto vector_accessible(const ArchState& state) noexcept -> bool {
    return isa::misa_has_extension(state.misa, isa::IsaExtension::V) &&
           state.extension_state_enabled(MstatusBit::Vs);
}

[[nodiscard]] constexpr auto pack_pmpcfg(
    const std::array<uint8_t, ArchState::kNumPmpEntries>& pmpcfg, size_t cfg_idx,
    unsigned xlen) noexcept -> simrv::xlen::CsrValue {
    if (xlen == 32) {
        if (cfg_idx >= 4) return 0;
        simrv::xlen::CsrValue val = 0;
        for (size_t b = 0; b < 4; ++b) {
            const size_t entry = cfg_idx * 4 + b;
            if (entry < ArchState::kNumPmpEntries) {
                val |= static_cast<simrv::xlen::CsrValue>(pmpcfg[entry]) << (8 * b);
            }
        }
        return val;
    }
    if (cfg_idx >= 4 || (cfg_idx & 1) != 0) return 0;
    const size_t base_pmp = (cfg_idx / 2) * 8;
    simrv::xlen::CsrValue val = 0;
    for (size_t b = 0; b < 8; ++b) {
        const size_t entry = base_pmp + b;
        if (entry < ArchState::kNumPmpEntries) {
            val |= static_cast<simrv::xlen::CsrValue>(pmpcfg[entry]) << (8 * b);
        }
    }
    return val;
}

void unpack_pmpcfg(std::array<uint8_t, ArchState::kNumPmpEntries>& pmpcfg, size_t cfg_idx,
                   simrv::xlen::CsrValue wdata, unsigned xlen) noexcept {
    const size_t bytes_per_csr = (xlen == 32) ? 4 : 8;
    if (xlen != 32 && (cfg_idx >= 4 || (cfg_idx & 1) != 0)) return;
    if (xlen == 32 && cfg_idx >= 4) return;
    const size_t base_entry = (xlen == 32) ? (cfg_idx * 4) : ((cfg_idx / 2) * 8);
    for (size_t b = 0; b < bytes_per_csr; ++b) {
        const size_t entry = base_entry + b;
        if (entry < ArchState::kNumPmpEntries && (pmpcfg[entry] & 0x80) == 0) {
            pmpcfg[entry] = pmpcfg_legalize(static_cast<uint8_t>((wdata >> (8 * b)) & 0xFF));
        }
    }
}

}  // namespace

auto CsrFile::compute_mstatus(const ArchState& state, simrv::xlen::CsrValue mask) noexcept
    -> simrv::xlen::CsrValue {
    const simrv::xlen::CsrValue misa = state.misa;
    const simrv::xlen::CsrValue implemented =
        mstatus_writable_mask(isa::misa_has_extension(misa, isa::IsaExtension::S),
                              isa::misa_has_extension(misa, isa::IsaExtension::U),
                              isa::misa_has_extension(misa, isa::IsaExtension::F),
                              isa::misa_has_extension(misa, isa::IsaExtension::V),
                              isa::misa_has_extension(misa, isa::IsaExtension::H));
    const auto readable = implemented | kMstatusSd |
                          (isa::misa_has_extension(misa, isa::IsaExtension::H) ? kMstatusGva : 0);
    return mstatus_read_value(state.mstatus, mask & readable, state.current_xlen());
}

auto CsrFile::getMstatus(simrv::xlen::CsrValue mask) const -> simrv::xlen::CsrValue {
    return compute_mstatus(cpu_.state(), mask);
}

void CsrFile::setMstatus(simrv::xlen::CsrValue wdata) {
    const simrv::xlen::CsrValue misa = cpu_.state().misa;
    const bool has_s = isa::misa_has_extension(misa, isa::IsaExtension::S);
    const bool has_u = isa::misa_has_extension(misa, isa::IsaExtension::U);
    const bool has_f = isa::misa_has_extension(misa, isa::IsaExtension::F);
    const bool has_v = isa::misa_has_extension(misa, isa::IsaExtension::V);
    const bool has_h = isa::misa_has_extension(misa, isa::IsaExtension::H);
    wdata = mstatus_legalize_mpp(wdata, has_s, has_u);
    if constexpr (simrv::xlen::kIsXLen64) {
        // SXL and UXL are WARL. SimRV supports 32 and 64 only; reserved
        // encodings retain their prior legal value. An RV32 machine
        // personality fixes both lower modes to RV32.
        const auto mxl =
            static_cast<unsigned>((static_cast<uint64_t>(cpu_.state().misa) >> 62U) & 0x3U);
        for (const unsigned shift : {32U, 34U}) {
            const uint64_t field_mask = uint64_t{0x3U} << shift;
            const uint64_t requested = (static_cast<uint64_t>(wdata) >> shift) & 0x3U;
            auto legalized = static_cast<uint64_t>(wdata);
            if (mxl == 1U) {
                legalized = (legalized & ~field_mask) | (uint64_t{1U} << shift);
            } else if (shift == 32U && mxl == 2U) {
                // The RV64 personality selects UXL=64 as its WARL value; it
                // does not advertise the optional RV32 U-mode personality.
                legalized = (legalized & ~field_mask) | (uint64_t{2U} << shift);
            } else if (requested != 1U && requested != 2U) {
                legalized = (legalized & ~field_mask) |
                            (static_cast<uint64_t>(cpu_.state().mstatus) & field_mask);
            }
            wdata = static_cast<simrv::xlen::CsrValue>(legalized);
        }
        const uint64_t sxl = (static_cast<uint64_t>(wdata) >> 34U) & 0x3U;
        const uint64_t uxl = (static_cast<uint64_t>(wdata) >> 32U) & 0x3U;
        if (uxl > sxl) {
            constexpr uint64_t kUxlMask = uint64_t{0x3U} << 32U;
            wdata = static_cast<simrv::xlen::CsrValue>((static_cast<uint64_t>(wdata) & ~kUxlMask) |
                                                       (sxl << 32U));
        }
    }
    simrv::xlen::CsrValue const mod = cpu_.state().mstatus ^ wdata;
    const simrv::xlen::CsrValue tlb_sensitive =
        MstatusBit::Mprv | MstatusBit::Sum | MstatusBit::Mxr | MstatusBit::Fs | MstatusBit::Vs;
    const auto xlen_bits = static_cast<simrv::xlen::CsrValue>(uint64_t{0xFULL} << 32U);
    if ((mod & (tlb_sensitive | xlen_bits)) != 0 ||
        (((cpu_.state().mstatus & MstatusBit::Mprv) != 0u) && (mod & MstatusBit::Mpp) != 0)) {
        cpu_.TLB_flush();
    }
    const simrv::xlen::CsrValue mask = mstatus_writable_mask(has_s, has_u, has_f, has_v, has_h);
    cpu_.state().mstatus = (cpu_.state().mstatus & ~mask) | (wdata & mask);
    // Read-only-zero fields must not retain reset/profile state internally,
    // otherwise SD and effective-XLEN calculations could observe absent state.
    cpu_.state().mstatus &= mask | (has_h ? kMstatusGva : 0);
    cpu_.state().update_xlen();
    cpu_.sync_decode_context();
}

auto CsrFile::read_from_state(const ArchState& state, simrv::xlen::CsrNumber addr, const CPU* cpu)
    -> std::expected<simrv::xlen::CsrValue, ExceptionCode> {
    const uint16_t address = addr.value();
    const unsigned active_xlen = state.current_xlen();
    constexpr uint64_t kStateenSuperEnable = uint64_t{1} << 63U;
    constexpr uint64_t kStateenEnvCfgEnable = uint64_t{1} << 62U;
    if (address >= 0x30CU && address <= 0x30FU) {
        const uint64_t mask =
            address == 0x30CU ? (kStateenSuperEnable | kStateenEnvCfgEnable) : kStateenSuperEnable;
        return static_cast<simrv::xlen::CsrValue>(state.mstateen[address - 0x30CU] & mask);
    }
    if (address >= 0x31CU && address <= 0x31FU) {
        if (active_xlen != 32) return std::unexpected(ExceptionCode::IllegalInstruction);
        return static_cast<simrv::xlen::CsrValue>(state.mstateen[address - 0x31CU] >> 32U);
    }
    if (address >= 0x10CU && address <= 0x10FU) {
        const size_t bank = address - 0x10CU;
        if (state.virt && isa::misa_has_extension(state.misa, isa::IsaExtension::H) &&
            (state.hstateen[bank] & kStateenSuperEnable) == 0) {
            return std::unexpected(ExceptionCode::VirtualInstruction);
        }
        if (state.priv != simrv::xlen::PrivilegeLevel::Machine &&
            (state.mstateen[bank] & kStateenSuperEnable) == 0) {
            return std::unexpected(ExceptionCode::IllegalInstruction);
        }
        // SimRV does not expose any subordinate-state controls through Sstateen yet.
        // Those fields are reserved/read-only-zero in the active RVA23 ACT profile.
        return simrv::xlen::CsrValue{0};
    }
    if (address >= 0x60CU && address <= 0x60FU) {
        const size_t bank = address - 0x60CU;
        if (state.priv != simrv::xlen::PrivilegeLevel::Machine &&
            (state.mstateen[bank] & kStateenSuperEnable) == 0) {
            if (isa::misa_has_extension(state.misa, isa::IsaExtension::H)) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            return simrv::xlen::CsrValue{0};
        }
        const uint64_t mask =
            bank == 0U ? (kStateenSuperEnable | kStateenEnvCfgEnable) : kStateenSuperEnable;
        return static_cast<simrv::xlen::CsrValue>(state.hstateen[bank] & mask);
    }
    if (address >= 0x71CU && address <= 0x71FU) {
        if (active_xlen != 32) return std::unexpected(ExceptionCode::IllegalInstruction);
        const size_t bank = address - 0x71CU;
        if ((state.mstateen[bank] & kStateenSuperEnable) == 0) {
            return simrv::xlen::CsrValue{0};
        }
        return static_cast<simrv::xlen::CsrValue>(state.hstateen[bank] >> 32U);
    }

    auto require_fp = [&state]() -> std::expected<void, ExceptionCode> {
        if (!fp_accessible(state)) {
            return std::unexpected(ExceptionCode::IllegalInstruction);
        }
        return {};
    };
    auto require_vector = [&state]() -> std::expected<void, ExceptionCode> {
        if (!vector_accessible(state)) {
            return std::unexpected(ExceptionCode::IllegalInstruction);
        }
        return {};
    };
    auto require_rv32_counter = [active_xlen]() -> std::expected<void, ExceptionCode> {
        if (active_xlen == 64) {
            return std::unexpected(ExceptionCode::IllegalInstruction);
        }
        return {};
    };

    simrv::xlen::CsrValue rcsr = 0;
    switch (addr.value()) {
        case csr_addr(Csr::Fflags):
            return require_fp().transform([&state]() { return state.fcsr & kFflagsMask; });
        case csr_addr(Csr::Frm):
            return require_fp().transform(
                [&state]() { return (state.fcsr >> kFrmShift) & kFrmMask; });
        case csr_addr(Csr::Fcsr):
            return require_fp().transform([&state]() { return state.fcsr & kFcsrMask; });
        case csr_addr(Csr::Vstart):
            return require_vector().transform([&state]() { return state.vstart; });
        case csr_addr(Csr::Vxsat):
            return require_vector().transform([&state]() { return state.vxsat; });
        case csr_addr(Csr::Vxrm):
            return require_vector().transform([&state]() { return state.vxrm; });
        case csr_addr(Csr::Vcsr):
            return require_vector().transform(
                [&state]() { return (state.vxrm << 1) | state.vxsat; });
        case csr_addr(Csr::Vl):
            return require_vector().transform([&state]() { return state.vl; });
        case csr_addr(Csr::Vtype):
            return require_vector().transform([&state]() { return state.vtype; });
        case csr_addr(Csr::Vlenb):
            // vlenb is the read-only architectural VLEN/8 constant for the current hart.
            return require_vector().transform(
                [&state]() { return static_cast<simrv::xlen::CsrValue>(state.regs.vlen_bytes()); });

        case csr_addr(Csr::Hstatus):
            rcsr = state.hstatus &
                   ((simrv::xlen::CsrValue{1} << 7U) | (simrv::xlen::CsrValue{1} << 8U) |
                    (simrv::xlen::CsrValue{1} << 9U) | (simrv::xlen::CsrValue{1} << 6U) |
                    kHstatusVgeinMask | (simrv::xlen::CsrValue{1} << 20U) |
                    (simrv::xlen::CsrValue{1} << 21U) | (simrv::xlen::CsrValue{1} << 22U));
            if constexpr (simrv::xlen::kIsXLen64) {
                rcsr |= (state.mstatus >> 2U) &
                        static_cast<simrv::xlen::CsrValue>(uint64_t{0x3} << 32U);
                rcsr |= state.hstatus & static_cast<simrv::xlen::CsrValue>(uint64_t{0x3} << 48U);
            }
            break;
        case csr_addr(Csr::Hedeleg):
            rcsr = state.hedeleg & kHedelegWritableMask;
            break;
        case csr_addr(Csr::Hideleg):
            rcsr = state.hideleg &
                   ((simrv::xlen::CsrValue{1} << 2U) | (simrv::xlen::CsrValue{1} << 6U) |
                    (simrv::xlen::CsrValue{1} << 10U));
            break;
        case csr_addr(Csr::Hie):
            rcsr =
                state.hie & ((simrv::xlen::CsrValue{1} << 12U) | (simrv::xlen::CsrValue{1} << 2U) |
                             (simrv::xlen::CsrValue{1} << 6U) | (simrv::xlen::CsrValue{1} << 10U));
            break;
        case csr_addr(Csr::Hip):
            rcsr = (state.hip | state.hvip | state.supervisor_guest_external_pending() |
                    vs_interrupts_to_h(state.vsip) | state.selected_guest_external_pending() |
                    (state.vstip_sstc ? (simrv::xlen::CsrValue{1} << 6U) : 0U)) &
                   ((simrv::xlen::CsrValue{1} << 12U) | (simrv::xlen::CsrValue{1} << 2U) |
                    (simrv::xlen::CsrValue{1} << 6U) | (simrv::xlen::CsrValue{1} << 10U));
            break;
        case csr_addr(Csr::Hvip):
            rcsr =
                (state.hvip | (vs_interrupts_to_h(state.vsip) & std::to_underlying(MipBit::Hsip))) &
                ((simrv::xlen::CsrValue{1} << 2U) | (simrv::xlen::CsrValue{1} << 6U) |
                 (simrv::xlen::CsrValue{1} << 10U));
            break;
        case csr_addr(Csr::Henvcfg):
            if (state.priv != simrv::xlen::PrivilegeLevel::Machine &&
                (state.mstateen[0] & kStateenEnvCfgEnable) == 0) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            rcsr = state.henvcfg & kHenvcfgWritableMask;
            if ((state.menvcfg & static_cast<simrv::xlen::CsrValue>(uint64_t{1} << 63U)) == 0) {
                rcsr &= ~static_cast<simrv::xlen::CsrValue>(uint64_t{1} << 63U);
            }
            break;
        case csr_addr(Csr::Htval):
            rcsr = state.htval;
            break;
        case csr_addr(Csr::Htinst):
            rcsr = state.htinst;
            break;
        case csr_addr(Csr::Hgatp):
            rcsr = state.hgatp & kHgatpWritableMask;
            break;

        case csr_addr(Csr::Vsstatus):
            rcsr = mstatus_read_value(state.vsstatus, kMstatusSstatusReadMask, active_xlen);
            break;
        case csr_addr(Csr::Vsie):
            rcsr = state.vsie & h_interrupts_to_vs(state.hideleg);
            break;
        case csr_addr(Csr::Vstvec):
            rcsr = state.vstvec;
            break;
        case csr_addr(Csr::Vsscratch):
            rcsr = state.vsscratch;
            break;
        case csr_addr(Csr::Vsepc):
            rcsr = isa::epc_read_value(state.vsepc, state.misa);
            break;
        case csr_addr(Csr::Vscause):
            rcsr = cause_read_value(state.vscause, active_xlen);
            break;
        case csr_addr(Csr::Vstval):
            rcsr = state.vstval;
            break;
        case csr_addr(Csr::Vsip):
            rcsr =
                (state.vsip | h_interrupts_to_vs(
                                  state.hip | state.hvip | state.selected_guest_external_pending() |
                                  (state.vstip_sstc ? (simrv::xlen::CsrValue{1} << 6U) : 0U))) &
                h_interrupts_to_vs(state.hideleg);
            break;
        case csr_addr(Csr::Vsatp):
            rcsr = state.vsatp;
            break;

        case csr_addr(Csr::Sie):
            rcsr = state.virt
                       ? (state.vsie & h_interrupts_to_vs(state.hideleg))
                       : (state.mie & state.mideleg & kSupervisorInterruptEnableMask &
                          interrupt_implemented_mask(
                              true, isa::misa_has_extension(state.misa, isa::IsaExtension::H)));
            break;
        case csr_addr(Csr::Stvec):
            rcsr = state.virt ? state.vstvec : state.stvec;
            break;
        case csr_addr(Csr::Scounteren):
            rcsr = state.scounteren;
            break;
        case csr_addr(Csr::Hcounteren):
            rcsr = state.hcounteren & kCounterEnableMask;
            break;
        case csr_addr(Csr::Htimedelta):
            rcsr = state.htimedelta;
            break;
        case csr_addr(Csr::Hgeie):
            rcsr = state.hgeie & kHgeieWritableMask;
            break;
        case csr_addr(Csr::Hgeip):
            rcsr = state.hgeip & kHgeieWritableMask;
            break;
        case csr_addr(Csr::Scountovf): {
            if (!isa::misa_has_extension(state.misa, isa::IsaExtension::S)) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            uint32_t overflow = cpu != nullptr ? cpu->perf_counters.read_overflow_mask() : 0U;
            if (state.priv != simrv::xlen::PrivilegeLevel::Machine) {
                for (size_t counter = 3; counter < PerformanceCounters::kNumHpmCounters;
                     ++counter) {
                    if ((state.mcounteren & (simrv::xlen::CsrValue{1} << counter)) == 0) {
                        overflow &= ~(uint32_t{1} << counter);
                    }
                }
            }
            rcsr = overflow;
            break;
        }
        case csr_addr(Csr::Sscratch):
            rcsr = state.virt ? state.vsscratch : state.sscratch;
            break;
        case csr_addr(Csr::Sepc):
            rcsr = isa::epc_read_value(state.virt ? state.vsepc : state.sepc, state.misa);
            break;
        case csr_addr(Csr::Scause):
            rcsr = cause_read_value(state.virt ? state.vscause : state.scause, active_xlen);
            break;
        case csr_addr(Csr::Stval):
            rcsr = state.virt ? state.vstval : state.stval;
            break;
        case csr_addr(Csr::Sip):
            rcsr = state.virt
                       ? ((state.vsip |
                           h_interrupts_to_vs(
                               state.hip | state.hvip | state.selected_guest_external_pending() |
                               (state.vstip_sstc ? (simrv::xlen::CsrValue{1} << 6U) : 0U))) &
                          h_interrupts_to_vs(state.hideleg))
                       : (state.mip & state.mideleg & interrupt_implemented_mask(true));
            break;
        case csr_addr(Csr::Satp):
            rcsr = state.virt ? state.vsatp : state.satp;
            break;
        case csr_addr(Csr::Senvcfg):
            if (state.virt && isa::misa_has_extension(state.misa, isa::IsaExtension::H) &&
                (state.hstateen[0] & kStateenEnvCfgEnable) == 0) {
                return std::unexpected(ExceptionCode::VirtualInstruction);
            }
            if (state.priv != simrv::xlen::PrivilegeLevel::Machine &&
                (state.mstateen[0] & kStateenEnvCfgEnable) == 0) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            rcsr = state.senvcfg & kSenvcfgWritableMask;
            break;
        case csr_addr(Csr::Stimecmp): {
            if (state.priv < simrv::xlen::PrivilegeLevel::Machine) {
                if ((state.mcounteren & (simrv::xlen::CsrValue{1} << 1U)) == 0) {
                    return std::unexpected(ExceptionCode::IllegalInstruction);
                }
                bool stce = false;
                if constexpr (simrv::xlen::kIsXLen64) {
                    stce = (state.menvcfg & (1ULL << 63U)) != 0;
                } else {
                    stce = (state.menvcfgh & (1UL << 31U)) != 0;
                }
                if (!stce) {
                    return std::unexpected(ExceptionCode::IllegalInstruction);
                }
                if (state.virt) {
                    if ((state.henvcfg & static_cast<simrv::xlen::CsrValue>(uint64_t{1} << 63U)) ==
                            0 ||
                        (state.hcounteren & (simrv::xlen::CsrValue{1} << 1U)) == 0) {
                        return std::unexpected(ExceptionCode::VirtualInstruction);
                    }
                }
            }
            const auto compare = state.virt ? state.vstimecmp : state.stimecmp;
            if constexpr (simrv::xlen::kIsXLen64) {
                rcsr = static_cast<simrv::xlen::CsrValue>(compare);
            } else {
                rcsr = static_cast<simrv::xlen::CsrValue>(compare & 0xFFFFFFFFULL);
            }
            break;
        }
        case csr_addr(Csr::Vstimecmp):
            if constexpr (simrv::xlen::kIsXLen64) {
                rcsr = static_cast<simrv::xlen::CsrValue>(state.vstimecmp);
            } else {
                rcsr = static_cast<simrv::xlen::CsrValue>(state.vstimecmp & 0xFFFFFFFFULL);
            }
            break;
        case csr_addr(Csr::Vstimecmph):
            return require_rv32_counter().transform(
                [&state]() { return static_cast<simrv::xlen::CsrValue>(state.vstimecmp >> 32U); });
        case csr_addr(Csr::Stimecmph):
            if (state.priv < simrv::xlen::PrivilegeLevel::Machine) {
                const bool stce = (state.menvcfgh & (1UL << 31U)) != 0;
                if (!stce) {
                    return std::unexpected(ExceptionCode::IllegalInstruction);
                }
            }
            return require_rv32_counter().transform(
                [&state]() { return static_cast<simrv::xlen::CsrValue>(state.stimecmp >> 32); });

        case csr_addr(Csr::Medeleg):
            rcsr = state.medeleg & kMedelegWritableMask;
            if (isa::misa_has_extension(state.misa, isa::IsaExtension::H)) {
                constexpr auto kHDelegatedToHs =
                    (simrv::xlen::CsrValue{1} << 20U) | (simrv::xlen::CsrValue{1} << 21U) |
                    (simrv::xlen::CsrValue{1} << 22U) | (simrv::xlen::CsrValue{1} << 23U);
                rcsr |= kHDelegatedToHs;
            }
            break;
        case csr_addr(Csr::Mideleg):
            rcsr = state.mideleg & mideleg_writable_mask(true);
            if (isa::misa_has_extension(state.misa, isa::IsaExtension::H)) {
                rcsr |= kVsInterruptBits | (simrv::xlen::CsrValue{1} << 12U);
            }
            break;
        case csr_addr(Csr::Mie):
            rcsr = state.mie & interrupt_implemented_mask(
                                   isa::misa_has_extension(state.misa, isa::IsaExtension::S),
                                   isa::misa_has_extension(state.misa, isa::IsaExtension::H));
            break;
        case csr_addr(Csr::Mtvec):
            rcsr = state.mtvec;
            break;
        case csr_addr(Csr::Mcounteren):
            rcsr = state.mcounteren;
            break;
        case csr_addr(Csr::Mcountinhibit):
            rcsr = state.mcountinhibit;
            break;
        case csr_addr(Csr::Menvcfg):
            rcsr = static_cast<simrv::xlen::CsrValue>(static_cast<uint64_t>(state.menvcfg) &
                                                      ~(uint64_t{1} << 61U));
            break;
        case csr_addr(Csr::Menvcfgh):
            return require_rv32_counter().transform(
                [&state]() { return state.menvcfgh & ~(simrv::xlen::CsrValue{1} << 29U); });
        case csr_addr(Csr::Mseccfg):
            rcsr = state.mseccfg;
            break;
        case csr_addr(Csr::Mseccfgh):
            return require_rv32_counter().transform([&state]() { return state.mseccfgh; });
        case csr_addr(Csr::Mscratch):
            rcsr = state.mscratch;
            break;
        case csr_addr(Csr::Mepc):
            rcsr = isa::epc_read_value(state.mepc, state.misa);
            break;
        case csr_addr(Csr::Mcause):
            rcsr = cause_read_value(state.mcause, active_xlen);
            break;
        case csr_addr(Csr::Mtval):
            rcsr = state.mtval;
            break;
        case csr_addr(Csr::Mtinst):
            rcsr = state.mtinst;
            break;
        case csr_addr(Csr::Mtval2):
            rcsr = state.mtval2;
            break;
        case csr_addr(Csr::Mip):
            rcsr = (state.mip | state.hip | state.hvip | state.supervisor_guest_external_pending() |
                    vs_interrupts_to_h(state.vsip) | state.selected_guest_external_pending() |
                    (state.vstip_sstc ? (simrv::xlen::CsrValue{1} << 6U) : 0U)) &
                   interrupt_implemented_mask(
                       isa::misa_has_extension(state.misa, isa::IsaExtension::S),
                       isa::misa_has_extension(state.misa, isa::IsaExtension::H));
            break;
        case csr_addr(Csr::Misa):
            rcsr = isa::misa_with_mxl(state.misa, active_xlen);
            break;

        case csr_addr(Csr::Mcycle):
        case csr_addr(Csr::Cycle):
            rcsr = static_cast<simrv::xlen::CsrValue>(cpu != nullptr ? cpu->clint_mmio.mcycle : 0U);
            break;
        case csr_addr(Csr::Minstret):
        case csr_addr(Csr::Instret):
            rcsr = static_cast<simrv::xlen::CsrValue>(state.minstret);
            break;
        case csr_addr(Csr::Time):
            rcsr = static_cast<simrv::xlen::CsrValue>(
                (cpu != nullptr ? cpu->clint_mmio.mtime.load(std::memory_order_relaxed) : 0U) +
                (state.virt ? state.htimedelta : simrv::xlen::CsrValue{0}));
            break;

        case csr_addr(Csr::Mcycleh):
        case csr_addr(Csr::Cycleh):
            return require_rv32_counter().transform([cpu]() {
                const uint64_t mcycle = cpu != nullptr ? cpu->clint_mmio.mcycle : 0U;
                return static_cast<simrv::xlen::CsrValue>(mcycle >> kHighWordShift);
            });
        case csr_addr(Csr::Minstreth):
        case csr_addr(Csr::Instreth):
            return require_rv32_counter().transform([&state]() {
                return static_cast<simrv::xlen::CsrValue>(state.minstret >> kHighWordShift);
            });
        case csr_addr(Csr::Timeh):
            return require_rv32_counter().transform([cpu]() {
                const uint64_t mtime =
                    cpu != nullptr ? cpu->clint_mmio.mtime.load(std::memory_order_relaxed) : 0U;
                return static_cast<simrv::xlen::CsrValue>(mtime >> kHighWordShift);
            });

        case csr_addr(Csr::Sstatus):
            rcsr = state.virt
                       ? mstatus_read_value(state.vsstatus, kMstatusSstatusReadMask, active_xlen)
                       : compute_mstatus(state, kMstatusSstatusReadMask);
            break;
        case csr_addr(Csr::Mstatus):
            rcsr = compute_mstatus(state, kMstatusReadMask);
            break;

        case csr_addr(Csr::Mvendorid):
        case csr_addr(Csr::Marchid):
        case csr_addr(Csr::Mimpid):
        case csr_addr(Csr::Mconfigptr):
            rcsr = 0;
            break;

        case csr_addr(Csr::Mhartid):
            rcsr = state.mhartid;
            break;

        case csr_addr(Csr::Tselect):
            rcsr = state.tselect;
            break;
        case csr_addr(Csr::Tdata1):
            if (state.tselect < ArchState::kNumTriggers) {
                rcsr = state.tdata1[state.tselect];
            } else {
                rcsr = 0;
            }
            break;
        case csr_addr(Csr::Tdata2):
            if (state.tselect < ArchState::kNumTriggers) {
                rcsr = state.tdata2[state.tselect];
            } else {
                rcsr = 0;
            }
            break;
        case csr_addr(Csr::Tdata3):
            if (state.tselect < ArchState::kNumTriggers) {
                rcsr = state.tdata3[state.tselect];
            } else {
                rcsr = 0;
            }
            break;
        case csr_addr(Csr::Tinfo):
            rcsr = (1U << 2U);
            break;
        case csr_addr(Csr::Tcontrol):
        case csr_addr(Csr::Mcontext):
        case csr_addr(Csr::Scontext):
            rcsr = 0;
            break;

        default:
            if (pmp_csr_exists(addr, active_xlen)) {
                if (addr.value() >= 0x3A0 && addr.value() <= 0x3AF) {
                    const size_t cfg_idx = addr.value() - 0x3A0;
                    rcsr = pack_pmpcfg(state.pmpcfg, cfg_idx, active_xlen);
                } else if (addr.value() >= 0x3B0 && addr.value() <= 0x3EF) {
                    const size_t pmp_idx = addr.value() - 0x3B0;
                    if (pmp_idx < ArchState::kNumPmpEntries) {
                        rcsr = static_cast<simrv::xlen::CsrValue>(state.pmpaddr[pmp_idx]);
                    } else {
                        rcsr = 0;
                    }
                } else {
                    rcsr = 0;
                }
                break;
            }
            const auto a = addr.value();
            if (a >= 0x323U && a <= 0x33FU) {
                const auto value =
                    cpu != nullptr ? cpu->perf_counters.read_hpmevent(a - 0x320U) : 0U;
                rcsr = static_cast<simrv::xlen::CsrValue>(value);
                break;
            }
            if (active_xlen == 32U && a >= 0x723U && a <= 0x73FU) {
                const auto value =
                    cpu != nullptr ? cpu->perf_counters.read_hpmevent(a - 0x720U) : 0U;
                rcsr = static_cast<simrv::xlen::CsrValue>(
                    static_cast<uint32_t>(value >> kHighWordShift));
                break;
            }
            if (a >= 0xB03U && a <= 0xB1FU) {
                rcsr = static_cast<simrv::xlen::CsrValue>(
                    cpu != nullptr ? cpu->perf_counters.read_hpmcounter(a - 0xB00U) : 0U);
                break;
            }
            if (a >= 0xC03U && a <= 0xC1FU) {
                rcsr = static_cast<simrv::xlen::CsrValue>(
                    cpu != nullptr ? cpu->perf_counters.read_hpmcounter(a - 0xC00U) : 0U);
                break;
            }
            if (active_xlen == 32U) {
                if (a >= 0xB83U && a <= 0xB9FU) {
                    return require_rv32_counter().transform([cpu, a]() {
                        const uint64_t val =
                            cpu != nullptr ? cpu->perf_counters.read_hpmcounter(a - 0xB80U) : 0U;
                        return static_cast<simrv::xlen::CsrValue>(val >> kHighWordShift);
                    });
                }
                if (a >= 0xC83U && a <= 0xC9FU) {
                    return require_rv32_counter().transform([cpu, a]() {
                        const uint64_t val =
                            cpu != nullptr ? cpu->perf_counters.read_hpmcounter(a - 0xC80U) : 0U;
                        return static_cast<simrv::xlen::CsrValue>(val >> kHighWordShift);
                    });
                }
            }
            return std::unexpected(ExceptionCode::IllegalInstruction);
    }
    return rcsr;
}

auto CsrFile::read(simrv::xlen::CsrNumber addr) const
    -> std::expected<simrv::xlen::CsrValue, ExceptionCode> {
    return read_from_state(cpu_.state(), addr, &cpu_);
}

auto CsrFile::write(simrv::xlen::CsrNumber addr, simrv::xlen::CsrValue wdata)
    -> std::expected<void, ExceptionCode> {  // NOLINT(bugprone-easily-swappable-parameters)
    const uint16_t address = addr.value();
    constexpr uint64_t kStateenSuperEnable = uint64_t{1} << 63U;
    constexpr uint64_t kStateenEnvCfgEnable = uint64_t{1} << 62U;
    if (address >= 0x30CU && address <= 0x30FU) {
        auto& value = cpu_.state().mstateen[address - 0x30CU];
        const uint64_t mask =
            address == 0x30CU ? (kStateenSuperEnable | kStateenEnvCfgEnable) : kStateenSuperEnable;
        value = (value & ~mask) | (static_cast<uint64_t>(wdata) & mask);
        return {};
    }
    if (address >= 0x31CU && address <= 0x31FU) {
        if (cpu_.state().regs.xlen != 32) return std::unexpected(ExceptionCode::IllegalInstruction);
        auto& value = cpu_.state().mstateen[address - 0x31CU];
        value = (value & 0xFFFFFFFFULL) | (static_cast<uint64_t>(wdata) << 32U);
        value &= kStateenSuperEnable;
        return {};
    }
    if (address >= 0x10CU && address <= 0x10FU) {
        const size_t bank = address - 0x10CU;
        if (cpu_.state().virt && isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::H) &&
            (cpu_.state().hstateen[bank] & kStateenSuperEnable) == 0) {
            return std::unexpected(ExceptionCode::VirtualInstruction);
        }
        if (cpu_.state().priv != simrv::xlen::PrivilegeLevel::Machine &&
            (cpu_.state().mstateen[bank] & kStateenSuperEnable) == 0) {
            return std::unexpected(ExceptionCode::IllegalInstruction);
        }
        cpu_.state().sstateen[bank] = 0;
        return {};
    }
    if (address >= 0x60CU && address <= 0x60FU) {
        const size_t bank = address - 0x60CU;
        if (cpu_.state().priv != simrv::xlen::PrivilegeLevel::Machine &&
            (cpu_.state().mstateen[bank] & kStateenSuperEnable) == 0) {
            if (isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::H)) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            return {};
        }
        auto& value = cpu_.state().hstateen[bank];
        const uint64_t mask =
            bank == 0U ? (kStateenSuperEnable | kStateenEnvCfgEnable) : kStateenSuperEnable;
        value = (value & ~mask) | (static_cast<uint64_t>(wdata) & mask);
        return {};
    }
    if (address >= 0x71CU && address <= 0x71FU) {
        if (cpu_.state().regs.xlen != 32) return std::unexpected(ExceptionCode::IllegalInstruction);
        const size_t bank = address - 0x71CU;
        if ((cpu_.state().mstateen[bank] & kStateenSuperEnable) == 0) return {};
        auto& value = cpu_.state().hstateen[bank];
        value = (value & 0xFFFFFFFFULL) | (static_cast<uint64_t>(wdata) << 32U);
        value &= kStateenSuperEnable;
        return {};
    }

    const bool has_s = isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::S);
    const bool has_h = isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::H);
    const simrv::xlen::CsrValue interrupt_mask = interrupt_implemented_mask(has_s, has_h);
    const auto log_pending_changes = [this](simrv::xlen::CsrValue before, std::string_view source) {
        if (cpu_.machine_ == nullptr) return;
        constexpr std::array interrupt_bits = {
            std::pair{MipBit::Ssip, simrv::xlen::TrapCause{1}},
            std::pair{MipBit::Msip, simrv::xlen::TrapCause{3}},
            std::pair{MipBit::Stip, simrv::xlen::TrapCause{5}},
            std::pair{MipBit::Mtip, simrv::xlen::TrapCause{7}},
            std::pair{MipBit::Seip, simrv::xlen::TrapCause{9}},
            std::pair{MipBit::Meip, simrv::xlen::TrapCause{11}},
            std::pair{MipBit::Lcofip, simrv::xlen::TrapCause{13}},
        };
        for (const auto& [bit, code] : interrupt_bits) {
            const bool was_pending = (before & bit) != 0;
            const bool is_pending = (cpu_.state().mip & bit) != 0;
            if (was_pending != is_pending) {
                cpu_.machine_->trace().log_interrupt_signal(cpu_, kInterruptCauseBit | code,
                                                            is_pending, source);
                cpu_.machine_->notify_interrupt_transition(cpu_.state().mhartid,
                                                           static_cast<uint32_t>(code), is_pending,
                                                           source, cpu_.clint_mmio.mcycle);
            }
        }
    };

    auto require_fp = [this]() -> std::expected<void, ExceptionCode> {
        if (!fp_accessible(cpu_.state())) {
            return std::unexpected(ExceptionCode::IllegalInstruction);
        }
        return {};
    };
    auto require_vector = [this]() -> std::expected<void, ExceptionCode> {
        if (!vector_accessible(cpu_.state())) {
            return std::unexpected(ExceptionCode::IllegalInstruction);
        }
        return {};
    };
    auto require_rv32_counter = [this]() -> std::expected<void, ExceptionCode> {
        if (cpu_.state().regs.xlen == 64) {
            return std::unexpected(ExceptionCode::IllegalInstruction);
        }
        return {};
    };

    switch (addr.value()) {
        case csr_addr(Csr::Mvendorid):
        case csr_addr(Csr::Marchid):
        case csr_addr(Csr::Mimpid):
        case csr_addr(Csr::Mconfigptr):
        case csr_addr(Csr::Mhartid):
        case csr_addr(Csr::Time):
        case csr_addr(Csr::Timeh):
        case csr_addr(Csr::Misa):
            break;

        case csr_addr(Csr::Mcycle):
            if (cpu_.state().regs.xlen == 32) {
                cpu_.clint_mmio.mcycle =
                    (cpu_.clint_mmio.mcycle & 0xFFFFFFFF00000000ULL) | static_cast<uint32_t>(wdata);
            } else {
                cpu_.clint_mmio.mcycle = wdata;
            }
            break;
        case csr_addr(Csr::Mcycleh):
            return require_rv32_counter().and_then(
                [this, wdata]() -> std::expected<void, ExceptionCode> {
                    cpu_.clint_mmio.mcycle = (cpu_.clint_mmio.mcycle & 0x00000000FFFFFFFFULL) |
                                             (static_cast<uint64_t>(wdata) << 32);
                    return {};
                });
        case csr_addr(Csr::Minstret):
            if (cpu_.state().regs.xlen == 32) {
                cpu_.state().minstret =
                    (cpu_.state().minstret & 0xFFFFFFFF00000000ULL) | static_cast<uint32_t>(wdata);
            } else {
                cpu_.state().minstret = wdata;
            }
            break;
        case csr_addr(Csr::Minstreth):
            return require_rv32_counter().and_then(
                [this, wdata]() -> std::expected<void, ExceptionCode> {
                    cpu_.state().minstret = (cpu_.state().minstret & 0x00000000FFFFFFFFULL) |
                                            (static_cast<uint64_t>(wdata) << 32);
                    return {};
                });

        case csr_addr(Csr::Fflags):
            return require_fp().and_then([this, wdata]() -> std::expected<void, ExceptionCode> {
                cpu_.state().fcsr = (cpu_.state().fcsr & ~kFflagsMask) | (wdata & kFflagsMask);
                cpu_.state().mark_extension_dirty(MstatusBit::Fs);
#if defined(__x86_64__) || defined(_M_X64)
                _mm_setcsr(_mm_getcsr() & ~0x3fu);
#endif
                return {};
            });
        case csr_addr(Csr::Frm):
            return require_fp().and_then([this, wdata]() -> std::expected<void, ExceptionCode> {
                cpu_.state().fcsr = (cpu_.state().fcsr & ~(kFrmMask << kFrmShift)) |
                                    ((wdata & kFrmMask) << kFrmShift);
                cpu_.state().mark_extension_dirty(MstatusBit::Fs);
                return {};
            });
        case csr_addr(Csr::Fcsr):
            return require_fp().and_then([this, wdata]() -> std::expected<void, ExceptionCode> {
                cpu_.state().fcsr = wdata & kFcsrMask;
                cpu_.state().mark_extension_dirty(MstatusBit::Fs);
#if defined(__x86_64__) || defined(_M_X64)
                _mm_setcsr(_mm_getcsr() & ~0x3fu);
#endif
                return {};
            });
        case csr_addr(Csr::Vstart):
            return require_vector().and_then([this, wdata]() -> std::expected<void, ExceptionCode> {
                cpu_.state().vstart = wdata & cpu_.state().regs.vstart_mask();
                cpu_.state().mark_extension_dirty(MstatusBit::Vs);
                return {};
            });
        case csr_addr(Csr::Vxsat):
            return require_vector().and_then([this, wdata]() -> std::expected<void, ExceptionCode> {
                cpu_.state().vxsat = wdata & 1;
                cpu_.state().mark_extension_dirty(MstatusBit::Vs);
                return {};
            });
        case csr_addr(Csr::Vxrm):
            return require_vector().and_then([this, wdata]() -> std::expected<void, ExceptionCode> {
                cpu_.state().vxrm = wdata & 3;
                cpu_.state().mark_extension_dirty(MstatusBit::Vs);
                return {};
            });
        case csr_addr(Csr::Vcsr):
            return require_vector().and_then([this, wdata]() -> std::expected<void, ExceptionCode> {
                cpu_.state().vxrm = (wdata >> 1) & 3;
                cpu_.state().vxsat = wdata & 1;
                cpu_.state().mark_extension_dirty(MstatusBit::Vs);
                return {};
            });
        case csr_addr(Csr::Vl):
            return require_vector().and_then([this, wdata]() -> std::expected<void, ExceptionCode> {
                cpu_.state().vl = wdata;
                cpu_.state().mark_extension_dirty(MstatusBit::Vs);
                return {};
            });
        case csr_addr(Csr::Vtype):
            return require_vector().and_then([this, wdata]() -> std::expected<void, ExceptionCode> {
                cpu_.state().vtype = wdata;
                cpu_.state().mark_extension_dirty(MstatusBit::Vs);
                return {};
            });

        case csr_addr(Csr::Hstatus):
            cpu_.state().hstatus =
                wdata & ((simrv::xlen::CsrValue{1} << 7U) | (simrv::xlen::CsrValue{1} << 8U) |
                         (simrv::xlen::CsrValue{1} << 9U) | (simrv::xlen::CsrValue{1} << 6U) |
                         kHstatusVgeinMask | (simrv::xlen::CsrValue{1} << 20U) |
                         (simrv::xlen::CsrValue{1} << 21U) | (simrv::xlen::CsrValue{1} << 22U));
            if constexpr (simrv::xlen::kIsXLen64) {
                constexpr simrv::xlen::CsrValue kHupmmMask =
                    static_cast<simrv::xlen::CsrValue>(uint64_t{0x3} << 48U);
                const auto hupmm = (wdata & kHupmmMask) >> 48U;
                if (isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::U) &&
                    hupmm == 2U) {
                    cpu_.state().hstatus |= hupmm << 48U;
                }
            }
            break;
        case csr_addr(Csr::Hedeleg):
            cpu_.state().hedeleg = wdata & kHedelegWritableMask;
            break;
        case csr_addr(Csr::Hideleg):
            cpu_.state().hideleg =
                wdata & ((simrv::xlen::CsrValue{1} << 2U) | (simrv::xlen::CsrValue{1} << 6U) |
                         (simrv::xlen::CsrValue{1} << 10U));
            break;
        case csr_addr(Csr::Hie):
            cpu_.state().hie =
                wdata & ((simrv::xlen::CsrValue{1} << 12U) | (simrv::xlen::CsrValue{1} << 2U) |
                         (simrv::xlen::CsrValue{1} << 6U) | (simrv::xlen::CsrValue{1} << 10U));
            break;
        case csr_addr(Csr::Hip):
            cpu_.state().hvip = (cpu_.state().hvip & ~std::to_underlying(MipBit::Hsip)) |
                                (wdata & std::to_underlying(MipBit::Hsip));
            break;
        case csr_addr(Csr::Hvip):
            cpu_.state().hvip =
                wdata & ((simrv::xlen::CsrValue{1} << 2U) | (simrv::xlen::CsrValue{1} << 6U) |
                         (simrv::xlen::CsrValue{1} << 10U));
            break;
        case csr_addr(Csr::Henvcfg):
            if (cpu_.state().priv != simrv::xlen::PrivilegeLevel::Machine &&
                (cpu_.state().mstateen[0] & kStateenEnvCfgEnable) == 0) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            wdata &= kHenvcfgWritableMask;
            if ((cpu_.state().menvcfg & static_cast<simrv::xlen::CsrValue>(uint64_t{1} << 63U)) ==
                0) {
                wdata &= ~static_cast<simrv::xlen::CsrValue>(uint64_t{1} << 63U);
            }
            // CBIE=2 is reserved. Its WARL legalization may choose any legal
            // value; SimRV resolves it to zero so bit-walks remain deterministic.
            if (((wdata >> 4U) & 0x3U) == 0x2U) wdata &= ~(simrv::xlen::CsrValue{0x3} << 4U);
            if constexpr (simrv::xlen::kIsXLen64) {
                constexpr simrv::xlen::CsrValue kPmmMask =
                    static_cast<simrv::xlen::CsrValue>(uint64_t{0x3} << 32U);
                const auto pmm = (wdata & kPmmMask) >> 32U;
                const bool has_u = isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::U);
                if (!has_u || (pmm != 0U && pmm != 2U)) wdata &= ~kPmmMask;
            }
            cpu_.state().henvcfg = wdata;
            cpu_.evaluate_timer_interrupt();
            break;
        case csr_addr(Csr::Htval):
            cpu_.state().htval = wdata;
            break;
        case csr_addr(Csr::Htinst):
            cpu_.state().htinst = wdata;
            break;
        case csr_addr(Csr::Hgatp): {
            const auto mode = simrv::xlen::satp_mode(wdata, cpu_.state().regs.xlen);
            const bool supported = mode == 0 || (cpu_.state().regs.xlen == 64 &&
                                                 (mode == 8 || mode == 9 || mode == 10));
            simrv::xlen::Word sanitized = wdata & kHgatpWritableMask;
            if (mode != 0) sanitized &= ~simrv::xlen::Word{3};
            if (supported && cpu_.state().hgatp != sanitized) {
                cpu_.state().hgatp = sanitized;
                cpu_.TLB_flush();
            }
            break;
        }

        case csr_addr(Csr::Vsstatus):
            cpu_.state().vsstatus =
                (cpu_.state().vsstatus & ~kSstatusMask) | (wdata & kSstatusMask);
            break;
        case csr_addr(Csr::Vsie):
            cpu_.state().vsie =
                wdata & h_interrupts_to_vs(cpu_.state().hideleg) & kSupervisorInterruptBits;
            break;
        case csr_addr(Csr::Vstvec):
            cpu_.state().vstvec = wdata & ~static_cast<simrv::xlen::CsrValue>(2);
            break;
        case csr_addr(Csr::Vsscratch):
            cpu_.state().vsscratch = wdata;
            break;
        case csr_addr(Csr::Vsepc):
            cpu_.state().vsepc = wdata & ~static_cast<simrv::xlen::CsrValue>(1);
            break;
        case csr_addr(Csr::Vscause):
            cpu_.state().vscause = cause_write_value(wdata, cpu_.state().regs.xlen);
            break;
        case csr_addr(Csr::Vstval):
            cpu_.state().vstval = wdata;
            break;
        case csr_addr(Csr::Vsip):
            cpu_.state().vsip =
                (cpu_.state().vsip & ~kVsSoftwareInterrupt) |
                (wdata & h_interrupts_to_vs(cpu_.state().hideleg) & kVsSoftwareInterrupt);
            break;
        case csr_addr(Csr::Vsatp): {
            const unsigned xlen = cpu_.state().regs.xlen;
            const simrv::xlen::Word mode = simrv::xlen::satp_mode(wdata, xlen);
            if (simrv::xlen::satp_mode_supported(mode, xlen) && cpu_.state().vsatp != wdata) {
                cpu_.state().vsatp = wdata;
                cpu_.TLB_flush();
            }
            break;
        }

        case csr_addr(Csr::Stvec):
            if (cpu_.state().virt) {
                cpu_.state().vstvec = wdata & ~static_cast<simrv::xlen::CsrValue>(2);
            } else {
                cpu_.state().stvec = wdata & ~static_cast<simrv::xlen::CsrValue>(2);
            }
            break;
        case csr_addr(Csr::Scounteren):
            cpu_.state().scounteren = wdata & kCounterEnableMask;
            break;
        case csr_addr(Csr::Sscratch):
            if (cpu_.state().virt) {
                cpu_.state().vsscratch = wdata;
            } else {
                cpu_.state().sscratch = wdata;
            }
            break;
        case csr_addr(Csr::Sepc):
            if (cpu_.state().virt) {
                cpu_.state().vsepc = wdata & ~static_cast<simrv::xlen::CsrValue>(1);
            } else {
                cpu_.state().sepc = wdata & ~static_cast<simrv::xlen::CsrValue>(1);
            }
            break;
        case csr_addr(Csr::Scause):
            if (cpu_.state().virt) {
                cpu_.state().vscause = cause_write_value(wdata, cpu_.state().regs.xlen);
            } else {
                cpu_.state().scause = cause_write_value(wdata, cpu_.state().regs.xlen);
            }
            break;
        case csr_addr(Csr::Stval):
            if (cpu_.state().virt) {
                cpu_.state().vstval = wdata;
            } else {
                cpu_.state().stval = wdata;
            }
            break;
        case csr_addr(Csr::Senvcfg): {
            if (cpu_.state().virt &&
                isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::H) &&
                (cpu_.state().hstateen[0] & kStateenEnvCfgEnable) == 0) {
                return std::unexpected(ExceptionCode::VirtualInstruction);
            }
            if (cpu_.state().priv != simrv::xlen::PrivilegeLevel::Machine &&
                (cpu_.state().mstateen[0] & kStateenEnvCfgEnable) == 0) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            wdata &= kSenvcfgWritableMask;
            // CBIE=2 is reserved; choose the legal zero encoding.
            if (((wdata >> 4U) & 0x3U) == 0x2U) {
                wdata &= ~(simrv::xlen::CsrValue{0x3} << 4U);
            }
            if constexpr (simrv::xlen::kIsXLen64) {
                constexpr uint64_t kPmmMask = uint64_t{0x3} << 32U;
                const bool has_u = isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::U);
                const auto pmm = (static_cast<uint64_t>(wdata) & kPmmMask) >> 32U;
                if (!has_s || !has_u || (pmm != 0U && pmm != 2U)) {
                    wdata = static_cast<simrv::xlen::CsrValue>(static_cast<uint64_t>(wdata) &
                                                               ~kPmmMask);
                }
            }
            cpu_.state().senvcfg = wdata;
            break;
        }
        case csr_addr(Csr::Stimecmp): {
            if (cpu_.state().priv < simrv::xlen::PrivilegeLevel::Machine) {
                if ((cpu_.state().mcounteren & (simrv::xlen::CsrValue{1} << 1U)) == 0) {
                    return std::unexpected(ExceptionCode::IllegalInstruction);
                }
                bool stce = false;
                if constexpr (simrv::xlen::kIsXLen64) {
                    stce = (cpu_.state().menvcfg & (1ULL << 63U)) != 0;
                } else {
                    stce = (cpu_.state().menvcfgh & (1UL << 31U)) != 0;
                }
                if (!stce) {
                    return std::unexpected(ExceptionCode::IllegalInstruction);
                }
                if (cpu_.state().virt) {
                    if ((cpu_.state().henvcfg &
                         static_cast<simrv::xlen::CsrValue>(uint64_t{1} << 63U)) == 0 ||
                        (cpu_.state().hcounteren & (simrv::xlen::CsrValue{1} << 1U)) == 0) {
                        return std::unexpected(ExceptionCode::VirtualInstruction);
                    }
                }
            }
            auto& compare = cpu_.state().virt ? cpu_.state().vstimecmp : cpu_.state().stimecmp;
            if (cpu_.state().regs.xlen == 32) {
                compare = (compare & 0xFFFFFFFF00000000ULL) | static_cast<uint32_t>(wdata);
            } else {
                compare = wdata;
            }
            cpu_.evaluate_timer_interrupt();
            break;
        }
        case csr_addr(Csr::Vstimecmp):
            if (cpu_.state().regs.xlen == 32) {
                cpu_.state().vstimecmp =
                    (cpu_.state().vstimecmp & 0xFFFFFFFF00000000ULL) | static_cast<uint32_t>(wdata);
            } else {
                cpu_.state().vstimecmp = wdata;
            }
            cpu_.evaluate_timer_interrupt();
            break;
        case csr_addr(Csr::Vstimecmph):
            return require_rv32_counter().and_then(
                [this, wdata]() -> std::expected<void, ExceptionCode> {
                    cpu_.state().vstimecmp = (cpu_.state().vstimecmp & 0x00000000FFFFFFFFULL) |
                                             (static_cast<uint64_t>(wdata) << 32U);
                    cpu_.evaluate_timer_interrupt();
                    return {};
                });
        case csr_addr(Csr::Stimecmph):
            if (cpu_.state().priv < simrv::xlen::PrivilegeLevel::Machine) {
                const bool stce = (cpu_.state().menvcfgh & (1UL << 31U)) != 0;
                if (!stce) {
                    return std::unexpected(ExceptionCode::IllegalInstruction);
                }
            }
            return require_rv32_counter().and_then(
                [this, wdata]() -> std::expected<void, ExceptionCode> {
                    cpu_.state().stimecmp = (cpu_.state().stimecmp & 0x00000000FFFFFFFFULL) |
                                            (static_cast<uint64_t>(wdata) << 32);
                    cpu_.evaluate_timer_interrupt();
                    return {};
                });

        case csr_addr(Csr::Mtvec):
            cpu_.state().mtvec = wdata & ~static_cast<simrv::xlen::CsrValue>(2);
            break;
        case csr_addr(Csr::Mcounteren):
            cpu_.state().mcounteren = wdata & kCounterEnableMask;
            break;
        case csr_addr(Csr::Hcounteren):
            cpu_.state().hcounteren = wdata & kCounterEnableMask;
            break;
        case csr_addr(Csr::Htimedelta):
            cpu_.state().htimedelta = wdata;
            break;
        case csr_addr(Csr::Hgeie):
            cpu_.state().hgeie = wdata & kHgeieWritableMask;
            break;
        case csr_addr(Csr::Mcountinhibit):
            cpu_.state().mcountinhibit = wdata & ~static_cast<simrv::xlen::CsrValue>(2);
            break;
        case csr_addr(Csr::Menvcfg):
            // CBIE=2 is reserved; choose the legal zero encoding.
            if (((wdata >> 4U) & 0x3U) == 0x2U) {
                wdata &= ~(simrv::xlen::CsrValue{0x3} << 4U);
            }
            if constexpr (simrv::xlen::kIsXLen64) {
                constexpr uint64_t kPmmMask = uint64_t{0x3} << 32U;
                wdata =
                    static_cast<simrv::xlen::CsrValue>(static_cast<uint64_t>(wdata) & ~kPmmMask);
            }
            // Svadu is not implemented, so ADUE is read-only zero and the
            // mandatory Svade page-fault scheme remains active.
            wdata = static_cast<simrv::xlen::CsrValue>(static_cast<uint64_t>(wdata) &
                                                       ~(uint64_t{1} << 61U));
            cpu_.state().menvcfg = wdata;
            if ((wdata & static_cast<simrv::xlen::CsrValue>(uint64_t{1} << 63U)) == 0) {
                cpu_.state().henvcfg &= ~static_cast<simrv::xlen::CsrValue>(uint64_t{1} << 63U);
            }
            cpu_.evaluate_timer_interrupt();
            break;
        case csr_addr(Csr::Menvcfgh):
            return require_rv32_counter().and_then(
                [this, wdata]() -> std::expected<void, ExceptionCode> {
                    cpu_.state().menvcfgh = wdata & ~(simrv::xlen::CsrValue{1} << 29U);
                    cpu_.evaluate_timer_interrupt();
                    return {};
                });
        case csr_addr(Csr::Mseccfg): {
            const auto sticky = cpu_.state().mseccfg & static_cast<simrv::xlen::CsrValue>(0x7u);
            cpu_.state().mseccfg = wdata | sticky;
            cpu_.state().refresh_pmp_status();
            cpu_.TLB_flush();
            break;
        }
        case csr_addr(Csr::Mseccfgh):
            return require_rv32_counter().and_then(
                [this, wdata]() -> std::expected<void, ExceptionCode> {
                    cpu_.state().mseccfgh = wdata;
                    cpu_.state().refresh_pmp_status();
                    cpu_.TLB_flush();
                    return {};
                });
        case csr_addr(Csr::Mscratch):
            cpu_.state().mscratch = wdata;
            break;
        case csr_addr(Csr::Mepc):
            cpu_.state().mepc = wdata & ~1;
            break;
        case csr_addr(Csr::Mcause):
            cpu_.state().mcause = cause_write_value(wdata, cpu_.state().regs.xlen);
            break;
        case csr_addr(Csr::Mtval):
            cpu_.state().mtval = wdata;
            break;
        case csr_addr(Csr::Mtinst):
            cpu_.state().mtinst = wdata;
            break;
        case csr_addr(Csr::Mtval2):
            cpu_.state().mtval2 = wdata;
            break;

        case csr_addr(Csr::Sie):
            if (cpu_.state().virt) {
                const auto mask = h_interrupts_to_vs(cpu_.state().hideleg);
                cpu_.state().vsie = (cpu_.state().vsie & ~mask) | (wdata & mask);
            } else {
                const auto mask = cpu_.state().mideleg & kSupervisorInterruptEnableMask;
                cpu_.state().mie = (cpu_.state().mie & ~mask) | (wdata & mask);
            }
            break;
        case csr_addr(Csr::Sip): {
            if (cpu_.state().virt) {
                const auto mask = h_interrupts_to_vs(cpu_.state().hideleg) & kVsSoftwareInterrupt;
                cpu_.state().vsip = (cpu_.state().vsip & ~mask) | (wdata & mask);
                break;
            }
            const simrv::xlen::CsrValue pending_before = cpu_.state().mip;
            const simrv::xlen::CsrValue mask =
                (MipBit::Ssip | MipBit::Lcofip) & cpu_.state().mideleg;
            cpu_.state().mip = (cpu_.state().mip & ~mask) | (wdata & mask);
            log_pending_changes(pending_before, "sip_csr");
            break;
        }
        case csr_addr(Csr::Medeleg):
            cpu_.state().medeleg = wdata & kMedelegWritableMask;
            if (isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::H)) {
                // S-mode ECALL must remain in M-mode for the SBI/T-SBI boundary.
                // H adds mandatory delegation for virtual-instruction and guest-page faults.
                cpu_.state().medeleg |=
                    (simrv::xlen::CsrValue{1} << 20U) | (simrv::xlen::CsrValue{1} << 21U) |
                    (simrv::xlen::CsrValue{1} << 22U) | (simrv::xlen::CsrValue{1} << 23U);
            }
            break;
        case csr_addr(Csr::Mideleg):
            cpu_.state().mideleg = wdata & mideleg_writable_mask(has_s);
            if (isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::H)) {
                cpu_.state().mideleg |= kVsInterruptBits;
            }
            break;
        case csr_addr(Csr::Mie):
            cpu_.state().mie = wdata & interrupt_mask;
            break;
        case csr_addr(Csr::Mip): {
            const simrv::xlen::CsrValue pending_before = cpu_.state().mip;
            const simrv::xlen::CsrValue writable = mip_writable_mask(has_s, has_h);
            const simrv::xlen::CsrValue sourced = MipBit::Seip | MipBit::Stip;
            const simrv::xlen::CsrValue ordinary = writable & ~sourced;
            cpu_.state().mip = (cpu_.state().mip & ~ordinary) | (wdata & ordinary);
            if (has_h) {
                cpu_.state().hvip = (cpu_.state().hvip & ~std::to_underlying(MipBit::Hsip)) |
                                    (wdata & std::to_underlying(MipBit::Hsip));
            }
            cpu_.state().seip_software = (wdata & writable & MipBit::Seip) != 0;
            cpu_.state().stip_software = (wdata & writable & MipBit::Stip) != 0;
            cpu_.state().refresh_supervisor_pending();
            log_pending_changes(pending_before, "mip_csr");
            break;
        }

        case csr_addr(Csr::Satp): {
            const unsigned xlen = cpu_.state().regs.xlen;
            const simrv::xlen::Word mode = simrv::xlen::satp_mode(wdata, xlen);
            if (simrv::xlen::satp_mode_supported(mode, xlen)) {
                if (cpu_.state().virt) {
                    if (cpu_.state().vsatp != wdata) {
                        cpu_.state().vsatp = wdata;
                        cpu_.TLB_flush();
                    }
                    break;
                }
                if (cpu_.state().satp != wdata) {
                    cpu_.state().satp = wdata;
                    cpu_.TLB_flush_satp();
                }
                // Latch: once translation is enabled it is never "un-seen"
                if (mode != 0) {
                    cpu_.machine_->s_mmu_ever_used = true;
                }
            }
            break;
        }

        case csr_addr(Csr::Mstatus):
            setMstatus(wdata);
            break;
        case csr_addr(Csr::Sstatus):
            if (cpu_.state().virt) {
                cpu_.state().vsstatus =
                    (cpu_.state().vsstatus & ~kSstatusMask) | (wdata & kSstatusMask);
            } else {
                setMstatus((cpu_.state().mstatus & ~kSstatusMask) | (wdata & kSstatusMask));
            }
            break;

        case csr_addr(Csr::Tselect):
            cpu_.state().tselect = wdata;
            break;
        case csr_addr(Csr::Tdata1):
            if (cpu_.state().tselect < ArchState::kNumTriggers) {
                const unsigned xlen = cpu_.state().regs.xlen;
                const auto type = static_cast<uint32_t>(
                    (static_cast<uint64_t>(wdata) >> (xlen == 64 ? 60U : 28U)) & 0xFU);
                if (type == 2) {
                    constexpr simrv::xlen::CsrValue mask = (1U << 6U) | (1U << 4U) | (1U << 3U) |
                                                           (1U << 2U) | (1U << 1U) | (1U << 0U) |
                                                           (1U << 19U);
                    const auto type_hdr =
                        static_cast<simrv::xlen::CsrValue>(uint64_t{2} << (xlen == 64 ? 60U : 28U));
                    cpu_.state().tdata1[cpu_.state().tselect] = type_hdr | (wdata & mask);
                } else if (type == 0) {
                    cpu_.state().tdata1[cpu_.state().tselect] = 0;
                }
                cpu_.state().refresh_trigger_status();
            }
            break;
        case csr_addr(Csr::Tdata2):
            if (cpu_.state().tselect < ArchState::kNumTriggers) {
                cpu_.state().tdata2[cpu_.state().tselect] = wdata;
            }
            break;
        case csr_addr(Csr::Tdata3):
            if (cpu_.state().tselect < ArchState::kNumTriggers) {
                cpu_.state().tdata3[cpu_.state().tselect] = wdata;
            }
            break;
        case csr_addr(Csr::Tinfo):
        case csr_addr(Csr::Tcontrol):
        case csr_addr(Csr::Mcontext):
        case csr_addr(Csr::Scontext):
            break;
        default:
            if (pmp_csr_exists(addr, cpu_.state().regs.xlen)) {
                if (addr.value() >= 0x3A0 && addr.value() <= 0x3AF) {
                    const size_t cfg_idx = addr.value() - 0x3A0;
                    unpack_pmpcfg(cpu_.state().pmpcfg, cfg_idx, wdata, cpu_.state().regs.xlen);
                } else if (addr.value() >= 0x3B0 && addr.value() <= 0x3EF) {
                    const size_t pmp_idx = addr.value() - 0x3B0;
                    if (pmp_idx < ArchState::kNumPmpEntries) {
                        const bool self_locked = (cpu_.state().pmpcfg[pmp_idx] & 0x80) != 0;
                        const bool next_locked_tor =
                            (pmp_idx + 1 < ArchState::kNumPmpEntries) &&
                            ((cpu_.state().pmpcfg[pmp_idx + 1] & 0x80) != 0) &&
                            ((cpu_.state().pmpcfg[pmp_idx + 1] & 0x18) == 0x08);
                        if (!self_locked && !next_locked_tor) {
                            cpu_.state().pmpaddr[pmp_idx] =
                                static_cast<simrv::xlen::Address>(wdata);
                        }
                    }
                }
                cpu_.state().refresh_pmp_status();
                cpu_.TLB_flush();
                break;
            }
            const auto a = addr.value();
            if (a >= 0x323U && a <= 0x33FU) {
                const auto idx = a - 0x320U;
                const auto value =
                    cpu_.state().regs.xlen == 32U
                        ? ((cpu_.perf_counters.read_hpmevent(idx) & 0xFFFFFFFF00000000ULL) |
                           static_cast<uint32_t>(wdata))
                        : static_cast<uint64_t>(wdata);
                cpu_.perf_counters.write_hpmevent(
                    idx, value, isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::H));
                break;
            }
            if (cpu_.state().regs.xlen == 32U && a >= 0x723U && a <= 0x73FU) {
                const auto idx = a - 0x720U;
                const auto value =
                    (cpu_.perf_counters.read_hpmevent(idx) & 0x00000000FFFFFFFFULL) |
                    (static_cast<uint64_t>(static_cast<uint32_t>(wdata)) << kHighWordShift);
                cpu_.perf_counters.write_hpmevent(
                    idx, value, isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::H));
                break;
            }
            if (a >= 0xB03U && a <= 0xB1FU) {
                const auto idx = a - 0xB00U;
                if constexpr (simrv::xlen::kIsXLen64) {
                    if (cpu_.state().regs.xlen == 64U) {
                        cpu_.perf_counters.write_hpmcounter(idx, static_cast<uint64_t>(wdata));
                    } else {
                        const auto cur = cpu_.perf_counters.read_hpmcounter(idx);
                        cpu_.perf_counters.write_hpmcounter(
                            idx, (cur & 0xFFFFFFFF00000000ULL) | static_cast<uint32_t>(wdata));
                    }
                } else {
                    const auto cur = cpu_.perf_counters.read_hpmcounter(idx);
                    cpu_.perf_counters.write_hpmcounter(
                        idx, (cur & 0xFFFFFFFF00000000ULL) | static_cast<uint32_t>(wdata));
                }
                break;
            }
            if (cpu_.state().regs.xlen == 32U && a >= 0xB83U && a <= 0xB9FU) {
                return require_rv32_counter().and_then(
                    [this, a, wdata]() -> std::expected<void, ExceptionCode> {
                        const auto idx = a - 0xB80U;
                        const auto cur = cpu_.perf_counters.read_hpmcounter(idx);
                        cpu_.perf_counters.write_hpmcounter(
                            idx, (cur & 0x00000000FFFFFFFFULL) |
                                     (static_cast<uint64_t>(wdata) << kHighWordShift));
                        return {};
                    });
            }
            return std::unexpected(ExceptionCode::IllegalInstruction);
    }
    return {};
}

}  // namespace simrv::core
