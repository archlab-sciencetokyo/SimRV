/**
 * @file Cpu.hpp
 * @brief CPU core state and control declarations.
 */
#pragma once

#include <array>
#include <expected>

#include <fstream>

#include "simrv/Define.hpp"
#include "simrv/cache/DCache.hpp"
#include "simrv/cache/ICache.hpp"
#include "simrv/core/CsrFile.hpp"
#include "simrv/pipeline/PipelineContext.hpp"
#include "simrv/core/RegisterFile.hpp"
#include "simrv/core/Sbi.hpp"
#include "simrv/core/StateControl.hpp"
#include "simrv/core/Tlb.hpp"
#include "simrv/execute/ExecuteUnit.hpp"

#include "simrv/pipeline/PipelineTask.hpp"
#include "simrv/pipeline/PipelineSim.hpp"
#include "simrv/core/DecodeCache.hpp"

namespace simrv::core {
class Machine;

/**
 * @struct ArchState
 * @brief Groups all architectural registers and CSRs into a cohesive block.
 */
struct ArchState {
    Register pc{};
    RegisterFile regs{};

    CSRValue mstatus{};
    CSRValue mtvec{};
    CSRValue mscratch{};
    CSRValue mepc{};
    TrapCause mcause{};
    CSRValue mtval{};
    CSRValue mhartid{};
    CSRValue misa = kMisaDefault;
    CSRValue mie{};
    CSRValue mip{};
    CSRValue medeleg{};
    CSRValue mideleg{};
    CSRValue mcounteren{};
    CSRValue stvec{};
    CSRValue sscratch{};
    CSRValue sepc{};
    TrapCause scause{};
    CSRValue stval{};
    CSRValue satp{};
    CSRValue scounteren{};
    CSRValue fcsr{};

    PrivilegeLevel priv = kPrivMachine;

    Address load_res{};
    CSRValue reserved{};

    [[nodiscard]] constexpr auto current_xlen() const -> unsigned {
        if constexpr (!simrv::xlen::kIsXLen64) {
            return 32;
        } else {
            const unsigned mxl = (misa >> 62) & 3;
            if (mxl == 1) {
                return 32;
            }
            if (priv == PrivilegeLevel::Machine) {
                return 64;
            } else if (priv == PrivilegeLevel::Supervisor) {
                const unsigned sxl = (mstatus >> 34) & 3;
                return (sxl == 1) ? 32 : 64;
            } else { // User
                const unsigned uxl = (mstatus >> 32) & 3;
                return (uxl == 1) ? 32 : 64;
            }
        }
    }

    constexpr void update_xlen() {
        regs.xlen = current_xlen();
    }
};

struct SoftTlbEntry {
    Address vpn = 0;          // Tag: vaddr >> 12
    Address paddr_base = 0;   // Physical base: paddr & ~0xFFF
    Address asid = 0;         // current_asid tag
    PrivilegeLevel priv = kPrivUser; // privilege level under translation
    bool valid = false;
};

class CPU {
   public:
    CPU();
    /// Flush all instruction/data TLB entries.
    void TLB_flush();
    /// Selectively flush TLB entries matching virtual address and/or ASID criteria.
    void TLB_flush(bool match_all_vaddr, Address vaddr, bool match_all_asid, Word asid);
    /// Write mstatus with architectural side effects applied.
    void set_mstatus(CSRValue);
    /// Read masked mstatus value with architectural projections.
    [[nodiscard]] auto get_mstatus(CSRValue) const -> CSRValue;
    /// Read a CSR value.
    [[nodiscard]] auto read_csr(CSRAddress addr) const -> std::expected<CSRValue, ExceptionCode>;
    /// Write a CSR value.
    auto write_csr(CSRAddress addr, CSRValue val) -> std::expected<void, ExceptionCode>;
    /// Return from machine-mode trap.
    void mret();
    /// Return from supervisor-mode trap.
    void sret();
    /// Recompute MIP from currently pending/served interrupts.
    void plic_update_mip();
    /// Assert or deassert a PLIC interrupt line.
    void plic_set_irq(int, int);
    /// Raise an architectural exception/interrupt trap.
    void raise_exception(TrapCause, CSRValue);
    /// Evaluate CLINT timer interrupt against mtimecmp.
    void evaluate_timer_interrupt();
    /// Execute one full CPU cycle (all pipeline stages).
    void run_cycle(Machine& machine);
    /// Execute one full CPU cycle in optimized baremetal mode.
    void run_cycle_baremetal(Machine& machine);
    /// Execute a cached instruction using the monolithic fast path in IA mode.
    void execute_cached_op_fast(Machine& machine, CachedOp& op);
    
    /// Coroutine generator for persistent zero-allocation pipeline
    simrv::pipeline::PipelineTask run_pipeline_coroutine(Machine* machine);

   public:
    /// Run instruction fetch + decode-normalization stage group.
    void run_fetch_stage(Machine& machine);
    /// Run instruction fetch in baremetal mode.
    void run_fetch_stage_baremetal(Machine& machine);
    /// Run decode + operand-fetch stage group.
    void run_decode_stage(Machine& machine);
    /// Run execute stage group.
    void run_execute_stage(Machine& machine);
    /// Run memory stage group.
    void run_memory_stage(Machine& machine);
    /// Run memory access stage in baremetal mode.
    void run_memory_stage_baremetal(Machine& machine);
    /// Run writeback stage group.
    void run_writeback_stage(Machine& machine);
    /// Run commit/trap resolution stage group.
    void run_commit_stage(Machine& machine);
    /// Run commit stage in baremetal mode.
    void run_commit_stage_baremetal(Machine& machine);

    /// Functional monadic stage transitions (C++23)
    [[nodiscard]] auto fetch_stage(Machine& machine, Address pc) -> bool;
    [[nodiscard]] auto decode_stage(Machine& machine) -> bool;
    [[nodiscard]] auto execute_stage(Machine& machine) -> bool;
    [[nodiscard]] auto memory_stage(Machine& machine) -> bool;
    [[nodiscard]] auto writeback_stage(Machine& machine) -> bool;
    [[nodiscard]] auto commit_stage(Machine& machine) -> bool;

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
    auto execute_cached_lui(CachedOp& op) -> void;
    auto execute_cached_auipc(CachedOp& op) -> void;
    auto execute_cached_jal(CachedOp& op) -> void;
    auto execute_cached_jalr(CachedOp& op, Register rrs1) -> void;
    auto execute_cached_branch(CachedOp& op, Register rrs1, Register rrs2) -> void;
    auto execute_cached_op(CachedOp& op, Register rrs1, Register rrs2) -> void;
    auto execute_cached_op_imm(CachedOp& op, Register rrs1) -> void;
    auto execute_cached_op_imm32(CachedOp& op, Register rrs1) -> void;
    auto execute_cached_op32(CachedOp& op, Register rrs1, Register rrs2) -> void;
    auto execute_cached_control_imm(CachedOp& op, Register rrs1, Register rrs2) -> void;
    auto execute_cached_alu(CachedOp& op, Register rrs1, Register rrs2) -> void;
    auto try_fast_load(Machine& machine, Address mem_addr, Funct3 funct3, Register& out_val) -> bool;
    auto try_fast_store(Machine& machine, Address mem_addr, Funct3 funct3, Register rrs2) -> bool;
    auto execute_cached_load(Machine& machine, CachedOp& op, Register rrs1) -> bool;
    auto execute_cached_store(Machine& machine, CachedOp& op, Register rrs1, Register rrs2) -> bool;
    auto execute_cached_load_store(Machine& machine, CachedOp& op, Register rrs1, Register rrs2) -> bool;
    auto execute_cached_fallback(Machine& machine) -> void;
    auto handle_cached_interrupts() -> void;

    ArchState state_;

   public:
    /// Zero-cost read-only access to architectural state for tracing/logging.
    [[nodiscard]] constexpr auto state() const -> const ArchState& { return state_; }
    /// Mutable access to architectural state for decoupled controllers.
    [[nodiscard]] constexpr auto state() -> ArchState& { return state_; }

    /// Get the effective privilege level for data accesses (considering MPRV).
    [[nodiscard]] constexpr auto effective_data_privilege() const -> PrivilegeLevel {
        if (simrv::compiler::unlikely((state_.mstatus & enum_mask(MstatusBit::Mprv)) != 0)) {
            return static_cast<PrivilegeLevel>((state_.mstatus & enum_mask(MstatusBit::Mpp)) >> 11);
        }
        return state_.priv;
    }

    Tlb tlb;

    PlicMmio plic_mmio;
    ClintMmio clint_mmio;
    CsrFile csr_file;
    execute::ExecuteUnit execute_unit;
    simrv::pipeline::PipelineContext pipeline_context;
    simrv::cache::ICache icache;
    simrv::cache::DCache dcache;
    sbi::Sbi sbi;
    std::ofstream* trap_log_stream = nullptr;
    bool use_opensbi = false;
    Machine* machine_ = nullptr;
    simrv::pipeline::PipelineTask pipeline_task;
    simrv::pipeline::PipelineSim pipeline_sim;
    DecodeCache decode_cache;
    std::array<SoftTlbEntry, 2048> soft_tlb_read{};
    std::array<SoftTlbEntry, 2048> soft_tlb_write{};
    void soft_tlb_flush();

    // ========== Execution Metrics ==========
    uint64_t e_icount{0};                                // Total instruction count
    Counter e_ccount = 0;                           // Compressed instructions executed
    std::array<uint64_t, OperationIdCount> e_instmix{};  // Instruction-mix statistics
};

}  // namespace simrv::core