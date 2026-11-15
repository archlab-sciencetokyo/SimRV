#include <array>
#include <cassert>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "simrv/core/CpuConfigParser.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/memory/Bus.hpp"
#include "simrv/memory/BusProtocolSpec.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/TemplatedBus.hpp"
#include "simrv/util/CliParser.hpp"
#include "simrv/xlen/AddressTypes.hpp"
#include "simrv/xlen/ScalarTypes.hpp"

namespace {

#define TEST_CHECK(cond)                                                                           \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::cerr << "Assertion failed: " #cond " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            std::abort();                                                                          \
        }                                                                                          \
    } while (false)

void test_catalog_lookup() {
    std::cout << "[Test] Bus catalog lookup...\n";
    TEST_CHECK(simrv::memory::kBusCatalog.size() == 5);
    TEST_CHECK(simrv::memory::find_bus_catalog_entry("tilelink-c")->protocol ==
               simrv::memory::BusProtocol::TileLinkC);
    TEST_CHECK(simrv::memory::find_bus_catalog_entry("tilelink-uh")->protocol ==
               simrv::memory::BusProtocol::TileLinkUH);
    TEST_CHECK(simrv::memory::find_bus_catalog_entry("tilelink-ul")->protocol ==
               simrv::memory::BusProtocol::TileLinkUL);
    TEST_CHECK(simrv::memory::find_bus_catalog_entry("axi4")->protocol ==
               simrv::memory::BusProtocol::Axi4);
    TEST_CHECK(simrv::memory::find_bus_catalog_entry("axi4-lite")->protocol ==
               simrv::memory::BusProtocol::Axi4Lite);
    TEST_CHECK(simrv::memory::find_bus_catalog_entry("axi")->protocol ==
               simrv::memory::BusProtocol::Axi4);
    TEST_CHECK(simrv::memory::find_bus_catalog_entry("tl-c")->protocol ==
               simrv::memory::BusProtocol::TileLinkC);
    TEST_CHECK(simrv::memory::find_bus_catalog_entry("nonexistent") == nullptr);
}

void test_tilelink_variants() {
    std::cout << "[Test] TileLink-C / UH / UL protocol specialization...\n";
    simrv::core::Machine machine;
    std::array<simrv::xlen::Byte, 4096> ram{};
    machine.set_ram_for_testing(ram.data(), ram.size());
    const auto base = machine.memory_geometry().dram_base;

    // TileLink-C: coherent + atomics + bursts
    machine.memory().configure_bus(simrv::memory::BusProtocol::TileLinkC);
    TEST_CHECK(machine.memory().system_bus().protocol() == simrv::memory::BusProtocol::TileLinkC);
    TEST_CHECK(machine.memory().system_bus().has_coherence());

    // TileLink-UH: non-coherent, atomics allowed
    machine.memory().configure_bus(simrv::memory::BusProtocol::TileLinkUH);
    auto& uh = machine.memory().system_bus();
    TEST_CHECK(!uh.has_coherence());

    simrv::memory::TlChannelA acq{};
    acq.opcode = simrv::memory::TlOpcodeA::AcquireBlock;
    acq.address = base;
    acq.size = 6;
    acq.source = 1;
    TEST_CHECK(!uh.send_request(acq));

    simrv::memory::TlChannelA amo{};
    amo.opcode = simrv::memory::TlOpcodeA::LogicalData;
    amo.logical = simrv::memory::TlLogical::Or;
    amo.address = base;
    amo.size = 2;
    amo.data = 0x55U;
    amo.source = 1;
    TEST_CHECK(uh.send_request(amo));
    simrv::memory::TlChannelD amo_resp{};
    TEST_CHECK(uh.get_response(1, amo_resp));
    TEST_CHECK(!amo_resp.failed());

    // TileLink-UL: non-coherent, no atomics, no multi-beat bursts
    machine.memory().configure_bus(simrv::memory::BusProtocol::TileLinkUL);
    auto& ul = machine.memory().system_bus();
    TEST_CHECK(!ul.has_coherence());
    TEST_CHECK(!ul.send_request(amo));

    simrv::memory::TlChannelA burst{};
    burst.opcode = simrv::memory::TlOpcodeA::Get;
    burst.address = base;
    burst.size = 6;
    burst.source = 1;
    TEST_CHECK(!ul.send_request(burst));

    simrv::memory::TlChannelA put{};
    put.opcode = simrv::memory::TlOpcodeA::PutFullData;
    put.address = base + 8;
    put.size = 2;
    put.data = 0x12345678U;
    put.source = 1;
    TEST_CHECK(ul.send_request(put));
    simrv::memory::TlChannelD put_resp{};
    TEST_CHECK(ul.get_response(1, put_resp));
    TEST_CHECK(!put_resp.failed());
}

void test_axi4_and_axi4_lite() {
    std::cout << "[Test] AXI4 & AXI4-Lite protocol specialization...\n";
    simrv::core::Machine machine;
    std::array<simrv::xlen::Byte, 4096> ram{};
    machine.set_ram_for_testing(ram.data(), ram.size());
    const auto base = machine.memory_geometry().dram_base;

    // AXI4: multi-beat bursts + channel-level AR/R and AW/W/B
    machine.memory().configure_bus(simrv::memory::BusProtocol::Axi4);
    auto& axi = machine.memory().system_bus();
    TEST_CHECK(axi.protocol() == simrv::memory::BusProtocol::Axi4);
    TEST_CHECK(!axi.has_coherence());

    const std::array<simrv::xlen::Word, 4> pattern = {0x11111111U, 0x22222222U, 0x33333333U,
                                                      0x44444444U};
    const auto wresp = axi.axi_write(base.raw() + 64, pattern, sizeof(simrv::xlen::Word));
    TEST_CHECK(wresp == simrv::memory::AxiResp::Okay);

    const auto [rresp, readback] = axi.axi_read(base.raw() + 64, sizeof(simrv::xlen::Word), 3);
    TEST_CHECK(rresp == simrv::memory::AxiResp::Okay);
    TEST_CHECK(readback.size() == 4);
    for (size_t i = 0; i < 4; ++i) {
        TEST_CHECK(readback[i] == pattern[i]);
    }

    // AXI4-Lite: single-beat allowed, multi-beat burst rejected
    machine.memory().configure_bus(simrv::memory::BusProtocol::Axi4Lite);
    auto& lite = machine.memory().system_bus();
    TEST_CHECK(lite.protocol() == simrv::memory::BusProtocol::Axi4Lite);
    const std::array<simrv::xlen::Word, 1> single = {0xCAFEBABEU};
    TEST_CHECK(lite.axi_write(base.raw() + 128, single, 4) == simrv::memory::AxiResp::Okay);
    const auto [lresp, ldata] = lite.axi_read(base.raw() + 128, 4, 0);
    TEST_CHECK(lresp == simrv::memory::AxiResp::Okay);
    TEST_CHECK(ldata.size() == 1 && (ldata[0] & 0xFFFFFFFFU) == 0xCAFEBABEU);

    // Burst len > 0 must be rejected on AXI4-Lite
    simrv::memory::Axi4Ar bad_ar{};
    bad_ar.addr = base.raw() + 128;
    bad_ar.size = 2;
    bad_ar.len = 3;
    TEST_CHECK(!lite.send_ar(bad_ar));
}

void test_config_and_cli_bus_selection() {
    std::cout << "[Test] Config [bus]/[soc] and CLI --bus selection...\n";
    simrv::core::SoCConfig soc{};
    std::istringstream cfg_stream("[soc]\nname = \"axi-soc\"\n[bus]\nprotocol = \"axi4\"\n");
    TEST_CHECK(simrv::core::parse_soc_config_stream(cfg_stream, soc));
    TEST_CHECK(soc.bus_protocol == simrv::memory::BusProtocol::Axi4);

    std::array<std::string, 4> args_str = {"SimRV", "--bus", "axi4-lite", "guest.bin"};
    std::array<char*, 4> args = {args_str[0].data(), args_str[1].data(), args_str[2].data(),
                                 args_str[3].data()};
    const auto parsed = simrv::util::parse_command_line(args);
    TEST_CHECK(parsed.has_value());
    TEST_CHECK(parsed->options.bus_protocol == simrv::memory::BusProtocol::Axi4Lite);
    const auto mcfg = parsed->options.to_machine_config();
    TEST_CHECK(mcfg.soc.bus_protocol == simrv::memory::BusProtocol::Axi4Lite);

    std::array<std::string, 4> bad_str = {"SimRV", "--bus", "wishbone-99", "guest.bin"};
    std::array<char*, 4> bad_args = {bad_str[0].data(), bad_str[1].data(), bad_str[2].data(),
                                     bad_str[3].data()};
    const auto bad = simrv::util::parse_command_line(bad_args);
    TEST_CHECK(!bad.has_value());
    TEST_CHECK(bad.error().find("axi4") != std::string::npos);
    TEST_CHECK(bad.error().find("tilelink-c") != std::string::npos);
}

}  // namespace

int main() {
    test_catalog_lookup();
    test_tilelink_variants();
    test_axi4_and_axi4_lite();
    test_config_and_cli_bus_selection();
    std::cout << "All BusProtocol tests passed!\n";
    return 0;
}
