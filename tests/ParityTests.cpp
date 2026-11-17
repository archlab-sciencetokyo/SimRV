#include <array>
#include <cstdlib>
#include <string_view>
#include <type_traits>

#include "simrv/rtl/ParityTarget.hpp"

#ifndef TEST_PARITY_PLUGIN_PATH
#error "TEST_PARITY_PLUGIN_PATH must point to the generic C ABI test plugin"
#endif

static_assert(std::is_same_v<decltype(simrv::rtl::RetirementEvent::pc), simrv::xlen::VirtAddr>);
static_assert(std::is_same_v<decltype(simrv::rtl::TimingDivergence::pc), simrv::xlen::VirtAddr>);

int main() {
    const auto in_range =
        simrv::rtl::parse_retirement_trace("CYCLETRACE cycle=1 inst=1 pc=80000000 ir=00000013");
    if (in_range.size() != 1 || in_range.front().pc != simrv::xlen::VirtAddr{0x80000000})
        std::abort();

    const auto above_rv32 =
        simrv::rtl::parse_retirement_trace("CYCLETRACE cycle=2 inst=2 pc=100000000 ir=00000013");
#if SIMRV_XLEN == 32
    if (!above_rv32.empty()) std::abort();
#else
    if (above_rv32.size() != 1 || above_rv32.front().pc != simrv::xlen::VirtAddr{0x100000000ULL})
        std::abort();
#endif

    std::array traces{in_range, in_range};
    const auto comparison = simrv::rtl::compare_retirement_traces(traces[0], traces[1]);
    if (!comparison.is_match()) std::abort();

    auto& registry = simrv::rtl::ParityRegistry::instance();
    if (!registry.load_plugin(TEST_PARITY_PLUGIN_PATH)) std::abort();
    auto* target = registry.get_target("test-target");
    if (!target || target->name() != "test-target") std::abort();
    SimRvRtlContext context{};
    if (!target->prepare(context)) std::abort();
    SimRvParityResult result{};
    if (!target->run_benchmark(context, "smoke", "smoke.elf", result)) std::abort();
    if (result.rtl_cycles != 17 || result.simrv_cycles != 19 ||
        result.rtl_instructions != 3 || result.simrv_instructions != 3)
        std::abort();
    target->cleanup(context);
}
