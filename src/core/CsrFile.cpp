/**
 * @file CsrFile.cpp
 * @brief Control and Status Register (CSR) file implementation.
 */
#include "simrv/core/CsrFile.hpp"

#include <array>
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
constexpr simrv::xlen::CsrValue kCounterEnableMask = static_cast<simrv::xlen::CsrValue>(0x7u);

[[nodiscard]] constexpr auto fp_accessible(const ArchState& state) noexcept -> bool {
    return isa::misa_has_extension(state.misa, isa::IsaExtension::F) &&
           (state.mstatus & MstatusBit::Fs) != 0;
}

[[nodiscard]] constexpr auto vector_accessible(const ArchState& state) noexcept -> bool {
    return isa::misa_has_extension(state.misa, isa::IsaExtension::V) &&
           (state.mstatus & MstatusBit::Vs) != 0;
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

auto CsrFile::getMstatus(simrv::xlen::CsrValue mask) const -> simrv::xlen::CsrValue {
    const simrv::xlen::CsrValue misa = cpu_.state().misa;
    const simrv::xlen::CsrValue implemented =
        mstatus_writable_mask(isa::misa_has_extension(misa, isa::IsaExtension::S),
                              isa::misa_has_extension(misa, isa::IsaExtension::U),
                              isa::misa_has_extension(misa, isa::IsaExtension::F),
                              isa::misa_has_extension(misa, isa::IsaExtension::V));
    return mstatus_read_value(cpu_.state().mstatus, mask & (implemented | kMstatusSd),
                              cpu_.state().regs.xlen);
}

void CsrFile::setMstatus(simrv::xlen::CsrValue wdata) {
    const simrv::xlen::CsrValue misa = cpu_.state().misa;
    const bool has_s = isa::misa_has_extension(misa, isa::IsaExtension::S);
    const bool has_u = isa::misa_has_extension(misa, isa::IsaExtension::U);
    const bool has_f = isa::misa_has_extension(misa, isa::IsaExtension::F);
    const bool has_v = isa::misa_has_extension(misa, isa::IsaExtension::V);
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
    const simrv::xlen::CsrValue mask = mstatus_writable_mask(has_s, has_u, has_f, has_v);
    cpu_.state().mstatus = (cpu_.state().mstatus & ~mask) | (wdata & mask);
    // Read-only-zero fields must not retain reset/profile state internally,
    // otherwise SD and effective-XLEN calculations could observe absent state.
    cpu_.state().mstatus &= mask;
    cpu_.state().update_xlen();
    cpu_.sync_decode_context();
}

auto CsrFile::read(simrv::xlen::CsrNumber addr) const
    -> std::expected<simrv::xlen::CsrValue, ExceptionCode> {
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

    simrv::xlen::CsrValue rcsr = 0;
    switch (addr.value()) {
        case csr_addr(Csr::Fflags):
            return require_fp().transform([this]() { return cpu_.state().fcsr & kFflagsMask; });
        case csr_addr(Csr::Frm):
            return require_fp().transform(
                [this]() { return (cpu_.state().fcsr >> kFrmShift) & kFrmMask; });
        case csr_addr(Csr::Fcsr):
            return require_fp().transform([this]() { return cpu_.state().fcsr & kFcsrMask; });
        case csr_addr(Csr::Vstart):
            return require_vector().transform([this]() { return cpu_.state().vstart; });
        case csr_addr(Csr::Vxsat):
            return require_vector().transform([this]() { return cpu_.state().vxsat; });
        case csr_addr(Csr::Vxrm):
            return require_vector().transform([this]() { return cpu_.state().vxrm; });
        case csr_addr(Csr::Vcsr):
            return require_vector().transform(
                [this]() { return (cpu_.state().vxrm << 1) | cpu_.state().vxsat; });
        case csr_addr(Csr::Vl):
            return require_vector().transform([this]() { return cpu_.state().vl; });
        case csr_addr(Csr::Vtype):
            return require_vector().transform([this]() { return cpu_.state().vtype; });
        case csr_addr(Csr::Vlenb):
            // vlenb is the read-only architectural VLEN/8 constant for the current hart.
            return require_vector().transform([this]() {
                return static_cast<simrv::xlen::CsrValue>(cpu_.state().regs.vlen_bytes());
            });

        case csr_addr(Csr::Sie):
            rcsr = cpu_.state().mie & cpu_.state().mideleg & interrupt_implemented_mask(true);
            break;
        case csr_addr(Csr::Stvec):
            rcsr = cpu_.state().stvec;
            break;
        case csr_addr(Csr::Scounteren):
            rcsr = cpu_.state().scounteren;
            break;
        case csr_addr(Csr::Scountovf): {
            if (!isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::S)) {
                return std::unexpected(ExceptionCode::IllegalInstruction);
            }
            uint32_t overflow = cpu_.perf_counters.read_overflow_mask();
            if (cpu_.state().priv != simrv::xlen::PrivilegeLevel::Machine) {
                for (size_t counter = 3; counter < PerformanceCounters::kNumHpmCounters;
                     ++counter) {
                    if ((cpu_.state().mcounteren & (simrv::xlen::CsrValue{1} << counter)) == 0) {
                        overflow &= ~(uint32_t{1} << counter);
                    }
                }
            }
            rcsr = overflow;
            break;
        }
        case csr_addr(Csr::Sscratch):
            rcsr = cpu_.state().sscratch;
            break;
        case csr_addr(Csr::Sepc):
            rcsr = isa::epc_read_value(cpu_.state().sepc, cpu_.state().misa);
            break;
        case csr_addr(Csr::Scause):
            rcsr = cause_read_value(cpu_.state().scause, cpu_.state().regs.xlen);
            break;
        case csr_addr(Csr::Stval):
            rcsr = cpu_.state().stval;
            break;
        case csr_addr(Csr::Sip):
            rcsr = cpu_.state().mip & cpu_.state().mideleg & interrupt_implemented_mask(true);
            break;
        case csr_addr(Csr::Satp):
            rcsr = cpu_.state().satp;
            break;
        case csr_addr(Csr::Senvcfg):
            rcsr = cpu_.state().senvcfg;
            break;
        case csr_addr(Csr::Stimecmp):
            if (cpu_.state().priv < simrv::xlen::PrivilegeLevel::Machine) {
                bool stce = false;
                if constexpr (simrv::xlen::kIsXLen64) {
                    stce = (cpu_.state().menvcfg & (1ULL << 63U)) != 0;
                } else {
                    stce = (cpu_.state().menvcfgh & (1UL << 31U)) != 0;
                }
                if (!stce) {
                    return std::unexpected(ExceptionCode::IllegalInstruction);
                }
            }
            if constexpr (simrv::xlen::kIsXLen64) {
                rcsr = static_cast<simrv::xlen::CsrValue>(cpu_.state().stimecmp);
            } else {
                rcsr = static_cast<simrv::xlen::CsrValue>(cpu_.state().stimecmp & 0xFFFFFFFFULL);
            }
            break;
        case csr_addr(Csr::Stimecmph):
            if (cpu_.state().priv < simrv::xlen::PrivilegeLevel::Machine) {
                const bool stce = (cpu_.state().menvcfgh & (1UL << 31U)) != 0;
                if (!stce) {
                    return std::unexpected(ExceptionCode::IllegalInstruction);
                }
            }
            return require_rv32_counter().transform([this]() {
                return static_cast<simrv::xlen::CsrValue>(cpu_.state().stimecmp >> 32);
            });

        case csr_addr(Csr::Medeleg):
            rcsr = cpu_.state().medeleg;
            break;
        case csr_addr(Csr::Mideleg):
            rcsr = cpu_.state().mideleg & mideleg_writable_mask(true);
            break;
        case csr_addr(Csr::Mie):
            rcsr = cpu_.state().mie & interrupt_implemented_mask(isa::misa_has_extension(
                                          cpu_.state().misa, isa::IsaExtension::S));
            break;
        case csr_addr(Csr::Mtvec):
            rcsr = cpu_.state().mtvec;
            break;
        case csr_addr(Csr::Mcounteren):
            rcsr = cpu_.state().mcounteren;
            break;
        case csr_addr(Csr::Mcountinhibit):
            rcsr = cpu_.state().mcountinhibit;
            break;
        case csr_addr(Csr::Menvcfg):
            rcsr = cpu_.state().menvcfg;
            break;
        case csr_addr(Csr::Menvcfgh):
            return require_rv32_counter().transform([this]() { return cpu_.state().menvcfgh; });
        case csr_addr(Csr::Mseccfg):
            rcsr = cpu_.state().mseccfg;
            break;
        case csr_addr(Csr::Mseccfgh):
            return require_rv32_counter().transform([this]() { return cpu_.state().mseccfgh; });
        case csr_addr(Csr::Mscratch):
            rcsr = cpu_.state().mscratch;
            break;
        case csr_addr(Csr::Mepc):
            rcsr = isa::epc_read_value(cpu_.state().mepc, cpu_.state().misa);
            break;
        case csr_addr(Csr::Mcause):
            rcsr = cause_read_value(cpu_.state().mcause, cpu_.state().regs.xlen);
            break;
        case csr_addr(Csr::Mtval):
            rcsr = cpu_.state().mtval;
            break;
        case csr_addr(Csr::Mip):
            rcsr = cpu_.state().mip & interrupt_implemented_mask(isa::misa_has_extension(
                                          cpu_.state().misa, isa::IsaExtension::S));
            break;
        case csr_addr(Csr::Misa):
            rcsr = isa::misa_with_mxl(cpu_.state().misa, cpu_.state().regs.xlen);
            break;

        case csr_addr(Csr::Mcycle):
        case csr_addr(Csr::Cycle):
            rcsr = static_cast<simrv::xlen::CsrValue>(cpu_.clint_mmio.mcycle);
            break;
        case csr_addr(Csr::Minstret):
        case csr_addr(Csr::Instret):
            rcsr = static_cast<simrv::xlen::CsrValue>(cpu_.state().minstret);
            break;
        case csr_addr(Csr::Time):
            rcsr = static_cast<simrv::xlen::CsrValue>(cpu_.clint_mmio.mtime);
            break;

        case csr_addr(Csr::Mcycleh):
        case csr_addr(Csr::Cycleh):
            return require_rv32_counter().transform([this]() {
                return static_cast<simrv::xlen::CsrValue>(cpu_.clint_mmio.mcycle >> kHighWordShift);
            });
        case csr_addr(Csr::Minstreth):
        case csr_addr(Csr::Instreth):
            return require_rv32_counter().transform([this]() {
                return static_cast<simrv::xlen::CsrValue>(cpu_.state().minstret >> kHighWordShift);
            });
        case csr_addr(Csr::Timeh):
            return require_rv32_counter().transform([this]() {
                return static_cast<simrv::xlen::CsrValue>(cpu_.clint_mmio.mtime >> kHighWordShift);
            });

        case csr_addr(Csr::Sstatus):
            rcsr = getMstatus(kMstatusSstatusReadMask);
            break;
        case csr_addr(Csr::Mstatus):
            rcsr = getMstatus(kMstatusReadMask);
            break;

        case csr_addr(Csr::Mvendorid):
        case csr_addr(Csr::Marchid):
        case csr_addr(Csr::Mimpid):
        case csr_addr(Csr::Mconfigptr):
            rcsr = 0;
            break;

        case csr_addr(Csr::Mhartid):
            rcsr = cpu_.state().mhartid;
            break;

        case csr_addr(Csr::Tselect):
            rcsr = cpu_.state().tselect;
            break;
        case csr_addr(Csr::Tdata1):
            if (cpu_.state().tselect < ArchState::kNumTriggers) {
                rcsr = cpu_.state().tdata1[cpu_.state().tselect];
            } else {
                rcsr = 0;
            }
            break;
        case csr_addr(Csr::Tdata2):
            if (cpu_.state().tselect < ArchState::kNumTriggers) {
                rcsr = cpu_.state().tdata2[cpu_.state().tselect];
            } else {
                rcsr = 0;
            }
            break;
        case csr_addr(Csr::Tdata3):
            if (cpu_.state().tselect < ArchState::kNumTriggers) {
                rcsr = cpu_.state().tdata3[cpu_.state().tselect];
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
            if (pmp_csr_exists(addr, cpu_.state().regs.xlen)) {
                if (addr.value() >= 0x3A0 && addr.value() <= 0x3AF) {
                    const size_t cfg_idx = addr.value() - 0x3A0;
                    rcsr = pack_pmpcfg(cpu_.state().pmpcfg, cfg_idx, cpu_.state().regs.xlen);
                } else if (addr.value() >= 0x3B0 && addr.value() <= 0x3EF) {
                    const size_t pmp_idx = addr.value() - 0x3B0;
                    if (pmp_idx < ArchState::kNumPmpEntries) {
                        rcsr = static_cast<simrv::xlen::CsrValue>(cpu_.state().pmpaddr[pmp_idx]);
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
                const auto value = cpu_.perf_counters.read_hpmevent(a - 0x320U);
                rcsr = static_cast<simrv::xlen::CsrValue>(value);
                break;
            }
            if (cpu_.state().regs.xlen == 32U && a >= 0x723U && a <= 0x73FU) {
                rcsr = static_cast<simrv::xlen::CsrValue>(static_cast<uint32_t>(
                    cpu_.perf_counters.read_hpmevent(a - 0x720U) >> kHighWordShift));
                break;
            }
            if (a >= 0xB03U && a <= 0xB1FU) {
                rcsr = static_cast<simrv::xlen::CsrValue>(
                    cpu_.perf_counters.read_hpmcounter(a - 0xB00U));
                break;
            }
            if (a >= 0xC03U && a <= 0xC1FU) {
                rcsr = static_cast<simrv::xlen::CsrValue>(
                    cpu_.perf_counters.read_hpmcounter(a - 0xC00U));
                break;
            }
            if (cpu_.state().regs.xlen == 32U) {
                if (a >= 0xB83U && a <= 0xB9FU) {
                    return require_rv32_counter().transform([this, a]() {
                        return static_cast<simrv::xlen::CsrValue>(
                            cpu_.perf_counters.read_hpmcounter(a - 0xB80U) >> kHighWordShift);
                    });
                }
                if (a >= 0xC83U && a <= 0xC9FU) {
                    return require_rv32_counter().transform([this, a]() {
                        return static_cast<simrv::xlen::CsrValue>(
                            cpu_.perf_counters.read_hpmcounter(a - 0xC80U) >> kHighWordShift);
                    });
                }
            }
            return std::unexpected(ExceptionCode::IllegalInstruction);
    }
    return rcsr;
}

auto CsrFile::write(simrv::xlen::CsrNumber addr, simrv::xlen::CsrValue wdata)
    -> std::expected<void, ExceptionCode> {  // NOLINT(bugprone-easily-swappable-parameters)
    const bool has_s = isa::misa_has_extension(cpu_.state().misa, isa::IsaExtension::S);
    const simrv::xlen::CsrValue interrupt_mask = interrupt_implemented_mask(has_s);
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
            return require_rv32_counter().and_then([this,
                                                    wdata]() -> std::expected<void, ExceptionCode> {
                cpu_.state().minstret =
                    (cpu_.state().minstret & 0x00000000FFFFFFFFULL) | (static_cast<uint64_t>(wdata) << 32);
                return {};
            });

        case csr_addr(Csr::Fflags):
            return require_fp().and_then([this, wdata]() -> std::expected<void, ExceptionCode> {
                cpu_.state().fcsr = (cpu_.state().fcsr & ~kFflagsMask) | (wdata & kFflagsMask);
                cpu_.state().mstatus |= MstatusBit::Fs;
#if defined(__x86_64__) || defined(_M_X64)
                _mm_setcsr(_mm_getcsr() & ~0x3fu);
#endif
                return {};
            });
        case csr_addr(Csr::Frm):
            return require_fp().and_then([this, wdata]() -> std::expected<void, ExceptionCode> {
                cpu_.state().fcsr = (cpu_.state().fcsr & ~(kFrmMask << kFrmShift)) |
                                    ((wdata & kFrmMask) << kFrmShift);
                cpu_.state().mstatus |= MstatusBit::Fs;
                return {};
            });
        case csr_addr(Csr::Fcsr):
            return require_fp().and_then([this, wdata]() -> std::expected<void, ExceptionCode> {
                cpu_.state().fcsr = wdata & kFcsrMask;
                cpu_.state().mstatus |= MstatusBit::Fs;
#if defined(__x86_64__) || defined(_M_X64)
                _mm_setcsr(_mm_getcsr() & ~0x3fu);
#endif
                return {};
            });
        case csr_addr(Csr::Vstart):
            return require_vector().and_then([this, wdata]() -> std::expected<void, ExceptionCode> {
                cpu_.state().vstart = wdata & cpu_.state().regs.vstart_mask();
                cpu_.state().mstatus |= MstatusBit::Vs;
                return {};
            });
        case csr_addr(Csr::Vxsat):
            return require_vector().and_then([this, wdata]() -> std::expected<void, ExceptionCode> {
                cpu_.state().vxsat = wdata & 1;
                cpu_.state().mstatus |= MstatusBit::Vs;
                return {};
            });
        case csr_addr(Csr::Vxrm):
            return require_vector().and_then([this, wdata]() -> std::expected<void, ExceptionCode> {
                cpu_.state().vxrm = wdata & 3;
                cpu_.state().mstatus |= MstatusBit::Vs;
                return {};
            });
        case csr_addr(Csr::Vcsr):
            return require_vector().and_then([this, wdata]() -> std::expected<void, ExceptionCode> {
                cpu_.state().vxrm = (wdata >> 1) & 3;
                cpu_.state().vxsat = wdata & 1;
                cpu_.state().mstatus |= MstatusBit::Vs;
                return {};
            });
        case csr_addr(Csr::Vl):
            return require_vector().and_then([this, wdata]() -> std::expected<void, ExceptionCode> {
                cpu_.state().vl = wdata;
                cpu_.state().mstatus |= MstatusBit::Vs;
                return {};
            });
        case csr_addr(Csr::Vtype):
            return require_vector().and_then([this, wdata]() -> std::expected<void, ExceptionCode> {
                cpu_.state().vtype = wdata;
                cpu_.state().mstatus |= MstatusBit::Vs;
                return {};
            });

        case csr_addr(Csr::Stvec):
            cpu_.state().stvec = wdata & ~static_cast<simrv::xlen::CsrValue>(2);
            break;
        case csr_addr(Csr::Scounteren):
            cpu_.state().scounteren = wdata & kCounterEnableMask;
            break;
        case csr_addr(Csr::Sscratch):
            cpu_.state().sscratch = wdata;
            break;
        case csr_addr(Csr::Sepc):
            cpu_.state().sepc = wdata & ~1;
            break;
        case csr_addr(Csr::Scause):
            cpu_.state().scause = cause_write_value(wdata, cpu_.state().regs.xlen);
            break;
        case csr_addr(Csr::Stval):
            cpu_.state().stval = wdata;
            break;
        case csr_addr(Csr::Senvcfg):
            cpu_.state().senvcfg = wdata;
            break;
        case csr_addr(Csr::Stimecmp):
            if (cpu_.state().priv < simrv::xlen::PrivilegeLevel::Machine) {
                bool stce = false;
                if constexpr (simrv::xlen::kIsXLen64) {
                    stce = (cpu_.state().menvcfg & (1ULL << 63U)) != 0;
                } else {
                    stce = (cpu_.state().menvcfgh & (1UL << 31U)) != 0;
                }
                if (!stce) {
                    return std::unexpected(ExceptionCode::IllegalInstruction);
                }
            }
            if (cpu_.state().regs.xlen == 32) {
                cpu_.state().stimecmp =
                    (cpu_.state().stimecmp & 0xFFFFFFFF00000000ULL) | static_cast<uint32_t>(wdata);
            } else {
                cpu_.state().stimecmp = wdata;
            }
            cpu_.evaluate_timer_interrupt();
            break;
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
        case csr_addr(Csr::Mcountinhibit):
            cpu_.state().mcountinhibit = wdata & ~static_cast<simrv::xlen::CsrValue>(2);
            break;
        case csr_addr(Csr::Menvcfg):
            cpu_.state().menvcfg = wdata;
            cpu_.evaluate_timer_interrupt();
            break;
        case csr_addr(Csr::Menvcfgh):
            return require_rv32_counter().and_then(
                [this, wdata]() -> std::expected<void, ExceptionCode> {
                    cpu_.state().menvcfgh = wdata;
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

        case csr_addr(Csr::Sie):
            cpu_.state().mie =
                (cpu_.state().mie & ~cpu_.state().mideleg) | (wdata & cpu_.state().mideleg);
            break;
        case csr_addr(Csr::Sip): {
            const simrv::xlen::CsrValue pending_before = cpu_.state().mip;
            const simrv::xlen::CsrValue mask =
                (MipBit::Ssip | MipBit::Lcofip) & cpu_.state().mideleg;
            cpu_.state().mip = (cpu_.state().mip & ~mask) | (wdata & mask);
            log_pending_changes(pending_before, "sip_csr");
            break;
        }
        case csr_addr(Csr::Medeleg):
            cpu_.state().medeleg = wdata & kMedelegWritableMask;
            break;
        case csr_addr(Csr::Mideleg):
            cpu_.state().mideleg = wdata & mideleg_writable_mask(has_s);
            break;
        case csr_addr(Csr::Mie):
            cpu_.state().mie = wdata & interrupt_mask;
            break;
        case csr_addr(Csr::Mip): {
            const simrv::xlen::CsrValue pending_before = cpu_.state().mip;
            const simrv::xlen::CsrValue writable = mip_writable_mask(has_s);
            const simrv::xlen::CsrValue sourced = MipBit::Seip | MipBit::Stip;
            const simrv::xlen::CsrValue ordinary = writable & ~sourced;
            cpu_.state().mip = (cpu_.state().mip & ~ordinary) | (wdata & ordinary);
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
            setMstatus((cpu_.state().mstatus & ~kSstatusMask) | (wdata & kSstatusMask));
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
                cpu_.perf_counters.write_hpmevent(idx, value);
                break;
            }
            if (cpu_.state().regs.xlen == 32U && a >= 0x723U && a <= 0x73FU) {
                const auto idx = a - 0x720U;
                const auto value =
                    (cpu_.perf_counters.read_hpmevent(idx) & 0x00000000FFFFFFFFULL) |
                    (static_cast<uint64_t>(static_cast<uint32_t>(wdata)) << kHighWordShift);
                cpu_.perf_counters.write_hpmevent(idx, value);
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
