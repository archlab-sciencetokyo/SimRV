/**
 * @file Tracer.hpp
 * @brief Architectural and simulation tracing facility.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "simrv/CompilerHints.hpp"
#include "simrv/isa/InstructionFields.hpp"
#include "simrv/isa/TrapTypes.hpp"
#include "simrv/xlen/IdentifierTypes.hpp"

namespace simrv::pipeline {
struct PipelineContext;
}
namespace simrv::memory {
struct DmaTransferTrace;
}

namespace simrv::core {

class Machine;
class CPU;
struct ArchState;
class GzipTraceWriter;

struct BusTraceContext {
    std::optional<uint64_t> sequence;
    std::optional<uint64_t> request_cycle;
    std::optional<uint64_t> completion_cycle;
    std::optional<uint64_t> latency_cycles;
    std::optional<uint32_t> hart;
    std::optional<uint8_t> transfer_size;
    std::optional<uint8_t> burst_length;
    std::optional<uint64_t> data;
    std::optional<uint32_t> byte_enable;
    std::optional<bool> denied;
    std::optional<bool> corrupt;
    std::string master;
    std::string target;
};

struct PipelineStallRecord {
    std::string_view stage;
    simrv::xlen::Address pc = 0;
    uint32_t remaining_cycles = 0;
    std::string_view reason;
};

class Tracer {
   public:
    explicit Tracer(Machine& machine);
    ~Tracer();

    void set_command_line(std::vector<std::string> command_line);
    void refresh_runtime_trace_metadata();
    void init_trace(bool trace_enabled);
    void init_architecture_trace(const std::string& path);
    void init_trap_log(bool traplog_mode, const std::string& fn_traplog);
    void init_dlog(bool dlog_mode);

    [[nodiscard]] SIMRV_ALWAYS_INLINE auto is_trace_enabled() const noexcept -> bool {
        return fp_trace.is_open();
    }
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto is_architecture_trace_enabled() const noexcept -> bool {
        return architecture_trace_enabled_;
    }
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto needs_retiring_context() const noexcept -> bool {
        return retirement_context_needed_;
    }
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto is_trap_log_enabled() const noexcept -> bool {
        return fp_traplog.is_open();
    }
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto is_dlog_enabled() const noexcept -> bool {
        return fp_dlog.is_open();
    }

    void write_instruction_mix_report();
    void print_summary();
    /** Write a stable machine-readable execution summary for automation. */
    [[nodiscard]] auto write_summary_json(const std::string& path) -> bool;
    void emit_branch_prediction_trace(simrv::xlen::Counter mtime, simrv::xlen::Register cpc,
                                      simrv::xlen::Register jmp_pc, isa::Opcode r_opcode,
                                      bool r_tkn);
    void write_trace_snapshot();
    void log_mmio(simrv::xlen::HartId hart, std::string_view dev_name, simrv::xlen::Address addr,
                  uint32_t size, simrv::xlen::Word data, bool is_write, bool faulted = false,
                  bool mapped = true);
    void log_bus_transaction(simrv::xlen::Counter cycle, char channel, std::string_view opcode,
                             std::uint16_t source, std::uint16_t sink, simrv::xlen::Address address,
                             std::string_view detail, const BusTraceContext& context = {});
    void log_pipeline_stalls(const CPU& cpu, std::span<const PipelineStallRecord> stalls);
    void log_dma_transfer(const simrv::memory::DmaTransferTrace& transfer);
    void log_trap(simrv::xlen::Counter mtime, simrv::xlen::Counter cycle,
                  simrv::xlen::TrapCause cause, simrv::xlen::Address trap_pc,
                  simrv::xlen::PrivilegeLevel priv, const ArchState& state,
                  simrv::xlen::CsrValue tval, const CPU& cpu);
    void log_architectural_trap_entry(const CPU& cpu, simrv::xlen::TrapCause cause,
                                      simrv::xlen::Address epc,
                                      simrv::xlen::PrivilegeLevel source_mode,
                                      simrv::xlen::CsrValue tval);
    void log_interrupt_signal(const CPU& cpu, simrv::xlen::TrapCause cause, bool asserted,
                              std::string_view source);
    void log_sbi(simrv::xlen::Counter mtime, simrv::xlen::Counter cycle, unsigned cause,
                 simrv::xlen::Word ext_id, simrv::xlen::Word func_id, simrv::xlen::Word a0,
                 simrv::xlen::Word a1, simrv::xlen::Address pc, simrv::xlen::HartId hart);
    void log_architecture_retirement(const CPU& cpu,
                                     const pipeline::PipelineContext& retiring_context);
    [[nodiscard]] SIMRV_ALWAYS_INLINE auto is_register_write_trace_enabled() const noexcept
        -> bool {
        return fp_registers_.is_open();
    }
    void capture_register_write_before(CPU& cpu, pipeline::PipelineContext& context);
    void flush_all();

    std::ofstream fp_trace;
    std::ofstream fp_dlog;
    std::ofstream fp_traplog;
    std::ofstream fp_archtrace;
    std::ofstream fp_retire_index_;
    std::ofstream fp_calls;
    std::ofstream fp_devices;
    std::ofstream fp_interrupts;
    std::ofstream fp_bus;
    std::ofstream fp_pipeline_;
    std::ofstream fp_memory;
    std::ofstream fp_registers_;

   private:
    [[nodiscard]] auto artifact_path(std::string_view filename) const -> std::filesystem::path;
    void ensure_artifact_directory() const;
    void write_trace_metadata(bool completed);
    [[nodiscard]] auto trace_level_at_least(unsigned level) const noexcept -> bool;
    [[nodiscard]] auto trace_event_selected(std::string_view event) const noexcept -> bool;
    [[nodiscard]] auto accepts_trace_event(std::string_view event, simrv::xlen::Counter cycle,
                                           uint32_t hart,
                                           std::optional<simrv::xlen::Address> pc = std::nullopt,
                                           std::string_view component = {}) const noexcept -> bool;

    Machine& machine_;
    mutable std::mutex mutex_;
    std::ofstream fp_bpred_;
    bool bpred_opened_ = false;
    std::vector<unsigned> call_depth_;
    std::vector<std::unordered_map<uint64_t, std::vector<uint8_t>>> vector_write_before_;
    std::vector<std::string> trace_event_filter_;
    bool architecture_trace_enabled_ = false;
    bool retirement_context_needed_ = false;
    struct TrapTraceFrame {
        simrv::xlen::TrapCause cause{};
        simrv::xlen::Address epc{};
        bool interrupt = false;
    };
    std::vector<std::vector<TrapTraceFrame>> trap_frames_;
    std::filesystem::path trace_metadata_path_;
    std::filesystem::path retire_index_path_;
    std::unique_ptr<GzipTraceWriter> gzip_retire_;
    uint64_t retire_record_count_ = 0;
    unsigned trace_vlen_ = 0;
    size_t trace_harts_ = 0;
    std::string trace_started_at_;
    std::string trace_isa_;
    std::string trace_guest_elf_;
    std::string trace_guest_elf_sha256_;
    std::string trace_configuration_sha256_;
    std::string trace_enabled_devices_json_ = "[]";
    std::vector<std::string> trace_command_line_;
};

}  // namespace simrv::core
