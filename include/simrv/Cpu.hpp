/**
 * @file Cpu.hpp
 * @brief CPU core state and control declarations.
 */
#pragma once

#include <array>
#include <fstream>

#include "CsrFile.hpp"
#include "DecodeUnit.hpp"
#include "Define.hpp"
#include "ExecuteUnit.hpp"
#include "PipelineContext.hpp"
#include "StateControl.hpp"

class Machine;

struct TLBEntry {
    Word v_addr;
    Word p_addr;
};

class CPU {
   public:
    CPU();
    /// Flush all instruction/data TLB entries.
    void TLB_flush();
    /// Write mstatus with architectural side effects applied.
    void set_mstatus(CSRValue);
    /// Read masked mstatus value with architectural projections.
    [[nodiscard]] auto get_mstatus(CSRValue) const -> CSRValue;
    /// Read a CSR value.
    [[nodiscard]] auto read_csr(CSRAddress) const -> CSRValue;
    /// Write a CSR value.
    void write_csr(CSRAddress, CSRValue);
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
    /// Execute one full CPU cycle (all pipeline stages).
    void run_cycle(Machine& machine);

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

   public:
    Register pc{};
    std::array<Register, 32> reg{};
    std::array<FloatingRegister, 32> freg{};

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
    CSRValue fcsr = 0;

    Address load_res{};
    CSRValue reserved{};
    TrapCause pending_exception{};
    CSRValue pending_tval{};
    PrivilegeLevel priv = static_cast<PrivilegeLevel>(PrivilegeMode::Machine);
    CSRValue plic_pending_irq{};
    CSRValue plic_served_irq{};
    Counter mtime = 1;
    Counter mtimecmp = 0;

    std::array<TLBEntry, simrv::memory::kTlbSize> TLB_inst_r{};
    std::array<TLBEntry, simrv::memory::kTlbSize> TLB_data_r{};
    std::array<TLBEntry, simrv::memory::kTlbSize> TLB_data_w{};

    TlbUnit tlb_unit;
    InterruptController interrupt_controller;
    PlicMmio plic_mmio;
    ClintMmio clint_mmio;
    TrapController trap_controller;
    CsrFile csr_file;
    DecodeUnit decode_unit;
    ExecuteUnit execute_unit;
    PipelineContext pipeline_context;
    std::ofstream* trap_log_stream = nullptr;
};
