/**
 * @file Cpu.hpp
 * @brief CPU core state and control declarations.
 */
#pragma once

#include <array>
#include <atomic>
#include <expected>
#include <iosfwd>
#include <optional>
#include <string>

#include "simrv/CompilerHints.hpp"
#include "simrv/cache/DCache.hpp"
#include "simrv/cache/ICache.hpp"
#include "simrv/cache/TimingCache.hpp"
#include "simrv/core/CsrFile.hpp"
#include "simrv/core/CsrTypes.hpp"
#include "simrv/core/DecodeCache.hpp"
#include "simrv/core/FastBatchPolicy.hpp"
#include "simrv/core/PerformanceCounters.hpp"
#include "simrv/core/RegisterFile.hpp"
#include "simrv/core/Sbi.hpp"
#include "simrv/core/Tlb.hpp"
#include "simrv/core/TlbTypes.hpp"
#include "simrv/core/TrapController.hpp"
#include "simrv/device/Clint.hpp"
#include "simrv/device/Plic.hpp"
#include "simrv/execute/CfuUnit.hpp"
#include "simrv/execute/ExecuteUnit.hpp"
#include "simrv/isa/InstructionFields.hpp"
#include "simrv/isa/IsaConfig.hpp"
#include "simrv/isa/IsaTypes.hpp"
#include "simrv/isa/OperationId.hpp"
#include "simrv/memory/Bus.hpp"
#include "simrv/memory/Mmu.hpp"
#include "simrv/memory/RamView.hpp"
#include "simrv/pipeline/CpuModel.hpp"
#include "simrv/pipeline/CycleTransition.hpp"
#include "simrv/pipeline/PipelineContext.hpp"
#include "simrv/pipeline/PipelineSim.hpp"
#include "simrv/pipeline/Scoreboard.hpp"
#include "simrv/xlen/IdentifierTypes.hpp"

namespace simrv::core {
class Machine;

/**
 * @struct ArchState
 * @brief Groups all architectural registers and CSRs into a cohesive block.
 */
struct alignas(128) ArchState {
    simrv::xlen::Register pc{};  ///< Program Counter (instruction address pointer)
    RegisterFile regs{};         ///< General-purpose integer, FP, and vector registers

    /* Machine Mode CSRs */
    simrv::xlen::CsrValue
        mstatus{};  ///< Machine Status (tracks processor mode, interrupt states, extension states)
    simrv::xlen::CsrValue
        mtvec{};  ///< Machine Trap-Vector Base-Address (address of machine mode trap handler)
    simrv::xlen::CsrValue
        mscratch{};  ///< Machine Scratch register (used for temporary context swapping)
    simrv::xlen::CsrValue mepc{};  ///< Machine Exception Program Counter (returns address on MRET)
    simrv::xlen::TrapCause
        mcause{};  ///< Machine Trap Cause (interrupt vs exception and exact code)
    simrv::xlen::CsrValue
        mtval{};  ///< Machine Trap Value (holds faulting addresses or instruction bytes)
    simrv::xlen::CsrValue mhartid{};  ///< Machine Hardware Thread ID (processor core identifier)
    simrv::xlen::CsrValue misa = isa::kMisaDefault;  ///< Machine ISA and extensions descriptor
    simrv::xlen::Word fetch_align_mask =
        (isa::kMisaDefault & (simrv::xlen::CsrValue{1} << 2)) != 0
            ? 1u
            : 3u;  ///< Cached fetch alignment mask: 1u for C-ext (2-byte align), 3u for non-C
    simrv::xlen::CsrValue mie{};  ///< Machine Interrupt Enable
    simrv::xlen::CsrValue mip{};  ///< Machine Interrupt Pending
    bool seip_software{};  ///< Software-writable component of mip.SEIP (ORed with PLIC signal).
    bool seip_external{};  ///< PLIC-driven component of mip.SEIP.
    bool stip_software{};  ///< M-mode software-posted component of writable mip.STIP.
    bool stip_timer{};     ///< Direct-SBI timer signal component of mip.STIP.
    bool stip_sstc{};      ///< Ratified Sstc stimecmp timer component of mip.STIP.
    simrv::xlen::CsrValue
        medeleg{};  ///< Machine Exception Delegation (delegates trap handling to supervisor mode)
    simrv::xlen::CsrValue
        mideleg{};  ///< Machine Interrupt Delegation (delegates interrupts to supervisor mode)
    simrv::xlen::CsrValue mcounteren{};     ///< Machine Counter Enable (controls user/supervisor
                                            ///< access to performance counters)
    simrv::xlen::CsrValue mcountinhibit{};  ///< Machine Counter Inhibit (mcountinhibit, 0x320)
    simrv::xlen::CsrValue menvcfg{};        ///< Machine Environment Configuration (menvcfg, 0x30A)
    simrv::xlen::CsrValue menvcfgh{};  ///< Machine Environment Configuration High (RV32, 0x31A)
    simrv::xlen::CsrValue mseccfg{};   ///< Machine Security Configuration (Smepmp, 0x747)
    simrv::xlen::CsrValue mseccfgh{};  ///< Machine Security Configuration High (RV32, 0x757)

    /* Supervisor Mode CSRs */
    simrv::xlen::CsrValue
        stvec{};  ///< Supervisor Trap-Vector Base-Address (address of supervisor trap handler)
    simrv::xlen::CsrValue sscratch{};  ///< Supervisor Scratch register
    simrv::xlen::CsrValue senvcfg{};   ///< Supervisor Environment Configuration (0x10A)
    simrv::xlen::TimeBaseTicks stimecmp{
        UINT64_MAX};  ///< Supervisor Timer Compare (Sstc, 0x14D / 0x15D)
    simrv::xlen::CsrValue
        sepc{};  ///< Supervisor Exception Program Counter (returns address on SRET)
    simrv::xlen::TrapCause scause{};     ///< Supervisor Trap Cause
    simrv::xlen::CsrValue stval{};       ///< Supervisor Trap Value
    simrv::xlen::CsrValue satp{};        ///< Supervisor Address Translation and Protection (virtual
                                         ///< memory mode and page table base physical address)
    simrv::xlen::CsrValue scounteren{};  ///< Supervisor Counter Enable

    /* Floating-Point CSRs */
    simrv::xlen::CsrValue fcsr{};  ///< Floating-Point Control and Status register (rounding mode
                                   ///< and cumulative exception flags)

    /* Vector Extension CSRs */
    simrv::xlen::CsrValue vstart{};  ///< Vector Start Index (specifies the register element index
                                     ///< to start vector execution)
    simrv::xlen::CsrValue vxsat{};   ///< Vector Fixed-Point Saturation flag
    simrv::xlen::CsrValue vxrm{};    ///< Vector Fixed-Point Rounding Mode selector
    simrv::xlen::CsrValue
        vl{};  ///< Vector Length (active number of elements to process in a vector instruction)
    simrv::xlen::CsrValue
        vtype{};  ///< Vector Type (contains element width VSEW, vector group multiplier LMUL, etc.)

    simrv::xlen::PrivilegeLevel priv = kPrivMachine;  ///< Current processor privilege level

    simrv::xlen::Address load_res{};     ///< Active memory physical address reservation for
                                         ///< Load-Reserved / Store-Conditional (LR/SC)
    bool lr_reservation_active = false;  ///< Active reservation flag for LR/SC

    /* Physical Memory Protection (PMP) */
    static constexpr size_t kNumPmpEntries = 16;
    std::array<uint8_t, kNumPmpEntries> pmpcfg{};
    std::array<simrv::xlen::Address, kNumPmpEntries> pmpaddr{};
    size_t num_active_pmp = 0;
    bool has_locked_pmp = false;

    /* Debug Triggers (Sdtrig) */
    static constexpr size_t kNumTriggers = 4;
    simrv::xlen::CsrValue tselect = 0;
    std::array<simrv::xlen::CsrValue, kNumTriggers> tdata1{};
    std::array<simrv::xlen::CsrValue, kNumTriggers> tdata2{};
    std::array<simrv::xlen::CsrValue, kNumTriggers> tdata3{};
    bool has_active_triggers = false;

    constexpr void refresh_trigger_status() noexcept {
        has_active_triggers = false;
        for (size_t i = 0; i < kNumTriggers; ++i) {
            if (tdata1[i] != 0) {
                has_active_triggers = true;
                break;
            }
        }
    }

    constexpr void refresh_misa_status() noexcept {
        fetch_align_mask = (misa & (simrv::xlen::CsrValue{1} << 2)) != 0 ? 1u : 3u;
    }

    constexpr void set_misa(simrv::xlen::CsrValue new_misa) noexcept {
        misa = new_misa;
        refresh_misa_status();
    }

    constexpr void refresh_pmp_status() {
        num_active_pmp = 0;
        has_locked_pmp = (mseccfg & static_cast<simrv::xlen::CsrValue>(0x3u)) != 0;
        for (size_t i = 0; i < kNumPmpEntries; ++i) {
            const uint8_t mode = pmpcfg[i] & 0x18;
            if (mode != 0) {
                num_active_pmp = i + 1;
            }
            if ((pmpcfg[i] & 0x80) != 0) {
                has_locked_pmp = true;
            }
        }
    }

    /** Recompute supervisor pending bits that combine independent software/device sources. */
    constexpr void refresh_supervisor_pending() {
        if (seip_software || seip_external)
            mip |= MipBit::Seip;
        else
            mip &= ~MipBit::Seip;
        if (stip_software || stip_timer || stip_sstc)
            mip |= MipBit::Stip;
        else
            mip &= ~MipBit::Stip;
    }

    /**
     * @brief Determines the active XLEN (register width in bits: 32 or 64) based on the CPU state.
     */
    [[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto xlen_for_privilege(
        simrv::xlen::PrivilegeLevel level) const noexcept -> unsigned {
        if constexpr (!simrv::xlen::kIsXLen64) {
            return 32;
        } else {
            const unsigned mxl = (misa >> 62) & 3;
            if (mxl == 1) {
                return 32;
            }
            if (level == simrv::xlen::PrivilegeLevel::Machine) {
                return 64;
            } else if (level == simrv::xlen::PrivilegeLevel::Supervisor) {
                const unsigned sxl = (mstatus >> 34) & 3;
                return (sxl == 1) ? 32 : 64;
            } else {  // User
                const unsigned uxl = (mstatus >> 32) & 3;
                return (uxl == 1) ? 32 : 64;
            }
        }
    }

    [[nodiscard]] constexpr auto current_xlen() const -> unsigned {
        return xlen_for_privilege(priv);
    }

    [[nodiscard]] constexpr auto hart_id() const noexcept -> simrv::xlen::HartId {
        return simrv::xlen::HartId{static_cast<uint32_t>(mhartid)};
    }

    SIMRV_ALWAYS_INLINE constexpr void canonicalize_pc() noexcept {
        if constexpr (simrv::xlen::kIsXLen64) {
            if (simrv::compiler::unlikely(regs.xlen == 32)) {
                pc = static_cast<simrv::xlen::Register>(
                    static_cast<int64_t>(static_cast<int32_t>(pc)));
            }
        }
    }

    /**
     * @brief Updates the active XLEN configurations in register file proxies.
     */
    constexpr void update_xlen() { regs.xlen = current_xlen(); }

    /**
     * @brief Initialize SXL/UXL consistently with the selected machine XLEN.
     *
     * An RV64 simulator binary may host an RV32 machine personality. In that
     * case lower privilege modes must remain RV32 when a trap enters S/U mode.
     */
    constexpr void initialize_lower_xlen_fields() {
        const bool has_s = isa::misa_has_extension(misa, isa::IsaExtension::S);
        const bool has_u = isa::misa_has_extension(misa, isa::IsaExtension::U);
        const bool has_f = isa::misa_has_extension(misa, isa::IsaExtension::F);
        const bool has_v = isa::misa_has_extension(misa, isa::IsaExtension::V);
        if constexpr (simrv::xlen::kIsXLen64) {
            constexpr uint64_t kLowerXlenMask = uint64_t{0xFU} << 32U;
            const auto mxl = static_cast<unsigned>((static_cast<uint64_t>(misa) >> 62U) & 0x3U);
            const uint64_t lower_xlen = (mxl == 1U) ? 1U : 2U;
            const uint64_t updated = (static_cast<uint64_t>(mstatus) & ~kLowerXlenMask) |
                                     (has_u ? (lower_xlen << 32U) : 0U) |
                                     (has_s ? (lower_xlen << 34U) : 0U);
            mstatus = static_cast<simrv::xlen::CsrValue>(updated);
        }
        mstatus = mstatus_legalize_mpp(mstatus, has_s, has_u);
        mstatus &= mstatus_writable_mask(has_s, has_u, has_f, has_v);
        update_xlen();
    }
};

/**
 * @struct SoftTlbEntry
 * @brief Compact software TLB entry (32 bytes) for the fast-path translation cache.
 *
 * Packs vpn, asid, priv, and valid into a single 64-bit tag for efficient comparison.
 * Tag layout (64-bit):
 *   bits [63:16] = VPN (page-frame number, vaddr >> 12)
 *   bits [15: 2] = ASID (14-bit ASID from satp)
 *   bits [ 1: 0] = privilege (0=User, 1=Supervisor, 3=Machine)
 * valid == false when tag == kInvalidTag.
 */
struct alignas(32) SoftTlbEntry {
    static constexpr uint64_t kInvalidTag = ~uint64_t{0};

    uint64_t tag = kInvalidTag;  ///< Packed VPN | ASID | priv; kInvalidTag when empty (8 bytes)
    simrv::xlen::PhysAddr paddr_base{0};  ///< Physical page base (paddr & ~0xFFF) (8 bytes)
    uintptr_t host_offset = 0;  ///< Direct host offset: (host_base - (vpn << 12)) (8 bytes)
    uint32_t epoch = 0;         ///< TLB generation epoch (4 bytes)
    bool global = false;        ///< Global translation flag (1 byte)
    bool has_host = false;      ///< Direct host pointer valid flag (1 byte)

    /// Build a lookup tag from the three key fields.
    [[nodiscard]] SIMRV_ALWAYS_INLINE static constexpr auto make_tag(
        simrv::xlen::VpnIndex vpn, uint64_t asid, simrv::xlen::PrivilegeLevel priv) noexcept
        -> uint64_t {
        return ((vpn & 0xFFFFFFFFFFULL) << 18) | ((asid & 0xFFFFu) << 2) |
               static_cast<uint64_t>(static_cast<uint8_t>(priv) & 0x3u);
    }

    [[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto matches(simrv::xlen::VpnIndex vpn,
                                                             uint64_t asid,
                                                             simrv::xlen::PrivilegeLevel priv,
                                                             uint32_t current_epoch) const noexcept
        -> bool {
        if (simrv::compiler::unlikely(epoch != current_epoch)) return false;
        const uint64_t expected_tag = make_tag(vpn, asid, priv);
        if (simrv::compiler::likely(tag == expected_tag)) return true;
        return simrv::compiler::unlikely(global) &&
               ((tag & ~uint64_t{0x3FFFCULL}) == make_tag(vpn, 0, priv));
    }

    [[nodiscard]] SIMRV_ALWAYS_INLINE bool has_host_ptr() const noexcept { return has_host; }

    [[nodiscard]] SIMRV_ALWAYS_INLINE auto host_ptr(simrv::xlen::Address vaddr) const noexcept
        -> simrv::xlen::Byte* {
        return reinterpret_cast<simrv::xlen::Byte*>(host_offset + static_cast<uintptr_t>(vaddr));
    }

    [[nodiscard]] SIMRV_ALWAYS_INLINE auto host_ptr_base() const noexcept -> simrv::xlen::Byte* {
        return has_host ? reinterpret_cast<simrv::xlen::Byte*>(host_offset + ((tag >> 18) << 12))
                        : nullptr;
    }

    void set(simrv::xlen::VpnIndex vpn, uint64_t asid, simrv::xlen::PrivilegeLevel priv,
             uint32_t current_epoch, simrv::xlen::PhysAddr paddr_base_in,
             simrv::xlen::Byte* host_ptr_base_in, bool is_global = false) noexcept {
        tag = make_tag(vpn, asid, priv);
        epoch = current_epoch;
        paddr_base = paddr_base_in;
        has_host = (host_ptr_base_in != nullptr);
        host_offset = has_host ? (reinterpret_cast<uintptr_t>(host_ptr_base_in) -
                                  (static_cast<uintptr_t>(vpn) << 12U))
                               : 0;
        global = is_global;
    }

    void invalidate() noexcept {
        tag = kInvalidTag;
        paddr_base = 0;
        host_offset = 0;
        has_host = false;
        global = false;
    }

    [[nodiscard]] bool valid(uint32_t current_epoch) const noexcept {
        return epoch == current_epoch && tag != kInvalidTag;
    }
};
static_assert(sizeof(SoftTlbEntry) == 32, "SoftTlbEntry must be exactly 32 bytes");

enum class HartStatus : uint8_t {
    Started = 0,
    Stopped = 1,
    StartPending = 2,
    StopPending = 3,
    Suspended = 4,
    SuspendPending = 5,
    ResumePending = 6
};

class CPU {
   public:
    inline static thread_local CPU* active_execution_cpu_ = nullptr;
    [[nodiscard]] static auto current_execution_cpu() noexcept -> CPU* {
        return active_execution_cpu_;
    }
    /**
     * @brief Compute the soft TLB slot index using bit-mixed XOR hashing to prevent power-of-two
     * stride aliasing.
     * @param vpn Virtual page number (vaddr >> 12).
     * @return Slot index in range [0, 2047].
     */
    [[nodiscard]] SIMRV_ALWAYS_INLINE static constexpr auto soft_tlb_index(
        simrv::xlen::VpnIndex vpn) noexcept -> size_t {
        return static_cast<size_t>((vpn ^ (vpn >> 11)) & 2047u);
    }

    std::atomic<HartStatus> hart_status{HartStatus::Started};
    /**
     * @brief Constructs a new CPU core, resetting GPRs, floating-point registers, and setting
     * initial status values.
     */
    CPU();

    /**
     * @brief Resets CPU architectural state, TLB entries, decode caches, and execution metrics.
     */
    void reset();

    /**
     * @brief Flushes all instruction/data Translation Lookaside Buffer (TLB) entries and
     * invalidates the decode cache.
     */
    void TLB_flush();

    /**
     * @brief Flushes only data TLB and soft-TLB entries (e.g., on SUM/MXR changes) while
     * preserving instruction TLB, I-Cache, and DecodeCache.
     */
    void flush_data_tlb();

    /**
     * @brief Selectively flushes TLB entries matching the specified filter criteria.
     * @param filter The selective flush criteria.
     */
    void TLB_flush(const core::TlbFlushFilter& filter);

    /**
     * @brief Selectively flushes TLB entries matching virtual address and/or Address Space
     * Identifier (ASID).
     */
    inline void TLB_flush(bool match_all_vaddr, simrv::xlen::Address vaddr, bool match_all_asid,
                          simrv::xlen::Word asid) {
        TLB_flush(core::TlbFlushFilter{
            .vaddr = match_all_vaddr ? std::nullopt : std::make_optional(vaddr),
            .asid = match_all_asid ? std::nullopt : std::make_optional(static_cast<Asid>(asid))});
    }

    /**
     * @brief Writes the `mstatus` CSR, applying any architectural side effects (such as flushing
     * the TLB if configuration changes).
     * @param val The new value to write to `mstatus`.
     */
    void set_mstatus(simrv::xlen::CsrValue val);

    /**
     * @brief Reads a masked `mstatus` value with active architectural projections.
     * @param mask The bitmask of the status fields to retrieve.
     * @return The masked `mstatus` register value.
     */
    [[nodiscard]] auto get_mstatus(simrv::xlen::CsrValue mask) const -> simrv::xlen::CsrValue;

    /**
     * @brief Reads a value from a specified Control and Status Register (CSR).
     * @param addr The 12-bit address of the CSR.
     * @return The CSR's value if successful, or an ExceptionCode if the access is unauthorized or
     * invalid.
     */
    [[nodiscard]] auto read_csr(simrv::xlen::CsrNumber addr) const
        -> std::expected<simrv::xlen::CsrValue, ExceptionCode>;

    /**
     * @brief Writes a value to a specified Control and Status Register (CSR).
     * @param addr The 12-bit address of the CSR.
     * @param val The new value to write.
     * @return A void result if successful, or an ExceptionCode on failure.
     */
    auto write_csr(simrv::xlen::CsrNumber addr, simrv::xlen::CsrValue val)
        -> std::expected<void, ExceptionCode>;

    /**
     * @brief Checks if an instruction fetch PC matches any active Sdtrig execution trigger.
     */
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto check_triggers_pc(simrv::xlen::Address pc) const noexcept
        -> bool {
        if (!state_.has_active_triggers) return false;
        const unsigned xlen = state_.regs.xlen;
        for (size_t i = 0; i < ArchState::kNumTriggers; ++i) {
            const auto t1 = state_.tdata1[i];
            if (t1 == 0) continue;
            const auto type = (static_cast<uint64_t>(t1) >> (xlen == 64 ? 60U : 28U)) & 0xFU;
            if (type != 2) continue;
            if ((t1 & (1U << 19U)) != 0) continue;  // select=1 matches data, not address
            if ((t1 & (1U << 2U)) == 0) continue;   // execute not enabled
            const bool m = (t1 & (1U << 6U)) != 0;
            const bool s = (t1 & (1U << 4U)) != 0;
            const bool u = (t1 & (1U << 3U)) != 0;
            if (state_.priv == simrv::xlen::PrivilegeLevel::Machine && !m) continue;
            if (state_.priv == simrv::xlen::PrivilegeLevel::Supervisor && !s) continue;
            if (state_.priv == simrv::xlen::PrivilegeLevel::User && !u) continue;
            if (pc == state_.tdata2[i]) return true;
        }
        return false;
    }

    /**
     * @brief Checks if a memory access range matches any active Sdtrig load/store trigger.
     */
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto check_triggers_mem(simrv::xlen::Address addr,
                                                              uint32_t size,
                                                              bool is_write) const noexcept
        -> bool {
        if (!state_.has_active_triggers) return false;
        const unsigned xlen = state_.regs.xlen;
        const uint32_t span = (size > 0) ? size : 1u;
        for (size_t i = 0; i < ArchState::kNumTriggers; ++i) {
            const auto t1 = state_.tdata1[i];
            if (t1 == 0) continue;
            const auto type = (static_cast<uint64_t>(t1) >> (xlen == 64 ? 60U : 28U)) & 0xFU;
            if (type != 2) continue;
            if ((t1 & (1U << 19U)) != 0) continue;  // select=1 matches data, not address
            if (is_write) {
                if ((t1 & (1U << 1U)) == 0) continue;  // store not enabled
            } else {
                if ((t1 & (1U << 0U)) == 0) continue;  // load not enabled
            }
            const bool m = (t1 & (1U << 6U)) != 0;
            const bool s = (t1 & (1U << 4U)) != 0;
            const bool u = (t1 & (1U << 3U)) != 0;
            if (state_.priv == simrv::xlen::PrivilegeLevel::Machine && !m) continue;
            if (state_.priv == simrv::xlen::PrivilegeLevel::Supervisor && !s) continue;
            if (state_.priv == simrv::xlen::PrivilegeLevel::User && !u) continue;
            const simrv::xlen::Address t2 = state_.tdata2[i];
            if (addr <= t2 && t2 < addr + span) return true;
        }
        return false;
    }

    /**
     * @brief Handles execution return from a machine-mode trap (MRET instruction).
     * Restores privilege level and interrupt enable bits from `mstatus`.
     */
    void mret();

    /**
     * @brief Handles execution return from a supervisor-mode trap (SRET instruction).
     * Restores privilege level and interrupt enable bits from `sstatus`.
     */
    void sret();

    /**
     * @brief Recomputes the MIP (Machine Interrupt Pending) register based on currently active
     * external interrupts.
     */
    void plic_update_mip();

    /**
     * @brief Sets or deasserts a PLIC (Platform-Level Interrupt Controller) interrupt source line.
     * @param irq_num The interrupt request number.
     * @param state The state to write (1 for asserted, 0 for deasserted).
     */
    void plic_set_irq(simrv::xlen::IrqNumber irq_num, simrv::xlen::IrqLevel state);
    void plic_set_irq(simrv::xlen::IrqNumber irq_num, bool asserted = true);
    void plic_set_irq(simrv::xlen::IrqNumber irq_num, int state);

    /**
     * @brief Triggers an architectural exception or interrupt trap, transitioning privilege levels
     * and saving return states.
     * @param cause The trap cause (exception code or interrupt bit).
     * @param tval The trap-specific value (bad address, illegal instruction word, etc.) to save in
     * `mtval`/`stval`.
     */
    void raise_exception(simrv::xlen::TrapCause cause, simrv::xlen::CsrValue tval);
    inline void raise_exception(ExceptionCode code, simrv::xlen::CsrValue tval) {
        raise_exception(static_cast<simrv::xlen::TrapCause>(code), tval);
    }

    /**
     * @brief Evaluates CLINT (Core Local Interruptor) timer interrupts against `mtimecmp`.
     * Asserts timer interrupt pending flags in `mip` if current `mtime` >= `mtimecmp`.
     */
    void evaluate_timer_interrupt();
    /// Queue external interrupt wire changes for application by this hart at its next boundary.
    void queue_interrupt_line(core::MipBit bit, bool asserted) noexcept;
    void apply_pending_interrupt_lines(Machine& machine);
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto has_pending_interrupt_lines() const noexcept -> bool {
        return pending_interrupt_lines_.load(std::memory_order_relaxed) != 0;
    }
    [[nodiscard]] auto next_logical_issue_sequence() noexcept -> simrv::xlen::Counter {
        return next_logical_issue_sequence_++;
    }
    /// Apply HSM status transitions only from the hart's own execution thread.
    void apply_hart_control_boundary() noexcept;

    /**
     * @brief Advances the selected execution engine by one scheduling step.
     * @param machine Reference to the top-level machine orchestration unit.
     */
    void run_cycle(Machine& machine);
    /// Advance exactly one authoritative CA transition.  Machine owns the global CA clock and
    /// calls this directly; IA never uses this entry point.
    void advance_ca_cycle(Machine& machine);
    void run_ca_cycle_fast(Machine& machine);
    void execute_ca_cycle_stages(Machine& machine, simrv::xlen::Counter retired_before);
    [[nodiscard]] auto is_wfi_quiescent() const noexcept -> bool { return wfi_quiescent_; }
    void wake_from_wfi() noexcept { wfi_quiescent_ = false; }
    void run_ca_pipeline_cycle(Machine& machine);
    /// Install a validated model before reset/reboot and recreate CA-local timing state.
    void apply_cpu_model_config(const simrv::pipeline::CpuModelConfig& config);

    /**
     * @brief Executes one full CPU cycle in optimized baremetal mode.
     * Bypasses full pipeline tracking for functional performance.
     * @param machine Reference to the top-level machine orchestration unit.
     */
    void run_cycle_baremetal(Machine& machine);
    void run_fast_cycle_miss(Machine& machine);
    void run_cycle_baremetal_miss(Machine& machine);
    /// Advance architectural time by one deterministic global CA clock.
    void tick_cycle_clock(Machine& machine, bool interrupt_boundary = true);

    /**
     * @brief Records active instruction details to the TUI (Text User Interface) trace buffer if
     * enabled.
     * @param machine Reference to the top-level machine orchestration unit.
     */
    void record_trace_for_tui(Machine& machine);

    /**
     * @brief Executes a cached decoded instruction via the monolithic fast path execution engine.
     * @param machine Reference to the top-level machine orchestration.
     * @param op Reference to the cached pre-decoded operation.
     */
    template <bool kCopyContext = false, bool kInstMix = false, bool kDeferAluCommit = false>
    void execute_cached_op_fast(Machine& machine, CachedOp& op, const simrv::memory::RamView& ram);
    template <bool kCopyContext, bool kInstMix, bool kDeferAluCommit>
    SIMRV_ALWAYS_INLINE auto execute_cached_alu_fast(CachedOp& op) -> bool;

    template <bool kCopyContext, bool kInstMix, bool kPollPause>
    SIMRV_ALWAYS_INLINE auto run_fast_baremetal_kernel(Machine& machine, uint32_t batch_size,
                                                       const simrv::memory::RamView& ram)
        -> uint32_t;

    /**
     * @brief Executes a batch of cached operations in a tight inlined loop for baremetal
     * acceleration.
     * @param machine Reference to top-level Machine.
     * @param batch_size Maximum number of instructions to execute in the batch.
     */
    void run_fast_baremetal_batch(Machine& machine, uint32_t batch_size,
                                  const FastBatchPolicy& policy);

    template <bool kCopyContext, bool kInstMix, bool kPollPause>
    SIMRV_ALWAYS_INLINE auto run_fast_os_kernel(Machine& machine, uint32_t batch_size,
                                                const simrv::memory::RamView& ram) -> uint32_t;

    /**
     * @brief Executes a batch of cached operations in a tight inlined loop for OS
     * acceleration.
     * @param machine Reference to top-level Machine.
     * @param batch_size Maximum number of instructions to execute in the batch.
     */
    void run_fast_os_batch(Machine& machine, uint32_t batch_size, const FastBatchPolicy& policy);

   public:
    /**
     * @brief Runs the Fetch stage of the cycle-accurate pipeline.
     */
    void run_fetch_stage(Machine& machine);

    /**
     * @brief Runs the Fetch stage of the baremetal execution path.
     */
    void run_fetch_stage_baremetal(Machine& machine);

    /**
     * @brief Runs the Decode stage of the cycle-accurate pipeline.
     */
    void run_decode_stage(Machine& machine);

    /**
     * @brief Runs the Execute stage of the cycle-accurate pipeline.
     */
    void run_execute_stage(Machine& machine);

    /**
     * @brief Runs the Memory stage of the cycle-accurate pipeline.
     */
    void run_memory_stage(Machine& machine);

    /**
     * @brief Runs the Memory stage of the baremetal execution path.
     */
    void run_memory_stage_baremetal(Machine& machine);

    /**
     * @brief Runs the Writeback stage of the cycle-accurate pipeline.
     */
    void run_writeback_stage(Machine& machine);

    /**
     * @brief Runs the Commit stage of the cycle-accurate pipeline.
     */
    void run_commit_stage(Machine& machine);

    /**
     * @brief Runs the Commit stage of the baremetal execution path.
     */
    void run_commit_stage_baremetal(Machine& machine);

    /**
     * @brief Functional monadic Fetch stage (C++23 expected monadic structure).
     */
    [[nodiscard]] auto fetch_stage(Machine& machine, simrv::xlen::Address pc) -> bool;

    /**
     * @brief Functional monadic Decode stage.
     */
    [[nodiscard]] auto decode_stage(Machine& machine) -> bool;

    /**
     * @brief Functional monadic Execute stage.
     */
    [[nodiscard]] auto execute_stage(Machine& machine) -> bool;

    /**
     * @brief Functional monadic Memory stage.
     */
    [[nodiscard]] auto memory_stage(Machine& machine) -> bool;

    /**
     * @brief Functional monadic Writeback stage.
     */
    [[nodiscard]] auto writeback_stage(Machine& machine) -> bool;

    /**
     * @brief Functional monadic Commit stage.
     */
    [[nodiscard]] auto commit_stage(Machine& machine) -> bool;

    /**
     * Translate one address. In CA mode this resumes a typed page walk and returns nullopt while
     * its physical PTE transaction is outstanding; IA drives the same MMU state synchronously.
     */
    auto translate_stage_address(Machine& machine, simrv::xlen::VirtAddr virtual_address,
                                 PteAccess access, simrv::xlen::PrivilegeLevel privilege,
                                 unsigned active_xlen, simrv::memory::TlPort port,
                                 simrv::pipeline::TimedPageWalkState& timed_walk)
        -> std::optional<std::expected<PageWalkResult, simrv::xlen::TrapCause>>;

   private:
    /// Translate fetch addresses and prime IF transient context.
    void fetch_address_translate(Machine& machine);
    /// Resolve fetch translation misses and update IF-side TLB state.
    void fetch_resolve_page_walk(Machine& machine, int state);
    /// Read instruction word bits from memory into pipeline context.
    void fetch_read_instruction_word(Machine& machine);
    /// Decompress/normalize instruction form and validate ISA availability.
    void decode_and_normalize_instruction(Machine& machine);
    /// Decode instruction fields (opcode, rd/rs, funct, immediate).
    void decode_fields(Machine& machine);
    /// Fetch integer register and CSR operands.
    void fetch_operands(Machine& machine);
    /// Perform ALU/branch/CSR/FP execute operations.
    void execute_core(Machine& machine);
    /// Helper for execute_core to process System instruction opcode.
    void execute_system(Machine& machine);
    /// Helper for execute_core to process floating point/fused FP instruction opcodes.
    void execute_fp(Machine& machine);
    /// Perform load-side memory access work.
    void memory_load_phase(Machine& machine);
    /// Prepare memory write payloads for store/AMO paths.
    void memory_prepare_store_data(Machine& machine);
    /// Perform store-side memory access work.
    void memory_store_phase(Machine& machine);
    /// Commit writeback results to integer/floating register files.
    void writeback_registers(Machine& machine);
    /// Finalize control flow and trap/interrupt transitions.
    void commit_control_flow_and_traps(Machine& machine);

   private:
    SIMRV_ALWAYS_INLINE auto execute_cached_jal(CachedOp& op) -> void;
    SIMRV_ALWAYS_INLINE auto execute_cached_jalr(CachedOp& op, simrv::xlen::Register rrs1) -> void;
    template <isa::OperationId kBranchOp, bool kCopyContext>
    SIMRV_ALWAYS_INLINE auto execute_cached_branch(CachedOp& op, simrv::xlen::Register rrs1,
                                                   simrv::xlen::Register rrs2) -> void;
    SIMRV_ALWAYS_INLINE auto try_fast_load(Machine& machine, simrv::xlen::Address mem_addr,
                                           isa::Funct3 funct3, simrv::xlen::Register& out_val,
                                           const simrv::memory::RamView& ram) -> bool;
    SIMRV_ALWAYS_INLINE auto try_fast_store(Machine& machine, simrv::xlen::Address mem_addr,
                                            isa::Funct3 funct3, simrv::xlen::Register rrs2,
                                            const simrv::memory::RamView& ram) -> bool;
    template <isa::OperationId kLoadOp>
    SIMRV_ALWAYS_INLINE auto execute_cached_load(Machine& machine, CachedOp& op,
                                                 simrv::xlen::Register rrs1,
                                                 const simrv::memory::RamView& ram) -> bool;
    template <isa::OperationId kStoreOp>
    SIMRV_ALWAYS_INLINE auto execute_cached_store(Machine& machine, CachedOp& op,
                                                  simrv::xlen::Register rrs1,
                                                  simrv::xlen::Register rrs2,
                                                  const simrv::memory::RamView& ram) -> bool;
    template <bool kGuestXlen64, bool kCopyContext, bool kInstMix, bool kDeferAluCommit>
    SIMRV_ALWAYS_INLINE auto execute_cached_alu_fast_xlen(CachedOp& op, FastOp kind) -> bool;
    template <bool kGuestXlen64, bool kCopyContext, bool kInstMix, bool kDeferAluCommit>
    SIMRV_ALWAYS_INLINE auto execute_cached_alu_fast_xlen(CachedOp& op) -> bool;
    [[gnu::noinline]] auto advance_sequential_miss_baremetal(CachedOp* chained,
                                                             simrv::xlen::Register next_pc) noexcept
        -> CachedOp*;
    [[gnu::noinline]] auto resolve_branch_target_baremetal(CachedOp* chained,
                                                           simrv::xlen::Register target_pc) noexcept
        -> CachedOp*;
    auto execute_cached_fallback(Machine& machine) -> void;
    auto dispatch_pending_interrupts() -> void;
    SIMRV_ALWAYS_INLINE auto handle_cached_interrupts() -> void {
        if (simrv::compiler::unlikely((state_.mip & state_.mie) != 0u)) {
            dispatch_pending_interrupts();
        }
    }
    SIMRV_ALWAYS_INLINE void pc_sign_extend() noexcept { state_.canonicalize_pc(); }

    /// Advance PC by op.len() and update instruction counters for straight ALU ops without
    /// branch/interrupt overhead.
    SIMRV_ALWAYS_INLINE void advance_cached_pc_alu(const CachedOp& op) {
        state_.pc += op.len();
        e_icount++;
        e_ccount += (op.cinsn != 0);
    }

    /// Advance PC by op.len(), update instruction counters, and sign-extend PC.
    /// The enclosing fast-execution kernel processes interrupts once after the instruction.
    SIMRV_ALWAYS_INLINE void advance_cached_pc(const CachedOp& op) {
        state_.pc += op.len();
        e_icount++;
        e_ccount += (op.cinsn != 0);
        pc_sign_extend();
    }

    /// Set PC to target_pc, update instruction counters, and sign-extend PC.
    /// The enclosing fast-execution kernel processes interrupts once after the instruction.
    SIMRV_ALWAYS_INLINE void commit_cached_branch_target(const CachedOp& op,
                                                         simrv::xlen::VirtAddr target_pc) {
        state_.pc = target_pc.raw();
        e_icount++;
        e_ccount += (op.cinsn != 0);
        pc_sign_extend();
    }

    ArchState state_;
    ArchState prev_state_;
    // Six dirty bits followed by six value bits: MTIP, MSIP, MEIP, STIP, SSIP, SEIP.
    std::atomic<uint16_t> pending_interrupt_lines_{0};
    simrv::xlen::Counter next_register_trace_sequence_ = 0;
    simrv::xlen::Counter next_logical_issue_sequence_ = 0;

   public:
    /// Zero-cost read-only access to architectural state for tracing/logging.
    [[nodiscard]] constexpr auto state() const -> const ArchState& { return state_; }
    /// Mutable access to architectural state for decoupled controllers.
    [[nodiscard]] constexpr auto state() -> ArchState& { return state_; }
    /// Access to architectural state prior to current step for diff rendering.
    [[nodiscard]] constexpr auto prev_state() const -> const ArchState& { return prev_state_; }
    /// Strongly typed Hart identifier for SMP topology and interrupt routing.
    [[nodiscard]] constexpr auto hart_id() const noexcept -> simrv::xlen::HartId {
        return state_.hart_id();
    }

    [[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto effective_data_privilege() const noexcept
        -> simrv::xlen::PrivilegeLevel {
        if (simrv::compiler::unlikely(state_.priv == kPrivMachine &&
                                      (state_.mstatus & MstatusBit::Mprv) != 0)) {
            return mstatus_mpp(state_.mstatus);
        }
        return state_.priv;
    }

    /**
     * @brief XLEN governing explicit loads/stores, including MPRV/MPP.
     *
     * The privileged architecture requires MPRV accesses to use MPP's XLEN,
     * which can differ from the currently executing M-mode XLEN.
     */
    [[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto effective_data_xlen() const noexcept
        -> unsigned {
        return state_.xlen_for_privilege(effective_data_privilege());
    }

    /**
     * @brief Computes an 8-bit architectural decode context tag encoding privilege, effective
     * data privilege (MPRV/MPP), active XLEN, FS/VS enable state, and RVC alignment.
     */
    [[nodiscard]] SIMRV_ALWAYS_INLINE constexpr auto compute_decode_context() const noexcept
        -> uint8_t {
        const uint8_t priv_bits = static_cast<uint8_t>(state_.priv) & 0x3u;
        const uint8_t eff_priv_bits = static_cast<uint8_t>(effective_data_privilege()) & 0x3u;
        const uint8_t xlen_bit = (state_.regs.xlen == 64) ? 0x10u : 0u;
        const uint8_t fs_bit = ((state_.mstatus & MstatusBit::Fs) != 0) ? 0x20u : 0u;
        const uint8_t vs_bit = ((state_.mstatus & MstatusBit::Vs) != 0) ? 0x40u : 0u;
        const uint8_t rvc_bit = (state_.fetch_align_mask == 1u) ? 0x80u : 0u;
        return static_cast<uint8_t>(priv_bits | (eff_priv_bits << 2u) | xlen_bit | fs_bit | vs_bit |
                                    rvc_bit);
    }

    SIMRV_ALWAYS_INLINE void sync_decode_context() noexcept {
        const auto ctx = compute_decode_context();
        if (simrv::compiler::unlikely(ctx != cached_decode_context_)) {
            cached_decode_context_ = ctx;
            decode_cache.set_active_context(ctx);
        }
    }

    Tlb tlb;

    simrv::device::Plic plic_mmio;
    simrv::device::Clint clint_mmio;
    CsrFile csr_file;
    execute::ExecuteUnit execute_unit;
    execute::CfuUnit cfu_unit;
    simrv::pipeline::PipelineContext pipeline_context;
    simrv::pipeline::PipelineContext* active_context_ = &pipeline_context;
    [[nodiscard]] constexpr auto& active_context() noexcept { return *active_context_; }
    [[nodiscard]] constexpr const auto& active_context() const noexcept { return *active_context_; }
    [[nodiscard]] constexpr auto& scoreboard() noexcept { return ca_pipeline.scoreboard; }
    [[nodiscard]] constexpr const auto& scoreboard() const noexcept {
        return ca_pipeline.scoreboard;
    }
    simrv::cache::ICache icache;
    simrv::cache::DCache dcache;
    simrv::cache::TimingCacheHierarchy instruction_cache_timing;
    sbi::Sbi sbi;
    bool use_opensbi = false;
    Machine* machine_ = nullptr;
    simrv::pipeline::PipelineSim pipeline_sim;
    simrv::pipeline::CpuModelConfig cpu_model_config{};
    simrv::pipeline::BranchPredictor branch_predictor;
    simrv::pipeline::HartCycleState ca_state{};
    simrv::pipeline::HartPipelineState ca_pipeline{};
    DecodeCache decode_cache;
    uint8_t cached_decode_context_{0xFF};
    alignas(64) std::array<SoftTlbEntry, 2048> soft_tlb_read{};
    alignas(64) std::array<SoftTlbEntry, 2048> soft_tlb_write{};
    alignas(64) std::array<SoftTlbEntry, 2048> soft_tlb_inst{};
    uint32_t soft_tlb_epoch = 1;
    void soft_tlb_flush();
    void soft_tlb_flush_selective(const core::TlbFlushFilter& filter);
    bool wfi_quiescent_{false};

    // ========== Execution Metrics ==========
    simrv::xlen::Counter e_icount{0};   // Total instruction count
    simrv::xlen::Counter e_ccount = 0;  // Compressed instructions executed
    simrv::xlen::LatencyCycles cycle_counter_start_delay_ = 0;
    simrv::xlen::CycleCount last_retire_cycle_{0};
    PerformanceCounters perf_counters{};
    std::array<simrv::xlen::Counter, isa::OperationIdCount>
        e_instmix{};  // Instruction-mix statistics
};

}  // namespace simrv::core
