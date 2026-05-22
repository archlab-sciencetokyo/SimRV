/**
 * @file Tracer.hpp
 * @brief Architectural and simulation tracing facility.
 */
#pragma once

#include <fstream>
#include <string>

#include "simrv/Define.hpp"

namespace simrv::core {

class Machine;

class Tracer {
   public:
    explicit Tracer(Machine& machine);

    void init_trace(bool trace_enabled);
    void init_trap_log(bool traplog_mode, const std::string& fn_traplog);
    void init_dlog(bool dlog_mode);

    void dump_init_artifacts();
    void write_instruction_mix_report();
    void print_summary();
    void emit_periodic_pc_trace(Counter mtime, Register cpc);
    void emit_branch_prediction_trace(Counter mtime, Register cpc, Register jmp_pc, Opcode r_opcode,
                                      bool r_tkn);
    void write_trace_snapshot();

    std::ofstream fp_trace;
    std::ofstream fp_dlog;
    std::ofstream fp_traplog;

   private:
    Machine& machine_;
    std::ofstream fp_tracepc_;
    bool tracepc_opened_ = false;
    std::ofstream fp_bpred_;
    bool bpred_opened_ = false;
};

}  // namespace simrv::core