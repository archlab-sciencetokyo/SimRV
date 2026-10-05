#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "simrv/core/Machine.hpp"
#include "simrv/core/RuntimeProfile.hpp"
#include "simrv/memory/MemoryUtil.hpp"

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

auto trace_path(std::string_view suffix) -> std::filesystem::path {
    return std::filesystem::temp_directory_path() /
           ("simrv-architectural-trace-" + std::string(suffix) + ".jsonl");
}

auto read_trace(const std::filesystem::path& path) -> std::vector<std::string> {
    std::ifstream input(path);
    std::vector<std::string> lines;
    for (std::string line; std::getline(input, line);) lines.push_back(std::move(line));
    return lines;
}

void test_retirement_context_and_call_trace() {
    const std::array engines = {ExecutionEngine::InstructionFast,
                                ExecutionEngine::InstructionObservable, ExecutionEngine::CycleFast};
    for (size_t i = 0; i < engines.size(); ++i) {
        Machine machine;
        std::vector<Byte> ram(1024 * 1024, Byte{0});
        std::memcpy(ram.data(), kProgram.data(), sizeof(kProgram));
        machine.set_ram_for_testing(ram.data(), ram.size());
        machine.primary_hart().machine_ = &machine;
        machine.primary_hart().reset();
        machine.primary_hart().state().pc = kPc;
        machine.runtime_profile.engine = engines[i];
        const auto path = trace_path(std::to_string(i));
        machine.trace().init_architecture_trace(path.string());
        while (machine.primary_hart().e_icount < 6) {
            machine.primary_hart().run_cycle(machine);
            if (machine.runtime_profile.is_cycle_mode())
                machine.memory().system_bus().advance_cycle();
        }
        machine.trace().flush_all();
        const auto lines = read_trace(path);
        size_t retire_count = 0;
        size_t call_count = 0;
        size_t return_count = 0;
        for (const auto& line : lines) {
            if (line.find("\"event\":\"retire\"") != std::string::npos) {
                ++retire_count;
                if (line.find("\"pc\":\"0x0\"") != std::string::npos) std::abort();
                if (line.find("call_depth") != std::string::npos) std::abort();
            }
            call_count += line.find("\"event\":\"call\"") != std::string::npos;
            return_count += line.find("\"event\":\"return\"") != std::string::npos;
        }
        if (retire_count < 6 || call_count != 2 || return_count != 2) std::abort();
        if (std::none_of(lines.begin(), lines.end(), [](const auto& line) {
                return line.find("\"call_depth\":2") != std::string::npos;
            }))
            std::abort();
        std::filesystem::remove(path);
    }
}

void test_branches_are_not_calls() {
    Machine machine;
    std::vector<Byte> ram(1024 * 1024, Byte{0});
    constexpr std::array<Instruction, 3> program = {
        0x00000463,  // beq x0, x0, +8
        0x0000006f,  // jal x0, 0 (skipped ordinary jump)
        0x0000006f,  // jal x0, 0
    };
    std::memcpy(ram.data(), program.data(), sizeof(program));
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.primary_hart().reset();
    machine.primary_hart().machine_ = &machine;
    machine.primary_hart().state().pc = kPc;
    const auto path = trace_path("branch");
    machine.trace().init_architecture_trace(path.string());
    while (machine.primary_hart().e_icount < 2) machine.primary_hart().run_cycle(machine);
    machine.trace().flush_all();
    const auto lines = read_trace(path);
    for (const auto& line : lines) {
        if (line.find("\"event\":\"call\"") != std::string::npos) std::abort();
    }
    std::filesystem::remove(path);
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
    const auto lines = read_trace(path);
    bool saw_hart0 = false;
    bool saw_hart1 = false;
    for (const auto& line : lines) {
        if (line.find("\"event\":\"call\"") == std::string::npos) continue;
        saw_hart0 |= line.find("\"hart\":0") != std::string::npos;
        saw_hart1 |= line.find("\"hart\":1") != std::string::npos;
    }
    if (!saw_hart0 || !saw_hart1) std::abort();
    std::filesystem::remove(path);
}
}  // namespace

int main() {
    test_retirement_context_and_call_trace();
    test_branches_are_not_calls();
    test_trace_hart_identity();
    return 0;
}
