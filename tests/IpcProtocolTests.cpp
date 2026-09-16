/**
 * @file IpcProtocolTests.cpp
 * @brief Unit and integration tests for SimRV IPC protocol, server, and client.
 */
#include <cassert>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <thread>
#include <vector>

#include "simrv/core/Machine.hpp"
#include "simrv/core/MachineConfig.hpp"
#include "simrv/net/Client.hpp"
#include "simrv/net/Protocol.hpp"
#include "simrv/net/Server.hpp"

namespace {

void test_frame_header_layout() {
    static_assert(sizeof(simrv::net::FrameHeader) == 8);
    simrv::net::FrameHeader hdr{
        .magic = simrv::net::kProtocolMagic,
        .channel = std::to_underlying(simrv::net::ChannelId::Telemetry),
        .flags = simrv::net::FrameFlags::Urgent,
        .payload_len = 1024,
    };

    uint8_t raw[sizeof(hdr)];
    std::memcpy(raw, &hdr, sizeof(hdr));

    uint16_t magic = 0;
    std::memcpy(&magic, raw, 2);
    assert(magic == simrv::net::kProtocolMagic);
    assert(raw[2] == std::to_underlying(simrv::net::ChannelId::Telemetry));
    assert(raw[3] == simrv::net::FrameFlags::Urgent);

    uint32_t len = 0;
    std::memcpy(&len, raw + 4, 4);
    assert(len == 1024);

    std::cout << "[PASS] test_frame_header_layout\n";
}

void test_telemetry_packet_layout() {
    simrv::net::TelemetryPacket pkt{};
    pkt.mcycle = 123456789ULL;
    pkt.mtime = 987654321ULL;
    pkt.pc = 0x80000000ULL;
    pkt.gpr[1] = 0xdeadbeef;
    pkt.sequence = 42;

    assert(pkt.mcycle == 123456789ULL);
    assert(pkt.sequence == 42);
    assert(sizeof(pkt) > 500);

    std::cout << "[PASS] test_telemetry_packet_layout (size=" << sizeof(pkt) << " bytes)\n";
}

void test_parse_endpoint() {
    auto ep1 = simrv::net::parse_endpoint("/tmp/my_socket.sock");
    assert(ep1.is_unix);
    assert(ep1.path == "/tmp/my_socket.sock");

    auto ep2 = simrv::net::parse_endpoint("127.0.0.1:9500");
    assert(!ep2.is_unix);
    assert(ep2.host == "127.0.0.1");
    assert(ep2.port == 9500);

    auto ep3 = simrv::net::parse_endpoint("8888");
    assert(!ep3.is_unix);
    assert(ep3.host == "127.0.0.1");
    assert(ep3.port == 8888);

    auto ep4 = simrv::net::parse_endpoint("");
    assert(ep4.is_unix);
    assert(ep4.path == "/tmp/simrv.sock");

    std::cout << "[PASS] test_parse_endpoint\n";
}

void test_client_server_socket_loopback() {
    const std::string sock_path = "/tmp/test_simrv_ipc_" + std::to_string(::getpid()) + ".sock";

    simrv::core::Machine machine(simrv::core::MachineConfig{
        .execution = {.appmode = true, .smp_quantum = 100, .smp_multithreaded = false},
    });
    std::vector<std::byte> ram(1024 * 1024, std::byte{0});
    machine.set_ram_for_testing(ram.data(), ram.size());
    machine.primary_hart().reset();

    simrv::net::SimRvServer server(machine, sock_path);
    assert(server.start());
    assert(server.is_running());

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    simrv::net::SimRvClient client(sock_path);
    assert(client.connect());
    assert(client.is_connected());

    std::atomic<bool> rpc_received{false};
    std::string received_rpc_response;
    client.set_rpc_callback([&](std::string_view resp) {
        received_rpc_response = std::string(resp);
        rpc_received.store(true, std::memory_order_release);
    });

    std::atomic<bool> telemetry_received{false};
    client.set_telemetry_callback([&](const simrv::net::TelemetryPacket& /*pkt*/) {
        telemetry_received.store(true, std::memory_order_release);
    });

    // Send RPC pause
    assert(client.send_pause());
    for (int i = 0; i < 50 && !rpc_received.load(std::memory_order_acquire); ++i) {
        machine.service_control_commands();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(rpc_received.load());
    assert(received_rpc_response.find("paused") != std::string::npos);

    // Verify telemetry frame received
    for (int i = 0; i < 50 && !telemetry_received.load(std::memory_order_acquire); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    assert(telemetry_received.load());

    // Disconnect client
    client.disconnect();
    assert(!client.is_connected());

    server.stop();
    assert(!server.is_running());

    std::filesystem::remove(sock_path);
    std::cout << "[PASS] test_client_server_socket_loopback\n";
}

}  // namespace

int main() {
    test_frame_header_layout();
    test_telemetry_packet_layout();
    test_parse_endpoint();
    test_client_server_socket_loopback();
    std::cout << "All IPC protocol tests passed successfully.\n";
    return 0;
}
