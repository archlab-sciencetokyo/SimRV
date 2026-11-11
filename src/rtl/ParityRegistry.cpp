/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Archlab, Science Tokyo
 */

#include <dlfcn.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <format>
#include <regex>
#include <sstream>
#include <utility>

#include "simrv/core/Logger.hpp"
#include "simrv/rtl/ParityTarget.hpp"

namespace simrv::rtl {

// Forward declarations of built-in target factories
auto create_rvcomp_target() -> std::unique_ptr<ParityTarget>;
auto create_cfu_pg_target() -> std::unique_ptr<ParityTarget>;

namespace {

const char* const kStandardCrt0 = R"asm(/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Archlab, Science Tokyo
 */
    .equ UART_BASE, 0x10000000
    .equ DRAM_BASE, 0x80000000
    .equ DRAM_SIZE, 0x10000000 # 256 MiB
    .section .text.init
    .align 4
    .globl _start
_start:
    j reset_vector

trap_vector: # exit(1)
    li t0, 1
write_tohost:
    sw t0, tohost, t1
    sw zero, tohost+4, t1
    unimp
    j write_tohost

    .globl uart_putc
uart_putc:
    li t0, UART_BASE
    sb a0, 0(t0)
    ret

reset_vector:
    li x1, 0
    li x3, 0
    li x4, 0
    li x5, 0
    li x6, 0
    li x7, 0
    li x8, 0
    li x9, 0
    li x10, 0
    li x11, 0
    li x12, 0
    li x13, 0
    li x14, 0
    li x15, 0
    li x16, 0
    li x17, 0
    li x18, 0
    li x19, 0
    li x20, 0
    li x21, 0
    li x22, 0
    li x23, 0
    li x24, 0
    li x25, 0
    li x26, 0
    li x27, 0
    li x28, 0
    li x29, 0
    li x30, 0
    li x31, 0
    li sp, DRAM_BASE+DRAM_SIZE # stack pointer
    la t0, trap_vector
    csrw mtvec, t0
    jal main
    j trap_vector

    .pushsection .tohost,"aw",@progbits
    .align 6
    .global tohost
tohost:
    .dword 0
    .size tohost, 8

    .align 6
    .global fromhost
fromhost:
    .dword 0
    .size fromhost, 8
    .popsection
)asm";

const char* const kStandardLinkerScript = R"ld(/*
 * SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Archlab, Science Tokyo
 */
OUTPUT_ARCH("riscv")
ENTRY(_start)

SECTIONS
{
  . = 0x80000000;
  .text.init : { *(.text.init) }
  . = ALIGN(0x1000);
  .text : { *(.text .text.*) }
  . = ALIGN(0x1000);
  .rodata : { *(.rodata .rodata.*) }
  . = ALIGN(0x1000);
  .data : { *(.data .data.*) }
  .bss : { *(.bss .bss.*) *(COMMON) }
  . = ALIGN(0x1000);
  .tohost : { *(.tohost) }
  _end = .;
}
)ld";

const std::vector<Benchmark>& build_benchmarks() {
    static const std::vector<Benchmark> kBenchmarks = {{"arithmetic_loop", R"c(
int main(void) {
    int a = 1;
    int b = 2;
    for (int i = 0; i < 100; i++) {
        a = (a * 3) + b;
        b = a ^ (b << 1);
    }
    return a + b;
}
)c"},
                                                       {"raw_hazard_chain", R"c(
int main(void) {
    register int v = 42;
    for (int i = 0; i < 100; i++) {
        asm volatile (
            "add %[v], %[v], %[i]\n"
            "sub %[v], %[v], %[i]\n"
            "add %[v], %[v], %[i]\n"
            "sub %[v], %[v], %[i]\n"
            : [v] "+r" (v) : [i] "r" (i)
        );
    }
    return v;
}
)c"},
                                                       {"branch_dense", R"c(
int main(void) {
    int s = 0;
    for (int i = 0; i < 100; i++) {
        if (i % 2 == 0) {
            s += i;
        } else {
            s -= i;
        }
    }
    return s;
}
)c"},
                                                       {"mul_div_suite", R"c(
int main(void) {
    int a = 12345;
    int b = 67;
    int s = 0;
    for (int i = 1; i <= 20; i++) {
        s += (a * i) / (b + i);
    }
    return s;
}
)c"},
                                                       {"load_store_array", R"c(
int arr[64];
int main(void) {
    for (int i = 0; i < 64; i++) {
        arr[i] = i * 3 + 1;
    }
    int sum = 0;
    for (int i = 0; i < 64; i++) {
        sum += arr[i];
    }
    return sum;
}
)c"}};
    return kBenchmarks;
}

class PluginTargetAdapter final : public ParityTarget {
   public:
    PluginTargetAdapter(const SimRvRtlTarget* target, void* dl_handle)
        : target_(target), dl_handle_(dl_handle) {}

    [[nodiscard]] auto name() const noexcept -> std::string_view override {
        return target_ && target_->name ? target_->name : "unknown";
    }

    [[nodiscard]] auto description() const noexcept -> std::string_view override {
        return target_ && target_->description ? target_->description : "";
    }

    auto prepare(const SimRvRtlContext& ctx) -> bool override {
        if (!target_ || !target_->prepare) return true;
        return target_->prepare(&ctx) == 0;
    }

    auto run_benchmark(const SimRvRtlContext& ctx, std::string_view bench_name,
                       const std::filesystem::path& elf_path, SimRvParityResult& out_result)
        -> bool override {
        if (!target_ || !target_->run_benchmark) return false;
        std::string bench_str(bench_name);
        std::string elf_str = elf_path.string();
        return target_->run_benchmark(&ctx, bench_str.c_str(), elf_str.c_str(), &out_result) == 0;
    }

    void cleanup(const SimRvRtlContext& ctx) override {
        if (target_ && target_->cleanup) {
            target_->cleanup(&ctx);
        }
    }

   private:
    const SimRvRtlTarget* target_{nullptr};
    void* dl_handle_{nullptr};
};

auto check_tool(std::string_view name) -> std::optional<std::filesystem::path> {
    const char* path_env = std::getenv("PATH");
    if (!path_env) return std::nullopt;

    std::string_view paths(path_env);
    std::size_t start = 0;
    while (start < paths.size()) {
        std::size_t end = paths.find(':', start);
        if (end == std::string_view::npos) end = paths.size();
        std::filesystem::path dir = std::string(paths.substr(start, end - start));
        auto candidate = dir / name;
        if (::access(candidate.c_str(), X_OK) == 0) {
            return candidate;
        }
        start = end + 1;
    }
    return std::nullopt;
}

}  // namespace

auto get_standard_crt0() -> std::string_view { return kStandardCrt0; }
auto get_standard_linker_script() -> std::string_view { return kStandardLinkerScript; }
auto get_embedded_benchmarks() -> const std::vector<Benchmark>& { return build_benchmarks(); }

auto find_riscv_toolchain() -> std::optional<Toolchain> {
    constexpr std::array<const char*, 4> kCandidates = {
        "riscv32-linux-gnu-gcc", "riscv32-unknown-elf-gcc", "riscv64-linux-gnu-gcc",
        "riscv64-unknown-elf-gcc"};

    for (const auto* gcc_cand : kCandidates) {
        if (auto found_gcc = check_tool(gcc_cand)) {
            std::string stem = found_gcc->filename().string();
            if (stem.ends_with("-gcc")) {
                std::string prefix = stem.substr(0, stem.size() - 3);
                auto objcopy = check_tool(prefix + "objcopy");
                auto objdump = check_tool(prefix + "objdump");
                if (objcopy && objdump) {
                    return Toolchain{.gcc = *found_gcc, .objcopy = *objcopy, .objdump = *objdump};
                }
            }
        }
    }
    return std::nullopt;
}

auto parse_retirement_trace(std::string_view text) -> std::vector<RetirementEvent> {
    std::vector<RetirementEvent> events;
    static const std::regex kTraceRegex(
        R"re(CYCLETRACE cycle=(\d+) inst=(\d+) pc=([0-9a-fA-F]+) ir=([0-9a-fA-F]+))re");

    std::string text_str(text);
    auto words_begin = std::sregex_iterator(text_str.begin(), text_str.end(), kTraceRegex);
    auto words_end = std::sregex_iterator();

    for (std::sregex_iterator i = words_begin; i != words_end; ++i) {
        const std::smatch& match = *i;
        const uint64_t pc = std::stoull(match[3].str(), nullptr, 16);
        if (!std::in_range<simrv::xlen::Word>(pc)) return {};

        RetirementEvent ev;
        ev.cycle = std::stoull(match[1].str());
        ev.inst = std::stoull(match[2].str());
        ev.pc = simrv::xlen::VirtAddr{static_cast<simrv::xlen::Word>(pc)};
        ev.ir = static_cast<uint32_t>(std::stoul(match[4].str(), nullptr, 16));
        events.push_back(ev);
    }
    return events;
}

auto compare_retirement_traces(std::span<const RetirementEvent> rtl,
                               std::span<const RetirementEvent> simrv) -> TraceComparison {
    TraceComparison comp;
    comp.rtl_events = rtl.size();
    comp.simrv_events = simrv.size();

    const std::size_t min_events = std::min(rtl.size(), simrv.size());
    for (std::size_t i = 0; i < min_events; ++i) {
        if (rtl[i].pc != simrv[i].pc || rtl[i].ir != simrv[i].ir) {
            comp.first_architectural_divergence = i;
            break;
        }

        const uint64_t rtl_step = i ? (rtl[i].cycle - rtl[i - 1].cycle) : rtl[i].cycle;
        const uint64_t simrv_step = i ? (simrv[i].cycle - simrv[i - 1].cycle) : simrv[i].cycle;
        if (!comp.first_timing_divergence.has_value() && rtl_step != simrv_step) {
            comp.first_timing_divergence = TimingDivergence{
                .index = i,
                .pc = rtl[i].pc,
                .rtl_step_cycles = rtl_step,
                .simrv_step_cycles = simrv_step,
            };
        }
    }

    if (!comp.first_architectural_divergence.has_value() && rtl.size() != simrv.size()) {
        comp.first_architectural_divergence = min_events;
    }

    return comp;
}

ParityRegistry::ParityRegistry() {
    targets_.push_back(create_rvcomp_target());
    targets_.push_back(create_cfu_pg_target());
}

auto ParityRegistry::instance() -> ParityRegistry& {
    static ParityRegistry kInstance;
    return kInstance;
}

void ParityRegistry::register_target(std::unique_ptr<ParityTarget> target) {
    if (target) {
        targets_.push_back(std::move(target));
    }
}

auto ParityRegistry::load_plugin(const std::filesystem::path& plugin_path) -> bool {
    void* handle = ::dlopen(plugin_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        simrv::log::error("ParityRegistry: dlopen failed for {}: {}", plugin_path.string(),
                          ::dlerror());
        return false;
    }

    auto init_fn = reinterpret_cast<SimRvRtlPluginInitFn>(::dlsym(handle, "simrv_rtl_plugin_init"));
    if (!init_fn) {
        simrv::log::error("ParityRegistry: dlsym 'simrv_rtl_plugin_init' failed for {}: {}",
                          plugin_path.string(), ::dlerror());
        ::dlclose(handle);
        return false;
    }

    const SimRvRtlTarget* target_desc = init_fn();
    if (!target_desc) {
        simrv::log::error("ParityRegistry: plugin init returned null target for {}",
                          plugin_path.string());
        ::dlclose(handle);
        return false;
    }

    dl_handles_.push_back(handle);
    targets_.push_back(std::make_unique<PluginTargetAdapter>(target_desc, handle));
    simrv::log::info("ParityRegistry: Loaded external RTL target plugin '{}' from {}",
                     target_desc->name ? target_desc->name : "unnamed", plugin_path.string());
    return true;
}

auto ParityRegistry::get_target(std::string_view name) -> ParityTarget* {
    for (const auto& t : targets_) {
        if (t && t->name() == name) {
            return t.get();
        }
    }
    return nullptr;
}

auto ParityRegistry::all_targets() const -> const std::vector<std::unique_ptr<ParityTarget>>& {
    return targets_;
}

}  // namespace simrv::rtl
