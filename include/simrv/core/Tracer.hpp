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
#include <vector>

#include "simrv/Define.hpp"

namespace simrv::pipeline {
struct PipelineContext;
}

namespace simrv::core {

class Machine;
class CPU;

class Tracer {
   public:
    explicit Tracer(Machine& machine);
    ~Tracer();

    void init_trace(bool trace_enabled);
    void init_architecture_trace(const std::string& path);
    void init_trap_log(bool traplog_mode, const std::string& fn_traplog);
    void init_dlog(bool dlog_mode);

    [[nodiscard]] auto is_trace_enabled() const noexcept -> bool;
    [[nodiscard]] auto is_architecture_trace_enabled() const noexcept -> bool;
    [[nodiscard]] auto is_trap_log_enabled() const noexcept -> bool;
    [[nodiscard]] auto is_dlog_enabled() const noexcept -> bool;

    void dump_init_artifacts();
    void write_instruction_mix_report();
    void print_summary();
    /** Write a stable machine-readable execution summary for automation. */
    [[nodiscard]] auto write_summary_json(const std::string& path) -> bool;
    void emit_periodic_pc_trace(Counter mtime, Register cpc);
    void emit_branch_prediction_trace(Counter mtime, Register cpc, Register jmp_pc,
                                      isa::Opcode r_opcode, bool r_tkn);
    void write_trace_snapshot();
    void log_mmio(std::string_view dev_name, Address addr, uint32_t size, Word data, bool is_write);
    void log_trap(Counter mtime, TrapCause cause, Address trap_pc, PrivilegeLevel priv,
                  const ArchState& state, CSRValue tval);
    void log_sbi(Counter mtime, unsigned cause, Word ext_id, Word func_id, Word a0, Word a1,
                 Address pc);
    void log_architecture_retirement(const CPU& cpu,
                                     const pipeline::PipelineContext& retiring_context);
    void flush_all();

    std::ofstream fp_trace;
    std::ofstream fp_dlog;
    std::ofstream fp_traplog;
    std::ofstream fp_archtrace;
    std::ofstream fp_calls;
    std::ofstream fp_devices;

   private:
    [[nodiscard]] auto artifact_path(std::string_view filename) const -> std::filesystem::path;
    void ensure_artifact_directory() const;
    void write_trace_metadata(bool completed);
    [[nodiscard]] auto trace_level_at_least(unsigned level) const noexcept -> bool;

    Machine& machine_;
    mutable std::mutex mutex_;
    std::ofstream fp_tracepc_;
    bool tracepc_opened_ = false;
    std::ofstream fp_bpred_;
    bool bpred_opened_ = false;
    std::vector<unsigned> call_depth_;
    std::filesystem::path trace_metadata_path_;
    unsigned trace_vlen_ = 0;
    size_t trace_harts_ = 0;
    std::string trace_started_at_;
};

}  // namespace simrv::core
