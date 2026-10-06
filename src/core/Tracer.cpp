/**
 * @file Tracer.cpp
 * @brief Architectural and simulation tracing facility implementation.
 */
#include "simrv/core/Tracer.hpp"

#include <sys/utsname.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <map>
#include <ostream>
#include <print>
#include <ranges>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

#include "simrv/Define.hpp"
#include "simrv/core/BuildInfo.hpp"
#include "simrv/core/Cpu.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/debug/SpikeLockstep.hpp"
#include "simrv/device/pci/VirtioPciBlock.hpp"
#include "simrv/device/pci/VirtioPciConsole.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/memory/TileLinkNode.hpp"
#include "simrv/pipeline/Decoder.hpp"
#include "simrv/pipeline/OperationInfo.hpp"
#include "simrv/pipeline/PipelineConfig.hpp"
#include "simrv/util/FormatUtil.hpp"
#include "simrv/util/Sha256.hpp"
#include "simrv/xlen/Constants.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {

using namespace simrv::isa;

class GzipTraceWriter {
   public:
    GzipTraceWriter() { buffer_.reserve(kBufferSize); }
    GzipTraceWriter(const GzipTraceWriter&) = delete;
    auto operator=(const GzipTraceWriter&) -> GzipTraceWriter& = delete;
    ~GzipTraceWriter() { close(); }

    auto open(const std::filesystem::path& path) -> bool {
        close();
#if defined(__unix__) || defined(__APPLE__)
        auto& api = gzip_api();
        if (!api.available()) return false;
        const auto native_path = path.string();
        // Level 1 favors throughput for high-volume retirement streams.
        file_ = api.open(native_path.c_str(), "wb1");
        return file_ != nullptr;
#else
        (void)path;
        return false;
#endif
    }

    auto is_open() const noexcept -> bool { return file_ != nullptr; }

    auto write(std::string_view bytes) -> bool {
#if defined(__unix__) || defined(__APPLE__)
        if (file_ == nullptr) return false;
        buffer_.append(bytes);
        return buffer_.size() < kBufferSize || write_pending();
#else
        (void)bytes;
        return false;
#endif
    }

    auto flush() -> bool {
#if defined(__unix__) || defined(__APPLE__)
        // zlib's Z_SYNC_FLUSH value; this keeps normal trace flush points observable.
        return file_ == nullptr || (write_pending() && gzip_api().flush(file_, 2) == 0);
#else
        return false;
#endif
    }

    auto close() -> bool {
#if defined(__unix__) || defined(__APPLE__)
        if (file_ == nullptr) return true;
        const bool written = write_pending();
        auto* file = std::exchange(file_, nullptr);
        return gzip_api().close(file) == 0 && written;
#else
        return true;
#endif
    }

   private:
    static constexpr size_t kBufferSize = 64 * 1024;
    std::string buffer_;
#if defined(__unix__) || defined(__APPLE__)
    struct gzFile_s;
    using GzFile = gzFile_s*;
    using GzOpen = GzFile (*)(const char*, const char*);
    using GzWrite = int (*)(GzFile, const void*, unsigned);
    using GzFlush = int (*)(GzFile, int);
    using GzClose = int (*)(GzFile);

    struct Api {
        void* library = nullptr;
        GzOpen open = nullptr;
        GzWrite write = nullptr;
        GzFlush flush = nullptr;
        GzClose close = nullptr;

        template <typename Function>
        static auto load(void* handle, const char* name) -> Function {
            void* symbol = dlsym(handle, name);
            Function function{};
            static_assert(sizeof(function) == sizeof(symbol));
            std::memcpy(&function, &symbol, sizeof(function));
            return function;
        }

        Api() {
#if defined(__APPLE__)
            constexpr std::array kLibraryNames = {"libz.dylib", "libz.1.dylib"};
#else
            constexpr std::array kLibraryNames = {"libz.so.1", "libz.so"};
#endif
            for (const auto* name : kLibraryNames) {
                library = dlopen(name, RTLD_NOW | RTLD_LOCAL);
                if (library != nullptr) break;
            }
            if (library == nullptr) return;
            open = load<GzOpen>(library, "gzopen");
            write = load<GzWrite>(library, "gzwrite");
            flush = load<GzFlush>(library, "gzflush");
            close = load<GzClose>(library, "gzclose");
            if (open == nullptr || write == nullptr || flush == nullptr || close == nullptr) {
                dlclose(library);
                library = nullptr;
            }
        }

        ~Api() {
            if (library != nullptr) dlclose(library);
        }

        [[nodiscard]] auto available() const noexcept -> bool { return library != nullptr; }
    };

    static auto gzip_api() -> Api& {
        static Api api;
        return api;
    }

    auto write_pending() -> bool {
        if (buffer_.empty()) return true;
        auto& api = gzip_api();
        size_t offset = 0;
        while (offset < buffer_.size()) {
            const auto chunk = static_cast<unsigned>(
                std::min<size_t>(buffer_.size() - offset, std::numeric_limits<unsigned>::max()));
            const int written = api.write(file_, buffer_.data() + offset, chunk);
            if (written <= 0) return false;
            offset += static_cast<size_t>(written);
        }
        buffer_.clear();
        return true;
    }

    GzFile file_ = nullptr;
#else
    void* file_ = nullptr;
#endif
};

namespace {

constexpr auto D_TRACE_HEX_WIDTH = static_cast<int>(kXLenHexDigits);
constexpr Counter D_TRACEPC_INTERVAL = 1000;
constexpr uint64_t kRetireIndexStride = 256;

auto trace_timestamp() -> std::string {
    const auto now = std::chrono::system_clock::now();
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
    gmtime_r(&time, &utc);
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z", utc.tm_year + 1900,
                       utc.tm_mon + 1, utc.tm_mday, utc.tm_hour, utc.tm_min, utc.tm_sec,
                       millis.count());
}

auto privilege_mode(PrivilegeLevel privilege) noexcept -> std::string_view {
    switch (privilege) {
        case PrivilegeLevel::User:
            return "U";
        case PrivilegeLevel::Supervisor:
            return "S";
        case PrivilegeLevel::Machine:
            return "M";
    }
    return "unknown";
}

auto json_quote(std::string_view value) -> std::string {
    std::string quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back('"');
    constexpr char kHex[] = "0123456789abcdef";
    for (const unsigned char ch : value) {
        switch (ch) {
            case '"':
                quoted += "\\\"";
                break;
            case '\\':
                quoted += "\\\\";
                break;
            case '\b':
                quoted += "\\b";
                break;
            case '\f':
                quoted += "\\f";
                break;
            case '\n':
                quoted += "\\n";
                break;
            case '\r':
                quoted += "\\r";
                break;
            case '\t':
                quoted += "\\t";
                break;
            default:
                if (ch < 0x20) {
                    quoted += "\\u00";
                    quoted.push_back(kHex[ch >> 4]);
                    quoted.push_back(kHex[ch & 0x0f]);
                } else {
                    quoted.push_back(static_cast<char>(ch));
                }
        }
    }
    quoted.push_back('"');
    return quoted;
}

auto json_string_array(const std::vector<std::string>& values) -> std::string {
    std::string json = "[";
    for (size_t i = 0; i < values.size(); ++i) {
        if (i != 0) json.push_back(',');
        json += json_quote(values[i]);
    }
    json.push_back(']');
    return json;
}

auto event_function_field(const Machine& machine, Address pc) -> std::string {
    const auto symbol = machine.symbol_table().lookup_function(pc);
    return symbol ? std::format(",\"function\":{}", json_quote(symbol->name)) : "";
}

auto categorize_operation(isa::OperationId op) noexcept -> std::string_view {
    const auto op_info = pipeline::operation::info(op);
    if (op_info.control == pipeline::operation::ControlFlowKind::Branch) return "Branch";
    if (op_info.control == pipeline::operation::ControlFlowKind::Jump) return "Jump";
    if (op_info.memory == pipeline::operation::MemoryAccessKind::Load) return "Load";
    if (op_info.memory == pipeline::operation::MemoryAccessKind::Store) return "Store";
    if (op_info.memory == pipeline::operation::MemoryAccessKind::Atomic) return "Atomic";
    if (op_info.execution_class == pipeline::operation::ExecutionClass::Multiply ||
        op_info.execution_class == pipeline::operation::ExecutionClass::DivideOrRemainder)
        return "Mul/Div";
    if (op_info.execution_class == pipeline::operation::ExecutionClass::FpAlu ||
        op_info.execution_class == pipeline::operation::ExecutionClass::FpDivideOrSqrt)
        return "Float";
    if (op_info.operands.rd == pipeline::operation::RegBank::Vector ||
        op_info.operands.rs1 == pipeline::operation::RegBank::Vector ||
        op_info.operands.rs2 == pipeline::operation::RegBank::Vector)
        return "Vector";
    if (op_info.side_effects != pipeline::operation::SideEffectFlags::None) return "System/CSR";
    return "Integer/ALU";
}

}  // namespace

Tracer::Tracer(Machine& machine) : machine_(machine) {}

Tracer::~Tracer() {
    machine_.dma_engine().set_trace_observer({});
    write_trace_metadata(true);
    flush_all();
    if (gzip_retire_) gzip_retire_->close();
}

void Tracer::set_command_line(std::vector<std::string> command_line) {
    trace_command_line_ = std::move(command_line);
    write_trace_metadata(false);
}

void Tracer::refresh_runtime_trace_metadata() {
    if (trace_metadata_path_.empty()) return;
    // MISA is finalized during machine initialization, after the trace streams
    // are opened. Replace the constructor's default profile with the effective
    // guest ISA before publishing runtime metadata.
    trace_isa_ = simrv::debug::spike_isa_string(machine_.primary_hart().state().misa);
    trace_vlen_ = machine_.isa_config().vlen;
    trace_harts_ = machine_.num_harts();
    trace_enabled_devices_json_ = "[";
    bool first_device = true;
    for (const auto* device : machine_.memory().system_bus().router().devices()) {
        if (device == nullptr) continue;
        if (!first_device) trace_enabled_devices_json_.push_back(',');
        first_device = false;
        trace_enabled_devices_json_ += std::format(
            "{{\"name\":{},\"base\":\"0x{:x}\",\"size\":\"0x{:x}\"}}",
            json_quote(device->name() != nullptr ? device->name() : ""),
            static_cast<uint64_t>(device->base_address()), static_cast<uint64_t>(device->size()));
    }
    trace_enabled_devices_json_.push_back(']');
    trace_configuration_sha256_ = simrv::util::sha256(
        trace_configuration_sha256_ + "\nregistered_devices=" + trace_enabled_devices_json_);
    write_trace_metadata(false);
}

auto Tracer::trace_level_at_least(unsigned level) const noexcept -> bool {
    return machine_.configuration().debug.trace_level >= level;
}

auto Tracer::accepts_trace_event(std::string_view event, Counter cycle, uint32_t hart,
                                 std::optional<Address> pc,
                                 std::string_view component) const noexcept -> bool {
    const auto& config = machine_.configuration().debug;
    if (!config.trace_device.empty() && component != config.trace_device) return false;
    if (!config.trace_function.empty()) {
        if (!pc) return false;
        const auto symbol = machine_.symbol_table().lookup_function(*pc);
        if (!symbol || symbol->name != config.trace_function) return false;
    }
    if (config.trace_hart &&
        (hart == std::numeric_limits<uint32_t>::max() || *config.trace_hart != hart))
        return false;
    if (config.trace_pc_start || config.trace_pc_end) {
        if (!config.trace_pc_start || !config.trace_pc_end || !pc || *pc < *config.trace_pc_start ||
            *pc > *config.trace_pc_end)
            return false;
    }
    if (config.trace_after_cycle && cycle < *config.trace_after_cycle) return false;
    if (config.trace_before_cycle && cycle > *config.trace_before_cycle) return false;
    if (config.trace_events.empty()) return true;

    std::string_view remaining = config.trace_events;
    while (!remaining.empty()) {
        const auto comma = remaining.find(',');
        auto candidate = remaining.substr(0, comma);
        while (!candidate.empty() && (candidate.front() == ' ' || candidate.front() == '\t'))
            candidate.remove_prefix(1);
        while (!candidate.empty() && (candidate.back() == ' ' || candidate.back() == '\t'))
            candidate.remove_suffix(1);
        if (candidate == event) return true;
        if (comma == std::string_view::npos) break;
        remaining.remove_prefix(comma + 1);
    }
    return false;
}

auto Tracer::artifact_path(std::string_view filename) const -> std::filesystem::path {
    return std::filesystem::path(machine_.configuration().debug.trace_dir) / filename;
}

void Tracer::ensure_artifact_directory() const {
    std::error_code ec;
    std::filesystem::create_directories(machine_.configuration().debug.trace_dir, ec);
}

void Tracer::init_trace(bool trace_enabled) {
    fp_trace.close();
    if (trace_enabled) {
        ensure_artifact_directory();
        fp_trace.clear();
        fp_trace.open(artifact_path("trace.txt"));
    }
}

void Tracer::init_architecture_trace(const std::string& path) {
    machine_.dma_engine().set_trace_observer({});
    if (gzip_retire_) gzip_retire_->close();
    gzip_retire_.reset();
    fp_archtrace.close();
    fp_retire_index_.close();
    fp_calls.close();
    fp_devices.close();
    fp_interrupts.close();
    fp_bus.close();
    fp_pipeline_.close();
    fp_memory.close();
    fp_markers.close();
    fp_registers_.close();
    trace_metadata_path_.clear();
    retire_index_path_.clear();
    retire_record_count_ = 0;
    trace_started_at_.clear();
    trace_vlen_ = 0;
    trace_harts_ = 0;
    call_depth_.clear();
    vector_write_before_.clear();
    trap_frames_.clear();
    marker_buffers_.clear();
    marker_depth_.clear();
    if (path.empty()) return;
    std::error_code ec;
    const std::filesystem::path trace_path(path);
    const bool gzip_retire = trace_path.extension() == ".gz";
    if (trace_path.extension() == ".zst") {
        simrv::log::error("zstd retirement trace output is not supported; use a .gz suffix");
        return;
    }
    const auto trace_directory =
        trace_path.has_parent_path() ? trace_path.parent_path() : std::filesystem::path(".");
    std::filesystem::create_directories(trace_directory, ec);
    if (ec) return;
    trace_metadata_path_ = trace_directory / "metadata.json";
    trace_started_at_ = trace_timestamp();
    trace_vlen_ = machine_.isa_config().vlen;
    trace_harts_ = machine_.num_harts();
    trace_isa_ = simrv::debug::spike_isa_string(machine_.primary_hart().state().misa);
    trace_guest_elf_ = machine_.binary_path();
    trace_guest_elf_sha256_.clear();
    if (!trace_guest_elf_.empty()) {
        if (const auto digest = simrv::util::sha256_file(trace_guest_elf_)) {
            trace_guest_elf_sha256_ = *digest;
        }
    }
    const auto& config = machine_.configuration();
    auto configuration_material = std::format(
        "{{\"xlen\":{},\"isa\":{},\"misa\":\"0x{:x}\",\"vlen\":{},"
        "\"harts\":{},\"dram_base\":\"0x{:x}\",\"dram_size\":\"0x{:x}\","
        "\"appmode\":{},\"engine\":{},\"pipeline\":{},\"platform_profile\":{},"
        "\"cpu_model_preset\":{},\"branch_predictor\":{},\"smp_quantum\":{},"
        "\"smp_multithreaded\":{},\"soc\":{},\"soc_cpu_model\":{},"
        "\"soc_pcie\":{},\"soc_mmio\":{},\"soc_disable_unlisted\":{},"
        "\"trace_level\":{},\"trace_events\":{},\"trace_function\":{},"
        "\"trace_device\":{},\"trace_hart\":{},"
        "\"trace_pc_start\":{},\"trace_pc_end\":{},"
        "\"trace_after_cycle\":{},\"trace_before_cycle\":{},"
        "\"checkpoint_every\":{},\"checkpoint_dir\":{},"
        "\"binary\":{},\"disk\":{},\"disk_enabled\":{},"
        "\"cpuconfig\":{},\"dvtree\":{},\"command_line\":{},\"devices\":[",
        simrv::xlen::kXLenBits, json_quote(trace_isa_),
        static_cast<uint64_t>(machine_.primary_hart().state().misa), trace_vlen_, trace_harts_,
        static_cast<uint64_t>(config.memory.dram_base),
        static_cast<uint64_t>(config.memory.dram_size), config.execution.appmode,
        json_quote(machine_.runtime_profile.execution_name()),
        json_quote(pipeline::pipeline_type_name(config.execution.pipeline_type)),
        std::to_underlying(config.platform_profile),
        config.cpu_model_preset ? std::format("{}", std::to_underlying(*config.cpu_model_preset))
                                : "null",
        config.branch_predictor_type
            ? std::format("{}", std::to_underlying(*config.branch_predictor_type))
            : "null",
        config.execution.smp_quantum, config.execution.smp_multithreaded,
        json_quote(config.soc.name), json_quote(config.soc.cpu_model), config.soc.enable_pcie,
        config.soc.enable_mmio, config.soc.disable_unlisted_devices, config.debug.trace_level,
        json_quote(config.debug.trace_events), json_quote(config.debug.trace_function),
        json_quote(config.debug.trace_device),
        config.debug.trace_hart ? std::format("{}", *config.debug.trace_hart) : "null",
        config.debug.trace_pc_start
            ? json_quote(std::format("0x{:x}", static_cast<uint64_t>(*config.debug.trace_pc_start)))
            : "null",
        config.debug.trace_pc_end
            ? json_quote(std::format("0x{:x}", static_cast<uint64_t>(*config.debug.trace_pc_end)))
            : "null",
        config.debug.trace_after_cycle ? std::format("{}", *config.debug.trace_after_cycle)
                                       : "null",
        config.debug.trace_before_cycle ? std::format("{}", *config.debug.trace_before_cycle)
                                        : "null",
        config.debug.checkpoint_every, json_quote(config.debug.checkpoint_dir),
        json_quote(config.files.binary_path), json_quote(config.files.disk_path),
        config.files.disk_enabled, json_quote(config.files.cpuconfig_path),
        json_quote(config.files.dvtree_path), json_string_array(trace_command_line_));
    bool first_device = true;
    for (const auto& device : config.soc.devices) {
        if (!first_device) configuration_material.push_back(',');
        first_device = false;
        configuration_material += std::format(
            "{{\"kind\":{},\"name\":{},\"base\":\"0x{:x}\","
            "\"size\":\"0x{:x}\",\"irq\":{},\"enabled\":{}}}",
            json_quote(soc_device_kind_name(device.kind)), json_quote(device.name),
            static_cast<uint64_t>(device.base), static_cast<uint64_t>(device.size), device.irq,
            device.enabled);
    }
    configuration_material += "]}";
    trace_configuration_sha256_ = simrv::util::sha256(configuration_material);
    trace_enabled_devices_json_ = "[]";
    fp_calls.open(trace_directory / "calls.jsonl", std::ios::out | std::ios::trunc);
    fp_devices.open(trace_directory / "devices.jsonl", std::ios::out | std::ios::trunc);
    fp_interrupts.open(trace_directory / "interrupts.jsonl", std::ios::out | std::ios::trunc);
    fp_bus.open(trace_directory / "bus.jsonl", std::ios::out | std::ios::trunc);
    if (machine_.configuration().debug.trace_level >= 4) {
        fp_memory.open(trace_directory / "memory.jsonl", std::ios::out | std::ios::trunc);
        fp_pipeline_.open(trace_directory / "pipeline.jsonl", std::ios::out | std::ios::trunc);
    }
    fp_markers.open(trace_directory / "markers.jsonl", std::ios::out | std::ios::trunc);
    if (config.debug.trace_register_writes) {
        fp_registers_.open(trace_directory / "registers.jsonl", std::ios::out | std::ios::trunc);
        vector_write_before_.resize(machine_.num_harts());
    }
    if (gzip_retire) {
        gzip_retire_ = std::make_unique<GzipTraceWriter>();
        if (!gzip_retire_->open(trace_path)) {
            simrv::log::error("cannot open compressed retirement trace {}; shared zlib is required",
                              trace_path.string());
            gzip_retire_.reset();
        }
    } else {
        fp_archtrace.clear();
        fp_archtrace.open(trace_path, std::ios::out | std::ios::trunc);
    }
    if (is_architecture_trace_enabled()) {
        machine_.dma_engine().set_trace_observer(
            [this](const simrv::memory::DmaTransferTrace& transfer) {
                log_dma_transfer(transfer);
            });
        if (!gzip_retire) {
            retire_index_path_ = trace_directory / (trace_path.stem().string() + ".index.jsonl");
            fp_retire_index_.open(retire_index_path_, std::ios::out | std::ios::trunc);
        }
        call_depth_.assign(machine_.num_harts(), 0);
        trap_frames_.resize(machine_.num_harts());
        const auto header = std::format(
            "{{\"schema_version\":1,\"event\":\"header\",\"xlen\":{},"
            "\"vlen\":{},\"harts\":{}}}\n",
            simrv::xlen::kXLenBits, machine_.isa_config().vlen, machine_.num_harts());
        if (gzip_retire) {
            if (!gzip_retire_->write(header)) {
                simrv::log::error("failed writing compressed retirement trace header");
                gzip_retire_->close();
            }
        } else {
            fp_archtrace << header;
        }
        if (fp_retire_index_.is_open()) {
            std::println(fp_retire_index_,
                         "{{\"schema_version\":1,\"kind\":\"trace_index\","
                         "\"stream\":\"retire\",\"stride\":{},"
                         "\"offset_units\":\"bytes\"}}",
                         kRetireIndexStride);
        }
    }
    if (fp_calls.is_open()) {
        std::println(fp_calls,
                     "{{\"schema_version\":2,\"event\":\"header\","
                     "\"timestamp\":\"{}\",\"cycle\":0,\"hart\":0,"
                     "\"mode\":\"M\",\"payload\":{{\"stream\":\"calls\"}}}}",
                     trace_timestamp());
    }
    if (fp_devices.is_open()) {
        std::println(fp_devices,
                     "{{\"schema_version\":2,\"event\":\"header\","
                     "\"timestamp\":\"{}\",\"cycle\":0,\"hart\":0,"
                     "\"mode\":\"M\",\"payload\":{{\"stream\":\"devices\"}}}}",
                     trace_timestamp());
    }
    if (fp_interrupts.is_open()) {
        std::println(fp_interrupts,
                     "{{\"schema_version\":2,\"event\":\"header\","
                     "\"timestamp\":\"{}\",\"cycle\":0,\"hart\":0,"
                     "\"mode\":\"M\",\"payload\":{{\"stream\":\"interrupts\"}}}}",
                     trace_timestamp());
    }
    if (fp_bus.is_open()) {
        std::println(fp_bus,
                     "{{\"schema_version\":2,\"event\":\"header\","
                     "\"timestamp\":\"{}\",\"cycle\":0,\"hart\":0,"
                     "\"mode\":\"M\",\"payload\":{{\"stream\":\"bus\"}}}}",
                     trace_timestamp());
    }
    if (fp_memory.is_open()) {
        std::println(
            fp_memory,
            "{{\"schema_version\":2,\"event\":\"header\","
            "\"timestamp\":\"{}\",\"cycle\":0,\"hart\":0,"
            "\"mode\":\"M\",\"payload\":{{\"stream\":\"memory\","
            "\"semantics\":\"retired scalar data accesses and precise synchronous faults\"}}}}",
            trace_timestamp());
    }
    if (fp_markers.is_open()) {
        std::println(fp_markers,
                     "{{\"schema_version\":2,\"event\":\"header\","
                     "\"timestamp\":\"{}\",\"cycle\":0,\"hart\":0,"
                     "\"mode\":\"M\",\"payload\":{{\"stream\":\"markers\","
                     "\"abi\":\"Xsimrvtrace-0.1\",\"csr\":\"0x800\"}}}}",
                     trace_timestamp());
    }
    write_trace_metadata(false);
}

void Tracer::init_trap_log(bool traplog_mode, const std::string& fn_traplog) {
    fp_traplog.close();
    if (traplog_mode) {
        const std::filesystem::path path =
            fn_traplog.empty() ? artifact_path("traplog.txt") : std::filesystem::path(fn_traplog);
        std::error_code ec;
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path(), ec);
        }
        fp_traplog.clear();
        fp_traplog.open(path, std::ios::out | std::ios::trunc);
    }
}

void Tracer::init_dlog(bool dlog_mode) {
    fp_dlog.close();
    if (dlog_mode) {
        ensure_artifact_directory();
        fp_dlog.clear();
        fp_dlog.open(artifact_path("dlog.txt"), std::ios::out | std::ios::trunc);
    }
}

auto Tracer::is_trace_enabled() const noexcept -> bool { return fp_trace.is_open(); }
auto Tracer::is_architecture_trace_enabled() const noexcept -> bool {
    return fp_archtrace.is_open() || (gzip_retire_ && gzip_retire_->is_open());
}
auto Tracer::is_trap_log_enabled() const noexcept -> bool { return fp_traplog.is_open(); }
auto Tracer::is_dlog_enabled() const noexcept -> bool { return fp_dlog.is_open(); }

auto Tracer::is_register_write_trace_enabled() const noexcept -> bool {
    return fp_registers_.is_open();
}

void Tracer::capture_register_write_before(CPU& cpu, pipeline::PipelineContext& context) {
    if (!fp_registers_.is_open() || !context.traits.writes_vec) return;
    std::lock_guard lock(mutex_);
    const auto hart = static_cast<size_t>(cpu.state().mhartid);
    if (hart >= vector_write_before_.size()) vector_write_before_.resize(hart + 1);
    if (context.trace_sequence == 0) return;
    auto& bytes = vector_write_before_[hart][context.trace_sequence];
    bytes.clear();
    bytes.reserve(32 * (trace_vlen_ / 8));
    for (unsigned index = 0; index < 32; ++index) {
        const auto& reg = cpu.state().regs.read_vector(static_cast<RegId>(index));
        bytes.insert(bytes.end(), reg.u8.begin(),
                     reg.u8.begin() + static_cast<std::ptrdiff_t>(trace_vlen_ / 8));
    }
}

void Tracer::flush_all() {
    std::lock_guard lock(mutex_);
    if (fp_trace.is_open()) fp_trace.flush();
    if (fp_dlog.is_open()) fp_dlog.flush();
    if (fp_traplog.is_open()) fp_traplog.flush();
    if (fp_archtrace.is_open()) fp_archtrace.flush();
    if (gzip_retire_ && gzip_retire_->is_open() && !gzip_retire_->flush())
        simrv::log::error("failed flushing compressed retirement trace");
    if (fp_retire_index_.is_open()) fp_retire_index_.flush();
    if (fp_calls.is_open()) fp_calls.flush();
    if (fp_devices.is_open()) fp_devices.flush();
    if (fp_interrupts.is_open()) fp_interrupts.flush();
    if (fp_bus.is_open()) fp_bus.flush();
    if (fp_pipeline_.is_open()) fp_pipeline_.flush();
    if (fp_memory.is_open()) fp_memory.flush();
    if (fp_markers.is_open()) fp_markers.flush();
    if (fp_registers_.is_open()) fp_registers_.flush();
    if (fp_tracepc_.is_open()) fp_tracepc_.flush();
    if (fp_bpred_.is_open()) fp_bpred_.flush();
}

void Tracer::log_architectural_trap_entry(const CPU& cpu, TrapCause cause, Address epc,
                                          PrivilegeLevel source_mode, CSRValue tval) {
    if (!fp_interrupts.is_open() || !trace_level_at_least(1)) return;
    const auto& state = cpu.state();
    const auto hart = static_cast<size_t>(state.mhartid);
    const auto cycle = cpu.clint_mmio.mcycle;
    std::lock_guard lock(mutex_);
    if (hart >= trap_frames_.size()) trap_frames_.resize(hart + 1);
    auto& frames = trap_frames_[hart];
    const bool interrupt = trap_is_interrupt(cause);
    frames.push_back({.cause = cause, .epc = epc, .interrupt = interrupt});
    if (!interrupt) return;
    const auto depth =
        std::ranges::count_if(frames, [](const TrapTraceFrame& frame) { return frame.interrupt; });
    if (!accepts_trace_event("interrupt_entry", cycle, static_cast<uint32_t>(hart), state.pc))
        return;
    std::println(fp_interrupts,
                 "{{\"schema_version\":2,\"event\":\"interrupt_entry\","
                 "\"timestamp\":\"{}\",\"cycle\":{},\"hart\":{},"
                 "\"pc\":\"0x{:x}\"{},\"mode\":\"{}\",\"payload\":{{"
                 "\"cause\":{},\"cause_value\":\"0x{:x}\",\"cause_name\":\"{}\","
                 "\"cause_register\":\"{}\",\"epc_register\":\"{}\","
                 "\"epc\":\"0x{:x}\",\"tval_register\":\"{}\","
                 "\"tval\":\"0x{:x}\","
                 "\"source_mode\":\"{}\",\"nesting_depth\":{}}}}}",
                 trace_timestamp(), cpu.clint_mmio.mcycle, static_cast<unsigned>(state.mhartid),
                 static_cast<uint64_t>(state.pc), event_function_field(machine_, state.pc),
                 privilege_mode(state.priv), static_cast<uint64_t>(trap_exception_code(cause)),
                 static_cast<uint64_t>(cause), trap_cause_name(cause),
                 state.priv == PrivilegeLevel::Supervisor ? "scause" : "mcause",
                 state.priv == PrivilegeLevel::Supervisor ? "sepc" : "mepc",
                 static_cast<uint64_t>(epc),
                 state.priv == PrivilegeLevel::Supervisor ? "stval" : "mtval",
                 static_cast<uint64_t>(tval), privilege_mode(source_mode), depth);
}

void Tracer::log_interrupt_signal(const CPU& cpu, TrapCause cause, bool asserted,
                                  std::string_view source) {
    if (!fp_interrupts.is_open() || !trace_level_at_least(1)) return;
    const auto& state = cpu.state();
    const auto event = asserted ? "interrupt_asserted" : "interrupt_deasserted";
    const auto cycle = cpu.clint_mmio.mcycle;
    if (!accepts_trace_event(event, cycle, static_cast<uint32_t>(state.mhartid), std::nullopt,
                             source))
        return;
    std::lock_guard lock(mutex_);
    std::println(fp_interrupts,
                 "{{\"schema_version\":2,\"event\":\"interrupt_{}\","
                 "\"timestamp\":\"{}\",\"cycle\":{},\"hart\":{},"
                 "\"mode\":\"{}\",\"component\":\"{}\",\"payload\":{{"
                 "\"cause\":{},\"cause_name\":\"{}\",\"asserted\":{}}}}}",
                 asserted ? "asserted" : "deasserted", trace_timestamp(), cpu.clint_mmio.mcycle,
                 static_cast<unsigned>(state.mhartid), privilege_mode(state.priv), source,
                 static_cast<uint64_t>(trap_exception_code(cause)), trap_cause_name(cause),
                 asserted ? "true" : "false");
}

void Tracer::log_mmio(HartId hart, std::string_view dev_name, Address addr, uint32_t size,
                      Word data, bool is_write, bool faulted, bool mapped) {
    if (!fp_dlog.is_open() && !(fp_devices.is_open() && trace_level_at_least(1))) return;
    const auto& cpu = machine_.hart(hart);
    const auto mtime = cpu.clint_mmio.mtime.load(std::memory_order_relaxed);
    const auto cycle = cpu.clint_mmio.mcycle;
    const auto event =
        std::format("{}_{}", mapped ? "mmio" : "unmapped", is_write ? "write" : "read");
    std::lock_guard lock(mutex_);
    if (fp_dlog.is_open() && !faulted) {
        std::println(fp_dlog, "[mtime={:12}] [{:<16}] {:5} addr=0x{:0{}x} size={:2} data=0x{:0{}x}",
                     mtime, dev_name, is_write ? "WRITE" : "READ", addr, kXLenHexDigits, size, data,
                     size * 2);
    }
    if (fp_devices.is_open() && trace_level_at_least(1) &&
        accepts_trace_event(event, cycle, hart.val, cpu.active_context().cpc.raw(), dev_name)) {
        std::println(fp_devices,
                     "{{\"schema_version\":2,\"event\":\"{}_{}\","
                     "\"timestamp\":\"{}\",\"cycle\":{},\"hart\":{},"
                     "\"pc\":\"0x{:x}\"{},\"mode\":\"{}\","
                     "\"component\":\"{}\",\"payload\":{{"
                     "\"address\":\"0x{:x}\",\"value\":\"0x{:x}\","
                     "\"width\":{},\"access\":\"{}\",\"faulted\":{},"
                     "\"latency_cycles\":null}}}}",
                     mapped ? "mmio" : "unmapped", is_write ? "write" : "read", trace_timestamp(),
                     cycle, static_cast<unsigned>(hart.val),
                     static_cast<uint64_t>(cpu.active_context().cpc.raw()),
                     event_function_field(machine_, cpu.active_context().cpc.raw()),
                     privilege_mode(cpu.state().priv), dev_name, static_cast<uint64_t>(addr),
                     static_cast<uint64_t>(data), size, mapped ? "mmio" : "unmapped",
                     faulted ? "true" : "false");
    }
}

void Tracer::log_bus_transaction(Counter cycle, char channel, std::string_view opcode,
                                 std::uint16_t source, std::uint16_t sink, Address address,
                                 std::string_view detail, const BusTraceContext& context) {
    if (!fp_bus.is_open() || !trace_level_at_least(4)) return;
    const auto hart = context.hart.value_or(std::numeric_limits<uint32_t>::max());
    const auto event_cycle =
        channel == 'D' && context.completion_cycle ? *context.completion_cycle : cycle;
    if (!accepts_trace_event("bus_transaction", event_cycle, hart, std::nullopt, context.target))
        return;
    std::lock_guard lock(mutex_);
    std::string fields;
    const auto add_number = [&fields](std::string_view name, const auto& value) {
        if (value) fields += std::format(",\"{}\":{}", name, static_cast<uint64_t>(*value));
    };
    add_number("sequence", context.sequence);
    add_number("request_cycle", context.request_cycle);
    add_number("completion_cycle", context.completion_cycle);
    add_number("latency_cycles", context.latency_cycles);
    add_number("hart", context.hart);
    add_number("size_log2", context.transfer_size);
    if (context.transfer_size)
        fields += std::format(",\"width_bytes\":{}", 1u << *context.transfer_size);
    add_number("burst_length", context.burst_length);
    if (context.data) {
        if (channel == 'D')
            fields += std::format(",\"response_data\":\"0x{:x}\"", *context.data);
        else
            fields += std::format(",\"data\":\"0x{:x}\"", *context.data);
    }
    if (context.byte_enable)
        fields += std::format(",\"byte_enable\":\"0x{:x}\"", *context.byte_enable);
    if (context.denied) fields += std::format(",\"denied\":{}", *context.denied ? "true" : "false");
    if (context.corrupt)
        fields += std::format(",\"corrupt\":{}", *context.corrupt ? "true" : "false");
    if (!context.master.empty())
        fields += std::format(",\"master\":{}", json_quote(context.master));
    if (!context.target.empty())
        fields += std::format(",\"target\":{}", json_quote(context.target));
    std::println(fp_bus,
                 "{{\"schema_version\":2,\"event\":\"bus_transaction\","
                 "\"cycle\":{},\"payload\":{{\"bus\":\"tilelink-c\","
                 "\"channel\":\"{}\",\"opcode\":{},\"source_id\":{},"
                 "\"sink_id\":{},\"address\":\"0x{:x}\",\"detail\":{}{}}}}}",
                 event_cycle, channel, json_quote(opcode), source, sink,
                 static_cast<uint64_t>(address), json_quote(detail), fields);
}

void Tracer::log_pipeline_stalls(const CPU& cpu, std::span<const PipelineStallRecord> stalls) {
    if (!fp_pipeline_.is_open() || !trace_level_at_least(4) || stalls.empty()) return;
    const auto& state = cpu.state();
    const auto cycle = cpu.clint_mmio.mcycle;
    if (!accepts_trace_event("pipeline_stall", cycle, static_cast<uint32_t>(state.mhartid),
                             stalls.front().pc))
        return;
    std::lock_guard lock(mutex_);
    std::println(fp_pipeline_,
                 "{{\"schema_version\":2,\"event\":\"pipeline_stall\","
                 "\"cycle\":{},\"hart\":{},\"mode\":\"{}\",\"payload\":{{"
                 "\"stages\":[{}]}}}}",
                 cycle, static_cast<uint32_t>(state.mhartid), privilege_mode(state.priv),
                 [&stalls] {
                     std::string result;
                     for (const auto& stall : stalls) {
                         if (!result.empty()) result.push_back(',');
                         result += std::format(
                             "{{\"stage\":{},\"pc\":\"0x{:x}\","
                             "\"remaining_cycles\":{},\"reason\":{}}}",
                             json_quote(stall.stage), static_cast<uint64_t>(stall.pc),
                             stall.remaining_cycles, json_quote(stall.reason));
                     }
                     return result;
                 }());
}

void Tracer::log_dma_transfer(const simrv::memory::DmaTransferTrace& transfer) {
    if (!fp_devices.is_open() || !trace_level_at_least(1)) return;
    const auto event = transfer.complete ? "dma_complete" : "dma_start";
    const auto cycle = transfer.complete ? transfer.completion_cycle : transfer.request_cycle;
    if (!accepts_trace_event(event, cycle, std::numeric_limits<uint32_t>::max(), std::nullopt,
                             transfer.component))
        return;
    std::lock_guard lock(mutex_);
    std::println(fp_devices,
                 "{{\"schema_version\":2,\"event\":\"{}\",\"timestamp\":\"{}\","
                 "\"cycle\":{},\"component\":{},\"payload\":{{"
                 "\"transfer_id\":{},\"byte_count\":{},\"request_cycle\":{},"
                 "\"start_cycle\":{},\"completion_cycle\":{},\"latency_cycles\":{}}}}}",
                 event, trace_timestamp(), cycle, json_quote(transfer.component), transfer.id,
                 transfer.byte_count, transfer.request_cycle, transfer.start_cycle,
                 transfer.completion_cycle, transfer.completion_cycle - transfer.start_cycle);
}

void Tracer::log_marker_csr_write(const CPU& cpu, CSRValue value) {
    if (!fp_markers.is_open() || !is_architecture_trace_enabled()) return;
    const auto hart = static_cast<size_t>(cpu.state().mhartid);
    std::lock_guard lock(mutex_);
    if (hart >= marker_buffers_.size()) {
        marker_buffers_.resize(hart + 1);
        marker_depth_.resize(hart + 1);
    }
    constexpr CSRValue kCommandMask = static_cast<CSRValue>(0xC0000000u);
    constexpr CSRValue kBegin = static_cast<CSRValue>(0x40000000u);
    constexpr CSRValue kEnd = static_cast<CSRValue>(0x80000000u);
    constexpr CSRValue kMarker = static_cast<CSRValue>(0xC0000000u);
    auto& label = marker_buffers_[hart];
    if ((value & kCommandMask) == 0) {
        if (value <= 0xFFu && label.size() < 1024) label.push_back(static_cast<char>(value));
        return;
    }

    std::string_view event;
    if (value == kBegin) {
        event = "marker_begin";
        ++marker_depth_[hart];
    } else if (value == kEnd) {
        event = "marker_end";
        if (marker_depth_[hart] != 0) --marker_depth_[hart];
    } else if (value == kMarker) {
        event = "marker";
    } else {
        label.clear();
        return;
    }
    const auto source_pc = static_cast<uint64_t>(cpu.pipeline_context.cpc.raw());
    if (!accepts_trace_event(event, cpu.clint_mmio.mcycle,
                             static_cast<uint32_t>(cpu.state().mhartid), source_pc)) {
        label.clear();
        return;
    }
    std::println(
        fp_markers,
        "{{\"schema_version\":2,\"event\":{},\"cycle\":{},"
        "\"hart\":{},\"pc\":\"0x{:x}\"{},\"mode\":{},"
        "\"payload\":{{\"label\":{},\"marker_depth\":{}}}}}",
        json_quote(event), cpu.clint_mmio.mcycle, static_cast<unsigned>(cpu.state().mhartid),
        source_pc, event_function_field(machine_, source_pc),
        json_quote(privilege_mode(cpu.state().priv)), json_quote(label), marker_depth_[hart]);
    label.clear();
}

void Tracer::log_trap(Counter mtime, Counter cycle, TrapCause cause, Address trap_pc,
                      PrivilegeLevel priv, const ArchState& state, CSRValue tval, const CPU& cpu) {
    if (!fp_traplog.is_open() && !(fp_devices.is_open() && trace_level_at_least(1))) return;
    constexpr int kLogHexWidth = static_cast<int>(kXLenHexDigits);
    std::lock_guard lock(mutex_);
    if (fp_traplog.is_open()) {
        std::println(
            fp_traplog,
            "TRAP mtime={} cause={:0{}x} ({}) pc={:0{}x} priv={} ra={:0{}x} sp={:0{}x} "
            "tp={:0{}x} a0={:0{}x} a1={:0{}x} mtvec={:0{}x} stvec={:0{}x} mepc={:0{}x} "
            "sepc={:0{}x} satp={:0{}x} tval={:0{}x}",
            mtime, static_cast<uint64_t>(cause), kLogHexWidth, trap_cause_name(cause),
            static_cast<uint64_t>(trap_pc), kLogHexWidth, static_cast<unsigned>(priv),
            static_cast<uint64_t>(state.regs.read(RegId::Ra)), kLogHexWidth,
            static_cast<uint64_t>(state.regs.read(RegId::Sp)), kLogHexWidth,
            static_cast<uint64_t>(state.regs.read(RegId::Tp)), kLogHexWidth,
            static_cast<uint64_t>(state.regs.read(RegId::A0)), kLogHexWidth,
            static_cast<uint64_t>(state.regs.read(RegId::A1)), kLogHexWidth,
            static_cast<uint64_t>(state.mtvec), kLogHexWidth, static_cast<uint64_t>(state.stvec),
            kLogHexWidth, static_cast<uint64_t>(state.mepc), kLogHexWidth,
            static_cast<uint64_t>(state.sepc), kLogHexWidth, static_cast<uint64_t>(state.satp),
            kLogHexWidth, static_cast<uint64_t>(tval), kLogHexWidth);
    }
    if (fp_devices.is_open() && trace_level_at_least(1) &&
        accepts_trace_event("trap", cycle, static_cast<uint32_t>(state.mhartid), trap_pc)) {
        std::println(fp_devices,
                     "{{\"schema_version\":2,\"event\":\"trap\","
                     "\"timestamp\":\"{}\",\"cycle\":{},\"hart\":{},"
                     "\"pc\":\"0x{:x}\"{},\"mode\":\"{}\",\"payload\":{{\"cause\":{},"
                     "\"cause_name\":\"{}\",\"pc\":\"0x{:x}\","
                     "\"tval\":\"0x{:x}\",\"privilege\":{}}}}}",
                     trace_timestamp(), cycle, static_cast<unsigned>(state.mhartid),
                     static_cast<uint64_t>(trap_pc), event_function_field(machine_, trap_pc),
                     privilege_mode(priv), static_cast<uint64_t>(cause), trap_cause_name(cause),
                     static_cast<uint64_t>(trap_pc), static_cast<uint64_t>(tval),
                     std::to_underlying(priv));
    }
    const auto& context = cpu.active_context();
    const bool memory_fault_cause =
        !trap_is_interrupt(cause) &&
        (cause == static_cast<TrapCause>(ExceptionCode::MisalignedLoad) ||
         cause == static_cast<TrapCause>(ExceptionCode::FaultLoad) ||
         cause == static_cast<TrapCause>(ExceptionCode::MisalignedStore) ||
         cause == static_cast<TrapCause>(ExceptionCode::FaultStore) ||
         cause == static_cast<TrapCause>(ExceptionCode::LoadPageFault) ||
         cause == static_cast<TrapCause>(ExceptionCode::StorePageFault));
    const bool memory_write_fault =
        cause == static_cast<TrapCause>(ExceptionCode::MisalignedStore) ||
        cause == static_cast<TrapCause>(ExceptionCode::FaultStore) ||
        cause == static_cast<TrapCause>(ExceptionCode::StorePageFault);
    const auto memory_kind = pipeline::operation::info(context.op_id).memory;
    const auto opcode = static_cast<Opcode>(context.opcode);
    const bool scalar_memory_opcode = opcode == Opcode::Load || opcode == Opcode::Store ||
                                      opcode == Opcode::LoadFp || opcode == Opcode::StoreFp ||
                                      opcode == Opcode::Amo;
    if (!fp_memory.is_open() || !trace_level_at_least(4) || !memory_fault_cause ||
        !scalar_memory_opcode || memory_kind == pipeline::operation::MemoryAccessKind::None)
        return;

    const Address effective_address =
        cpu.effective_data_xlen() == 32 ? (context.mem_addr & 0xffffffffULL) : context.mem_addr;
    const auto access_privilege = cpu.effective_data_privilege();
    std::optional<Address> physical_address;
    const bool translation_enabled =
        access_privilege != kPrivMachine &&
        simrv::xlen::satp_translation_enabled(state.satp, cpu.effective_data_xlen());
    if (!translation_enabled) {
        physical_address = effective_address;
    } else {
        const auto asid =
            static_cast<Asid>(simrv::xlen::satp_asid(state.satp, cpu.effective_data_xlen()));
        const Address vpn = effective_address >> simrv::memory::kPageShift;
        const auto tlb_index = CPU::soft_tlb_index(vpn);
        const auto& soft_tlb =
            memory_write_fault ? cpu.soft_tlb_write[tlb_index] : cpu.soft_tlb_read[tlb_index];
        if (soft_tlb.matches(vpn, asid, access_privilege, cpu.soft_tlb_epoch)) {
            physical_address =
                soft_tlb.paddr_base.raw() | (effective_address & simrv::memory::kPageMask);
        } else {
            const auto* entry =
                memory_write_fault ? cpu.tlb.peek_data_w(effective_address, asid, access_privilege)
                                   : cpu.tlb.peek_data_r(effective_address, asid, access_privilege);
            if (entry != nullptr) {
                physical_address = entry->p_addr | (effective_address & simrv::memory::kPageMask);
            }
        }
    }
    const unsigned width = 1u << (std::to_underlying(context.funct3) & 0x3u);
    std::string_view region = "unresolved";
    std::string_view component;
    if (physical_address) {
        if (machine_.memory_geometry().contains(*physical_address, width)) {
            region = "ram";
        } else if (const auto* device =
                       machine_.memory().system_bus().router().resolve_device(*physical_address);
                   device != nullptr) {
            region = "mmio";
            component = device->name() != nullptr ? device->name() : "";
        } else {
            region = "unmapped";
        }
    }
    if (!accepts_trace_event("memory_fault", cycle, static_cast<uint32_t>(state.mhartid), trap_pc,
                             component))
        return;
    const uint64_t store_value =
        static_cast<uint64_t>(opcode == Opcode::StoreFp ? context.fp_mem_wdata : context.mem_wdata);
    const std::string value_json =
        memory_write_fault ? json_quote(std::format("0x{:x}", store_value)) : "null";
    std::println(fp_memory,
                 "{{\"schema_version\":2,\"event\":\"memory_fault\","
                 "\"cycle\":{},\"hart\":{},\"pc\":\"0x{:x}\"{},\"mode\":\"{}\","
                 "{}\"payload\":{{\"effective_address\":\"0x{:x}\",{}"
                 "\"region\":\"{}\",\"direction\":\"{}\",\"operation\":{},"
                 "\"width\":{},\"value\":{},\"cause\":{},\"cause_name\":{},"
                 "\"tval\":\"0x{:x}\",\"faulted\":true,\"latency_cycles\":null}}}}",
                 cycle, static_cast<unsigned>(state.mhartid), static_cast<uint64_t>(trap_pc),
                 event_function_field(machine_, trap_pc), privilege_mode(access_privilege),
                 component.empty() ? "" : std::format("\"component\":{},", json_quote(component)),
                 static_cast<uint64_t>(effective_address),
                 physical_address ? std::format("\"physical_address\":\"0x{:x}\",",
                                                static_cast<uint64_t>(*physical_address))
                                  : "",
                 region, memory_write_fault ? "write" : "read",
                 json_quote(pipeline::operation_name(context.op_id)), width, value_json,
                 static_cast<uint64_t>(trap_exception_code(cause)),
                 json_quote(trap_cause_name(cause)), static_cast<uint64_t>(tval));
}

void Tracer::log_sbi(Counter mtime, Counter cycle, unsigned cause, Word ext_id, Word func_id,
                     Word a0, Word a1, Address pc, HartId hart) {
    if (!fp_traplog.is_open() && !(fp_devices.is_open() && trace_level_at_least(1))) return;
    constexpr int kLogHexWidth = static_cast<int>(kXLenHexDigits);
    std::lock_guard lock(mutex_);
    if (fp_traplog.is_open()) {
        std::println(fp_traplog,
                     "__ SBI ecall mtime={} cause={} ext={:0{}x} fid={:0{}x} a0={:0{}x} a1={:0{}x} "
                     "pc={:0{}x}",
                     mtime, cause, static_cast<uint64_t>(ext_id), kLogHexWidth,
                     static_cast<uint64_t>(func_id), kLogHexWidth, static_cast<uint64_t>(a0),
                     kLogHexWidth, static_cast<uint64_t>(a1), kLogHexWidth,
                     static_cast<uint64_t>(pc), kLogHexWidth);
    }
    if (fp_devices.is_open() && trace_level_at_least(1) &&
        accepts_trace_event("sbi", cycle, hart.val, pc)) {
        std::println(fp_devices,
                     "{{\"schema_version\":2,\"event\":\"sbi\","
                     "\"timestamp\":\"{}\",\"cycle\":{},\"hart\":{},"
                     "\"pc\":\"0x{:x}\"{},\"mode\":\"M\",\"payload\":{{\"cause\":{},"
                     "\"extension\":\"0x{:x}\",\"function\":\"0x{:x}\","
                     "\"a0\":\"0x{:x}\",\"a1\":\"0x{:x}\","
                     "\"pc\":\"0x{:x}\"}}}}",
                     trace_timestamp(), cycle, hart.val, static_cast<uint64_t>(pc),
                     event_function_field(machine_, pc), cause, static_cast<uint64_t>(ext_id),
                     static_cast<uint64_t>(func_id), static_cast<uint64_t>(a0),
                     static_cast<uint64_t>(a1), static_cast<uint64_t>(pc));
    }
}

void Tracer::log_architecture_retirement(const CPU& cpu,
                                         const pipeline::PipelineContext& retiring_context) {
    if (!is_architecture_trace_enabled()) return;
    const auto hart_id = static_cast<uint32_t>(cpu.state().mhartid);
    const auto cycle = cpu.clint_mmio.mcycle;
    const auto memory_kind = pipeline::operation::info(retiring_context.op_id).memory;
    const auto opcode = static_cast<Opcode>(retiring_context.opcode);
    const bool scalar_memory_opcode = opcode == Opcode::Load || opcode == Opcode::Store ||
                                      opcode == Opcode::LoadFp || opcode == Opcode::StoreFp ||
                                      opcode == Opcode::Amo;
    const bool emit_memory = fp_memory.is_open() && scalar_memory_opcode &&
                             memory_kind != pipeline::operation::MemoryAccessKind::None &&
                             trace_level_at_least(4);
    const auto memory_event =
        memory_kind == pipeline::operation::MemoryAccessKind::Load     ? "memory_read"
        : memory_kind == pipeline::operation::MemoryAccessKind::Store  ? "memory_write"
        : memory_kind == pipeline::operation::MemoryAccessKind::Atomic ? "memory_atomic"
                                                                       : "";
    const bool emit_retire =
        trace_level_at_least(3) &&
        accepts_trace_event("retire", cycle, hart_id, retiring_context.cpc.raw());
    const bool emit_calls = fp_calls.is_open() && trace_level_at_least(2);
    const bool emit_interrupts = fp_interrupts.is_open() && trace_level_at_least(1);
    const bool emit_registers =
        fp_registers_.is_open() &&
        accepts_trace_event("register_write", cycle, hart_id, retiring_context.cpc.raw());
    if (!emit_retire && !emit_calls && !emit_interrupts && !emit_memory && !emit_registers) return;
    const auto& state = cpu.state();
    std::lock_guard lock(mutex_);
    if (emit_registers && retiring_context.traits.writes_fp) {
        const auto index = static_cast<unsigned>(retiring_context.rd);
        const auto after = state.regs.read_fp(retiring_context.rd);
        std::println(fp_registers_,
                     "{{\"schema_version\":1,\"event\":\"register_write\","
                     "\"hart\":{},\"cycle\":{},\"pc\":\"0x{:x}\","
                     "\"instruction\":\"0x{:x}\",\"operation\":{},"
                     "\"register_file\":\"fpr\",\"index\":{},"
                     "\"before_bits\":\"0x{:016x}\",\"after_bits\":\"0x{:016x}\","
                     "\"flen\":64,\"fcsr\":\"0x{:x}\"}}",
                     hart_id, cycle, static_cast<uint64_t>(retiring_context.cpc.raw()),
                     static_cast<uint32_t>(retiring_context.ir),
                     json_quote(pipeline::operation_name(retiring_context.op_id)), index,
                     static_cast<uint64_t>(retiring_context.fp_old_value),
                     static_cast<uint64_t>(after), static_cast<uint64_t>(state.fcsr));
    }
    if (emit_registers && retiring_context.traits.writes_vec) {
        const auto hart = static_cast<size_t>(hart_id);
        const auto sequence = retiring_context.trace_sequence;
        auto before = std::vector<uint8_t>{};
        if (hart < vector_write_before_.size()) {
            auto found = vector_write_before_[hart].find(sequence);
            if (found != vector_write_before_[hart].end()) {
                before = std::move(found->second);
                vector_write_before_[hart].erase(found);
            }
        }
        auto to_hex = [](std::span<const uint8_t> bytes) {
            std::string value;
            value.reserve(bytes.size() * 2 + 2);
            value += "0x";
            for (auto it = bytes.rbegin(); it != bytes.rend(); ++it)
                value += std::format("{:02x}", static_cast<unsigned>(*it));
            return value;
        };
        const auto width = trace_vlen_ / 8;
        for (unsigned index = 0; index < 32; ++index) {
            const auto& after = state.regs.read_vector(static_cast<RegId>(index));
            const auto after_bits = std::span<const uint8_t>(after.u8.data(), width);
            const auto start = static_cast<size_t>(index) * width;
            const auto before_bits = start + width <= before.size()
                                         ? std::span<const uint8_t>(before.data() + start, width)
                                         : std::span<const uint8_t>{};
            if (index != static_cast<unsigned>(retiring_context.rd) &&
                before_bits.size() == after_bits.size() &&
                std::equal(before_bits.begin(), before_bits.end(), after_bits.begin()))
                continue;
            std::println(fp_registers_,
                         "{{\"schema_version\":1,\"event\":\"register_write\","
                         "\"hart\":{},\"cycle\":{},\"pc\":\"0x{:x}\","
                         "\"instruction\":\"0x{:x}\",\"operation\":{},"
                         "\"register_file\":\"vector\",\"index\":{},"
                         "\"before_bits\":{},\"after_bits\":{},\"vlen\":{},"
                         "\"vl\":{},\"vtype\":\"0x{:x}\"}}",
                         hart_id, cycle, static_cast<uint64_t>(retiring_context.cpc.raw()),
                         static_cast<uint32_t>(retiring_context.ir),
                         json_quote(pipeline::operation_name(retiring_context.op_id)), index,
                         to_hex(before_bits), to_hex(after_bits), trace_vlen_,
                         static_cast<uint64_t>(state.vl), static_cast<uint64_t>(state.vtype));
        }
    }
    if (emit_memory) {
        const Address effective_address = cpu.effective_data_xlen() == 32
                                              ? (retiring_context.mem_addr & 0xffffffffULL)
                                              : retiring_context.mem_addr;
        const auto access_privilege = cpu.effective_data_privilege();
        std::optional<Address> physical_address;
        const bool translation_enabled =
            access_privilege != kPrivMachine &&
            simrv::xlen::satp_translation_enabled(state.satp, cpu.effective_data_xlen());
        if (!translation_enabled) {
            physical_address = effective_address;
        } else {
            const auto asid =
                static_cast<Asid>(simrv::xlen::satp_asid(state.satp, cpu.effective_data_xlen()));
            const Address vpn = effective_address >> simrv::memory::kPageShift;
            const auto tlb_index = CPU::soft_tlb_index(vpn);
            const auto& soft_tlb = memory_kind == pipeline::operation::MemoryAccessKind::Store
                                       ? cpu.soft_tlb_write[tlb_index]
                                       : cpu.soft_tlb_read[tlb_index];
            if (soft_tlb.matches(vpn, asid, access_privilege, cpu.soft_tlb_epoch)) {
                physical_address =
                    soft_tlb.paddr_base.raw() | (effective_address & simrv::memory::kPageMask);
            } else {
                const auto* entry =
                    memory_kind == pipeline::operation::MemoryAccessKind::Store
                        ? cpu.tlb.peek_data_w(effective_address, asid, access_privilege)
                        : cpu.tlb.peek_data_r(effective_address, asid, access_privilege);
                if (entry != nullptr) {
                    physical_address =
                        entry->p_addr | (effective_address & simrv::memory::kPageMask);
                }
            }
        }

        const unsigned width = 1u << (std::to_underlying(retiring_context.funct3) & 0x3u);
        std::string_view region = "unresolved";
        std::string_view component;
        if (physical_address) {
            if (machine_.memory_geometry().contains(*physical_address, width)) {
                region = "ram";
            } else if (const auto* device = machine_.memory().system_bus().router().resolve_device(
                           *physical_address);
                       device != nullptr) {
                region = "mmio";
                component = device->name() != nullptr ? device->name() : "";
            } else {
                region = "unmapped";
            }
        }
        const bool is_load = memory_kind == pipeline::operation::MemoryAccessKind::Load;
        const bool is_store = memory_kind == pipeline::operation::MemoryAccessKind::Store;
        const uint64_t read_value = static_cast<uint64_t>(
            opcode == Opcode::LoadFp ? retiring_context.fp_mem_rdata : retiring_context.mem_rdata);
        const uint64_t write_value = static_cast<uint64_t>(
            opcode == Opcode::StoreFp ? retiring_context.fp_mem_wdata : retiring_context.mem_wdata);
        if (accepts_trace_event(memory_event, cycle, hart_id, retiring_context.cpc.raw(),
                                component)) {
            if (memory_kind == pipeline::operation::MemoryAccessKind::Atomic) {
                std::println(
                    fp_memory,
                    "{{\"schema_version\":2,\"event\":\"memory_atomic\","
                    "\"cycle\":{},\"hart\":{},\"pc\":\"0x{:x}\","
                    "{},\"mode\":\"{}\",{}\"payload\":{{"
                    "\"effective_address\":\"0x{:x}\",{}"
                    "\"region\":\"{}\",\"operation\":{},\"width\":{},"
                    "\"read_value\":\"0x{:x}\",\"write_value\":\"0x{:x}\","
                    "\"faulted\":false,\"latency_cycles\":null}}}}",
                    cycle, hart_id, static_cast<uint64_t>(retiring_context.cpc.raw()),
                    event_function_field(machine_, retiring_context.cpc.raw()),
                    privilege_mode(access_privilege),
                    component.empty() ? ""
                                      : std::format("\"component\":{},", json_quote(component)),
                    static_cast<uint64_t>(effective_address),
                    physical_address ? std::format("\"physical_address\":\"0x{:x}\",",
                                                   static_cast<uint64_t>(*physical_address))
                                     : "",
                    region, json_quote(pipeline::operation_name(retiring_context.op_id)), width,
                    read_value, write_value);
            } else {
                std::println(
                    fp_memory,
                    "{{\"schema_version\":2,\"event\":{},\"cycle\":{},"
                    "\"hart\":{},\"pc\":\"0x{:x}\"{},\"mode\":\"{}\","
                    "{}\"payload\":{{\"effective_address\":\"0x{:x}\",{}"
                    "\"region\":\"{}\",\"direction\":\"{}\","
                    "\"operation\":{},\"width\":{},\"value\":\"0x{:x}\","
                    "\"faulted\":false,\"latency_cycles\":null}}}}",
                    json_quote(memory_event), cycle, hart_id,
                    static_cast<uint64_t>(retiring_context.cpc.raw()),
                    event_function_field(machine_, retiring_context.cpc.raw()),
                    privilege_mode(access_privilege),
                    component.empty() ? ""
                                      : std::format("\"component\":{},", json_quote(component)),
                    static_cast<uint64_t>(effective_address),
                    physical_address ? std::format("\"physical_address\":\"0x{:x}\",",
                                                   static_cast<uint64_t>(*physical_address))
                                     : "",
                    region, is_load ? "read" : (is_store ? "write" : "access"),
                    json_quote(pipeline::operation_name(retiring_context.op_id)), width,
                    is_load ? read_value : write_value);
            }
        }
    }
    if (emit_retire) {
        if (fp_retire_index_.is_open() && retire_record_count_ % kRetireIndexStride == 0) {
            const auto record_offset = fp_archtrace.tellp();
            if (record_offset >= 0) {
                std::println(fp_retire_index_,
                             "{{\"schema_version\":1,\"kind\":\"trace_index_entry\","
                             "\"record\":{},\"byte_offset\":{},\"cycle\":{},"
                             "\"hart\":{},\"pc\":\"0x{:x}\",\"event\":\"retire\"}}",
                             retire_record_count_, static_cast<uint64_t>(record_offset), cycle,
                             hart_id, static_cast<uint64_t>(retiring_context.cpc.raw()));
            }
        }
        const auto record = std::format(
            "{{\"schema_version\":1,\"event\":\"retire\",\"hart\":{},\"cycle\":{},"
            "\"retired\":{},\"pc\":\"0x{:x}\",\"instruction\":\"0x{:x}\","
            "\"operation\":\"{}\",\"next_pc\":\"0x{:x}\",\"privilege\":{}}}",
            static_cast<unsigned>(state.mhartid), cpu.clint_mmio.mcycle, cpu.e_icount,
            static_cast<uint64_t>(retiring_context.cpc.raw()),
            static_cast<uint32_t>(retiring_context.ir),
            pipeline::operation_name(retiring_context.op_id), static_cast<uint64_t>(state.pc),
            std::to_underlying(state.priv));
        if (gzip_retire_ && gzip_retire_->is_open()) {
            if (!gzip_retire_->write(record + "\n")) {
                simrv::log::error("failed writing compressed retirement record");
                gzip_retire_->close();
            }
        } else {
            std::println(fp_archtrace, "{}", record);
        }
        ++retire_record_count_;
    }

    const auto funct12 = static_cast<Funct12Priv>(retiring_context.funct12);
    if (emit_interrupts && (funct12 == Funct12Priv::Mret || funct12 == Funct12Priv::Sret ||
                            funct12 == Funct12Priv::Uret)) {
        const auto hart_id = static_cast<size_t>(state.mhartid);
        if (hart_id < trap_frames_.size() && !trap_frames_[hart_id].empty()) {
            const auto frame = trap_frames_[hart_id].back();
            trap_frames_[hart_id].pop_back();
            if (frame.interrupt &&
                accepts_trace_event("interrupt_return", cycle, static_cast<uint32_t>(hart_id),
                                    retiring_context.cpc.raw())) {
                const auto depth = std::ranges::count_if(
                    trap_frames_[hart_id],
                    [](const TrapTraceFrame& trap_frame) { return trap_frame.interrupt; });
                std::println(
                    fp_interrupts,
                    "{{\"schema_version\":2,\"event\":\"interrupt_return\","
                    "\"timestamp\":\"{}\",\"cycle\":{},\"hart\":{},"
                    "\"pc\":\"0x{:x}\"{},\"mode\":\"{}\",\"payload\":{{"
                    "\"cause\":{},\"epc\":\"0x{:x}\","
                    "\"target_pc\":\"0x{:x}\",\"nesting_depth\":{}}}}}",
                    trace_timestamp(), cpu.clint_mmio.mcycle, static_cast<unsigned>(state.mhartid),
                    static_cast<uint64_t>(retiring_context.cpc.raw()),
                    event_function_field(machine_, retiring_context.cpc.raw()),
                    privilege_mode(state.priv),
                    static_cast<uint64_t>(trap_exception_code(frame.cause)),
                    static_cast<uint64_t>(frame.epc), static_cast<uint64_t>(state.pc), depth);
            }
        }
    }

    const auto hart = static_cast<size_t>(state.mhartid);
    if (!emit_calls) return;
    if (hart >= call_depth_.size()) call_depth_.resize(hart + 1);
    const auto source_pc = retiring_context.cpc.raw();
    const auto target_pc = retiring_context.jmp_pc.raw();
    const auto return_pc = source_pc + (retiring_context.cinsn != 0 ? 2 : 4);
    const bool is_link_register =
        retiring_context.rd == RegId::Ra || retiring_context.rd == RegId::T0;
    const bool is_return_register =
        retiring_context.rs1 == RegId::Ra || retiring_context.rs1 == RegId::T0;
    const bool is_return = retiring_context.opcode == isa::Opcode::Jalr &&
                           retiring_context.rd == RegId::Zero && is_return_register &&
                           retiring_context.imm == 0;
    const bool is_call = retiring_context.tkn &&
                         (retiring_context.opcode == isa::Opcode::Jal ||
                          retiring_context.opcode == isa::Opcode::Jalr) &&
                         is_link_register;
    if (is_call) {
        const auto depth = ++call_depth_[hart];
        if (fp_calls.is_open() && accepts_trace_event("call", cycle, hart_id, source_pc)) {
            std::println(fp_calls,
                         "{{\"schema_version\":2,\"event\":\"call\","
                         "\"timestamp\":\"{}\",\"cycle\":{},\"hart\":{},"
                         "\"pc\":\"0x{:x}\"{},\"mode\":\"{}\",\"payload\":{{"
                         "\"source_pc\":\"0x{:x}\",\"target_pc\":\"0x{:x}\","
                         "\"return_pc\":\"0x{:x}\",\"call_depth\":{}}}}}",
                         trace_timestamp(), cpu.clint_mmio.mcycle,
                         static_cast<unsigned>(state.mhartid), static_cast<uint64_t>(source_pc),
                         event_function_field(machine_, source_pc), privilege_mode(state.priv),
                         static_cast<uint64_t>(source_pc), static_cast<uint64_t>(target_pc),
                         static_cast<uint64_t>(return_pc), depth);
        }
    } else if (is_return) {
        const auto depth = call_depth_[hart] == 0 ? 0 : --call_depth_[hart];
        if (fp_calls.is_open() && accepts_trace_event("return", cycle, hart_id, source_pc)) {
            std::println(fp_calls,
                         "{{\"schema_version\":2,\"event\":\"return\","
                         "\"timestamp\":\"{}\",\"cycle\":{},\"hart\":{},"
                         "\"pc\":\"0x{:x}\"{},\"mode\":\"{}\",\"payload\":{{"
                         "\"source_pc\":\"0x{:x}\",\"target_pc\":\"0x{:x}\","
                         "\"call_depth\":{}}}}}",
                         trace_timestamp(), cpu.clint_mmio.mcycle,
                         static_cast<unsigned>(state.mhartid), static_cast<uint64_t>(source_pc),
                         event_function_field(machine_, source_pc), privilege_mode(state.priv),
                         static_cast<uint64_t>(source_pc), static_cast<uint64_t>(target_pc), depth);
        }
    }
}

void Tracer::write_trace_metadata(bool completed) {
    if (trace_metadata_path_.empty()) return;
    std::ofstream out(trace_metadata_path_, std::ios::out | std::ios::trunc);
    if (!out) return;
    struct utsname host_info{};
    const bool has_host_info = uname(&host_info) == 0;
    const auto host =
        has_host_info ? std::format("{{\"sysname\":{},\"release\":{},\"machine\":{}}}",
                                    json_quote(host_info.sysname), json_quote(host_info.release),
                                    json_quote(host_info.machine))
                      : "null";
    std::println(
        out,
        "{{\"schema_version\":2,\"simulator_version\":{},"
        "\"git_commit\":{},\"isa\":{},"
        "\"nonstandard_extensions\":[\"Xsimrvtrace\"],"
        "\"xlen\":{},\"vlen\":{},"
        "\"harts\":{},\"trace_level\":{},\"trace_filters\":{{"
        "\"events\":{},\"function\":{},\"device\":{},"
        "\"hart\":{},\"pc_start\":{},\"pc_end\":{},"
        "\"after_cycle\":{},\"before_cycle\":{}}},\"enabled_devices\":{},"
        "\"random_seeds\":{{\"virtio_rng\":1337}},\"guest_elf\":{},"
        "\"guest_elf_sha256\":{},\"configuration_sha256\":{},"
        "\"command_line\":{},\"host\":{},\"started_at\":{},"
        "\"completed_at\":{}}}",
        json_quote(simrv::buildinfo::kVersion), json_quote(simrv::buildinfo::kGitSha),
        json_quote(trace_isa_), simrv::xlen::kXLenBits, trace_vlen_, trace_harts_,
        machine_.configuration().debug.trace_level,
        json_quote(machine_.configuration().debug.trace_events),
        json_quote(machine_.configuration().debug.trace_function),
        json_quote(machine_.configuration().debug.trace_device),
        machine_.configuration().debug.trace_hart
            ? std::format("{}", *machine_.configuration().debug.trace_hart)
            : "null",
        machine_.configuration().debug.trace_pc_start
            ? json_quote(std::format(
                  "0x{:x}", static_cast<uint64_t>(*machine_.configuration().debug.trace_pc_start)))
            : "null",
        machine_.configuration().debug.trace_pc_end
            ? json_quote(std::format(
                  "0x{:x}", static_cast<uint64_t>(*machine_.configuration().debug.trace_pc_end)))
            : "null",
        machine_.configuration().debug.trace_after_cycle
            ? std::format("{}", *machine_.configuration().debug.trace_after_cycle)
            : "null",
        machine_.configuration().debug.trace_before_cycle
            ? std::format("{}", *machine_.configuration().debug.trace_before_cycle)
            : "null",
        trace_enabled_devices_json_,
        trace_guest_elf_.empty() ? "null" : json_quote(trace_guest_elf_),
        trace_guest_elf_sha256_.empty() ? "null" : json_quote(trace_guest_elf_sha256_),
        json_quote(trace_configuration_sha256_), json_string_array(trace_command_line_), host,
        json_quote(trace_started_at_), completed ? json_quote(trace_timestamp()) : "null");
}

void Tracer::dump_init_artifacts() {
    auto* cpu = &machine_.primary_hart();
    const auto ram = machine_.ram_view();
    std::error_code ec;
    ensure_artifact_directory();

    {
        std::ofstream out(artifact_path("init_mem.txt"));
        const auto dram_base = machine_.memory_geometry().dram_base;
        const auto dram_size = static_cast<uint64_t>(machine_.memory_geometry().dram_size);
        constexpr uint64_t kBlockSize = 16;
        bool skipping = false;

        for (uint64_t offset = 0; offset < dram_size; offset += kBlockSize) {
            const uint64_t current_len = std::min<uint64_t>(kBlockSize, dram_size - offset);
            bool all_zero = true;
            for (uint64_t b = 0; b < current_len; ++b) {
                if (std::to_integer<uint8_t>(*ram.unchecked_ptr(dram_base + offset + b)) != 0) {
                    all_zero = false;
                    break;
                }
            }
            if (all_zero) {
                if (!skipping) {
                    std::println(out, "*");
                    skipping = true;
                }
                continue;
            }
            skipping = false;

            std::print(out, "{:08x}: ", offset);
            for (uint64_t b = 0; b < current_len; ++b) {
                const uint8_t val =
                    std::to_integer<uint8_t>(*ram.unchecked_ptr(dram_base + offset + b));
                std::print(out, "{:02x}{}", val, (b == 7 ? "  " : " "));
            }
            for (uint64_t b = current_len; b < kBlockSize; ++b) {
                std::print(out, "   {}", (b == 7 ? " " : ""));
            }
            std::print(out, " |");
            for (uint64_t b = 0; b < current_len; ++b) {
                const char ch = static_cast<char>(
                    std::to_integer<uint8_t>(*ram.unchecked_ptr(dram_base + offset + b)));
                out.put((ch >= 32 && ch <= 126) ? ch : '.');
            }
            std::println(out, "|");
        }
        simrv::log::info("file {} was generated after {} cycle(s)",
                         artifact_path("init_mem.txt").string(),
                         static_cast<Counter>(cpu->clint_mmio.mtime.load()));
    }

    std::ofstream out(artifact_path("init_reg.txt"));
    auto write_xlen = [&out](std::string_view lhs, Word value) -> void {
        std::println(out, "{}={}'h{:0{}x};", lhs, simrv::xlen::kXLenBits, value, kXLenHexDigits);
    };
    auto write_32 = [&out](std::string_view lhs, Word value) -> void {
        std::println(out, "{}=32'h{:08x};", lhs, value);
    };
    auto write_64 = [&out](std::string_view lhs, Counter value) -> void {
        std::println(out, "{}=64'h{:016x};", lhs, value);
    };

    write_xlen("p.pc", cpu->state().pc);
    for (uint8_t i = 1; i < 32; ++i) {
        std::println(out, "p.regs.mem[{}]={}'h{:0{}x};", i, simrv::xlen::kXLenBits,
                     cpu->state().regs.read(static_cast<RegId>(i)), kXLenHexDigits);
    }
    for (uint8_t i = 0; i < 32; ++i) {
        std::println(out, "p.fregs.mem[{}]={}'h{:016x};", i, 64,
                     cpu->state().regs.read_fp(static_cast<RegId>(i)));
    }
    write_xlen("p.fcsr        ", cpu->state().fcsr);
    write_xlen("p.mstatus     ", cpu->state().mstatus);
    write_xlen("p.mtvec       ", cpu->state().mtvec);
    write_xlen("p.mscratch    ", cpu->state().mscratch);
    write_xlen("p.mepc        ", cpu->state().mepc);
    write_xlen("p.mcause      ", cpu->state().mcause);
    write_xlen("p.mtval       ", cpu->state().mtval);
    write_xlen("p.mhartid     ", cpu->state().mhartid);
    write_xlen("p.misa        ", cpu->state().misa);
    write_xlen("p.mie         ", cpu->state().mie);
    write_xlen("p.mip         ", cpu->state().mip);
    write_xlen("p.medeleg     ", cpu->state().medeleg);
    write_xlen("p.mideleg     ", cpu->state().mideleg);
    write_xlen("p.mcounteren  ", cpu->state().mcounteren);
    write_xlen("p.stvec       ", cpu->state().stvec);
    write_xlen("p.sscratch    ", cpu->state().sscratch);
    write_xlen("p.sepc        ", cpu->state().sepc);
    write_xlen("p.scause      ", cpu->state().scause);
    write_xlen("p.stval       ", cpu->state().stval);
    write_xlen("p.satp        ", cpu->state().satp);
    write_xlen("p.scounteren  ", cpu->state().scounteren);
    write_xlen("p.priv        ", std::to_underlying(cpu->state().priv));

    write_64("p.mtime       ", cpu->clint_mmio.mtime);
    write_64("p.mtimecmp    ", cpu->clint_mmio.mtimecmp);

    write_xlen("p.load_res    ", cpu->state().load_res);
    std::println(out, "p.reserved    = 1'h{:x};", cpu->state().reserved);
    write_xlen("p.pending_exception   ",
               cpu->pipeline_context.pending_exception
                   ? std::to_underlying(*cpu->pipeline_context.pending_exception)
                   : simrv::xlen::kWordAllOnes);
    write_xlen("p.pending_tval", cpu->pipeline_context.pending_tval);

    const size_t kNumSets = simrv::core::Tlb::kNumSets;
    for (Word i = 0; i < simrv::memory::kTlbSize; ++i) {
        const auto& entry = cpu->tlb.inst_r.at(i % kNumSets).at(i / kNumSets);
        std::println(out, "mmu.TLB_inst_r.r_valid[{}] ={};", i, static_cast<int>(entry.valid));
        std::println(out, "mmu.TLB_inst_r.mem[{}][39:22] =18'h{:05x};", i, entry.v_addr >> 14);
        std::println(out, "mmu.TLB_inst_r.mem[{}][21:0] =22'h{:06x};", i, entry.p_addr >> 10);
    }
    for (Word i = 0; i < simrv::memory::kTlbSize; ++i) {
        const auto& entry = cpu->tlb.data_r.at(i % kNumSets).at(i / kNumSets);
        std::println(out, "mmu.TLB_data_r.r_valid[{}] ={};", i, static_cast<int>(entry.valid));
        std::println(out, "mmu.TLB_data_r.mem[{}][39:22] =18'h{:05x};", i, entry.v_addr >> 14);
        std::println(out, "mmu.TLB_data_r.mem[{}][21:0] =22'h{:06x};", i, entry.p_addr >> 10);
    }
    for (Word i = 0; i < simrv::memory::kTlbSize; ++i) {
        const auto& entry = cpu->tlb.data_w.at(i % kNumSets).at(i / kNumSets);
        std::println(out, "mmu.TLB_data_w.r_valid[{}] ={};", i, static_cast<int>(entry.valid));
        std::println(out, "mmu.TLB_data_w.mem[{}][39:22] =18'h{:05x};", i, entry.v_addr >> 14);
        std::println(out, "mmu.TLB_data_w.mem[{}][21:0] =22'h{:06x};", i, entry.p_addr >> 10);
    }

    const auto platform = machine_.platform_status();
    write_32("platform.virtio_console.status", platform.console_status);
    write_32("platform.virtio_disk.status   ", platform.disk_status);
    write_32("platform.virtio_disk.isr      ", platform.disk_isr);
    write_64("platform.virtio_disk.capacity ", platform.disk_capacity_sectors);

    simrv::log::info("file {} was generated after {} cycle(s)",
                     artifact_path("init_reg.txt").string(),
                     static_cast<Counter>(cpu->clint_mmio.mtime.load()));
}

void Tracer::write_instruction_mix_report() {
    std::error_code ec;
    ensure_artifact_directory();
    const auto path = artifact_path("instmix.txt");
    std::ofstream out(path);
    if (!out.is_open()) {
        simrv::log::error("cannot open {}", path.string());
        return;
    }

    struct Entry {
        std::string_view name;
        std::string_view category;
        uint64_t count;
    };

    std::vector<Entry> entries;
    std::map<std::string_view, uint64_t> category_totals;
    uint64_t total = 0;

    for (auto const [i, count] : std::views::enumerate(machine_.primary_hart().e_instmix)) {
        if (count == 0) continue;
        const auto op = static_cast<simrv::isa::OperationId>(i);
        const auto name = simrv::pipeline::operation_name(op);
        const auto category = categorize_operation(op);
        entries.push_back({name, category, count});
        category_totals[category] += count;
        total += count;
    }

    std::ranges::sort(entries, [](const auto& a, const auto& b) {
        if (a.count != b.count) return a.count > b.count;
        return a.name < b.name;
    });

    std::println(
        out, "================================================================================");
    std::println(out, "                         INSTRUCTION MIX REPORT");
    std::println(
        out, "================================================================================");
    std::println(out, " Target: RV{} | Primary hart: {} | Configured harts: {} | Engine: {}",
                 simrv::xlen::kXLenBits,
                 static_cast<unsigned>(machine_.primary_hart().state().mhartid),
                 machine_.num_harts(), machine_.runtime_profile.execution_name());
    std::println(out, " Counts below are retired instructions from the primary hart.");
    std::println(out, "");
    std::println(out,
                 " Rank  Instruction             Category              Count      Share  Cumul");
    std::println(
        out, "--------------------------------------------------------------------------------");

    uint64_t cumulative = 0;
    for (size_t rank = 0; rank < entries.size(); ++rank) {
        const auto& e = entries[rank];
        cumulative += e.count;
        const double share =
            total == 0 ? 0.0 : (static_cast<double>(e.count) * 100.0) / static_cast<double>(total);
        const double cumul_pct =
            total == 0 ? 0.0
                       : (static_cast<double>(cumulative) * 100.0) / static_cast<double>(total);
        std::println(out, "{:>5}  {:<22}  {:<16}  {:>12}  {:>5.2f}%  {:>5.2f}%", rank + 1, e.name,
                     e.category, simrv::util::format_with_commas(e.count), share, cumul_pct);
    }

    std::println(
        out, "--------------------------------------------------------------------------------");
    std::println(out, " Total Instructions Retired: {:>16}",
                 simrv::util::format_with_commas(total));
    std::println(
        out, "================================================================================\n");

    std::println(out, "--- Category Summary ---");
    std::vector<std::pair<std::string_view, uint64_t>> cat_sorted(category_totals.begin(),
                                                                  category_totals.end());
    std::ranges::sort(cat_sorted, [](const auto& a, const auto& b) { return a.second > b.second; });
    for (const auto& [cat, cat_cnt] : cat_sorted) {
        const double cat_share =
            total == 0 ? 0.0 : (static_cast<double>(cat_cnt) * 100.0) / static_cast<double>(total);
        std::println(out, "  {:<18} : {:>12}  ({:>5.2f}%)", cat,
                     simrv::util::format_with_commas(cat_cnt), cat_share);
    }

    simrv::log::info("file {} was generated after {} cycle(s)", path.string(),
                     static_cast<Counter>(machine_.primary_hart().clint_mmio.mtime.load()));
}

void Tracer::print_summary() {
    using simrv::util::format_scaled;
    using simrv::util::format_with_commas;
    const auto now = std::chrono::steady_clock::now();
    const auto elapsed =
        std::chrono::duration_cast<std::chrono::microseconds>(now - machine_.start_time()).count();
    const auto etime = static_cast<Counter>(elapsed == 0 ? 1 : elapsed);

    const auto mcycle = machine_.primary_hart().clint_mmio.mcycle;
    Counter icount = 0;
    Counter ccount = 0;
    for (size_t hart = 0; hart < machine_.num_harts(); ++hart) {
        icount += machine_.hart(hart).e_icount;
        ccount += machine_.hart(hart).e_ccount;
    }

    const double cpi =
        icount == 0 ? 0.0 : static_cast<double>(mcycle) / static_cast<double>(icount);
    const double ipc =
        mcycle == 0 ? 0.0 : static_cast<double>(icount) / static_cast<double>(mcycle);
    const double comp_ratio =
        icount == 0 ? 0.0 : (static_cast<double>(ccount) * 100.0) / static_cast<double>(icount);
    const double etime_sec = static_cast<double>(etime) / 1000000.0;

    simrv::log::info("--------------------------------------------------");
    simrv::log::info("                Execution Summary                 ");
    simrv::log::info("--------------------------------------------------");
    simrv::log::info("Simulation Engine        : {:>22}",
                     machine_.runtime_profile.execution_name());
    simrv::log::info("Termination reason       : {:>22}",
                     Machine::stop_reason_name(machine_.stop_reason()));
    simrv::log::info("Process exit status      : {:>22}", machine_.exit_code.load());
    simrv::log::info("Target / harts           : {:>13} / {:<3}",
                     std::format("RV{}", simrv::xlen::kXLenBits), machine_.num_harts());
    if (machine_.runtime_profile.is_cycle_mode()) {
        simrv::log::info("CA pipeline              : {:>22}",
                         pipeline::pipeline_type_name(machine_.execution_config().pipeline_type));
    }
    simrv::log::info("Elapsed cycles (clocks)  : {:>12}  ({})", format_scaled(mcycle),
                     format_with_commas(mcycle));
    simrv::log::info("Executed instructions    : {:>12}  ({})", format_scaled(icount),
                     format_with_commas(icount));
    simrv::log::info("Final architectural PC   :   0x{:016x}", machine_.primary_hart().state().pc);
    simrv::log::info("Fetched compressed insns : {:>12}  ({})  [{:.1f}%]", format_scaled(ccount),
                     format_with_commas(ccount), comp_ratio);
    simrv::log::info("Cycles Per Instr (CPI)   : {:>12.3f}", cpi);
    simrv::log::info("Instrs Per Cycle (IPC)   : {:>12.3f}", ipc);

    if (machine_.num_harts() > 1) {
        for (size_t i = 0; i < machine_.num_harts(); ++i) {
            const auto& h = machine_.hart(i);
            const char* priv_str = (h.state().priv == PrivilegeLevel::Machine)      ? "M"
                                   : (h.state().priv == PrivilegeLevel::Supervisor) ? "S"
                                                                                    : "U";
            simrv::log::info("Hart {:<2} [mode {:>1}, PC 0x{:016x}] Executed Insns : {:>12}  ({})",
                             i, priv_str, h.state().pc, format_scaled(h.e_icount),
                             format_with_commas(h.e_icount));
        }
    }

    if (machine_.runtime_profile.is_cycle_mode()) {
        const auto& ps = machine_.primary_hart().pipeline_sim;
        double stall_pct = mcycle == 0 ? 0.0
                                       : (static_cast<double>(ps.stall_cycles()) * 100.0) /
                                             static_cast<double>(mcycle);
        simrv::log::info("Total Stall/Bubble Cycles: {:>12}  [{:.1f}%]",
                         format_with_commas(ps.stall_cycles()), stall_pct);
        simrv::log::info(" - Data RAW Stalls       : {:>12}",
                         format_with_commas(ps.data_hazard_stalls()));
        simrv::log::info(" - Control Bubbles       : {:>12}",
                         format_with_commas(ps.control_hazard_bubbles()));
        simrv::log::info(" - ICache Miss Stalls    : {:>12}",
                         format_with_commas(ps.icache_stalls()));
        simrv::log::info(" - DCache Miss Stalls    : {:>12}",
                         format_with_commas(ps.dcache_stalls()));
        simrv::log::info(" - Page-walk Stalls      : {:>12}", format_with_commas(ps.tlb_stalls()));
        simrv::log::info("L1I hits / misses        : {:>12} / {}",
                         format_with_commas(machine_.primary_hart().icache.hit_count()),
                         format_with_commas(machine_.primary_hart().icache.miss_count()));
        simrv::log::info("L1D hits / misses        : {:>12} / {}",
                         format_with_commas(machine_.primary_hart().dcache.hit_count()),
                         format_with_commas(machine_.primary_hart().dcache.miss_count()));
        const auto& bp_stats = machine_.primary_hart().branch_predictor.stats();
        if (bp_stats.direction_predictions > 0) {
            simrv::log::info("Branch hits / misses     : {:>12} / {}",
                             format_with_commas(bp_stats.direction_hits),
                             format_with_commas(bp_stats.direction_misses));
        }
        const auto& bus = machine_.memory().system_bus();
        simrv::log::info("Bus reads / writes       : {:>12} / {}",
                         format_with_commas(bus.read_count()),
                         format_with_commas(bus.write_count()));
        simrv::log::info("Bus pending req / resp   : {:>12} / {}",
                         format_with_commas(bus.pending_requests()),
                         format_with_commas(bus.pending_responses()));
    }

    simrv::log::info("Elapsed time (real)      : {:>12.3f} sec", etime_sec);

    const auto kips = icount * 1000UL / etime;
    const double mips = static_cast<double>(icount) / static_cast<double>(etime);
    if (mips >= 1.0) {
        simrv::log::info("Simulation speed         : {:>12.2f} MIPS ({}/s)", mips,
                         format_with_commas(kips * 1000));
    } else {
        simrv::log::info("Simulation speed         : {:>12} KIPS", format_with_commas(kips));
    }
    simrv::log::info("--------------------------------------------------");

    if (machine_.instruction_mix_enabled()) {
        write_instruction_mix_report();
    }
    flush_all();
}

auto Tracer::write_summary_json(const std::string& path) -> bool {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return false;

    Counter retired = 0;
    Counter compressed = 0;
    for (size_t hart = 0; hart < machine_.num_harts(); ++hart) {
        retired += machine_.hart(hart).e_icount;
        compressed += machine_.hart(hart).e_ccount;
    }
    const auto cycles = machine_.primary_hart().clint_mmio.mcycle;
    const auto pc = machine_.primary_hart().state().pc;
    const auto& cpu = machine_.primary_hart();
    const auto& bp = cpu.branch_predictor.stats();
    const auto& bus = machine_.memory().system_bus();
    const double cpi = retired == 0 ? 0.0 : static_cast<double>(cycles) / retired;
    const double ipc = cycles == 0 ? 0.0 : static_cast<double>(retired) / cycles;

    out << "{\n"
        << "  \"schema_version\": 1,\n"
        << "  \"simrv_version\": \"" << simrv::buildinfo::kVersion << "\",\n"
        << "  \"xlen\": " << simrv::xlen::kXLenBits << ",\n"
        << "  \"harts\": " << machine_.num_harts() << ",\n"
        << "  \"engine\": \"" << machine_.runtime_profile.execution_name() << "\",\n"
        << "  \"stop_reason\": \"" << Machine::stop_reason_name(machine_.stop_reason()) << "\",\n"
        << "  \"exit_status\": " << machine_.exit_code.load() << ",\n"
        << "  \"retired_instructions\": " << retired << ",\n"
        << "  \"cycles\": " << cycles << ",\n"
        << "  \"pc\": " << pc << ",\n"
        << "  \"compressed_instructions\": " << compressed << ",\n"
        << "  \"cpi\": " << std::format("{:.9f}", cpi) << ",\n"
        << "  \"ipc\": " << std::format("{:.9f}", ipc) << ",\n"
        << "  \"performance\": {\n"
        << "    \"icache_hits\": " << cpu.icache.hit_count() << ",\n"
        << "    \"icache_misses\": " << cpu.icache.miss_count() << ",\n"
        << "    \"dcache_hits\": " << cpu.dcache.hit_count() << ",\n"
        << "    \"dcache_misses\": " << cpu.dcache.miss_count() << ",\n"
        << "    \"branch_predictions\": " << bp.direction_predictions << ",\n"
        << "    \"branch_hits\": " << bp.direction_hits << ",\n"
        << "    \"branch_misses\": " << bp.direction_misses << ",\n"
        << "    \"branch_misprediction_cycles\": " << bp.misprediction_penalty_cycles << ",\n"
        << "    \"bus_reads\": " << bus.read_count() << ",\n"
        << "    \"bus_writes\": " << bus.write_count() << "\n"
        << "  },\n"
        << "  \"hart_stats\": [\n";
    for (size_t hart = 0; hart < machine_.num_harts(); ++hart) {
        const auto& cpu = machine_.hart(hart);
        const auto& hart_bp = cpu.branch_predictor.stats();
        out << "    {\"hart\": " << hart << ", \"retired_instructions\": " << cpu.e_icount
            << ", \"compressed_instructions\": " << cpu.e_ccount << ", \"pc\": " << cpu.state().pc
            << ", \"cycles\": " << cpu.clint_mmio.mcycle
            << ", \"performance\": {\"icache_hits\": " << cpu.icache.hit_count()
            << ", \"icache_misses\": " << cpu.icache.miss_count()
            << ", \"dcache_hits\": " << cpu.dcache.hit_count()
            << ", \"dcache_misses\": " << cpu.dcache.miss_count()
            << ", \"branch_predictions\": " << hart_bp.direction_predictions
            << ", \"branch_hits\": " << hart_bp.direction_hits
            << ", \"branch_misses\": " << hart_bp.direction_misses << "}}"
            << (hart + 1 == machine_.num_harts() ? "\n" : ",\n");
    }
    out << "  ]\n}\n";
    return static_cast<bool>(out);
}

void Tracer::emit_periodic_pc_trace(Counter mtime, Register cpc) {
    if ((mtime % D_TRACEPC_INTERVAL) == 0) {
        std::lock_guard lock(mutex_);
        if (!tracepc_opened_) {
            tracepc_opened_ = true;
            ensure_artifact_directory();
            const auto path = artifact_path("tracepc.txt");
            fp_tracepc_.open(path);
            simrv::log::info("generate trace file: {}", path.string());
        }
        std::println(fp_tracepc_, "{:08} {:0{}x}", static_cast<int>(mtime / D_TRACEPC_INTERVAL),
                     cpc, D_TRACE_HEX_WIDTH);
    }
}

void Tracer::emit_branch_prediction_trace(Counter mtime, Register cpc, Register jmp_pc,
                                          Opcode r_opcode, bool r_tkn) {
    std::lock_guard lock(mutex_);
    if (!bpred_opened_) {
        bpred_opened_ = true;
        ensure_artifact_directory();
        const auto path = artifact_path("bpred.txt");
        fp_bpred_.open(path);
        simrv::log::info("generate trace file: {}", path.string());
    }

    const auto opcode = r_opcode;
    const int ir_jb = static_cast<int>((opcode == Opcode::Jal) || (opcode == Opcode::Jalr) ||
                                       (opcode == Opcode::Branch));
    const int ir_jump = (opcode == Opcode::Jal) ? 2 : (opcode == Opcode::Jalr) ? 3 : 0;
    const int ir_branch = static_cast<int>(opcode == Opcode::Branch);

    const Word targ = ((ir_jump | ir_branch) != 0) ? jmp_pc : 0;
    std::println(fp_bpred_, "{:08} {:0{}x} {:0{}x} {} {} {} {}", static_cast<int>(mtime), cpc,
                 D_TRACE_HEX_WIDTH, targ, D_TRACE_HEX_WIDTH, ir_jb, static_cast<int>(r_tkn),
                 ir_jump, ir_branch);
}

void Tracer::write_trace_snapshot() {
    if (!fp_trace.is_open()) {
        return;
    }
    std::lock_guard lock(mutex_);
    const auto& cpu = machine_.primary_hart();
    const auto& st = cpu.state();
    const auto& context = cpu.pipeline_context;
    const auto operation = pipeline::operation_name(context.op_id);
    std::println(fp_trace,
                 "cycle {:>10} | hart {:>2} | pc 0x{:0{}x} | ir 0x{:08x} | {:<12} | next 0x{:0{}x}",
                 static_cast<Counter>(cpu.clint_mmio.mtime), static_cast<unsigned>(st.mhartid),
                 static_cast<uint64_t>(context.cpc.raw()), D_TRACE_HEX_WIDTH,
                 static_cast<uint32_t>(context.ir), operation, static_cast<uint64_t>(st.pc),
                 D_TRACE_HEX_WIDTH);
    for (unsigned row = 0; row < 8; ++row) {
        std::print(fp_trace, "  ");
        for (unsigned column = 0; column < 4; ++column) {
            const unsigned index = row * 4 + column;
            std::print(fp_trace, "x{:>2}=0x{:0{}x}{}", index,
                       static_cast<uint64_t>(st.regs.read(static_cast<RegId>(index))),
                       D_TRACE_HEX_WIDTH, column == 3 ? "\n" : "  ");
        }
    }
    std::println(fp_trace, "  mstatus=0x{:0{}x}  mepc=0x{:0{}x}  mcause=0x{:0{}x}  satp=0x{:0{}x}",
                 static_cast<uint64_t>(st.mstatus), D_TRACE_HEX_WIDTH,
                 static_cast<uint64_t>(st.mepc), D_TRACE_HEX_WIDTH,
                 static_cast<uint64_t>(st.mcause), D_TRACE_HEX_WIDTH,
                 static_cast<uint64_t>(st.satp), D_TRACE_HEX_WIDTH);
    std::println(fp_trace, "  vstart=0x{:0{}x}  vl=0x{:0{}x}  vtype=0x{:0{}x}  privilege={}",
                 static_cast<uint64_t>(st.vstart), D_TRACE_HEX_WIDTH, static_cast<uint64_t>(st.vl),
                 D_TRACE_HEX_WIDTH, static_cast<uint64_t>(st.vtype), D_TRACE_HEX_WIDTH,
                 std::to_underlying(st.priv));
    std::println(fp_trace, "");
}

}  // namespace simrv::core
