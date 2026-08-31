/**
 * @file Cpu.hpp
 * @brief CPU core state and control declarations.
 */
#pragma once

#include <array>
#include <deque>
#include <expected>
#include <fstream>
#include <mutex>
#include <optional>
#include <vector>

#include "simrv/Define.hpp"
#include "simrv/cache/DCache.hpp"
#include "simrv/cache/ICache.hpp"
#include "simrv/core/CsrFile.hpp"
#include "simrv/core/DecodeCache.hpp"
#include "simrv/core/RegisterFile.hpp"
#include "simrv/core/Sbi.hpp"
#include "simrv/core/StateControl.hpp"
#include "simrv/core/Tlb.hpp"
#include "simrv/execute/ExecuteUnit.hpp"
#include "simrv/memory/Bus.hpp"
#include "simrv/pipeline/CpuModel.hpp"
#include "simrv/pipeline/CycleTransition.hpp"
#include "simrv/pipeline/PipelineContext.hpp"
#include "simrv/pipeline/PipelineSim.hpp"

namespace simrv::core {
class Machine;

/// Immutable fast-path decisions captured by a runner at a batch boundary.
struct FastBatchPolicy {
    bool copy_pipeline_context = false;
    bool collect_instruction_mix = false;
    bool poll_pause = true;
    bool has_instruction_limit = false;
    uint32_t pause_poll_mask = 0xFFu;
};

/**
 * @struct ArchState
 * @brief Groups all architectural registers and CSRs into a cohesive block.
 */
struct alignas(128) ArchState {
    Register pc{};        ///< Program Counter (instruction address pointer)
    RegisterFile regs{};  ///< General-purpose integer, FP, and vector registers

    /* Machine Mode CSRs */
    CSRValue
        mstatus{};  ///< Machine Status (tracks processor mode, interrupt states, extension states)
    CSRValue mtvec{};  ///< Machine Trap-Vector Base-Address (address of machine mode trap handler)
    CSRValue mscratch{};  ///< Machine Scratch register (used for temporary context swapping)
    CSRValue mepc{};      ///< Machine Exception Program Counter (returns address on MRET)
    TrapCause mcause{};   ///< Machine Trap Cause (interrupt vs exception and exact code)
    CSRValue mtval{};     ///< Machine Trap Value (holds faulting addresses or instruction bytes)
    CSRValue mhartid{};   ///< Machine Hardware Thread ID (processor core identifier)
    CSRValue misa = isa::kMisaDefault;  ///< Machine ISA and extensions descriptor
    CSRValue mie{};                     ///< Machine Interrupt Enable
    CSRValue mip{};                     ///< Machine Interrupt Pending
    bool seip_software{};  ///< Software-writable component of mip.SEIP (ORed with PLIC signal).
    bool seip_external{};  ///< PLIC-driven component of mip.SEIP.
    bool stip_software{};  ///< M-mode software-posted component of writable mip.STIP.
    bool stip_timer{};     ///< Direct-SBI timer signal component of mip.STIP.
    CSRValue
        medeleg{};  ///< Machine Exception Delegation (delegates trap handling to supervisor mode)
    CSRValue mideleg{};  ///< Machine Interrupt Delegation (delegates interrupts to supervisor mode)
    CSRValue mcounteren{};  ///< Machine Counter Enable (controls user/supervisor access to
                            ///< performance counters)

    /* Supervisor Mode CSRs */
    CSRValue stvec{};  ///< Supervisor Trap-Vector Base-Address (address of supervisor trap handler)
    CSRValue sscratch{};  ///< Supervisor Scratch register
    CSRValue sepc{};      ///< Supervisor Exception Program Counter (returns address on SRET)
    TrapCause scause{};   ///< Supervisor Trap Cause
    CSRValue stval{};     ///< Supervisor Trap Value
    CSRValue satp{};  ///< Supervisor Address Translation and Protection (virtual memory mode and
                      ///< page table base physical address)
    CSRValue scounteren{};  ///< Supervisor Counter Enable

    /* Floating-Point CSRs */
    CSRValue fcsr{};  ///< Floating-Point Control and Status register (rounding mode and cumulative
                      ///< exception flags)

    /* Vector Extension CSRs */
    CSRValue vstart{};  ///< Vector Start Index (specifies the register element index to start
                        ///< vector execution)
    CSRValue vxsat{};   ///< Vector Fixed-Point Saturation flag
    CSRValue vxrm{};    ///< Vector Fixed-Point Rounding Mode selector
    CSRValue
        vl{};  ///< Vector Length (active number of elements to process in a vector instruction)
    CSRValue
        vtype{};  ///< Vector Type (contains element width VSEW, vector group multiplier LMUL, etc.)

    PrivilegeLevel priv = kPrivMachine;  ///< Current processor privilege level

    Address load_res{};   ///< Active memory physical address reservation for Load-Reserved /
                          ///< Store-Conditional (LR/SC)
    CSRValue reserved{};  ///< Reserved for internal architectural extensions or tracking

    /* Physical Memory Protection (PMP) */
    static constexpr size_t kNumPmpEntries = 16;
    std::array<uint8_t, kNumPmpEntries> pmpcfg{};
    std::array<Address, kNumPmpEntries> pmpaddr{};
    size_t num_active_pmp = 0;
    bool has_locked_pmp = false;

    constexpr void refresh_pmp_status() {
        num_active_pmp = 0;
        has_locked_pmp = false;
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
            mip |= enum_mask(MipBit::Seip);
        else
            mip &= ~enum_mask(MipBit::Seip);
        if (stip_software || stip_timer)
            mip |= enum_mask(MipBit::Stip);
        else
            mip &= ~enum_mask(MipBit::Stip);
    }

    /**
     * @brief Determines the active XLEN (register width in bits: 32 or 64) based on the CPU state.
     */
    [[nodiscard]] constexpr auto xlen_for_privilege(PrivilegeLevel level) const -> unsigned {
        if constexpr (!simrv::xlen::kIsXLen64) {
            return 32;
        } else {
            const unsigned mxl = (misa >> 62) & 3;
            if (mxl == 1) {
                return 32;
            }
            if (level == PrivilegeLevel::Machine) {
                return 64;
            } else if (level == PrivilegeLevel::Supervisor) {
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
            const unsigned mxl = static_cast<unsigned>((static_cast<uint64_t>(misa) >> 62U) & 0x3U);
            const uint64_t lower_xlen = (mxl == 1U) ? 1U : 2U;
            const uint64_t updated = (static_cast<uint64_t>(mstatus) & ~kLowerXlenMask) |
                                     (has_u ? (lower_xlen << 32U) : 0U) |
                                     (has_s ? (lower_xlen << 34U) : 0U);
            mstatus = static_cast<CSRValue>(updated);
        }
        mstatus = mstatus_legalize_mpp(mstatus, has_s, has_u);
        mstatus &= mstatus_writable_mask(has_s, has_u, has_f, has_v);
        update_xlen();
    }
};

#include <deque>

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

    uint64_t tag = kInvalidTag;     ///< Packed VPN | ASID | priv; kInvalidTag when empty (8 bytes)
    Address paddr_base = 0;         ///< Physical page base (paddr & ~0xFFF) (8 bytes)
    Byte* host_ptr_base = nullptr;  ///< Direct host pointer base (nullptr = use paddr) (8 bytes)
    uint32_t epoch = 0;             ///< TLB generation epoch (4 bytes)
    uint32_t reserved = 0;          ///< Padding to 32 bytes (4 bytes)

    /// Build a lookup tag from the three key fields.
    [[nodiscard]] static constexpr auto make_tag(uint64_t vpn, uint64_t asid,
                                                 PrivilegeLevel priv) noexcept -> uint64_t {
        return ((vpn & 0xFFFFFFFFFFULL) << 18) | ((asid & 0xFFFFu) << 2) |
               static_cast<uint64_t>(static_cast<uint8_t>(priv) & 0x3u);
    }

    [[nodiscard]] constexpr auto matches(uint64_t vpn, uint64_t asid, PrivilegeLevel priv,
                                         uint32_t current_epoch) const noexcept -> bool {
        return epoch == current_epoch && tag == make_tag(vpn, asid, priv);
    }

    void set(uint64_t vpn, uint64_t asid, PrivilegeLevel priv, uint32_t current_epoch,
             Address paddr_base_in, Byte* host_ptr_base_in) noexcept {
        tag = make_tag(vpn, asid, priv);
        epoch = current_epoch;
        paddr_base = paddr_base_in;
        host_ptr_base = host_ptr_base_in;
    }

    void invalidate() noexcept {
        tag = kInvalidTag;
        paddr_base = 0;
        host_ptr_base = nullptr;
    }
    [[nodiscard]] bool valid(uint32_t current_epoch) const noexcept {
        return epoch == current_epoch && tag != kInvalidTag;
    }
};

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
     * @brief Selectively flushes TLB entries matching virtual address and/or Address Space
     * Identifier (ASID).
     * @param match_all_vaddr If true, ignores the vaddr matching criteria (flushes all virtual
     * addresses matching ASID).
     * @param vaddr The target virtual address to match.
     * @param match_all_asid If true, ignores the ASID matching criteria.
     * @param asid The target ASID to match.
     */
    void TLB_flush(bool match_all_vaddr, Address vaddr, bool match_all_asid, Word asid);

    /**
     * @brief Writes the `mstatus` CSR, applying any architectural side effects (such as flushing
     * the TLB if configuration changes).
     * @param val The new value to write to `mstatus`.
     */
    void set_mstatus(CSRValue val);

    /**
     * @brief Reads a masked `mstatus` value with active architectural projections.
     * @param mask The bitmask of the status fields to retrieve.
     * @return The masked `mstatus` register value.
     */
    [[nodiscard]] auto get_mstatus(CSRValue mask) const -> CSRValue;

    /**
     * @brief Reads a value from a specified Control and Status Register (CSR).
     * @param addr The 12-bit address of the CSR.
     * @return The CSR's value if successful, or an ExceptionCode if the access is unauthorized or
     * invalid.
     */
    [[nodiscard]] auto read_csr(CSRAddress addr) const -> std::expected<CSRValue, ExceptionCode>;
    [[nodiscard]] auto read_csr(CsrNumber addr) const -> std::expected<CSRValue, ExceptionCode>;

    /**
     * @brief Writes a value to a specified Control and Status Register (CSR).
     * @param addr The 12-bit address of the CSR.
     * @param val The new value to write.
     * @return A void result if successful, or an ExceptionCode on failure.
     */
    auto write_csr(CSRAddress addr, CSRValue val) -> std::expected<void, ExceptionCode>;
    auto write_csr(CsrNumber addr, CSRValue val) -> std::expected<void, ExceptionCode>;

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
    void plic_set_irq(int irq_num, int state);

    /**
     * @brief Triggers an architectural exception or interrupt trap, transitioning privilege levels
     * and saving return states.
     * @param cause The trap cause (exception code or interrupt bit).
     * @param tval The trap-specific value (bad address, illegal instruction word, etc.) to save in
     * `mtval`/`stval`.
     */
    void raise_exception(TrapCause cause, CSRValue tval);

    /**
     * @brief Evaluates CLINT (Core Local Interruptor) timer interrupts against `mtimecmp`.
     * Asserts timer interrupt pending flags in `mip` if current `mtime` >= `mtimecmp`.
     */
    void evaluate_timer_interrupt();

    /**
     * @brief Advances the selected execution engine by one scheduling step.
     * @param machine Reference to the top-level machine orchestration unit.
     */
    void run_cycle(Machine& machine);
    /// Advance exactly one authoritative CA transition.  Machine owns the global CA clock and
    /// calls this directly; IA never uses this entry point.
    void advance_ca_cycle(Machine& machine);
    void run_ca_pipeline_cycle(Machine& machine);
    /// Install a validated model before reset/reboot and recreate CA-local timing state.
    void apply_cpu_model_config(const simrv::pipeline::CpuModelConfig& config);

    /**
     * @brief Executes one full CPU cycle in optimized baremetal mode.
     * Bypasses full pipeline tracking for functional performance.
     * @param machine Reference to the top-level machine orchestration unit.
     */
    void run_cycle_baremetal(Machine& machine);
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
    template <bool kCopyContext = false, bool kInstMix = false>
    SIMRV_ALWAYS_INLINE void execute_cached_op_fast(Machine& machine, CachedOp& op);

    template <bool kCopyContext, bool kInstMix, bool kPollPause>
    SIMRV_ALWAYS_INLINE auto run_fast_baremetal_kernel(Machine& machine, uint32_t batch_size)
        -> uint32_t;

    /**
     * @brief Executes a batch of cached operations in a tight inlined loop for baremetal
     * acceleration.
     * @param machine Reference to top-level Machine.
     * @param batch_size Maximum number of instructions to execute in the batch.
     */
    void run_fast_baremetal_batch(Machine& machine, uint32_t batch_size,
                                  const FastBatchPolicy& policy);

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
    [[nodiscard]] auto fetch_stage(Machine& machine, Address pc) -> bool;

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
    auto translate_stage_address(Machine& machine, VirtAddr virtual_address, PteAccess access,
                                 PrivilegeLevel privilege, unsigned active_xlen,
                                 simrv::memory::TlPort port,
                                 simrv::pipeline::TimedPageWalkState& timed_walk)
        -> std::optional<std::expected<PhysAddr, TrapCause>>;

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
    auto execute_cached_jal(CachedOp& op) -> void;
    auto execute_cached_jalr(CachedOp& op, Register rrs1) -> void;
    auto execute_cached_branch(CachedOp& op, Register rrs1, Register rrs2) -> void;
    SIMRV_ALWAYS_INLINE auto try_fast_load(Machine& machine, Address mem_addr, isa::Funct3 funct3,
                                           Register& out_val) -> bool;
    SIMRV_ALWAYS_INLINE auto try_fast_store(Machine& machine, Address mem_addr, isa::Funct3 funct3,
                                            Register rrs2) -> bool;
    auto execute_cached_load(Machine& machine, CachedOp& op, Register rrs1) -> bool;
    auto execute_cached_store(Machine& machine, CachedOp& op, Register rrs1, Register rrs2) -> bool;
    auto execute_cached_fallback(Machine& machine) -> void;
    auto dispatch_pending_interrupts() -> void;
    SIMRV_ALWAYS_INLINE auto handle_cached_interrupts() -> void {
        if (simrv::compiler::unlikely((state_.mip & state_.mie) != 0u)) {
            dispatch_pending_interrupts();
        }
    }
    inline void pc_sign_extend() {
        if constexpr (simrv::xlen::kIsXLen64) {
            if (simrv::compiler::unlikely(state_.regs.xlen == 32)) {
                state_.pc =
                    static_cast<Register>(static_cast<int64_t>(static_cast<int32_t>(state_.pc)));
            }
        }
    }

    /// Advance PC by op.len, update instruction counters, sign-extend PC, and process interrupts.
    SIMRV_ALWAYS_INLINE void advance_cached_pc(const CachedOp& op) {
        state_.pc += op.len;
        e_icount++;
        if (op.cinsn) e_ccount++;
        pc_sign_extend();
        handle_cached_interrupts();
    }

    /// Set PC to target_pc, update instruction counters, sign-extend PC, and process interrupts.
    SIMRV_ALWAYS_INLINE void commit_cached_branch_target(const CachedOp& op, Register target_pc) {
        state_.pc = target_pc;
        e_icount++;
        if (op.cinsn) e_ccount++;
        pc_sign_extend();
        handle_cached_interrupts();
    }

    ArchState state_;
    ArchState prev_state_;

   public:
    /// Zero-cost read-only access to architectural state for tracing/logging.
    [[nodiscard]] constexpr auto state() const -> const ArchState& { return state_; }
    /// Mutable access to architectural state for decoupled controllers.
    [[nodiscard]] constexpr auto state() -> ArchState& { return state_; }
    /// Access to architectural state prior to current step for diff rendering.
    [[nodiscard]] constexpr auto prev_state() const -> const ArchState& { return prev_state_; }

    struct TraceHistoryEntry {
        Address pc = 0;
        Instruction inst = 0;
        std::string symbol;
    };

    /// Circular ring buffer for instruction trace history (TUI trace panel).
    /// Fixed capacity of 50; O(1) push with no heap allocation after initialization.
    static constexpr std::size_t kTraceHistoryCapacity = 50;
    std::array<TraceHistoryEntry, kTraceHistoryCapacity> trace_history_buf_{};
    std::size_t trace_history_head_ = 0;  ///< Next write position (wraps around)
    std::size_t trace_history_size_ = 0;  ///< Number of valid entries (≤ capacity)

    /// Provides a read-only view over trace history in chronological order.
    /// Returned as a vector for backward compatibility with TUI consumers.
    [[nodiscard]] auto trace_history_view() const -> std::vector<TraceHistoryEntry>;

    void push_trace_history(Address pc, Instruction inst, const std::string& symbol);

    /// Get the effective privilege level for data accesses (considering MPRV).
    [[nodiscard]] constexpr auto effective_data_privilege() const -> PrivilegeLevel {
        if (simrv::compiler::unlikely(state_.priv == kPrivMachine &&
                                      (state_.mstatus & enum_mask(MstatusBit::Mprv)) != 0)) {
            return static_cast<PrivilegeLevel>((state_.mstatus & enum_mask(MstatusBit::Mpp)) >> 11);
        }
        return state_.priv;
    }

    /**
     * @brief XLEN governing explicit loads/stores, including MPRV/MPP.
     *
     * The privileged architecture requires MPRV accesses to use MPP's XLEN,
     * which can differ from the currently executing M-mode XLEN.
     */
    [[nodiscard]] constexpr auto effective_data_xlen() const -> unsigned {
        return state_.xlen_for_privilege(effective_data_privilege());
    }

    Tlb tlb;

    PlicMmio plic_mmio;
    ClintMmio clint_mmio;
    CsrFile csr_file;
    execute::ExecuteUnit execute_unit;
    simrv::pipeline::PipelineContext pipeline_context;
    simrv::pipeline::PipelineContext* active_context_ = &pipeline_context;
    [[nodiscard]] constexpr auto& active_context() noexcept { return *active_context_; }
    [[nodiscard]] constexpr const auto& active_context() const noexcept { return *active_context_; }
    simrv::cache::ICache icache;
    simrv::cache::DCache dcache;
    sbi::Sbi sbi;
    std::ofstream* trap_log_stream = nullptr;
    bool use_opensbi = false;
    Machine* machine_ = nullptr;
    // Serializes a TUI snapshot with this hart's architectural/pipeline transition. It is only
    // acquired while TUI telemetry is enabled, so headless execution keeps its lock-free path.
    mutable std::mutex tui_snapshot_mutex;
    simrv::pipeline::PipelineSim pipeline_sim;
    simrv::pipeline::CpuModelConfig cpu_model_config{};
    simrv::pipeline::BranchPredictor branch_predictor;
    simrv::pipeline::HartCycleState ca_state{};
    simrv::pipeline::HartPipelineState ca_pipeline{};
    DecodeCache decode_cache;
    alignas(64) std::array<SoftTlbEntry, 2048> soft_tlb_read{};
    alignas(64) std::array<SoftTlbEntry, 2048> soft_tlb_write{};
    uint32_t soft_tlb_epoch = 1;
    void soft_tlb_flush();
    void soft_tlb_flush_selective(bool match_all_vaddr, Address vaddr, bool match_all_asid,
                                  Word asid);

    // ========== Execution Metrics ==========
    uint64_t e_icount{0};                                     // Total instruction count
    Counter e_ccount = 0;                                     // Compressed instructions executed
    std::array<uint64_t, isa::OperationIdCount> e_instmix{};  // Instruction-mix statistics
};

}  // namespace simrv::core
