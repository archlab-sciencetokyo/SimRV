#include <array>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/CpuConfigParser.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/memory/Bus.hpp"
#include "simrv/memory/BusProtocolSpec.hpp"
#include "simrv/memory/MemoryAccess.hpp"
#include "simrv/memory/MemorySubsystem.hpp"
#include "simrv/memory/MmioDevice.hpp"
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
    TEST_CHECK(simrv::memory::kBusCatalog.size() == 6);
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
    TEST_CHECK(simrv::memory::find_bus_catalog_entry("axi4-lite-unified")->protocol ==
               simrv::memory::BusProtocol::Axi4LiteUnified);
    TEST_CHECK(simrv::memory::find_bus_catalog_entry("axi")->protocol ==
               simrv::memory::BusProtocol::Axi4);
    TEST_CHECK(simrv::memory::find_bus_catalog_entry("tl-c")->protocol ==
               simrv::memory::BusProtocol::TileLinkC);
    TEST_CHECK(simrv::memory::find_bus_catalog_entry("nonexistent") == nullptr);
}

void test_probe_tracking() {
    simrv::memory::TileLinkProtocolChecker checker;
    simrv::memory::TlChannelB first{};
    first.source = 1;
    first.address = simrv::xlen::PhysAddr{0x80000000};
    auto second = first;
    second.source = 2;
    TEST_CHECK(checker.accept_b(first).has_value());
    TEST_CHECK(checker.accept_b(second).has_value());
    TEST_CHECK(!checker.accept_b(first).has_value());
    TEST_CHECK(checker.outstanding_probes() == 2);
    simrv::memory::TlChannelC ack{};
    ack.opcode = simrv::memory::TlOpcodeC::ProbeAck;
    ack.source = 1;
    ack.address = first.address + simrv::memory::kTlBlockBytes;
    TEST_CHECK(!checker.accept_c(ack).has_value());
    TEST_CHECK(checker.outstanding_probes() == 2);
    ack.address = first.address;
    TEST_CHECK(checker.accept_c(ack).has_value());
    TEST_CHECK(!checker.accept_c(ack).has_value());
    TEST_CHECK(checker.outstanding_probes() == 1);
    checker.cancel(second.source);
    TEST_CHECK(checker.outstanding_probes() == 0);

    simrv::memory::TlChannelC release{};
    release.opcode = simrv::memory::TlOpcodeC::Release;
    release.source = 3;
    release.address = first.address;
    TEST_CHECK(checker.accept_c(release).has_value());
    simrv::memory::TlChannelD response{};
    response.opcode = simrv::memory::TlOpcodeD::ReleaseAck;
    response.source = release.source;
    response.size = release.size - 1;
    TEST_CHECK(!checker.accept_d(response).has_value());
    TEST_CHECK(checker.outstanding_releases() == 1);
    response.size = release.size;
    TEST_CHECK(checker.accept_d(response).has_value());
}

void test_external_reservations() {
    simrv::memory::ReservationTable reservations;
    const simrv::xlen::PhysAddr base{0x80000000};
    const simrv::xlen::HartId hart{0};
    reservations.set_reservation(hart, base);
    reservations.set_external_reservation(1, base, 8);
    reservations.set_external_reservation(2, base + 16, 8);
    TEST_CHECK(reservations.may_have_reservations());
    TEST_CHECK(reservations.may_have_external_reservations());
    reservations.invalidate_range(base + 4, 1, hart);
    TEST_CHECK(reservations.check_reservation(hart, base));
    TEST_CHECK(!reservations.check_and_clear_external_reservation(1, base, 8));
    TEST_CHECK(reservations.check_and_clear_external_reservation(2, base + 16, 8));
    TEST_CHECK(!reservations.may_have_external_reservations());
    TEST_CHECK(reservations.may_have_reservations());
    reservations.set_external_reservation(3, base, 4);
    TEST_CHECK(!reservations.check_and_clear_external_reservation(3, base, 8));
    TEST_CHECK(!reservations.check_and_clear_external_reservation(3, base, 4));
    reservations.clear_all();
    TEST_CHECK(!reservations.may_have_reservations());
}

void test_external_reservation_invalidation() {
    simrv::core::Machine machine;
    std::array<simrv::xlen::Byte, 4096> ram{};
    machine.set_ram_for_testing(ram.data(), ram.size());
    auto& cpu = machine.hart(simrv::xlen::HartId{0});
    auto& reservations = machine.memory().reservation_table();
    const auto base = machine.memory_geometry().dram_base;
    reservations.set_external_reservation(4, base, 4);
    simrv::memory::MemoryAccess::storeInt(machine.memory(), cpu, simrv::xlen::VirtAddr{base.raw()},
                                          0x12345678, simrv::isa::Funct3::Sw);
    TEST_CHECK(!reservations.check_and_clear_external_reservation(4, base, 4));

    reservations.set_external_reservation(5, base + 8, 4);
    const uint32_t dma_value = 0x11223344;
    TEST_CHECK(machine.memory().dma_write(base + 8, &dma_value, sizeof(dma_value)));
    TEST_CHECK(!reservations.check_and_clear_external_reservation(5, base + 8, 4));

    machine.memory().configure_bus(simrv::memory::BusProtocol::Axi4);
    reservations.set_external_reservation(6, base + 16, 4);
    const std::array<simrv::xlen::Word, 1> bus_value{0xaabbccdd};
    TEST_CHECK(machine.memory().system_bus().axi_write(base.raw() + 16, bus_value, 4) ==
               simrv::memory::AxiResp::Okay);
    TEST_CHECK(!reservations.check_and_clear_external_reservation(6, base + 16, 4));
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
    simrv::memory::TlChannelC invalid_release{};
    invalid_release.opcode = simrv::memory::TlOpcodeC::Release;
    invalid_release.address = base + 1;
    simrv::memory::TlChannelD release_response{};
    TEST_CHECK(!machine.memory().system_bus().release_line(invalid_release, release_response));
    invalid_release.address = base;
    invalid_release.opcode = simrv::memory::TlOpcodeC::ReleaseData;
    TEST_CHECK(!machine.memory().system_bus().release_line(invalid_release, release_response));

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

    // TileLink-UL: non-coherent, no atomics, multibeat reads permitted.
    machine.memory().configure_bus(simrv::memory::BusProtocol::TileLinkUL);
    auto& ul = machine.memory().system_bus();
    TEST_CHECK(!ul.has_coherence());
    TEST_CHECK(!ul.send_request(amo));

    simrv::memory::TlChannelA burst{};
    burst.opcode = simrv::memory::TlOpcodeA::Get;
    burst.address = base;
    burst.size = 6;
    burst.source = 1;
    TEST_CHECK(ul.send_request(burst));
    simrv::memory::TlChannelD burst_resp{};
    TEST_CHECK(ul.get_response(1, burst_resp));
    TEST_CHECK(!burst_resp.failed());

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

    // Reject invalid lengths and widths before shifting or touching memory.
    simrv::memory::Axi4Ar invalid{};
    invalid.addr = base.raw();
    invalid.size = 255;
    TEST_CHECK(!axi.send_ar(invalid));
    invalid.size = 2;
    invalid.burst = simrv::memory::AxiBurst::Fixed;
    invalid.len = 16;
    TEST_CHECK(!axi.send_ar(invalid));
    invalid.burst = simrv::memory::AxiBurst::Incr;
    invalid.addr = (base.raw() & ~simrv::xlen::Address{0xfff}) + 0xffc;
    invalid.len = 1;
    TEST_CHECK(!axi.send_ar(invalid));
    simrv::memory::Axi4Aw invalid_write{};
    invalid_write.addr = invalid.addr;
    invalid_write.size = invalid.size;
    invalid_write.len = invalid.len;
    TEST_CHECK(!axi.send_aw(invalid_write));
    simrv::memory::Axi4Ar exclusive_read{};
    exclusive_read.addr = base.raw();
    exclusive_read.size = 2;
    exclusive_read.lock = 1;
    exclusive_read.len = 2;
    TEST_CHECK(!axi.send_ar(exclusive_read));
    simrv::memory::Axi4Aw exclusive_write{};
    exclusive_write.addr = base.raw();
    exclusive_write.size = 2;
    exclusive_write.lock = 1;
    exclusive_write.len = 2;
    TEST_CHECK(!axi.send_aw(exclusive_write));

    // Write data and address channels are independent: data can arrive first.
    simrv::memory::Axi4W early_data{};
    early_data.data = 0x12345678U;
    early_data.strb = 0xf;
    TEST_CHECK(axi.send_w(early_data));
    simrv::memory::Axi4B early_response{};
    TEST_CHECK(!axi.recv_b(early_response));
    simrv::memory::Axi4Aw late_address{};
    late_address.addr = base.raw() + 256;
    late_address.size = 2;
    TEST_CHECK(axi.send_aw(late_address));
    TEST_CHECK(axi.recv_b(early_response));
    TEST_CHECK(early_response.resp == simrv::memory::AxiResp::Okay);
    const auto [early_read_status, early_read_data] = axi.axi_read(base.raw() + 256, 4);
    TEST_CHECK(early_read_status == simrv::memory::AxiResp::Okay);
    TEST_CHECK(early_read_data.size() == 1 && early_read_data[0] == early_data.data);

    // A narrow unaligned first beat uses the addressed lanes; the second beat
    // starts at the next transfer-size boundary.
    simrv::memory::Axi4Aw narrow{};
    narrow.addr = base.raw() + 257;
    narrow.size = 1;
    narrow.len = 1;
    TEST_CHECK(axi.send_aw(narrow));
    simrv::memory::Axi4W first{};
    first.data = simrv::xlen::Word{0xaa} << 8;
    first.strb = 0x2;
    first.last = false;
    TEST_CHECK(axi.send_w(first));
    simrv::memory::Axi4W second{};
    second.data = simrv::xlen::Word{0xccbb} << 16;
    second.strb = 0xc;
    TEST_CHECK(axi.send_w(second));
    TEST_CHECK(axi.recv_b(early_response));
    TEST_CHECK(early_response.resp == simrv::memory::AxiResp::Okay);
    TEST_CHECK(ram[257] == simrv::xlen::Byte{0xaa} && ram[258] == simrv::xlen::Byte{0xbb} &&
               ram[259] == simrv::xlen::Byte{0xcc});
    simrv::memory::Axi4Ar narrow_read{};
    narrow_read.addr = narrow.addr;
    narrow_read.size = narrow.size;
    narrow_read.len = narrow.len;
    TEST_CHECK(axi.send_ar(narrow_read));
    simrv::memory::Axi4R read_beat{};
    TEST_CHECK(axi.recv_r(read_beat));
    TEST_CHECK(read_beat.data == (simrv::xlen::Word{0xaa} << 8) && !read_beat.last);
    TEST_CHECK(axi.recv_r(read_beat));
    TEST_CHECK(read_beat.data == (simrv::xlen::Word{0xccbb} << 16) && read_beat.last);

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

void test_custom_axi_spec() {
    simrv::core::Machine machine;
    auto spec = simrv::memory::kDefaultAxi4Spec;
    spec.id_width_bits = 1;
    spec.max_outstanding = 1;
    spec.max_burst_beats = 2;
    machine.memory().configure_bus(spec);
    auto& bus = machine.memory().system_bus();
    TEST_CHECK(bus.protocol_spec() == spec);
    simrv::memory::Axi4Ar ar{};
    ar.size = 2;
    ar.id = 2;
    TEST_CHECK(!bus.send_ar(ar));
    ar.id = 0;
    ar.len = 2;
    TEST_CHECK(!bus.send_ar(ar));
    ar.len = 0;
    TEST_CHECK(bus.send_ar(ar));
    TEST_CHECK(!bus.send_ar(ar));
    simrv::memory::Axi4R response{};
    TEST_CHECK(bus.recv_r(response));
    TEST_CHECK(bus.send_ar(ar));
    TEST_CHECK(bus.recv_r(response));
    auto invalid = spec;
    invalid.max_outstanding = 0;
    bool rejected = false;
    try {
        machine.memory().configure_bus(invalid);
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    TEST_CHECK(rejected);
    TEST_CHECK(machine.memory().system_bus().protocol_spec() == spec);
}

void test_axi_cycle_handshake() {
    simrv::core::Machine machine;
    std::array<simrv::xlen::Byte, 4096> ram{};
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.memory().configure_bus(simrv::memory::BusProtocol::Axi4);
    auto& bus = machine.memory().system_bus();
    simrv::memory::Axi4CycleInput in{};
    in.ar_valid = true;
    in.ar.addr = machine.memory_geometry().dram_base.raw();
    in.ar.size = 2;
    auto out = bus.axi_cycle(in);
    TEST_CHECK(out.ar_ready && !out.r_valid);
    in.ar_valid = false;
    out = bus.axi_cycle(in);
    TEST_CHECK(out.r_valid && out.r.last);
    const auto held = out.r;
    out = bus.axi_cycle(in);
    TEST_CHECK(out.r_valid && out.r.id == held.id && out.r.data == held.data &&
               out.r.resp == held.resp && out.r.last == held.last);
    in.r_ready = true;
    TEST_CHECK(bus.axi_cycle(in).r_valid);
    TEST_CHECK(!bus.axi_cycle(in).r_valid);

    in = {};
    in.w_valid = true;
    in.w.data = 0x12345678;
    in.w.strb = 0xf;
    TEST_CHECK(bus.axi_cycle(in).w_ready);
    in = {};
    in.aw_valid = true;
    in.aw.addr = machine.memory_geometry().dram_base.raw();
    in.aw.size = 2;
    TEST_CHECK(bus.axi_cycle(in).aw_ready);
    in = {};
    const auto held_b = bus.axi_cycle(in);
    TEST_CHECK(held_b.b_valid && held_b.b.resp == simrv::memory::AxiResp::Okay);
    const auto next_b = bus.axi_cycle(in);
    TEST_CHECK(next_b.b_valid && next_b.b.id == held_b.b.id && next_b.b.resp == held_b.b.resp);
    in.b_ready = true;
    TEST_CHECK(bus.axi_cycle(in).b_valid);
    TEST_CHECK(!bus.axi_cycle(in).b_valid);

    // A stalled read response must not prefetch later burst beats.
    ram[0] = simrv::xlen::Byte{0x11};
    ram[1] = simrv::xlen::Byte{0x22};
    const auto reads_before = bus.read_count();
    in = {};
    in.ar_valid = true;
    in.ar.addr = machine.memory_geometry().dram_base.raw();
    in.ar.size = 0;
    in.ar.len = 1;
    TEST_CHECK(bus.axi_cycle(in).ar_ready);
    TEST_CHECK(bus.read_count() == reads_before + 1);
    in.ar_valid = false;
    out = bus.axi_cycle(in);
    TEST_CHECK(out.r_valid && out.r.data == 0x11 && !out.r.last);
    TEST_CHECK(bus.read_count() == reads_before + 1);
    ram[1] = simrv::xlen::Byte{0xbb};
    in.r_ready = true;
    TEST_CHECK(bus.axi_cycle(in).r.data == 0x11);
    TEST_CHECK(bus.read_count() == reads_before + 2);
    out = bus.axi_cycle(in);
    TEST_CHECK(out.r_valid && out.r.data == 0xbb00 && out.r.last);

    // Buffered write data is applied no faster than one beat per clock.
    in = {};
    in.w_valid = true;
    in.w.data = 0x44;
    in.w.strb = 1;
    in.w.last = false;
    TEST_CHECK(bus.axi_cycle(in).w_ready);
    in.w.data = 0x5500;
    in.w.strb = 2;
    in.w.last = true;
    TEST_CHECK(bus.axi_cycle(in).w_ready);
    in = {};
    in.aw_valid = true;
    in.aw.addr = machine.memory_geometry().dram_base.raw() + 320;
    in.aw.size = 0;
    in.aw.len = 1;
    TEST_CHECK(bus.axi_cycle(in).aw_ready);
    TEST_CHECK(ram[320] == simrv::xlen::Byte{0x44});
    TEST_CHECK(ram[321] == simrv::xlen::Byte{0});
    in = {};
    TEST_CHECK(!bus.axi_cycle(in).b_valid);
    TEST_CHECK(ram[321] == simrv::xlen::Byte{0x55});
    const auto write_response = bus.axi_cycle(in);
    TEST_CHECK(write_response.b_valid && write_response.b.resp == simrv::memory::AxiResp::Okay);
    in.b_ready = true;
    TEST_CHECK(bus.axi_cycle(in).b_valid);
}

void test_tilelink_cycle_handshake() {
    simrv::core::Machine machine;
    std::array<simrv::xlen::Byte, 4096> ram{};
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.memory().configure_bus(simrv::memory::BusProtocol::TileLinkUL);
    auto& bus = machine.memory().system_bus();
    simrv::memory::TlCycleInput in{};
    in.a_valid = true;
    in.a.opcode = simrv::memory::TlOpcodeA::Get;
    in.a.address = machine.memory_geometry().dram_base;
    in.a.size = 2;
    in.a.mask = 0;
    in.a.source = 7;
    TEST_CHECK(!bus.tilelink_cycle(in).a_ready);
    in.a.mask = 0xf;
    auto out = bus.tilelink_cycle(in);
    TEST_CHECK(out.a_ready && !out.d_valid && !out.e_ready);
    in.a_valid = false;
    out = bus.tilelink_cycle(in);
    TEST_CHECK(out.d_valid && out.d.source == 7 && !out.d.failed());
    const auto held = out.d;
    out = bus.tilelink_cycle(in);
    TEST_CHECK(out.d_valid && out.d == held);
    TEST_CHECK(bus.protocol_checker().outstanding_sources() == 1);
    in.d_ready = true;
    TEST_CHECK(bus.tilelink_cycle(in).d_valid);
    TEST_CHECK(bus.protocol_checker().outstanding_sources() == 0);
    TEST_CHECK(!bus.tilelink_cycle(in).d_valid);
}

void test_tilelink_cycle_release() {
    simrv::core::Machine machine;
    std::array<simrv::xlen::Byte, 4096> ram{};
    machine.set_ram_for_testing(ram.data(), ram.size());
    auto& bus = machine.memory().system_bus();
    simrv::memory::TlCycleInput in{};
    in.c_valid = true;
    in.c.opcode = simrv::memory::TlOpcodeC::ReleaseData;
    in.c.address = machine.memory_geometry().dram_base;
    in.c.source = 11;
    for (uint64_t beat = 0; beat < simrv::memory::kTlBlockBytes / simrv::memory::kTlBeatBytes;
         ++beat) {
        in.c.data = 0x0101010101010101ULL * (beat + 1);
        auto out = bus.tilelink_cycle(in);
        TEST_CHECK(out.c_ready && !out.d_valid);
        TEST_CHECK(bus.protocol_checker().outstanding_releases() == 1);
    }
    for (size_t i = 0; i < simrv::memory::kTlBlockBytes; ++i) {
        TEST_CHECK(ram[i] == simrv::xlen::Byte{static_cast<unsigned char>(i / 8 + 1)});
    }
    in.c_valid = false;
    auto out = bus.tilelink_cycle(in);
    TEST_CHECK(out.d_valid && out.d.opcode == simrv::memory::TlOpcodeD::ReleaseAck &&
               out.d.source == 11);
    TEST_CHECK(bus.protocol_checker().outstanding_releases() == 1);
    const auto held = out.d;
    out = bus.tilelink_cycle(in);
    TEST_CHECK(out.d_valid && out.d == held);
    in.d_ready = true;
    TEST_CHECK(bus.tilelink_cycle(in).d_valid);
    TEST_CHECK(bus.protocol_checker().outstanding_releases() == 0);
}

void test_tilelink_cycle_probe_handshake() {
    simrv::core::Machine machine;
    std::array<simrv::xlen::Byte, 4096> ram{};
    machine.set_ram_for_testing(ram.data(), ram.size());
    auto secondary = std::make_unique<simrv::core::CPU>();
    secondary->machine_ = &machine;
    machine.test_secondary_harts().push_back(std::move(secondary));
    auto& bus = machine.memory().system_bus();
    const auto base = machine.memory_geometry().dram_base;
    bus.coherence_hub().register_preloaded_line(base, simrv::xlen::HartId{0});
    bus.coherence_hub().register_preloaded_line(base, simrv::xlen::HartId{1});
    simrv::memory::TlCycleInput in{};
    in.a_valid = true;
    in.a.opcode = simrv::memory::TlOpcodeA::AcquirePerm;
    in.a.grow = simrv::memory::TlGrow::BtoT;
    in.a.size = simrv::memory::kTlBlockSize;
    in.a.mask = 0xff;
    in.a.source = 19;
    in.a.hart = simrv::xlen::HartId{1};
    in.a.address = base;
    TEST_CHECK(bus.tilelink_cycle(in).a_ready);
    in.a_valid = false;
    auto out = bus.tilelink_cycle(in);
    TEST_CHECK(out.b_valid && !out.d_valid && out.b.target == simrv::xlen::HartId{0});
    const auto held_probe = out.b;
    out = bus.tilelink_cycle(in);
    TEST_CHECK(out.b_valid && out.b == held_probe && !out.d_valid);
    in.b_ready = true;
    TEST_CHECK(bus.tilelink_cycle(in).b_valid);
    TEST_CHECK(bus.protocol_checker().outstanding_probes() == 1);
    in.b_ready = false;
    in.c_valid = true;
    in.c.opcode = simrv::memory::TlOpcodeC::ProbeAck;
    in.c.report = simrv::memory::TlReport::BtoN;
    in.c.source = held_probe.source;
    in.c.address = held_probe.address;
    in.c.hart = simrv::xlen::HartId{1};
    const auto wrong_ack = bus.tilelink_cycle(in);
    TEST_CHECK(!wrong_ack.c_ready && !wrong_ack.d_valid);
    TEST_CHECK(bus.protocol_checker().outstanding_probes() == 1);
    in.c.hart = held_probe.target;
    TEST_CHECK(bus.tilelink_cycle(in).c_ready);
    TEST_CHECK(bus.protocol_checker().outstanding_probes() == 0);
    in.c_valid = false;
    out = bus.tilelink_cycle(in);
    TEST_CHECK(out.d_valid && out.d.opcode == simrv::memory::TlOpcodeD::Grant &&
               out.d.source == 19);
    const auto sink = out.d.sink;
    in.d_ready = true;
    TEST_CHECK(bus.tilelink_cycle(in).d_valid);
    in.d_ready = false;
    in.e_valid = true;
    in.e.sink = sink;
    TEST_CHECK(bus.tilelink_cycle(in).e_ready);
    const auto dir = bus.coherence_hub().get_directory_state(base);
    TEST_CHECK(dir.owner_hart == simrv::xlen::HartId{1});
}

void test_tilelink_cycle_dirty_probe() {
    simrv::core::Machine machine;
    std::array<simrv::xlen::Byte, 4096> ram{};
    machine.set_ram_for_testing(ram.data(), ram.size());
    auto secondary = std::make_unique<simrv::core::CPU>();
    secondary->machine_ = &machine;
    machine.test_secondary_harts().push_back(std::move(secondary));
    auto& bus = machine.memory().system_bus();
    const auto base = machine.memory_geometry().dram_base;
    simrv::memory::TlChannelA owner{};
    owner.opcode = simrv::memory::TlOpcodeA::AcquireBlock;
    owner.grow = simrv::memory::TlGrow::NtoT;
    owner.size = simrv::memory::kTlBlockSize;
    owner.mask = 0xff;
    owner.source = 30;
    owner.address = base;
    simrv::memory::TlChannelD grant{};
    std::array<simrv::xlen::Byte, simrv::memory::kTlBlockBytes> line{};
    TEST_CHECK(bus.acquire_block(owner, grant, line));
    bus.grant_ack(simrv::memory::TlChannelE{.sink = grant.sink});
    bus.mark_modified(base, simrv::xlen::HartId{0});

    simrv::memory::TlCycleInput in{};
    in.a_valid = true;
    in.a = owner;
    in.a.grow = simrv::memory::TlGrow::NtoB;
    in.a.source = 31;
    in.a.hart = simrv::xlen::HartId{1};
    TEST_CHECK(bus.tilelink_cycle(in).a_ready);
    in.a_valid = false;
    in.b_ready = true;
    const auto probe = bus.tilelink_cycle(in);
    TEST_CHECK(probe.b_valid && probe.b.cap == simrv::memory::TlCap::ToB);
    in.b_ready = false;
    in.c_valid = true;
    in.c.opcode = simrv::memory::TlOpcodeC::ProbeAckData;
    in.c.report = simrv::memory::TlReport::TtoB;
    in.c.source = probe.b.source;
    in.c.hart = probe.b.target;
    in.c.address = probe.b.address;
    for (uint64_t i = 0; i < simrv::memory::kTlBlockBytes / simrv::memory::kTlBeatBytes; ++i) {
        in.c.data = 0x0101010101010101ULL * (i + 1);
        const auto out = bus.tilelink_cycle(in);
        TEST_CHECK(out.c_ready && !out.d_valid);
    }
    in.c_valid = false;
    in.d_ready = true;
    simrv::memory::TlSinkId sink = 0;
    for (uint64_t i = 0; i < simrv::memory::kTlBlockBytes / simrv::memory::kTlBeatBytes; ++i) {
        const auto out = bus.tilelink_cycle(in);
        TEST_CHECK(out.d_valid && out.d.opcode == simrv::memory::TlOpcodeD::GrantData &&
                   out.d.data == 0x0101010101010101ULL * (i + 1));
        sink = out.d.sink;
    }
    in.d_ready = false;
    in.e_valid = true;
    in.e.sink = sink;
    TEST_CHECK(bus.tilelink_cycle(in).e_ready);
    TEST_CHECK(bus.coherence_hub().get_directory_state(base).state ==
               simrv::memory::MesiState::Shared);

    const auto corrupt_base = base + simrv::memory::kTlBlockBytes;
    owner.address = corrupt_base;
    owner.source = 32;
    TEST_CHECK(bus.acquire_block(owner, grant, line));
    bus.grant_ack(simrv::memory::TlChannelE{.sink = grant.sink});
    bus.mark_modified(corrupt_base, simrv::xlen::HartId{0});
    in = {};
    in.a_valid = true;
    in.a = owner;
    in.a.grow = simrv::memory::TlGrow::NtoB;
    in.a.source = 33;
    in.a.hart = simrv::xlen::HartId{1};
    TEST_CHECK(bus.tilelink_cycle(in).a_ready);
    in.a_valid = false;
    in.b_ready = true;
    const auto corrupt_probe = bus.tilelink_cycle(in);
    TEST_CHECK(corrupt_probe.b_valid);
    in.b_ready = false;
    in.c_valid = true;
    in.c.opcode = simrv::memory::TlOpcodeC::ProbeAckData;
    in.c.report = simrv::memory::TlReport::TtoB;
    in.c.source = corrupt_probe.b.source;
    in.c.hart = corrupt_probe.b.target;
    in.c.address = corrupt_probe.b.address;
    for (uint64_t i = 0; i < simrv::memory::kTlBlockBytes / simrv::memory::kTlBeatBytes; ++i) {
        in.c.data = 0xaaaaaaaaaaaaaaaaULL;
        in.c.corrupt = (i == 0);
        TEST_CHECK(bus.tilelink_cycle(in).c_ready);
    }
    in.c_valid = false;
    in.c.corrupt = false;
    in.d_ready = true;
    for (uint64_t i = 0; i < simrv::memory::kTlBlockBytes / simrv::memory::kTlBeatBytes; ++i) {
        const auto denied = bus.tilelink_cycle(in);
        TEST_CHECK(denied.d_valid && denied.d.opcode == simrv::memory::TlOpcodeD::GrantData &&
                   denied.d.denied && denied.d.corrupt);
    }
    TEST_CHECK(ram[simrv::memory::kTlBlockBytes] == simrv::xlen::Byte{0});
}

void test_tilelink_cycle_multibeat_memory() {
    simrv::core::Machine machine;
    std::array<simrv::xlen::Byte, 4096> ram{};
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.memory().configure_bus(simrv::memory::BusProtocol::TileLinkUH);
    auto& bus = machine.memory().system_bus();
    simrv::memory::TlCycleInput in{};
    in.a_valid = true;
    in.a.opcode = simrv::memory::TlOpcodeA::PutFullData;
    in.a.address = machine.memory_geometry().dram_base;
    in.a.source = 3;
    in.a.size = 4;
    in.a.mask = 0xff;
    in.a.data = 0x0101010101010101ULL;
    TEST_CHECK(bus.tilelink_cycle(in).a_ready);
    TEST_CHECK(bus.protocol_checker().outstanding_sources() == 1);
    in.a.data = 0x0202020202020202ULL;
    TEST_CHECK(bus.tilelink_cycle(in).a_ready);
    in.a_valid = false;
    auto out = bus.tilelink_cycle(in);
    TEST_CHECK(out.d_valid && out.d.opcode == simrv::memory::TlOpcodeD::AccessAck);
    in.d_ready = true;
    TEST_CHECK(bus.tilelink_cycle(in).d_valid);
    TEST_CHECK(bus.protocol_checker().outstanding_sources() == 0);
    in = {};
    in.a_valid = true;
    in.a.opcode = simrv::memory::TlOpcodeA::Get;
    in.a.address = machine.memory_geometry().dram_base;
    in.a.source = 4;
    in.a.size = 4;
    in.a.mask = 0xff;
    TEST_CHECK(bus.tilelink_cycle(in).a_ready);
    in.a_valid = false;
    out = bus.tilelink_cycle(in);
    TEST_CHECK(out.d_valid && out.d.data == 0x0101010101010101ULL);
    TEST_CHECK(bus.tilelink_cycle(in).d == out.d);
    in.d_ready = true;
    TEST_CHECK(bus.tilelink_cycle(in).d.data == 0x0101010101010101ULL);
    TEST_CHECK(bus.protocol_checker().outstanding_sources() == 1);
    out = bus.tilelink_cycle(in);
    TEST_CHECK(out.d_valid && out.d.data == 0x0202020202020202ULL);
    TEST_CHECK(bus.protocol_checker().outstanding_sources() == 0);

    in = {};
    in.a_valid = true;
    in.a.opcode = simrv::memory::TlOpcodeA::PutFullData;
    in.a.address = machine.memory_geometry().dram_base + 64;
    in.a.source = 5;
    in.a.size = 4;
    in.a.mask = 0xff;
    in.a.data = 0xffffffffffffffffULL;
    in.a.corrupt = true;
    TEST_CHECK(bus.tilelink_cycle(in).a_ready);
    in.a.corrupt = false;
    in.a.data = 0xeeeeeeeeeeeeeeeeULL;
    TEST_CHECK(bus.tilelink_cycle(in).a_ready);
    in.a_valid = false;
    out = bus.tilelink_cycle(in);
    TEST_CHECK(out.d_valid && out.d.opcode == simrv::memory::TlOpcodeD::AccessAck && out.d.denied);
    TEST_CHECK(ram[64] == simrv::xlen::Byte{0});
}

void test_tilelink_atomic_operations() {
    simrv::core::Machine machine;
    std::array<simrv::xlen::Byte, 4096> ram{};
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.memory().configure_bus(simrv::memory::BusProtocol::TileLinkUH);
    auto& bus = machine.memory().system_bus();
    auto run = [&](simrv::memory::TlChannelA request, uint8_t old, uint8_t operand,
                   uint8_t expected) {
        ram[0] = simrv::xlen::Byte{old};
        request.address = machine.memory_geometry().dram_base;
        request.source = 9;
        request.size = 0;
        request.data = operand;
        TEST_CHECK(bus.send_request(request));
        simrv::memory::TlChannelD response{};
        TEST_CHECK(bus.get_response(request.source, response));
        TEST_CHECK(!response.failed() && response.data == old);
        TEST_CHECK(ram[0] == simrv::xlen::Byte{expected});
    };
    simrv::memory::TlChannelA request{};
    request.opcode = simrv::memory::TlOpcodeA::LogicalData;
    request.logical = simrv::memory::TlLogical::Xor;
    run(request, 0x5a, 0x0f, 0x55);
    request.logical = simrv::memory::TlLogical::Or;
    run(request, 0x50, 0x0f, 0x5f);
    request.logical = simrv::memory::TlLogical::And;
    run(request, 0x5a, 0x0f, 0x0a);
    request.logical = simrv::memory::TlLogical::Swap;
    run(request, 0x5a, 0x0f, 0x0f);
    request.opcode = simrv::memory::TlOpcodeA::ArithmeticData;
    request.arithmetic = simrv::memory::TlArithmetic::Min;
    run(request, 0x80, 0x7f, 0x80);
    request.arithmetic = simrv::memory::TlArithmetic::Max;
    run(request, 0x80, 0x7f, 0x7f);
    request.arithmetic = simrv::memory::TlArithmetic::MinU;
    run(request, 0x80, 0x7f, 0x7f);
    request.arithmetic = simrv::memory::TlArithmetic::MaxU;
    run(request, 0x80, 0x7f, 0x80);
    request.arithmetic = simrv::memory::TlArithmetic::Add;
    run(request, 0xff, 0x02, 0x01);
}

void test_tilelink_empty_partial_write() {
    simrv::core::Machine machine;
    std::array<simrv::xlen::Byte, 4096> ram{};
    ram[0] = simrv::xlen::Byte{0x5a};
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.memory().configure_bus(simrv::memory::BusProtocol::TileLinkUH);
    auto& bus = machine.memory().system_bus();
    simrv::memory::TlChannelA request{};
    request.opcode = simrv::memory::TlOpcodeA::PutPartialData;
    request.address = machine.memory_geometry().dram_base;
    request.size = 2;
    request.source = 10;
    request.mask = 0;
    request.data = 0xffffffff;
    TEST_CHECK(bus.send_request(request));
    simrv::memory::TlChannelD response{};
    TEST_CHECK(bus.get_response(request.source, response));
    TEST_CHECK(!response.failed() && ram[0] == simrv::xlen::Byte{0x5a});
}

void test_axi_mmio_byte_enables() {
    class ByteDevice final : public simrv::memory::MmioDevice {
       public:
        auto name() const -> const char* override { return "byte-test"; }
        auto base_address() const -> simrv::xlen::PhysAddr override {
            return simrv::xlen::PhysAddr{0x10000000};
        }
        auto size() const -> simrv::xlen::Address override { return 16; }
        auto read8(simrv::memory::MmioOffset offset) -> uint8_t override {
            return bytes[offset.raw()];
        }
        void write8(simrv::memory::MmioOffset offset, uint8_t value) override {
            bytes[offset.raw()] = value;
        }
        std::array<uint8_t, 16> bytes{};
    } device;
    simrv::core::Machine machine;
    machine.memory().configure_bus(simrv::memory::BusProtocol::Axi4);
    auto& bus = machine.memory().system_bus();
    TEST_CHECK(bus.add_node(&device));
    const std::array<simrv::xlen::Word, 1> data{0x44332211};
    TEST_CHECK(bus.axi_write(device.base_address().raw(), data, 4, 0x5) ==
               simrv::memory::AxiResp::Okay);
    TEST_CHECK(device.bytes[0] == 0x11 && device.bytes[1] == 0 && device.bytes[2] == 0x33 &&
               device.bytes[3] == 0);
    simrv::memory::Axi4Ar read{};
    read.addr = device.base_address().raw() + 1;
    read.size = 2;
    TEST_CHECK(bus.send_ar(read));
    simrv::memory::Axi4R response{};
    TEST_CHECK(bus.recv_r(response));
    TEST_CHECK(response.resp == simrv::memory::AxiResp::Okay && response.data == 0x330000);

    read.addr = device.base_address().raw();
    read.lock = 1;
    TEST_CHECK(bus.send_ar(read));
    TEST_CHECK(bus.recv_r(response));
    TEST_CHECK(response.resp == simrv::memory::AxiResp::Okay);
    simrv::memory::Axi4Aw exclusive_aw{};
    exclusive_aw.addr = device.base_address().raw();
    exclusive_aw.size = 2;
    exclusive_aw.lock = 1;
    simrv::memory::Axi4W exclusive_w{};
    exclusive_w.data = 0xaabbccdd;
    exclusive_w.strb = 0x5;
    TEST_CHECK(bus.send_aw(exclusive_aw) && bus.send_w(exclusive_w));
    simrv::memory::Axi4B exclusive_b{};
    TEST_CHECK(bus.recv_b(exclusive_b));
    TEST_CHECK(exclusive_b.resp == simrv::memory::AxiResp::Okay);
    TEST_CHECK(device.bytes[0] == 0xdd && device.bytes[1] == 0 && device.bytes[2] == 0xbb &&
               device.bytes[3] == 0);
}

void test_axi_single_beat_exclusives() {
    simrv::core::Machine machine;
    std::array<simrv::xlen::Byte, 4096> ram{};
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.memory().configure_bus(simrv::memory::BusProtocol::Axi4);
    auto& bus = machine.memory().system_bus();
    const auto base = machine.memory_geometry().dram_base;
    simrv::memory::Axi4Ar ar{};
    ar.id = 3;
    ar.addr = base.raw();
    ar.size = 2;
    ar.lock = 1;
    simrv::memory::Axi4Aw aw{};
    aw.id = ar.id;
    aw.addr = ar.addr;
    aw.size = ar.size;
    aw.lock = 1;
    simrv::memory::Axi4W w{};
    w.data = 0x12345678;
    w.strb = 0xf;
    simrv::memory::Axi4R r{};
    simrv::memory::Axi4B b{};
    TEST_CHECK(bus.send_ar(ar) && bus.recv_r(r) && r.resp == simrv::memory::AxiResp::Exokay);
    TEST_CHECK(bus.send_aw(aw) && bus.send_w(w) && bus.recv_b(b));
    TEST_CHECK(b.resp == simrv::memory::AxiResp::Exokay);
    TEST_CHECK(ram[0] == simrv::xlen::Byte{0x78});

    TEST_CHECK(bus.send_ar(ar) && bus.recv_r(r) && r.resp == simrv::memory::AxiResp::Exokay);
    auto& cpu = machine.hart(simrv::xlen::HartId{0});
    simrv::memory::MemoryAccess::storeInt(machine.memory(), cpu, simrv::xlen::VirtAddr{base.raw()},
                                          0x12345678, simrv::isa::Funct3::Sw);
    w.data = 0xffffffffU;
    TEST_CHECK(bus.send_aw(aw) && bus.send_w(w) && bus.recv_b(b));
    TEST_CHECK(b.resp == simrv::memory::AxiResp::Okay);
    TEST_CHECK(ram[0] == simrv::xlen::Byte{0x78});

    TEST_CHECK(bus.send_ar(ar) && bus.recv_r(r) && r.resp == simrv::memory::AxiResp::Exokay);
    aw.prot = 1;
    TEST_CHECK(bus.send_aw(aw) && bus.send_w(w) && bus.recv_b(b));
    TEST_CHECK(b.resp == simrv::memory::AxiResp::Okay);
    TEST_CHECK(ram[0] == simrv::xlen::Byte{0x78});

    aw.prot = 0;
    TEST_CHECK(bus.send_ar(ar) && bus.recv_r(r) && r.resp == simrv::memory::AxiResp::Exokay);
    const uint32_t dma_value = 0x12345678;
    TEST_CHECK(machine.memory().dma_write(base, &dma_value, sizeof(dma_value)));
    TEST_CHECK(bus.send_aw(aw) && bus.send_w(w) && bus.recv_b(b));
    TEST_CHECK(b.resp == simrv::memory::AxiResp::Okay);
    TEST_CHECK(ram[0] == simrv::xlen::Byte{0x78});
}

void test_axi_multibeat_exclusives() {
    simrv::core::Machine machine;
    std::array<simrv::xlen::Byte, 4096> ram{};
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.memory().configure_bus(simrv::memory::BusProtocol::Axi4);
    auto& bus = machine.memory().system_bus();
    const auto base = machine.memory_geometry().dram_base;
    simrv::memory::Axi4Ar ar{};
    ar.id = 2;
    ar.addr = base.raw();
    ar.size = 2;
    ar.len = 1;
    ar.lock = 1;
    simrv::memory::Axi4Aw aw{};
    aw.id = ar.id;
    aw.addr = ar.addr;
    aw.size = ar.size;
    aw.len = ar.len;
    aw.lock = 1;
    simrv::memory::Axi4R read{};
    simrv::memory::Axi4B response{};
    TEST_CHECK(bus.send_ar(ar));
    TEST_CHECK(bus.recv_r(read) && read.resp == simrv::memory::AxiResp::Exokay && !read.last);
    TEST_CHECK(bus.recv_r(read) && read.resp == simrv::memory::AxiResp::Exokay && read.last);
    TEST_CHECK(bus.send_aw(aw));
    simrv::memory::Axi4W beat{};
    beat.data = 0x11223344;
    beat.strb = 0xf;
    beat.last = false;
    TEST_CHECK(bus.send_w(beat));
    TEST_CHECK(ram[0] == simrv::xlen::Byte{0} && !bus.recv_b(response));
    const unsigned lane = 4 % sizeof(simrv::xlen::Word);
    beat.data = simrv::xlen::Word{0x55667788} << (lane * 8);
    beat.strb = 0xfU << lane;
    beat.last = true;
    TEST_CHECK(bus.send_w(beat) && bus.recv_b(response));
    TEST_CHECK(response.resp == simrv::memory::AxiResp::Exokay);
    TEST_CHECK(ram[0] == simrv::xlen::Byte{0x44} && ram[4] == simrv::xlen::Byte{0x88});

    TEST_CHECK(bus.send_ar(ar));
    TEST_CHECK(bus.recv_r(read) && read.resp == simrv::memory::AxiResp::Exokay);
    const uint32_t same_value = 0x55667788;
    TEST_CHECK(machine.memory().dma_write(base + 4, &same_value, sizeof(same_value)));
    TEST_CHECK(bus.recv_r(read) && read.resp == simrv::memory::AxiResp::Exokay);
    TEST_CHECK(bus.send_aw(aw));
    beat.data = 0xffffffffU;
    beat.strb = 0xf;
    beat.last = false;
    TEST_CHECK(bus.send_w(beat));
    beat.data = simrv::xlen::Word{0xffffffffU} << (lane * 8);
    beat.strb = 0xfU << lane;
    beat.last = true;
    TEST_CHECK(bus.send_w(beat) && bus.recv_b(response));
    TEST_CHECK(response.resp == simrv::memory::AxiResp::Okay);
    TEST_CHECK(ram[0] == simrv::xlen::Byte{0x44} && ram[4] == simrv::xlen::Byte{0x88});
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

void test_axi4_lite_unified_contention_and_trace() {
    std::cout << "[Test] AXI4-Lite (Unified) contention telemetry & AXI trace...\n";
    simrv::core::Machine machine;
    std::vector<simrv::xlen::Byte> ram(4096, simrv::xlen::Byte{0x42});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.memory().initialize_mmu();
    machine.memory().configure_bus(simrv::memory::BusProtocol::Axi4LiteUnified);

    auto& bus = machine.memory().system_bus();
    TEST_CHECK(bus.protocol() == simrv::memory::BusProtocol::Axi4LiteUnified);
    bus.configure_timing(/*request_latency=*/2, /*response_latency=*/2,
                         /*data_request_latency=*/2, /*data_response_latency=*/2,
                         /*startup_data_response_latency=*/0);
    auto* axi_bus =
        dynamic_cast<simrv::memory::TemplatedBus<simrv::memory::kAxi4LiteUnifiedSpec>*>(&bus);
    TEST_CHECK(axi_bus != nullptr);
    const std::string trace_path = "/tmp/simrv_axi4_lite_unified_trace_test.txt";
    axi_bus->axi_tracer().set_path(trace_path);
    axi_bus->axi_tracer().enable(true);

    const auto base = simrv::memory::kDramBaseAddress;

    // Submit an instruction fetch (source 1) and an overlapping data read (source 0) in the
    // same cycle so both become ready at cycle 2, forcing the data read to wait 1 extra cycle.
    simrv::memory::TlChannelA ifetch_req{};
    ifetch_req.opcode = simrv::memory::TlOpcodeA::Get;
    ifetch_req.size = 2;
    ifetch_req.source = 1;  // TlPort::Instruction
    ifetch_req.address = base;
    ifetch_req.mask = simrv::memory::TlChannelA::compute_mask(ifetch_req.size, ifetch_req.address);
    TEST_CHECK(bus.send_request(ifetch_req));

    simrv::memory::TlChannelA data_req{};
    data_req.opcode = simrv::memory::TlOpcodeA::Get;
    data_req.size = 2;
    data_req.source = 0;  // TlPort::Data
    data_req.address = base + 4;
    data_req.mask = simrv::memory::TlChannelA::compute_mask(data_req.size, data_req.address);
    TEST_CHECK(bus.send_request(data_req));

    for (int i = 0; i < 6; ++i) {
        bus.advance_cycle();
    }

    simrv::memory::Bus::TimedResponse resp{};
    TEST_CHECK(bus.try_get_timed_response(ifetch_req.source, resp));
    TEST_CHECK(bus.try_get_timed_response(data_req.source, resp));

    const auto stats = bus.contention_stats();
    TEST_CHECK(stats.queue_contended_requests == 1);
    TEST_CHECK(stats.data_contended_requests == 1);
    TEST_CHECK(stats.ifetch_contended_requests == 0);
    TEST_CHECK(stats.queue_wait_cycles >= 1);

    axi_bus->axi_tracer().enable(false);
    std::ifstream trace_in(trace_path);
    TEST_CHECK(trace_in.is_open());
    std::string trace_contents((std::istreambuf_iterator<char>(trace_in)),
                               std::istreambuf_iterator<char>());
    TEST_CHECK(trace_contents.find("AR") != std::string::npos);
    TEST_CHECK(trace_contents.find("R") != std::string::npos);
}

}  // namespace

int main() {
    test_catalog_lookup();
    test_probe_tracking();
    test_external_reservations();
    test_external_reservation_invalidation();
    test_tilelink_variants();
    test_axi4_and_axi4_lite();
    test_custom_axi_spec();
    test_axi_cycle_handshake();
    test_tilelink_cycle_handshake();
    test_tilelink_cycle_release();
    test_tilelink_cycle_probe_handshake();
    test_tilelink_cycle_dirty_probe();
    test_tilelink_cycle_multibeat_memory();
    test_tilelink_atomic_operations();
    test_tilelink_empty_partial_write();
    test_axi_mmio_byte_enables();
    test_axi_single_beat_exclusives();
    test_axi_multibeat_exclusives();
    test_config_and_cli_bus_selection();
    test_axi4_lite_unified_contention_and_trace();
    std::cout << "All BusProtocol tests passed!\n";
    return 0;
}
