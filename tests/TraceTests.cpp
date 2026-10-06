#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

#include "simrv/core/Machine.hpp"
#include "simrv/core/RuntimeProfile.hpp"
#include "simrv/core/TrapController.hpp"
#include "simrv/debug/SymbolTable.hpp"
#include "simrv/device/Aclint.hpp"
#include "simrv/memory/MemoryUtil.hpp"
#include "simrv/memory/TileLinkNode.hpp"
#include "simrv/pipeline/PipelineContext.hpp"
#include "simrv/util/Sha256.hpp"

namespace {
using simrv::core::ExecutionEngine;
using simrv::core::Machine;

constexpr Address kPc = simrv::memory::kDramBaseAddress;
constexpr std::array<Instruction, 10> kProgram = {
    0x010000ef,  // jal  ra, 0x10 (direct call)
    0x0000006f,  // jal  x0, 0 (ordinary jump, not a call)
    0x00000013, 0x00000013,
    0x00000297,  // auipc t0, 0
    0x01028293,  // addi t0, t0, 0x10 -> 0x20
    0x000280e7,  // jalr ra, t0, 0 (indirect call)
    0x00008067,  // ret
    0x00008067,  // ret
    0x00000013,
};
std::vector<std::filesystem::path> g_retirement_indexes;

auto trace_path(std::string_view suffix) -> std::filesystem::path {
    const auto path = std::filesystem::temp_directory_path() /
                      ("simrv-architectural-trace-" + std::string(suffix) + ".jsonl");
    g_retirement_indexes.push_back(path.parent_path() / (path.stem().string() + ".index.jsonl"));
    return path;
}

auto sibling_path(const std::filesystem::path& retire_path, std::string_view filename)
    -> std::filesystem::path {
    return retire_path.parent_path() / filename;
}

auto read_trace(const std::filesystem::path& path) -> std::vector<std::string> {
    std::ifstream input(path);
    std::vector<std::string> lines;
    for (std::string line; std::getline(input, line);) lines.push_back(std::move(line));
    return lines;
}

class TraceTestNode final : public simrv::memory::TileLinkNode {
   public:
    [[nodiscard]] auto name() const -> const char* override { return "trace-test-device"; }
    [[nodiscard]] auto base_address() const -> Address override { return 0x50000000; }
    [[nodiscard]] auto size() const -> Address override { return 0x1000; }
    auto handle_request(const simrv::memory::TlChannelA&, simrv::memory::TlChannelD& response)
        -> bool override {
        if (fail_) {
            response.denied = true;
            return true;
        }
        response.data = 0x41;
        return true;
    }
    void set_fail(bool fail) { fail_ = fail; }

   private:
    bool fail_ = false;
};

void test_retirement_context_and_call_trace() {
    const auto guest_path = trace_path("guest-image");
    {
        std::ofstream guest(guest_path, std::ios::binary);
        guest << "abc";
    }
    const std::array engines = {ExecutionEngine::InstructionFast,
                                ExecutionEngine::InstructionObservable, ExecutionEngine::CycleFast};
    for (size_t i = 0; i < engines.size(); ++i) {
        simrv::core::MachineConfig config;
        config.files.binary_path = guest_path.string();
        if (engines[i] == ExecutionEngine::CycleFast) config.debug.trace_level = 4;
        Machine machine(config);
        std::vector<Byte> ram(1024 * 1024, Byte{0});
        std::memcpy(ram.data(), kProgram.data(), sizeof(kProgram));
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.primary_hart().machine_ = &machine;
        machine.primary_hart().reset();
        machine.primary_hart().state().pc = kPc;
        machine.runtime_profile.engine = engines[i];
        const auto path = trace_path(std::to_string(i));
        machine.trace().set_command_line(
            {"simrv", "--arch-trace", path.string(), "guest\"image.elf"});
        machine.trace().init_architecture_trace(path.string());
        while (machine.primary_hart().e_icount < 6) {
            machine.primary_hart().run_cycle(machine);
            if (machine.runtime_profile.is_cycle_mode())
                machine.memory().system_bus().advance_cycle();
        }
        machine.trace().flush_all();
        const auto lines = read_trace(path);
        const auto call_lines = read_trace(sibling_path(path, "calls.jsonl"));
        size_t retire_count = 0;
        size_t call_count = 0;
        size_t return_count = 0;
        for (const auto& line : lines) {
            if (line.find("\"event\":\"retire\"") != std::string::npos) {
                ++retire_count;
                if (line.find("\"pc\":\"0x0\"") != std::string::npos) std::abort();
                if (line.find("call_depth") != std::string::npos) std::abort();
            }
        }
        for (const auto& line : call_lines) {
            call_count += line.find("\"event\":\"call\"") != std::string::npos;
            return_count += line.find("\"event\":\"return\"") != std::string::npos;
        }
        if (retire_count < 6 || call_count != 2 || return_count != 2) std::abort();
        if (std::none_of(call_lines.begin(), call_lines.end(), [](const auto& line) {
                return line.find("\"call_depth\":2") != std::string::npos;
            }))
            std::abort();
        const auto metadata = read_trace(sibling_path(path, "metadata.json"));
        if (metadata.size() != 1 ||
            metadata.front().find("\"schema_version\":2") == std::string::npos)
            std::abort();
        if (std::any_of(call_lines.begin(), call_lines.end(), [](const auto& line) {
                return line.find("\"event\":\"header\"") != std::string::npos &&
                       line.find("\"mode\":\"M\"") == std::string::npos;
            }))
            std::abort();
        if (engines[i] == ExecutionEngine::CycleFast) {
            const auto stalls = read_trace(sibling_path(path, "pipeline.jsonl"));
            if (stalls.empty() || std::none_of(stalls.begin(), stalls.end(), [](const auto& line) {
                    return line.find("\"event\":\"pipeline_stall\"") != std::string::npos;
                }))
                std::abort();
        }
        if (std::any_of(call_lines.begin(), call_lines.end(), [](const auto& line) {
                return line.find("\"event\":\"return\"") != std::string::npos &&
                       line.find("return_pc") != std::string::npos;
            }))
            std::abort();
        if (std::any_of(call_lines.begin(), call_lines.end(), [](const auto& line) {
                return line.find("\"event\":\"call\"") != std::string::npos &&
                       line.find("return_pc") == std::string::npos;
            }))
            std::abort();
        if (metadata.front().find("\"git_commit\"") == std::string::npos ||
            metadata.front().find("\"trace_level\"") == std::string::npos ||
            metadata.front().find("\"nonstandard_extensions\":[\"Xsimrvtrace\"]") ==
                std::string::npos ||
            metadata.front().find("\"isa\":\"rv") == std::string::npos ||
            metadata.front().find("\"command_line\":[\"simrv\"") == std::string::npos ||
            metadata.front().find("guest\\\"image.elf") == std::string::npos ||
            metadata.front().find(
                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") ==
                std::string::npos ||
            metadata.front().find("\"configuration_sha256\":\"") == std::string::npos ||
            metadata.front().find("\"enabled_devices\":[") == std::string::npos ||
            metadata.front().find("\"random_seeds\":{\"virtio_rng\":1337}") == std::string::npos ||
            metadata.front().find("\"host\":{") == std::string::npos)
            std::abort();
        std::filesystem::remove(path);
        std::filesystem::remove(sibling_path(path, "calls.jsonl"));
        std::filesystem::remove(sibling_path(path, "devices.jsonl"));
        std::filesystem::remove(sibling_path(path, "interrupts.jsonl"));
        std::filesystem::remove(sibling_path(path, "bus.jsonl"));
        std::filesystem::remove(sibling_path(path, "pipeline.jsonl"));
        std::filesystem::remove(sibling_path(path, "markers.jsonl"));
        std::filesystem::remove(sibling_path(path, "metadata.json"));
    }
    std::filesystem::remove(guest_path);
}

void test_retirement_index_offsets() {
    constexpr size_t kRetirements = 257;
    Machine machine;
    std::vector<Instruction> program(kRetirements, 0x00000013);  // addi x0, x0, 0
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    std::memcpy(ram.data(), program.data(), program.size() * sizeof(Instruction));
    machine.set_ram_for_testing(ram.data(), ram.size());
    auto& cpu = machine.primary_hart();
    cpu.machine_ = &machine;
    cpu.reset();
    cpu.machine_ = &machine;
    cpu.state().pc = kPc;

    const auto path = trace_path("index");
    const auto index_path = g_retirement_indexes.back();
    machine.trace().init_architecture_trace(path.string());
    while (cpu.e_icount < kRetirements) cpu.run_cycle(machine);
    machine.trace().flush_all();

    const auto index = read_trace(index_path);
    std::ifstream retirement_file(path, std::ios::binary);
    const std::string retirement_data((std::istreambuf_iterator<char>(retirement_file)), {});
    if (index.size() != 3 || index[0].find("\"stride\":256") == std::string::npos ||
        index[1].find("\"record\":0") == std::string::npos ||
        index[2].find("\"record\":256") == std::string::npos)
        std::abort();

    for (size_t i = 1; i < index.size(); ++i) {
        const auto key = index[i].find("\"byte_offset\":");
        if (key == std::string::npos) std::abort();
        const auto value_start = key + std::string_view("\"byte_offset\":").size();
        const auto offset = std::stoull(index[i].substr(value_start));
        constexpr std::string_view kRetirePrefix = "{\"schema_version\":1,\"event\":\"retire\"";
        if (offset >= retirement_data.size() ||
            retirement_data.compare(static_cast<size_t>(offset), kRetirePrefix.size(),
                                    kRetirePrefix) != 0)
            std::abort();
    }

    std::filesystem::remove(path);
    std::filesystem::remove(index_path);
    for (const auto* stream :
         {"calls.jsonl", "devices.jsonl", "interrupts.jsonl", "bus.jsonl", "memory.jsonl",
          "pipeline.jsonl", "markers.jsonl", "metadata.json"}) {
        std::filesystem::remove(sibling_path(path, stream));
    }
}

void test_gzip_retirement_preserves_schema_one_records() {
#if defined(__unix__) || defined(__APPLE__)
    using GzFile = void*;
    using GzOpen = GzFile (*)(const char*, const char*);
    using GzRead = int (*)(GzFile, void*, unsigned);
    using GzClose = int (*)(GzFile);
    void* library = dlopen("libz.so.1", RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) library = dlopen("libz.dylib", RTLD_NOW | RTLD_LOCAL);
    if (library == nullptr) return;
    auto load = [library]<typename Function>(const char* name) {
        void* symbol = dlsym(library, name);
        Function function{};
        static_assert(sizeof(function) == sizeof(symbol));
        std::memcpy(&function, &symbol, sizeof(function));
        return function;
    };
    const auto gzopen = load.template operator()<GzOpen>("gzopen");
    const auto gzread = load.template operator()<GzRead>("gzread");
    const auto gzclose = load.template operator()<GzClose>("gzclose");
    if (gzopen == nullptr || gzread == nullptr || gzclose == nullptr) {
        dlclose(library);
        std::abort();
    }

    const auto path = trace_path("gzip").replace_extension(".jsonl.gz");
    {
        Machine machine;
        std::array<Instruction, 2> program = {0x00000013, 0x00000013};
        std::vector<Byte> ram(1024 * 1024, Byte{0});
        std::memcpy(ram.data(), program.data(), sizeof(program));
        machine.set_ram_for_testing(ram.data(), ram.size());
        auto& cpu = machine.primary_hart();
        cpu.machine_ = &machine;
        cpu.reset();
        cpu.state().pc = kPc;
        machine.trace().init_architecture_trace(path.string());
        if (!machine.trace().is_architecture_trace_enabled()) std::abort();
        while (cpu.e_icount < 1) cpu.run_cycle(machine);
        machine.trace().flush_all();
    }

    std::ifstream raw(path, std::ios::binary);
    std::array<unsigned char, 2> magic{};
    raw.read(reinterpret_cast<char*>(magic.data()), magic.size());
    if (magic != std::array<unsigned char, 2>{0x1f, 0x8b}) std::abort();
    raw.close();
    const auto compressed = gzopen(path.c_str(), "rb");
    if (compressed == nullptr) std::abort();
    std::string decoded;
    std::array<char, 1024> buffer{};
    for (int count = 0; (count = gzread(compressed, buffer.data(), buffer.size())) > 0;)
        decoded.append(buffer.data(), static_cast<size_t>(count));
    if (gzclose(compressed) != 0 ||
        decoded.find("\"schema_version\":1,\"event\":\"header\"") == std::string::npos ||
        decoded.find("\"event\":\"retire\"") == std::string::npos ||
        decoded.find("\"pc\":\"0x80000000\"") == std::string::npos)
        std::abort();
    dlclose(library);
    std::filesystem::remove(path);
    for (const auto name : {"calls.jsonl", "devices.jsonl", "interrupts.jsonl", "bus.jsonl",
                            "markers.jsonl", "metadata.json"})
        std::filesystem::remove(sibling_path(path, name));
#endif
}

void test_branches_are_not_calls() {
    Machine machine;
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    constexpr std::array<Instruction, 10> program = {
        0x00000463,  // beq x0, x0, +8
        0x0000006f,  // jal x0, 0 (skipped ordinary jump)
        0x0040006f,  // jal x0, +4 (ordinary direct jump)
        0x00030067,  // jalr x0, 0(t1) (ordinary indirect jump)
        0x00408067,  // jalr x0, 4(ra) (not the return convention)
        0x00000013, 0x00000013, 0x00000013, 0x00000013, 0x00000013,
    };
    std::memcpy(ram.data(), program.data(), sizeof(program));
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.primary_hart().reset();
    machine.primary_hart().machine_ = &machine;
    machine.primary_hart().state().pc = kPc;
    machine.primary_hart().state().regs.write(RegId::T1, kPc + 16);
    machine.primary_hart().state().regs.write(RegId::Ra, kPc + 24);
    const auto path = trace_path("branch");
    machine.trace().init_architecture_trace(path.string());
    while (machine.primary_hart().e_icount < 4) machine.primary_hart().run_cycle(machine);
    machine.trace().flush_all();
    const auto call_lines = read_trace(sibling_path(path, "calls.jsonl"));
    for (const auto& line : call_lines) {
        if (line.find("\"event\":\"call\"") != std::string::npos ||
            line.find("\"event\":\"return\"") != std::string::npos)
            std::abort();
    }
    std::filesystem::remove(path);
    std::filesystem::remove(sibling_path(path, "calls.jsonl"));
    std::filesystem::remove(sibling_path(path, "devices.jsonl"));
    std::filesystem::remove(sibling_path(path, "interrupts.jsonl"));
    std::filesystem::remove(sibling_path(path, "bus.jsonl"));
    std::filesystem::remove(sibling_path(path, "markers.jsonl"));
    std::filesystem::remove(sibling_path(path, "metadata.json"));
}

void test_trace_level_filters_streams() {
    simrv::core::MachineConfig config;
    config.debug.trace_level = 2;
    Machine machine(config);
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    std::memcpy(ram.data(), kProgram.data(), sizeof(kProgram));
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.primary_hart().machine_ = &machine;
    machine.primary_hart().reset();
    machine.primary_hart().state().pc = kPc;
    const auto path = trace_path("level");
    machine.trace().init_architecture_trace(path.string());
    while (machine.primary_hart().e_icount < 2) machine.primary_hart().run_cycle(machine);
    machine.trace().flush_all();

    const auto retire_lines = read_trace(path);
    const auto call_lines = read_trace(sibling_path(path, "calls.jsonl"));
    if (std::any_of(retire_lines.begin(), retire_lines.end(), [](const auto& line) {
            return line.find("\"event\":\"retire\"") != std::string::npos;
        }))
        std::abort();
    if (std::none_of(call_lines.begin(), call_lines.end(), [](const auto& line) {
            return line.find("\"event\":\"call\"") != std::string::npos;
        }))
        std::abort();

    std::filesystem::remove(path);
    std::filesystem::remove(sibling_path(path, "calls.jsonl"));
    std::filesystem::remove(sibling_path(path, "devices.jsonl"));
    std::filesystem::remove(sibling_path(path, "interrupts.jsonl"));
    std::filesystem::remove(sibling_path(path, "bus.jsonl"));
    std::filesystem::remove(sibling_path(path, "markers.jsonl"));
    std::filesystem::remove(sibling_path(path, "metadata.json"));
}

void test_trace_hart_identity() {
    simrv::core::MachineConfig config;
    Machine machine(config);
    machine.add_hart_for_testing(std::make_unique<simrv::core::CPU>());
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    std::memcpy(ram.data(), kProgram.data(), sizeof(kProgram));
    machine.set_ram_for_testing(ram.data(), ram.size());
    for (size_t hart = 0; hart < machine.num_harts(); ++hart) {
        machine.hart(hart).machine_ = &machine;
        machine.hart(hart).reset();
        machine.hart(hart).state().mhartid = hart;
        machine.hart(hart).state().pc = kPc;
    }
    const auto path = trace_path("smp");
    machine.trace().init_architecture_trace(path.string());
    while (machine.hart(0).e_icount < 2 || machine.hart(1).e_icount < 2) {
        for (size_t hart = 0; hart < machine.num_harts(); ++hart) {
            if (machine.hart(hart).e_icount < 2) machine.hart(hart).run_cycle(machine);
        }
    }
    machine.trace().flush_all();
    const auto lines = read_trace(sibling_path(path, "calls.jsonl"));
    bool saw_hart0 = false;
    bool saw_hart1 = false;
    for (const auto& line : lines) {
        if (line.find("\"event\":\"call\"") == std::string::npos) continue;
        saw_hart0 |= line.find("\"hart\":0") != std::string::npos;
        saw_hart1 |= line.find("\"hart\":1") != std::string::npos;
    }
    if (!saw_hart0 || !saw_hart1) std::abort();
    std::filesystem::remove(path);
    std::filesystem::remove(sibling_path(path, "calls.jsonl"));
    std::filesystem::remove(sibling_path(path, "devices.jsonl"));
    std::filesystem::remove(sibling_path(path, "interrupts.jsonl"));
    std::filesystem::remove(sibling_path(path, "bus.jsonl"));
    std::filesystem::remove(sibling_path(path, "markers.jsonl"));
    std::filesystem::remove(sibling_path(path, "metadata.json"));
}

void test_arch_trace_event_hart_and_cycle_filters() {
    simrv::core::MachineConfig config;
    config.debug.trace_events = "call";
    config.debug.trace_hart = 1;
    config.debug.trace_pc_start = kPc;
    config.debug.trace_pc_end = kPc + 0x10;
    config.debug.trace_after_cycle = 0;
    config.debug.trace_before_cycle = 100;
    Machine machine(config);
    machine.add_hart_for_testing(std::make_unique<simrv::core::CPU>());
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    std::memcpy(ram.data(), kProgram.data(), sizeof(kProgram));
    machine.set_ram_for_testing(ram.data(), ram.size());
    for (size_t hart = 0; hart < machine.num_harts(); ++hart) {
        machine.hart(hart).machine_ = &machine;
        machine.hart(hart).reset();
        machine.hart(hart).state().mhartid = hart;
        machine.hart(hart).state().pc = kPc;
    }

    const auto path = trace_path("event-filters");
    machine.trace().init_architecture_trace(path.string());
    simrv::pipeline::PipelineContext call_context;
    call_context.cpc = VirtAddr{kPc};
    call_context.opcode = simrv::isa::Opcode::Jal;
    call_context.rd = RegId::Ra;
    call_context.jmp_pc = VirtAddr{kPc + 8};
    call_context.tkn = true;
    for (size_t hart = 0; hart < machine.num_harts(); ++hart) {
        machine.hart(hart).clint_mmio.mcycle = 50;
        machine.trace().log_architecture_retirement(machine.hart(hart), call_context);
    }
    machine.hart(1).clint_mmio.mcycle = 100;
    machine.trace().log_architecture_retirement(machine.hart(1), call_context);
    machine.hart(1).clint_mmio.mcycle = 101;
    machine.trace().log_architecture_retirement(machine.hart(1), call_context);
    auto end_pc_context = call_context;
    end_pc_context.cpc = VirtAddr{kPc + 0x10};
    machine.hart(1).clint_mmio.mcycle = 75;
    machine.trace().log_architecture_retirement(machine.hart(1), end_pc_context);
    auto outside_pc_context = call_context;
    outside_pc_context.cpc = VirtAddr{kPc + 0x11};
    machine.hart(1).clint_mmio.mcycle = 75;
    machine.trace().log_architecture_retirement(machine.hart(1), outside_pc_context);
    machine.trace().flush_all();
    const auto retire_lines = read_trace(path);
    const auto calls = read_trace(sibling_path(path, "calls.jsonl"));
    if (std::any_of(retire_lines.begin(), retire_lines.end(),
                    [](const auto& line) {
                        return line.find("\"event\":\"retire\"") != std::string::npos;
                    }) ||
        calls.size() != 4 ||
        std::none_of(calls.begin(), calls.end(),
                     [](const auto& line) {
                         return line.find("\"event\":\"call\"") != std::string::npos &&
                                line.find("\"hart\":1") != std::string::npos;
                     }) ||
        std::any_of(calls.begin(), calls.end(), [](const auto& line) {
            return line.find("\"event\":\"call\"") != std::string::npos &&
                   line.find("\"hart\":0") != std::string::npos;
        }))
        std::abort();

    std::filesystem::remove(path);
    for (const auto name : {"calls.jsonl", "devices.jsonl", "interrupts.jsonl", "bus.jsonl",
                            "markers.jsonl", "metadata.json"})
        std::filesystem::remove(sibling_path(path, name));
}

void test_arch_trace_device_filter() {
    simrv::core::MachineConfig config;
    config.debug.trace_device = "trace-test-device";
    Machine machine(config);
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.primary_hart().machine_ = &machine;
    machine.primary_hart().reset();
    const auto path = trace_path("device-filter");
    machine.trace().init_architecture_trace(path.string());
    machine.trace().log_mmio(0, "other-device", 0x1000, 4, 1, true);
    machine.trace().log_mmio(0, "trace-test-device", 0x2000, 4, 2, true);
    machine.trace().flush_all();
    const auto devices = read_trace(sibling_path(path, "devices.jsonl"));
    if (devices.size() != 2 ||
        std::none_of(devices.begin(), devices.end(),
                     [](const auto& line) {
                         return line.find("trace-test-device") != std::string::npos &&
                                line.find("mmio_write") != std::string::npos;
                     }) ||
        std::any_of(devices.begin(), devices.end(), [](const auto& line) {
            return line.find("other-device") != std::string::npos;
        }))
        std::abort();
    for (const auto name : {"retire.jsonl", "calls.jsonl", "devices.jsonl", "interrupts.jsonl",
                            "bus.jsonl", "markers.jsonl", "metadata.json"})
        std::filesystem::remove(sibling_path(path, name));
}

void test_arch_trace_function_filter(std::string_view executable_path) {
    simrv::core::MachineConfig config;
    config.debug.trace_function = "main";
    Machine machine(config);
    const auto elf_path = std::filesystem::absolute(executable_path).string();
    if (!machine.symbol_table().load_from_elf(elf_path)) std::abort();
    const auto main_pc = machine.symbol_table().lookup_name("main");
    if (!main_pc) std::abort();

    const auto path = trace_path("function-filter");
    machine.trace().init_architecture_trace(path.string());
    simrv::pipeline::PipelineContext call_context;
    call_context.cpc = VirtAddr{*main_pc};
    call_context.opcode = simrv::isa::Opcode::Jal;
    call_context.rd = RegId::Ra;
    call_context.jmp_pc = VirtAddr{*main_pc + 0x40};
    call_context.tkn = true;
    machine.primary_hart().state().mhartid = 0;
    machine.primary_hart().clint_mmio.mcycle = 10;
    machine.trace().log_architecture_retirement(machine.primary_hart(), call_context);

    call_context.cpc = VirtAddr{std::numeric_limits<Address>::max()};
    machine.primary_hart().clint_mmio.mcycle = 11;
    machine.trace().log_architecture_retirement(machine.primary_hart(), call_context);
    machine.trace().flush_all();

    const auto calls = read_trace(sibling_path(path, "calls.jsonl"));
    if (calls.size() != 2 || calls.back().find("\"event\":\"call\"") == std::string::npos ||
        calls.back().find("\"function\":\"main\"") == std::string::npos ||
        calls.back().find("\"source_pc\":\"0x" + std::format("{:x}", *main_pc) + "\"") ==
            std::string::npos)
        std::abort();

    for (const auto name : {"calls.jsonl", "devices.jsonl", "interrupts.jsonl", "bus.jsonl",
                            "markers.jsonl", "metadata.json"})
        std::filesystem::remove(sibling_path(path, name));
    std::filesystem::remove(path);
}

void test_retired_memory_events_and_address_classification() {
    simrv::core::MachineConfig config;
    config.debug.trace_level = 4;
    TraceTestNode device;
    Machine machine(config);
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.primary_hart().machine_ = &machine;
    machine.primary_hart().reset();
    if (!machine.memory().system_bus().router().register_device(&device)) std::abort();

    const auto path = trace_path("memory-events");
    machine.trace().init_architecture_trace(path.string());
    simrv::pipeline::PipelineContext load{};
    load.cpc = VirtAddr{kPc + 0x40};
    load.opcode = simrv::isa::Opcode::Load;
    load.op_id = simrv::isa::LW;
    load.funct3 = simrv::isa::Funct3::Lw;
    load.mem_addr = kPc + 0x100;
    load.mem_rdata = 0x12345678;
    machine.primary_hart().clint_mmio.mcycle = 10;
    machine.trace().log_architecture_retirement(machine.primary_hart(), load);

    simrv::pipeline::PipelineContext store{};
    store.cpc = VirtAddr{kPc + 0x44};
    store.opcode = simrv::isa::Opcode::Store;
    store.op_id = simrv::isa::SW;
    store.funct3 = simrv::isa::Funct3::Sw;
    store.mem_addr = device.base_address();
    store.mem_wdata = 0x89abcdef;
    machine.primary_hart().clint_mmio.mcycle = 12;
    machine.trace().log_architecture_retirement(machine.primary_hart(), store);
    machine.trace().flush_all();

    const auto events = read_trace(sibling_path(path, "memory.jsonl"));
    if (events.size() != 3 || events[1].find("\"event\":\"memory_read\"") == std::string::npos ||
        events[1].find("\"region\":\"ram\"") == std::string::npos ||
        events[1].find("\"physical_address\":\"0x80000100\"") == std::string::npos ||
        events[1].find("\"value\":\"0x12345678\"") == std::string::npos ||
        events[1].find("\"width\":4") == std::string::npos ||
        events[2].find("\"event\":\"memory_write\"") == std::string::npos ||
        events[2].find("\"region\":\"mmio\"") == std::string::npos ||
        events[2].find("\"component\":\"trace-test-device\"") == std::string::npos ||
        events[2].find("\"value\":\"0x89abcdef\"") == std::string::npos)
        std::abort();
    for (const auto name :
         {"retire.jsonl", "calls.jsonl", "devices.jsonl", "interrupts.jsonl", "bus.jsonl",
          "memory.jsonl", "pipeline.jsonl", "markers.jsonl", "metadata.json"})
        std::filesystem::remove(sibling_path(path, name));
}

void test_precise_memory_fault_event() {
    simrv::core::MachineConfig config;
    config.debug.trace_level = 4;
    TraceTestNode device;
    Machine machine(config);
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    auto& cpu = machine.primary_hart();
    cpu.machine_ = &machine;
    cpu.reset();
    cpu.machine_ = &machine;
    cpu.state().pc = kPc + 0x80;
    cpu.state().mtvec = kPc + 0x200;
    auto& context = cpu.pipeline_context;
    context.cpc = VirtAddr{kPc + 0x80};
    context.opcode = simrv::isa::Opcode::Store;
    context.op_id = simrv::isa::SW;
    context.funct3 = simrv::isa::Funct3::Sw;
    context.mem_addr = device.base_address();
    context.mem_wdata = 0xfeedcafe;
    if (!machine.memory().system_bus().router().register_device(&device)) std::abort();

    const auto path = trace_path("memory-fault");
    machine.trace().init_architecture_trace(path.string());
    cpu.raise_exception(static_cast<TrapCause>(ExceptionCode::FaultStore), device.base_address());
    machine.trace().flush_all();

    const auto events = read_trace(sibling_path(path, "memory.jsonl"));
    if (events.size() != 2 ||
        events.back().find("\"event\":\"memory_fault\"") == std::string::npos ||
        events.back().find("\"cycle\":") == std::string::npos ||
        events.back().find("\"pc\":\"0x80000080\"") == std::string::npos ||
        events.back().find("\"component\":\"trace-test-device\"") == std::string::npos ||
        events.back().find("\"direction\":\"write\"") == std::string::npos ||
        events.back().find("\"value\":\"0xfeedcafe\"") == std::string::npos ||
        events.back().find("\"cause\":7") == std::string::npos ||
        events.back().find("\"tval\":\"0x50000000\"") == std::string::npos ||
        events.back().find("\"faulted\":true") == std::string::npos)
        std::abort();
    for (const auto name :
         {"retire.jsonl", "calls.jsonl", "devices.jsonl", "interrupts.jsonl", "bus.jsonl",
          "memory.jsonl", "pipeline.jsonl", "markers.jsonl", "metadata.json"})
        std::filesystem::remove(sibling_path(path, name));
}

void test_bus_trace_level_four() {
    simrv::core::MachineConfig config;
    config.debug.trace_level = 4;
    Machine machine(config);
    const auto path = trace_path("bus-events");
    machine.trace().init_architecture_trace(path.string());
    machine.memory().system_bus().record_transaction(TileLinkChannel::A, "Get", 3, 0, 0x1000,
                                                     "read");
    machine.trace().flush_all();

    const auto lines = read_trace(sibling_path(path, "bus.jsonl"));
    if (lines.size() != 2 ||
        lines.back().find("\"event\":\"bus_transaction\"") == std::string::npos ||
        lines.back().find("\"cycle\":0") == std::string::npos ||
        lines.back().find("\"bus\":\"tilelink-c\"") == std::string::npos ||
        lines.back().find("\"channel\":\"A\"") == std::string::npos ||
        lines.back().find("\"source_id\":3") == std::string::npos ||
        lines.back().find("\"address\":\"0x1000\"") == std::string::npos)
        std::abort();

    std::filesystem::remove(path);
    std::filesystem::remove(sibling_path(path, "calls.jsonl"));
    std::filesystem::remove(sibling_path(path, "devices.jsonl"));
    std::filesystem::remove(sibling_path(path, "interrupts.jsonl"));
    std::filesystem::remove(sibling_path(path, "bus.jsonl"));
    std::filesystem::remove(sibling_path(path, "markers.jsonl"));
    std::filesystem::remove(sibling_path(path, "metadata.json"));
}

void test_mmio_arch_trace_uses_requesting_hart_without_dlog() {
    simrv::core::MachineConfig config;
    config.debug.trace_level = 1;
    TraceTestNode device;
    Machine machine(config);
    machine.add_hart_for_testing(std::make_unique<simrv::core::CPU>());
    auto& issuing_hart = machine.hart(1);
    issuing_hart.machine_ = &machine;
    issuing_hart.state().mhartid = 1;
    issuing_hart.active_context().cpc = VirtAddr{kPc + 0x24};
    issuing_hart.clint_mmio.mcycle = 42;
    issuing_hart.clint_mmio.mtime = 7;
    auto& router = machine.memory().system_bus().router();
    if (!router.register_device(&device)) std::abort();

    const auto path = trace_path("mmio-hart-context");
    machine.trace().init_architecture_trace(path.string());
    simrv::memory::TlChannelA request{};
    request.opcode = simrv::memory::TlOpcodeA::Get;
    request.size = 2;
    request.hart = HartId{1};
    request.address = 0x50000000;
    simrv::memory::TlChannelD response{};
    if (!router.route_request(request, response) || response.failed()) std::abort();
    device.set_fail(true);
    response = {};
    if (!router.route_request(request, response) || !response.failed()) std::abort();
    request.address = 0x60000000;
    response = {};
    if (router.route_request(request, response) || !response.failed()) std::abort();
    machine.trace().flush_all();

    const auto lines = read_trace(sibling_path(path, "devices.jsonl"));
    if (lines.size() != 4 ||
        lines.back().find("\"event\":\"unmapped_read\"") == std::string::npos ||
        lines.back().find("\"hart\":1") == std::string::npos ||
        lines.back().find("\"pc\":\"0x80000024\"") == std::string::npos ||
        lines.back().find("\"cycle\":42") == std::string::npos ||
        lines.back().find("\"component\":\"unmapped\"") == std::string::npos ||
        lines.back().find("\"access\":\"unmapped\"") == std::string::npos ||
        lines.back().find("\"faulted\":true") == std::string::npos ||
        lines[1].find("\"component\":\"trace-test-device\"") == std::string::npos ||
        lines[1].find("\"faulted\":false") == std::string::npos ||
        lines[1].find("\"latency_cycles\":null") == std::string::npos ||
        lines[2].find("\"event\":\"mmio_read\"") == std::string::npos ||
        lines[2].find("\"faulted\":true") == std::string::npos)
        std::abort();

    std::filesystem::remove(path);
    std::filesystem::remove(sibling_path(path, "calls.jsonl"));
    std::filesystem::remove(sibling_path(path, "devices.jsonl"));
    std::filesystem::remove(sibling_path(path, "interrupts.jsonl"));
    std::filesystem::remove(sibling_path(path, "bus.jsonl"));
    std::filesystem::remove(sibling_path(path, "markers.jsonl"));
    std::filesystem::remove(sibling_path(path, "metadata.json"));
}

void test_guest_marker_csr_events() {
    {
        Machine tracing_disabled;
        if (!tracing_disabled.primary_hart().write_csr(0x800, 0x40000000u)) std::abort();
    }
    simrv::core::MachineConfig config;
    Machine machine(config);
    auto& cpu = machine.primary_hart();
    cpu.machine_ = &machine;
    cpu.pipeline_context.cpc = kPc + 0x24;
    const auto path = trace_path("markers");
    machine.trace().init_architecture_trace(path.string());

    const auto emit_text = [&cpu](std::string_view text) {
        for (const unsigned char byte : text) {
            if (!cpu.write_csr(0x800, byte)) std::abort();
        }
    };
    emit_text("outer");
    if (!cpu.write_csr(0x800, 0x40000000u)) std::abort();
    emit_text("note");
    if (!cpu.write_csr(0x800, 0xC0000000u)) std::abort();
    emit_text("outer");
    if (!cpu.write_csr(0x800, 0x80000000u)) std::abort();
    machine.trace().flush_all();

    const auto lines = read_trace(sibling_path(path, "markers.jsonl"));
    if (lines.size() != 4 || lines[1].find("\"event\":\"marker_begin\"") == std::string::npos ||
        lines[1].find("\"label\":\"outer\"") == std::string::npos ||
        lines[1].find("\"marker_depth\":1") == std::string::npos ||
        lines[1].find("\"pc\":\"0x80000024\"") == std::string::npos ||
        lines[2].find("\"event\":\"marker\"") == std::string::npos ||
        lines[2].find("\"label\":\"note\"") == std::string::npos ||
        lines[3].find("\"event\":\"marker_end\"") == std::string::npos ||
        lines[3].find("\"marker_depth\":0") == std::string::npos)
        std::abort();

    std::filesystem::remove(path);
    std::filesystem::remove(sibling_path(path, "calls.jsonl"));
    std::filesystem::remove(sibling_path(path, "devices.jsonl"));
    std::filesystem::remove(sibling_path(path, "interrupts.jsonl"));
    std::filesystem::remove(sibling_path(path, "bus.jsonl"));
    std::filesystem::remove(sibling_path(path, "markers.jsonl"));
    std::filesystem::remove(sibling_path(path, "metadata.json"));
}

void test_user_mode_marker_csr_instruction() {
    Machine machine;
    auto& cpu = machine.primary_hart();
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    constexpr Instruction kCsrwUtraceRa = (0x800u << 20) | (1u << 15) | (1u << 12) | 0x73u;
    const std::array program = {kCsrwUtraceRa, kCsrwUtraceRa};
    std::memcpy(ram.data(), program.data(), sizeof(program));
    machine.set_ram_for_testing(ram.data(), ram.size());
    cpu.machine_ = &machine;
    cpu.reset();
    cpu.machine_ = &machine;
    cpu.state().priv = PrivilegeLevel::User;
    cpu.state().pc = kPc;
    if (cpu.read_csr(0x0C0)) std::abort();  // Reserved standard U-level space is not our ABI.
    if (!simrv::isa::misa_has_extension(cpu.state().misa, simrv::isa::IsaExtension::X))
        std::abort();

    const auto path = trace_path("markers-user-csr");
    machine.trace().init_architecture_trace(path.string());
    cpu.state().regs.write(RegId::Ra, 'u');
    while (cpu.e_icount < 1) cpu.run_cycle(machine);
    cpu.state().regs.write(RegId::Ra, 0x40000000u);
    while (cpu.e_icount < 2) cpu.run_cycle(machine);
    machine.trace().flush_all();

    const auto lines = read_trace(sibling_path(path, "markers.jsonl"));
    if (lines.size() != 2 || lines.back().find("\"event\":\"marker_begin\"") == std::string::npos ||
        lines.back().find("\"hart\":0") == std::string::npos ||
        lines.back().find("\"mode\":\"U\"") == std::string::npos ||
        lines.back().find("\"label\":\"u\"") == std::string::npos ||
        lines.back().find("\"pc\":\"0x80000004\"") == std::string::npos)
        std::abort();

    std::filesystem::remove(path);
    std::filesystem::remove(sibling_path(path, "calls.jsonl"));
    std::filesystem::remove(sibling_path(path, "devices.jsonl"));
    std::filesystem::remove(sibling_path(path, "interrupts.jsonl"));
    std::filesystem::remove(sibling_path(path, "bus.jsonl"));
    std::filesystem::remove(sibling_path(path, "markers.jsonl"));
    std::filesystem::remove(sibling_path(path, "metadata.json"));
}

void test_interrupt_entry_and_return_events() {
    Machine machine;
    auto& cpu = machine.primary_hart();
    cpu.machine_ = &machine;
    cpu.reset();
    cpu.state().pc = kPc + 0x40;
    cpu.state().mtvec = kPc + 0x100;

    const auto path = trace_path("interrupt");
    machine.trace().init_architecture_trace(path.string());
    machine.set_hart_irq(HartId{0}, simrv::core::InterruptType::Timer,
                         simrv::PrivilegeLevel::Machine, true);
    simrv::core::TrapController::raise_exception(cpu, kInterruptCauseBit | 7, 0);

    simrv::pipeline::PipelineContext return_context{};
    return_context.funct12 = 0x302;
    return_context.cpc = simrv::VirtAddr{cpu.state().pc};
    cpu.state().pc = cpu.state().mepc;
    machine.trace().log_architecture_retirement(cpu, return_context);
    machine.trace().flush_all();

    const auto lines = read_trace(sibling_path(path, "interrupts.jsonl"));
    if (std::none_of(lines.begin(), lines.end(), [](const auto& line) {
            return line.find("\"event\":\"interrupt_asserted\"") != std::string::npos &&
                   line.find("\"cause\":7") != std::string::npos &&
                   line.find("\"component\":\"aclint_mtimer\"") != std::string::npos;
        }))
        std::abort();
    if (std::none_of(lines.begin(), lines.end(), [](const auto& line) {
            return line.find("\"event\":\"interrupt_entry\"") != std::string::npos &&
                   line.find("\"cause\":7") != std::string::npos &&
                   line.find("\"epc\":\"0x80000040\"") != std::string::npos;
        }))
        std::abort();
    if (std::none_of(lines.begin(), lines.end(), [](const auto& line) {
            return line.find("\"event\":\"interrupt_return\"") != std::string::npos &&
                   line.find("\"target_pc\":\"0x80000040\"") != std::string::npos;
        }))
        std::abort();

    std::filesystem::remove(path);
    std::filesystem::remove(sibling_path(path, "calls.jsonl"));
    std::filesystem::remove(sibling_path(path, "devices.jsonl"));
    std::filesystem::remove(sibling_path(path, "interrupts.jsonl"));
    std::filesystem::remove(sibling_path(path, "bus.jsonl"));
    std::filesystem::remove(sibling_path(path, "metadata.json"));
}

void test_software_mmio_interrupt_bus_completion_fixture() {
    simrv::core::MachineConfig config;
    config.execution.fincnt = 8;
    config.debug.trace_level = 4;
    Machine machine(config);
    simrv::device::AclintMswi mswi(&machine);
    machine.memory().system_bus().add_node(&mswi);
    std::vector<Byte> ram(1024 * 1024, Byte{0});

    // sw x6, 0(x5): software writes ACLINT MSWI; sw x8, 0(x7): signal tohost completion.
    constexpr Instruction kWriteMsip = 0x0062A023;
    constexpr Instruction kSignalCompletion = 0x0083A023;
    constexpr Instruction kClearMsip = 0x0002A023;
    constexpr Instruction kMret = 0x30200073;
    std::memcpy(ram.data(), &kWriteMsip, sizeof(kWriteMsip));
    std::memcpy(ram.data() + 4, &kSignalCompletion, sizeof(kSignalCompletion));
    std::memcpy(ram.data() + 0x100, &kClearMsip, sizeof(kClearMsip));
    std::memcpy(ram.data() + 0x104, &kMret, sizeof(kMret));
    machine.set_ram_for_testing(ram.data(), ram.size());

    auto& cpu = machine.primary_hart();
    cpu.machine_ = &machine;
    cpu.reset();
    cpu.machine_ = &machine;
    cpu.state().pc = kPc;
    cpu.state().mtvec = kPc + 0x100;
    cpu.state().mie = enum_mask(simrv::core::MipBit::Msip);
    cpu.state().mstatus |= enum_mask(simrv::core::MstatusBit::Mie);
    cpu.state().regs.write(RegId::T0, simrv::mmio::kAclintMswiBaseAddress);
    cpu.state().regs.write(RegId::T1, 1);
    cpu.state().regs.write(RegId::T2, machine.isa_test_tohost());
    cpu.state().regs.write(RegId::S0, 1);

    const auto base_path = trace_path("hardware-software-trace-fixture");
    const auto fixture_dir = base_path.parent_path() / base_path.stem();
    std::filesystem::create_directories(fixture_dir);
    const auto path = fixture_dir / "retire.jsonl";
    g_retirement_indexes.push_back(fixture_dir / "retire.index.jsonl");
    machine.trace().init_architecture_trace(path.string());
    machine.runtime_profile.engine = ExecutionEngine::InstructionObservable;
    for (size_t cycle = 0; cycle < 32 && machine.tohost.load() == 0; ++cycle) {
        cpu.run_cycle(machine);
    }
    machine.trace().flush_all();

    const auto devices = read_trace(sibling_path(path, "devices.jsonl"));
    const auto interrupts = read_trace(sibling_path(path, "interrupts.jsonl"));
    const auto bus = read_trace(sibling_path(path, "bus.jsonl"));
    const auto has_event = [](const auto& lines, std::string_view event) {
        return std::any_of(lines.begin(), lines.end(), [event](const auto& line) {
            return line.find(std::format("\"event\":\"{}\"", event)) != std::string::npos;
        });
    };
    const bool request_context = std::any_of(bus.begin(), bus.end(), [](const auto& line) {
        return line.find("\"channel\":\"A\"") != std::string::npos &&
               line.find("\"target\":\"aclint-mswi\"") != std::string::npos &&
               line.find("\"master\":\"hart-0-data\"") != std::string::npos &&
               line.find("\"byte_enable\":\"0xf\"") != std::string::npos &&
               line.find("\"request_cycle\"") != std::string::npos;
    });
    const bool response_context = std::any_of(bus.begin(), bus.end(), [](const auto& line) {
        return line.find("\"channel\":\"D\"") != std::string::npos &&
               line.find("\"target\":\"aclint-mswi\"") != std::string::npos &&
               line.find("\"completion_cycle\"") != std::string::npos &&
               line.find("\"latency_cycles\"") != std::string::npos &&
               line.find("\"response_data\"") != std::string::npos &&
               line.find("\"denied\":false") != std::string::npos;
    });
    if (machine.tohost.load() != 1 || !has_event(devices, "mmio_write") ||
        std::none_of(devices.begin(), devices.end(),
                     [](const auto& line) {
                         return line.find("\"event\":\"mmio_write\"") != std::string::npos &&
                                line.find("\"component\":\"aclint-mswi\"") != std::string::npos;
                     }) ||
        !has_event(interrupts, "interrupt_asserted") || !has_event(interrupts, "interrupt_entry") ||
        !has_event(interrupts, "interrupt_return") || !has_event(bus, "bus_transaction") ||
        !request_context || !response_context)
        std::abort();

    std::filesystem::remove(path);
    for (const auto name : {"calls.jsonl", "devices.jsonl", "interrupts.jsonl", "bus.jsonl",
                            "memory.jsonl", "pipeline.jsonl", "markers.jsonl", "metadata.json"})
        std::filesystem::remove(sibling_path(path, name));
    std::filesystem::remove(path);
    std::filesystem::remove(sibling_path(path, "retire.index.jsonl"));
    std::filesystem::remove(fixture_dir);
}

void test_sha256_multiblock_vector() {
    constexpr std::string_view input = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    constexpr std::string_view expected =
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1";
    if (simrv::util::sha256(input) != expected) std::abort();
}

void test_cycle_accurate_pipeline_stall_events() {
    const auto path = trace_path("pipeline-stalls");
    simrv::core::MachineConfig config;
    config.debug.trace_level = 4;
    Machine machine(config);
    machine.primary_hart().state().mhartid = 0;
    machine.primary_hart().clint_mmio.mcycle = 17;
    machine.trace().init_architecture_trace(path.string());
    const std::array stalls = {
        simrv::core::PipelineStallRecord{"fetch", kPc, 3, "instruction_fill"},
        simrv::core::PipelineStallRecord{"execute", kPc + 4, 0, "data_hazard"},
    };
    machine.trace().log_pipeline_stalls(machine.primary_hart(), stalls);
    machine.trace().flush_all();
    const auto events = read_trace(sibling_path(path, "pipeline.jsonl"));
    if (events.size() != 1 || events.front().find("\"schema_version\":2") == std::string::npos ||
        events.front().find("\"event\":\"pipeline_stall\"") == std::string::npos ||
        events.front().find("\"cycle\":17") == std::string::npos ||
        events.front().find("\"stage\":\"fetch\"") == std::string::npos ||
        events.front().find("\"remaining_cycles\":3") == std::string::npos ||
        events.front().find("\"reason\":\"data_hazard\"") == std::string::npos)
        std::abort();
    for (const auto name : {"pipeline.jsonl", "calls.jsonl", "devices.jsonl", "interrupts.jsonl",
                            "bus.jsonl", "memory.jsonl", "markers.jsonl", "metadata.json"})
        std::filesystem::remove(sibling_path(path, name));
    std::filesystem::remove(path);
}

void test_dma_schedule_and_completion_events() {
    const auto path = trace_path("dma-events");
    simrv::core::MachineConfig config;
    config.debug.trace_level = 1;
    Machine machine(config);
    machine.trace().init_architecture_trace(path.string());
    machine.dma_engine().set_config(
        {.enabled = true, .setup_latency = 2, .bandwidth_bytes_per_cycle = 4});
    bool completed = false;
    const auto completion_cycle = machine.dma_engine().schedule_transfer(
        10, 5, [&completed] { completed = true; }, "test-dma-device");
    if (completion_cycle != 10 || completed) std::abort();
    machine.dma_engine().advance_cycle(9);
    if (completed) std::abort();
    machine.dma_engine().advance_cycle(10);
    if (!completed) std::abort();
    machine.trace().flush_all();
    const auto events = read_trace(sibling_path(path, "devices.jsonl"));
    if (events.size() != 3 || events[1].find("\"event\":\"dma_start\"") == std::string::npos ||
        events[2].find("\"event\":\"dma_complete\"") == std::string::npos ||
        events[1].find("\"component\":\"test-dma-device\"") == std::string::npos ||
        events[1].find("\"byte_count\":10") == std::string::npos ||
        events[2].find("\"transfer_id\":1") == std::string::npos ||
        events[2].find("\"completion_cycle\":10") == std::string::npos ||
        events[2].find("\"latency_cycles\":5") == std::string::npos)
        std::abort();
    for (const auto name : {"pipeline.jsonl", "calls.jsonl", "devices.jsonl", "interrupts.jsonl",
                            "bus.jsonl", "memory.jsonl", "markers.jsonl", "metadata.json"})
        std::filesystem::remove(sibling_path(path, name));
    std::filesystem::remove(path);
}

void test_function_symbol_lookup(std::string_view executable_path) {
    simrv::debug::SymbolTable symbols;
    const auto path = std::filesystem::absolute(executable_path).string();
    if (!symbols.load_from_elf(path)) std::abort();
    const auto main_pc = symbols.lookup_name("main");
    if (!main_pc) std::abort();
    const auto main_function = symbols.lookup_function(*main_pc);
    if (!main_function || main_function->name != "main" || main_function->offset != 0) std::abort();
    if (symbols.lookup_function(std::numeric_limits<Address>::max()).has_value()) std::abort();
}
}  // namespace

int main(int argc, char** argv) {
    if (argc < 1) std::abort();
    test_function_symbol_lookup(argv[0]);
    test_retirement_context_and_call_trace();
    test_retirement_index_offsets();
    test_gzip_retirement_preserves_schema_one_records();
    test_branches_are_not_calls();
    test_trace_level_filters_streams();
    test_trace_hart_identity();
    test_arch_trace_event_hart_and_cycle_filters();
    test_arch_trace_device_filter();
    test_arch_trace_function_filter(argv[0]);
    test_retired_memory_events_and_address_classification();
    test_precise_memory_fault_event();
    test_bus_trace_level_four();
    test_mmio_arch_trace_uses_requesting_hart_without_dlog();
    test_guest_marker_csr_events();
    test_user_mode_marker_csr_instruction();
    test_interrupt_entry_and_return_events();
    test_software_mmio_interrupt_bus_completion_fixture();
    test_cycle_accurate_pipeline_stall_events();
    test_dma_schedule_and_completion_events();
    test_sha256_multiblock_vector();
    for (const auto& index_path : g_retirement_indexes) std::filesystem::remove(index_path);
    return 0;
}
