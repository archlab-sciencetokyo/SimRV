#include <array>
#include <cstdlib>
#include <string_view>
#include <type_traits>

#include "simrv/rtl/ParityTarget.hpp"

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
}
