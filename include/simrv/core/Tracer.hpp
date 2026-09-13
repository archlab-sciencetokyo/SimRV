/**
 * @file Tracer.hpp
 * @brief Architectural and simulation tracing facility.
 */
#pragma once

#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

#include "simrv/Define.hpp"

namespace simrv::core {

class Machine;

class Tracer {
   public:
    explicit Tracer(Machine& machine);
    ~Tracer();

    void init_trace(bool trace_enabled);
    void init_trap_log(bool traplog_mode, const std::string& fn_traplog);
    void init_dlog(bool dlog_mode);

    [[nodiscard]] auto is_trace_enabled() const noexcept -> bool;
    [[nodiscard]] auto is_trap_log_enabled() const noexcept -> bool;
    [[nodiscard]] auto is_dlog_enabled() const noexcept -> bool;

    void dump_init_artifacts();
    void write_instruction_mix_report();
    void print_summary();
    void emit_periodic_pc_trace(Counter mtime, Register cpc);
    void emit_branch_prediction_trace(Counter mtime, Register cpc, Register jmp_pc,
                                      isa::Opcode r_opcode, bool r_tkn);
    void write_trace_snapshot();
    void log_mmio(std::string_view dev_name, Address addr, uint32_t size, Word data, bool is_write);
    void log_trap(Counter mtime, TrapCause cause, Address trap_pc, PrivilegeLevel priv,
                  const ArchState& state, CSRValue tval);
    void log_sbi(Counter mtime, unsigned cause, Word ext_id, Word func_id, Word a0, Word a1,
                 Address pc);
    void flush_all();

    std::ofstream fp_trace;
    std::ofstream fp_dlog;
    std::ofstream fp_traplog;

   private:
    Machine& machine_;
    mutable std::mutex mutex_;
    std::ofstream fp_tracepc_;
    bool tracepc_opened_ = false;
    std::ofstream fp_bpred_;
    bool bpred_opened_ = false;
};

}  // namespace simrv::core
