/**
 * @file Cpu.hpp
 * @brief CPU core state and control declarations.
 */
#pragma once

#include <array>

#include <fstream>

#include "simrv/Define.hpp"
#include "simrv/cache/DCache.hpp"
#include "simrv/cache/ICache.hpp"
#include "simrv/core/CsrFile.hpp"
#include "simrv/core/PipelineContext.hpp"
#include "simrv/core/RegisterFile.hpp"
#include "simrv/core/Sbi.hpp"
#include "simrv/core/StateControl.hpp"
#include "simrv/core/Tlb.hpp"
#include "simrv/execute/ExecuteUnit.hpp"

#include "simrv/core/PipelineTask.hpp"

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
    
    /// Coroutine generator for persistent zero-allocation pipeline
    PipelineTask run_pipeline_coroutine(Machine& machine);

   public:
    /// Run instruction fetch + decode-normalization stage group.
    void run_fetch_stage(Machine& machine);
    /// Run decode + operand-fetch stage group.
    void run_decode_stage(Machine& machine);
    /// Run execute stage group.
    void run_execute_stage(Machine& machine);
    /// Run memory stage group.
    void run_memory_stage(Machine& machine);
    /// Run writeback stage group.
    void run_writeback_stage(Machine& machine);
    /// Run commit/trap resolution stage group.
    void run_commit_stage(Machine& machine);

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
    PipelineContext pipeline_context;
    simrv::cache::ICache icache;
    simrv::cache::DCache dcache;
    sbi::Sbi sbi;
    std::ofstream* trap_log_stream = nullptr;
    bool use_opensbi = false;
    Machine* machine_ = nullptr;
    PipelineTask pipeline_task;

    // ========== Execution Metrics ==========
    uint64_t e_icount{0};                                // Total instruction count
    Counter e_ccount = 0;                           // Compressed instructions executed
    std::array<uint64_t, OperationIdCount> e_instmix{};  // Instruction-mix statistics
};

}  // namespace simrv::core