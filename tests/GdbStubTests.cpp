/**
 * @file GdbStubTests.cpp
 * @brief Unit tests for the GDB Remote Serial Protocol (RSP) stub.
 *
 * Each test starts the event-driven server on an ephemeral loopback port and drives machine
 * requests from a wake/notify service thread.
 */
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <format>
#include <print>
#include <span>
#include <string>
#include <thread>

#include "simrv/core/Machine.hpp"
#include "simrv/debug/GdbStub.hpp"

// ---------------------------------------------------------------------------
// Minimal test scaffolding
// ---------------------------------------------------------------------------

static int g_failed = 0;
static int g_passed = 0;

#define EXPECT_TRUE(cond)                                                       \
    do {                                                                        \
        if (!(cond)) {                                                          \
            std::println(stderr, "FAIL [{}:{}] {}", __FILE__, __LINE__, #cond); \
            ++g_failed;                                                         \
        } else {                                                                \
            ++g_passed;                                                         \
        }                                                                       \
    } while (false)

#define EXPECT_EQ(a, b)                                                                \
    do {                                                                               \
        if ((a) != (b)) {                                                              \
            std::println(stderr, "FAIL [{}:{}] {} != {}", __FILE__, __LINE__, #a, #b); \
            ++g_failed;                                                                \
        } else {                                                                       \
            ++g_passed;                                                                \
        }                                                                              \
    } while (false)

// ---------------------------------------------------------------------------
// RspClient — loopback TCP client for RSP protocol
// ---------------------------------------------------------------------------

class RspClient {
   public:
    explicit RspClient(uint16_t port) {
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        EXPECT_EQ(::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)),
                  0);  // NOLINT
        int nd = 1;
        ::setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &nd, sizeof(nd));
    }

    ~RspClient() {
        if (fd_ >= 0) ::close(fd_);
    }

    void send_raw(const std::string& s) { ::write(fd_, s.data(), s.size()); }

    void send_packet_fragmented(const std::string& data) {
        uint8_t cs = 0;
        for (char c : data) cs += static_cast<uint8_t>(c);
        const auto framed = std::format("${}#{:02x}", data, cs);
        for (const char byte : framed) {
            EXPECT_EQ(::write(fd_, &byte, 1), 1);
        }
    }

    void send_packet(const std::string& data) {
        uint8_t cs = 0;
        for (char c : data) cs += static_cast<uint8_t>(c);
        send_raw(std::format("${}#{:02x}", data, cs));
    }

    // Read all available data with a short timeout
    auto recv_raw(int timeout_ms = 500) -> std::string {
        struct timeval tv{};
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        std::string result;
        char buf[4096];
        while (true) {
            const ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
            if (n <= 0) break;
            result.append(buf, static_cast<size_t>(n));
        }
        return result;
    }

    // Strip '+' acks and extract the first RSP payload from raw bytes
    static auto extract_payload(const std::string& raw) -> std::string {
        const auto start = raw.find('$');
        const auto hash = raw.rfind('#');
        if (start == std::string::npos || hash == std::string::npos || hash <= start) return "";
        return raw.substr(start + 1, hash - start - 1);
    }

    auto recv_response(int timeout_ms = 500) -> std::string {
        struct timeval tv{};
        tv.tv_sec = timeout_ms / 1000;
        tv.tv_usec = (timeout_ms % 1000) * 1000;
        ::setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        std::string raw;
        bool framed = false;
        bool saw_hash = false;
        size_t checksum_bytes = 0;
        while (checksum_bytes < 2) {
            char byte = 0;
            if (::recv(fd_, &byte, 1, 0) != 1) return {};
            raw += byte;
            if (!framed) {
                framed = byte == '$';
                continue;
            }
            if (!saw_hash && byte == '#') {
                saw_hash = true;
            } else if (saw_hash) {
                ++checksum_bytes;
            }
        }
        return extract_payload(raw);
    }

    // Send a packet and return the response payload (strips framing and acks)
    auto transact(const std::string& data, int timeout_ms = 500) -> std::string {
        send_packet(data);
        return recv_response(timeout_ms);
    }

   private:
    int fd_ = -1;
};

// ---------------------------------------------------------------------------
// StubHarness — manages stub lifecycle + poll loop for a single test
// ---------------------------------------------------------------------------

struct StubHarness {
    simrv::core::Machine machine;
    simrv::debug::GdbStub stub;
    std::atomic<uint64_t> generation{0};
    std::jthread service_thread;

    StubHarness() : stub(0) {}

    void start() {
        service_thread = std::jthread([this](const std::stop_token& stop_token) {
            while (!stop_token.stop_requested()) {
                stub.service_pending(machine);
                const auto observed = generation.load(std::memory_order_acquire);
                if (!stub.has_pending_commands() && !stop_token.stop_requested()) {
                    generation.wait(observed, std::memory_order_relaxed);
                }
            }
        });
        stub.start([this]() {
            generation.fetch_add(1, std::memory_order_release);
            generation.notify_all();
        });
    }

    [[nodiscard]] auto port() const -> uint16_t { return stub.bound_port(); }

    ~StubHarness() {
        stub.stop();
        service_thread.request_stop();
        generation.fetch_add(1, std::memory_order_release);
        generation.notify_all();
        if (service_thread.joinable()) service_thread.join();
    }
};

// ---------------------------------------------------------------------------
// Test: reg_to_hex / hex_to_reg round-trips (pure unit, no network)
// ---------------------------------------------------------------------------

static void test_reg_encoding() {
    using simrv::debug::GdbStub;

    const auto hex0 = GdbStub::reg_to_hex(0);
    EXPECT_EQ(hex0.size(), sizeof(Register) * 2);

    const auto hex1 = GdbStub::reg_to_hex(static_cast<Register>(0x12345678U));
    EXPECT_TRUE(!hex1.empty());
    EXPECT_EQ(GdbStub::hex_to_reg(hex1, 0), static_cast<Register>(0x12345678U));

    const Register all_ones = ~Register{0};
    EXPECT_EQ(GdbStub::hex_to_reg(GdbStub::reg_to_hex(all_ones), 0), all_ones);
}

// ---------------------------------------------------------------------------
// Test: checksum computation (pure unit, no network)
// ---------------------------------------------------------------------------

static void test_checksum() {
    using simrv::debug::GdbStub;
    EXPECT_EQ(GdbStub::checksum(""), uint8_t{0});
    EXPECT_EQ(GdbStub::checksum("g"), static_cast<uint8_t>('g'));
    uint8_t expected = 0;
    for (char c : std::string("qSupported")) expected += static_cast<uint8_t>(c);
    EXPECT_EQ(GdbStub::checksum("qSupported"), expected);
}

// ---------------------------------------------------------------------------
// Test: sw_breakpoint state is empty on construction (no network)
// ---------------------------------------------------------------------------

static void test_breakpoint_ownership() {
    simrv::debug::BreakpointManager manager;
    constexpr Address address = static_cast<Address>(0x80000000ULL);
    manager.add_pc_breakpoint(address, simrv::debug::BreakpointOwner::Tui);
    manager.add_pc_breakpoint(address, simrv::debug::BreakpointOwner::Gdb);
    EXPECT_EQ(manager.get_pc_breakpoint_records().size(), 2U);
    const auto first_id = manager.get_pc_breakpoint_records().front().id;
    manager.add_pc_breakpoint(address, simrv::debug::BreakpointOwner::Tui);
    EXPECT_EQ(manager.get_pc_breakpoint_records().front().id, first_id);
    manager.remove_pc_breakpoint(address, simrv::debug::BreakpointOwner::Gdb);
    EXPECT_TRUE(manager.has_pc_breakpoint(address));
    manager.clear_owner(simrv::debug::BreakpointOwner::Tui);
    EXPECT_TRUE(!manager.has_pc_breakpoint(address));
    manager.add_watchpoint(address, 4, simrv::debug::WatchType::Write, "tui");
    const auto tui_id = manager.get_watchpoints().front().id;
    manager.add_watchpoint(address, 4, simrv::debug::WatchType::Write, "gdb",
                           simrv::debug::BreakpointOwner::Gdb);
    manager.add_watchpoint(address, 4, simrv::debug::WatchType::Read, "gdb",
                           simrv::debug::BreakpointOwner::Gdb);
    manager.clear_owner(simrv::debug::BreakpointOwner::Gdb);
    EXPECT_EQ(manager.get_watchpoints().size(), 1U);
    EXPECT_EQ(manager.get_watchpoints().front().id, tui_id);
}

static void test_packet_framing_and_malformed_input() {
    StubHarness h;
    h.start();

    RspClient client(h.port());
    client.send_packet_fragmented("qAttached");
    EXPECT_EQ(client.recv_response(), std::string("1"));

    client.send_raw("$qAttached#00");
    EXPECT_TRUE(client.recv_raw().find('-') != std::string::npos);

    EXPECT_EQ(client.transact("Pnot-a-register"), std::string("E01"));
    for (const auto* packet :
         {"p1x", "P1=zz", "Pff=00", "G00", "mno,4", "M0,4:ff", "Z0,xyz,4", "z0,0,bad!", "Hg99",
          "Hg", "vCont;s:99", "qXfer:features:read:target.xml:bad!,1"}) {
        EXPECT_EQ(client.transact(packet), std::string("E01"));
    }
    EXPECT_EQ(client.transact("qAttached"), std::string("1"));
}

// ---------------------------------------------------------------------------
// Helper: negotiate no-ack mode and return the first post-ack payload
// ---------------------------------------------------------------------------
static auto negotiate_no_ack(RspClient& client) -> bool {
    const auto resp = client.transact("QStartNoAckMode");
    return resp == "OK";
}

// ---------------------------------------------------------------------------
// Test: QStartNoAckMode
// ---------------------------------------------------------------------------

static void test_noack_negotiation() {
    StubHarness h;
    h.start();

    RspClient client(h.port());
    client.send_raw("+");  // initial ack GDB sends
    EXPECT_TRUE(negotiate_no_ack(client));
}

// ---------------------------------------------------------------------------
// Test: qSupported advertises expected features
// ---------------------------------------------------------------------------

static void test_qsupported() {
    StubHarness h;
    h.start();

    RspClient client(h.port());
    client.send_raw("+");
    negotiate_no_ack(client);

    const auto resp = client.transact("qSupported:multiprocess+;swbreak+;hwbreak+");
    EXPECT_TRUE(resp.find("PacketSize") != std::string::npos);
    EXPECT_TRUE(resp.find("swbreak+") != std::string::npos);
    EXPECT_TRUE(resp.find("qXfer:features:read+") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Test: qXfer:features:read:target.xml
// ---------------------------------------------------------------------------

static void test_target_xml() {
    StubHarness h;
    h.start();

    RspClient client(h.port());
    client.send_raw("+");
    negotiate_no_ack(client);

    const auto resp = client.transact("qXfer:features:read:target.xml:0,1000");
    EXPECT_TRUE(!resp.empty());
    EXPECT_TRUE(resp[0] == 'm' || resp[0] == 'l');
    EXPECT_TRUE(resp.find("riscv:rv") != std::string::npos);
    EXPECT_TRUE(resp.find("zero") != std::string::npos);

    std::string xml;
    for (size_t offset = 0;;) {
        const auto chunk =
            client.transact(std::format("qXfer:features:read:target.xml:{:x},20", offset));
        EXPECT_TRUE(!chunk.empty());
        if (chunk.empty()) break;
        xml += chunk.substr(1);
        offset += chunk.size() - 1;
        if (chunk.front() == 'l') break;
    }
    EXPECT_TRUE(xml.find("org.gnu.gdb.riscv.fpu") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Test: qfThreadInfo / qsThreadInfo
// ---------------------------------------------------------------------------

static void test_thread_info() {
    StubHarness h;
    h.start();

    RspClient client(h.port());
    client.send_raw("+");
    negotiate_no_ack(client);

    const auto resp = client.transact("qfThreadInfo");
    EXPECT_TRUE(resp.starts_with("m"));
    EXPECT_TRUE(resp.find('1') != std::string::npos);

    const auto resp2 = client.transact("qsThreadInfo");
    EXPECT_EQ(resp2, std::string("l"));
}

// ---------------------------------------------------------------------------
// Test: vCont? capability advertisement
// ---------------------------------------------------------------------------

static void test_vcont_query() {
    StubHarness h;
    h.start();

    RspClient client(h.port());
    client.send_raw("+");
    negotiate_no_ack(client);

    const auto resp = client.transact("vCont?");
    EXPECT_TRUE(resp.find('c') != std::string::npos);
    EXPECT_TRUE(resp.find('s') != std::string::npos);
}

// ---------------------------------------------------------------------------
// Test: H thread-select and T thread-alive
// ---------------------------------------------------------------------------

static void test_thread_commands() {
    StubHarness h;
    h.start();

    RspClient client(h.port());
    client.send_raw("+");
    negotiate_no_ack(client);

    EXPECT_EQ(client.transact("Hg1"), std::string("OK"));
    EXPECT_EQ(client.transact("Hc1"), std::string("OK"));
    EXPECT_EQ(client.transact("T1"), std::string("OK"));

    // Non-existent thread
    const auto dead = client.transact("T99");
    EXPECT_TRUE(dead != "OK");
}

// ---------------------------------------------------------------------------
// Test: 'g' register read returns correct-length hex string
// ---------------------------------------------------------------------------

static void test_register_read() {
    StubHarness h;
    // Allocate RAM so register reads don't crash
    EXPECT_TRUE(h.machine.allocate_ram_for_testing(4096));
    h.start();

    RspClient client(h.port());
    client.send_raw("+");
    negotiate_no_ack(client);

    const auto resp = client.transact("g");
    // RV64: 33 regs * 16 hex chars + 32 FP regs * 16 hex = 33*16 + 32*16 = 1040 chars
    // RV32: 33 regs * 8 hex chars + 32 FP regs * 16 hex = 264 + 512 = 776 chars
    const size_t expected_len = (sizeof(Register) == 8) ? (33 * 16 + 32 * 16) : (33 * 8 + 32 * 16);
    EXPECT_EQ(resp.size(), expected_len);
}

// ---------------------------------------------------------------------------
// Test: 'p' single register read
// ---------------------------------------------------------------------------

static void test_single_reg_read() {
    StubHarness h;
    EXPECT_TRUE(h.machine.allocate_ram_for_testing(4096));
    h.start();

    RspClient client(h.port());
    client.send_raw("+");
    negotiate_no_ack(client);

    // p0 = x0 (always zero)
    const auto resp = client.transact("p0");
    // Should be all-zero hex, length = sizeof(Register)*2
    EXPECT_EQ(resp.size(), sizeof(Register) * 2);
    for (char c : resp) EXPECT_TRUE(c == '0');
}

// ---------------------------------------------------------------------------
// Test: 'P' single register write responds OK
// ---------------------------------------------------------------------------

static void test_single_reg_write() {
    StubHarness h;
    EXPECT_TRUE(h.machine.allocate_ram_for_testing(4096));
    h.start();

    RspClient client(h.port());
    client.send_raw("+");
    negotiate_no_ack(client);

    // Write 0xdeadbeef to x1 (ra)
    const std::string val = simrv::debug::GdbStub::reg_to_hex(static_cast<Register>(0xdeadbeefU));
    const auto resp = client.transact(std::format("P1={}", val));
    EXPECT_EQ(resp, std::string("OK"));
    EXPECT_EQ(client.transact("P21=8877665544332211"), std::string("OK"));
    EXPECT_EQ(client.transact("p21"), std::string("8877665544332211"));
}

// ---------------------------------------------------------------------------
// Test: 'm' memory read on valid RAM
// ---------------------------------------------------------------------------

static void test_memory_read() {
    StubHarness h;
    EXPECT_TRUE(h.machine.allocate_ram_for_testing(4096));
    h.start();

    RspClient client(h.port());
    client.send_raw("+");
    negotiate_no_ack(client);

    // Read 4 bytes from base of DRAM
    const Address base = h.machine.memory_geometry().dram_base;
    const auto resp = client.transact(std::format("m{:x},4", base));
    // Should be 8 hex chars for 4 bytes
    EXPECT_EQ(resp.size(), 8u);

    EXPECT_EQ(client.transact(std::format("M{:x},4:78563412", base)), std::string("OK"));
    EXPECT_EQ(client.transact(std::format("m{:x},4", base)), std::string("78563412"));
}

static void test_logical_breakpoints_and_watchpoints() {
    StubHarness h;
    EXPECT_TRUE(h.machine.allocate_ram_for_testing(4096));
    h.start();

    RspClient client(h.port());
    client.send_raw("+");
    negotiate_no_ack(client);
    const Address base = h.machine.memory_geometry().dram_base;
    auto ram = h.machine.ram_view();
    *ram.unchecked_ptr(base) = static_cast<Byte>(0x13);

    EXPECT_EQ(client.transact(std::format("Z0,{:x},4", base)), std::string("OK"));
    EXPECT_TRUE(h.machine.breakpoint_manager().has_pc_breakpoint(base));
    EXPECT_EQ(static_cast<uint8_t>(*ram.unchecked_ptr(base)), uint8_t{0x13});
    EXPECT_EQ(client.transact(std::format("Z2,{:x},4", base + 8)), std::string("OK"));
    EXPECT_EQ(client.transact(std::format("Z3,{:x},4", base + 16)), std::string("OK"));
    EXPECT_EQ(client.transact(std::format("Z4,{:x},4", base + 24)), std::string("OK"));
    EXPECT_EQ(h.machine.breakpoint_manager().get_watchpoints().size(), 3U);

    EXPECT_EQ(client.transact(std::format("z0,{:x},4", base)), std::string("OK"));
    EXPECT_TRUE(!h.machine.breakpoint_manager().has_pc_breakpoint(base));
    EXPECT_EQ(client.transact(std::format("z2,{:x},4", base + 8)), std::string("OK"));
    EXPECT_EQ(h.machine.breakpoint_manager().get_watchpoints().size(), 2U);
}

static void test_detach_and_reconnect_cleanup() {
    StubHarness h;
    h.start();
    constexpr Address bp = static_cast<Address>(0x80000000ULL);

    {
        RspClient client(h.port());
        client.send_raw("+");
        negotiate_no_ack(client);
        EXPECT_EQ(client.transact(std::format("Z0,{:x},4", bp)), std::string("OK"));
        EXPECT_EQ(client.transact("D"), std::string("OK"));
    }
    EXPECT_TRUE(!h.machine.breakpoint_manager().has_pc_breakpoint(bp));
    EXPECT_EQ(h.machine.execution_state(), simrv::core::ExecutionState::Running);

    {
        RspClient client(h.port());
        client.send_raw("+");
        EXPECT_EQ(client.transact("qAttached"), std::string("1"));
    }
    {
        RspClient client(h.port());
        client.send_raw("+");
        EXPECT_EQ(client.transact("qAttached"), std::string("1"));
        EXPECT_EQ(h.machine.execution_state(), simrv::core::ExecutionState::Paused);
    }
}

static void test_shutdown_blocked_io() {
    {
        simrv::debug::GdbStub stub(0);
        stub.start([] {});
        stub.stop();
        EXPECT_EQ(stub.state(), simrv::debug::GdbConnectionState::Stopped);
    }
    {
        StubHarness h;
        h.start();
        RspClient client(h.port());
        client.send_raw("+");
        EXPECT_EQ(client.transact("qAttached"), std::string("1"));
        h.stub.stop();
        EXPECT_EQ(h.stub.state(), simrv::debug::GdbConnectionState::Stopped);
    }
}

static void test_modeled_step_and_watchpoint() {
    for (const auto engine :
         {simrv::core::ExecutionEngine::InstructionFast, simrv::core::ExecutionEngine::CycleFast}) {
        simrv::core::MachineConfig config;
        config.execution.num_harts = 2;
        config.debug.gdb_enabled = true;
        simrv::core::Machine machine(config);
        EXPECT_TRUE(machine.allocate_ram_for_testing(4096));
        machine.runtime_profile.engine = engine;
        auto& cpu = machine.primary_hart();
        cpu.reset();
        const auto base = machine.memory_geometry().dram_base;
        cpu.state().pc = base;
        cpu.hart_status.store(simrv::core::HartStatus::Started);
        // addi x1,x0,7; sw x1,0(x2); jal x0,0
        const Instruction program[]{0x00700093, 0x00112023, 0x0000006f};
        std::memcpy(machine.ram_view().unchecked_ptr(base), program, sizeof(program));
        cpu.state().regs.write(static_cast<RegId>(2), base + 128);
        auto secondary = std::make_unique<simrv::core::CPU>();
        secondary->reset();
        secondary->state().mhartid = 1;
        secondary->state().pc = base + 8;
        secondary->hart_status.store(simrv::core::HartStatus::Started);
        machine.add_hart_for_testing(std::move(secondary));
        machine.install_debugger_for_testing(std::make_unique<simrv::debug::GdbStub>(0));
        machine.pause();
        machine.debugger()->start([&machine] { machine.notify_control_event(); });
        std::jthread simulation([&machine] { machine.run(); });
        {
            RspClient client(machine.debugger()->bound_port());
            EXPECT_EQ(client.transact("qAttached"), std::string("1"));
            EXPECT_EQ(cpu.e_icount, 0U);
            EXPECT_TRUE(client.transact("s", 2000).starts_with("T05"));
            EXPECT_EQ(cpu.e_icount, 1U);
            EXPECT_EQ(cpu.state().regs.read(static_cast<RegId>(1)), Register{7});
            EXPECT_EQ(client.transact(std::format("Z2,{:x},4", base + 128)), std::string("OK"));
            client.send_packet("c");
            client.send_raw("+");
            EXPECT_TRUE(client.recv_response(2000).find("watch:") != std::string::npos);
            EXPECT_EQ(cpu.e_icount, 2U);
            EXPECT_EQ(static_cast<unsigned char>(*machine.ram_view().unchecked_ptr(base + 128)),
                      7U);
            const auto before = machine.hart(HartId{1}).e_icount;
            EXPECT_TRUE(client.transact("vCont;s:2;c", 2000).find("thread:2;") !=
                        std::string::npos);
            EXPECT_EQ(machine.hart(HartId{1}).e_icount, before + 1);
            client.send_packet("c");
            client.send_raw(std::string(1, '\x03'));
            EXPECT_TRUE(client.recv_response(2000).starts_with("T02"));
            const auto primary_stopped = cpu.e_icount;
            const auto secondary_stopped = machine.hart(HartId{1}).e_icount;
            EXPECT_EQ(client.transact("qAttached"), std::string("1"));
            EXPECT_EQ(cpu.e_icount, primary_stopped);
            EXPECT_EQ(machine.hart(HartId{1}).e_icount, secondary_stopped);
            client.send_packet("k");
        }
        simulation.join();
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main() {
    std::println("Running GdbStub unit tests...");

    // Pure unit tests (no network)
    test_reg_encoding();
    test_checksum();
    test_breakpoint_ownership();
    test_packet_framing_and_malformed_input();

    // Network / protocol tests
    test_noack_negotiation();
    test_qsupported();
    test_target_xml();
    test_thread_info();
    test_vcont_query();
    test_thread_commands();
    test_register_read();
    test_single_reg_read();
    test_single_reg_write();
    test_memory_read();
    test_logical_breakpoints_and_watchpoints();
    test_detach_and_reconnect_cleanup();
    test_shutdown_blocked_io();
    test_modeled_step_and_watchpoint();

    std::println("{} passed, {} failed", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
