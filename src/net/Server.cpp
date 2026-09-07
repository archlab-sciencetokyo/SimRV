/**
 * @file Server.cpp
 * @brief SimRV IPC server daemon implementation.
 */
#include "simrv/net/Server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <format>
#include <string>
#include <utility>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Logger.hpp"
#include "simrv/device/Uart.hpp"

namespace simrv::net {

auto parse_endpoint(std::string_view ep) -> Endpoint {
    Endpoint result;
    if (ep.empty()) {
        result.is_unix = true;
        result.path = "/tmp/simrv.sock";
        return result;
    }
    if (ep.find('/') != std::string_view::npos) {
        result.is_unix = true;
        result.path = std::string(ep);
        return result;
    }
    const auto colon = ep.find(':');
    if (colon != std::string_view::npos) {
        result.is_unix = false;
        result.host = std::string(ep.substr(0, colon));
        if (result.host.empty()) result.host = "127.0.0.1";
        try {
            result.port = static_cast<uint16_t>(std::stoi(std::string(ep.substr(colon + 1))));
        } catch (...) {
            result.port = 9000;
        }
        return result;
    }
    if (!ep.empty() && std::ranges::all_of(ep, [](char c) { return c >= '0' && c <= '9'; })) {
        result.is_unix = false;
        result.host = "127.0.0.1";
        try {
            result.port = static_cast<uint16_t>(std::stoi(std::string(ep)));
        } catch (...) {
            result.port = 9000;
        }
        return result;
    }
    result.is_unix = true;
    result.path = std::string(ep);
    return result;
}

SimRvServer::SimRvServer(core::Machine& machine, std::string_view endpoint_str)
    : machine_(machine), endpoint_(parse_endpoint(endpoint_str)) {}

SimRvServer::~SimRvServer() { stop(); }

auto SimRvServer::start() -> bool {
    if (running_.load(std::memory_order_relaxed)) return true;

    if (endpoint_.is_unix) {
        listen_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            simrv::log::error("Failed to create Unix socket: {}", strerror(errno));
            return false;
        }
        ::unlink(endpoint_.path.c_str());
        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, endpoint_.path.c_str(), sizeof(addr.sun_path) - 1);
        if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            simrv::log::error("Failed to bind Unix socket '{}': {}", endpoint_.path,
                              strerror(errno));
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }
    } else {
        listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd_ < 0) {
            simrv::log::error("Failed to create TCP socket: {}", strerror(errno));
            return false;
        }
        int opt = 1;
        ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(endpoint_.port);
        ::inet_pton(AF_INET, endpoint_.host.c_str(), &addr.sin_addr);
        if (::bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            simrv::log::error("Failed to bind TCP socket {}:{}: {}", endpoint_.host, endpoint_.port,
                              strerror(errno));
            ::close(listen_fd_);
            listen_fd_ = -1;
            return false;
        }
    }

    if (::listen(listen_fd_, 8) < 0) {
        simrv::log::error("Failed to listen on socket: {}", strerror(errno));
        ::close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }

    running_.store(true, std::memory_order_release);
    accept_thread_ = std::thread([this] { accept_worker(); });
    telemetry_thread_ = std::thread([this] { telemetry_worker(); });

    simrv::log::info(
        "SimRV IPC server listening on {}",
        endpoint_.is_unix ? endpoint_.path : std::format("{}:{}", endpoint_.host, endpoint_.port));
    return true;
}

void SimRvServer::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) return;

    if (listen_fd_ >= 0) {
        ::shutdown(listen_fd_, SHUT_RDWR);
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    if (endpoint_.is_unix) {
        ::unlink(endpoint_.path.c_str());
    }

    {
        std::scoped_lock lock(clients_mutex_);
        for (int fd : client_fds_) {
            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);
        }
        client_fds_.clear();
    }

    if (accept_thread_.joinable()) accept_thread_.join();
    if (telemetry_thread_.joinable()) telemetry_thread_.join();

    for (auto& th : client_threads_) {
        if (th.joinable()) th.join();
    }
    client_threads_.clear();
}

void SimRvServer::accept_worker() {
    while (running_.load(std::memory_order_relaxed)) {
        struct pollfd pfd{};
        pfd.fd = listen_fd_;
        pfd.events = POLLIN;
        int rc = ::poll(&pfd, 1, 100);
        if (rc <= 0) continue;

        int client_fd = ::accept(listen_fd_, nullptr, nullptr);
        if (client_fd < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            break;
        }

        {
            std::scoped_lock lock(clients_mutex_);
            client_fds_.push_back(client_fd);
        }
        client_threads_.emplace_back([this, client_fd] { client_worker(client_fd); });
    }
}

void SimRvServer::client_worker(int client_fd) {
    while (running_.load(std::memory_order_relaxed)) {
        struct pollfd pfd{};
        pfd.fd = client_fd;
        pfd.events = POLLIN;
        int rc = ::poll(&pfd, 1, 100);
        if (rc < 0) break;
        if (rc == 0) continue;

        FrameHeader hdr{};
        ssize_t n = ::recv(client_fd, &hdr, sizeof(hdr), MSG_WAITALL);
        if (n <= 0) break;
        if (hdr.magic != kProtocolMagic) break;

        std::vector<uint8_t> payload(hdr.payload_len);
        if (hdr.payload_len > 0) {
            ssize_t pn = ::recv(client_fd, payload.data(), hdr.payload_len, MSG_WAITALL);
            if (pn != static_cast<ssize_t>(hdr.payload_len)) break;
        }

        const auto channel = static_cast<ChannelId>(hdr.channel);
        if (channel == ChannelId::Control) {
            std::string_view json_str(reinterpret_cast<const char*>(payload.data()),
                                      payload.size());
            handle_rpc_command(client_fd, json_str);
        } else if (channel == ChannelId::Console) {
            if (auto* uart = machine_.uart_device()) {
                for (uint8_t b : payload) {
                    uart->push_rx_byte(b);
                }
            }
        }
    }

    ::close(client_fd);
    {
        std::scoped_lock lock(clients_mutex_);
        std::erase(client_fds_, client_fd);
    }
}

void SimRvServer::telemetry_worker() {
    uint32_t seq = 0;
    while (running_.load(std::memory_order_relaxed)) {
        const uint32_t rate = telemetry_rate_hz_.load(std::memory_order_relaxed);
        const uint32_t interval_ms = (rate > 0) ? (1000 / rate) : 100;
        std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));

        if (!running_.load(std::memory_order_relaxed)) break;

        const auto snapshot = machine_.tui_execution_snapshot(0);
        TelemetryPacket packet{};
        packet.mcycle = snapshot.cycle_count;
        packet.mtime = snapshot.timer_ticks;
        packet.retired_icount = snapshot.instruction_count;
        packet.pc = snapshot.pc;

        const auto& state = machine_.primary_hart().state();
        for (size_t i = 0; i < 32; ++i) {
            packet.gpr[i] = state.regs.read(static_cast<RegId>(i));
            packet.fpr[i] = state.regs.read_fp(static_cast<FpRegId>(i));
        }
        packet.csrs[0] = state.mstatus;
        packet.csrs[1] = state.mepc;
        packet.csrs[2] = state.mcause;
        packet.csrs[3] = state.mtval;
        packet.csrs[4] = state.satp;
        packet.sequence = seq++;

        broadcast_telemetry(packet);
    }
}

void SimRvServer::broadcast_frame(ChannelId channel, uint8_t flags,
                                  std::span<const uint8_t> payload) {
    FrameHeader hdr{
        .magic = kProtocolMagic,
        .channel = std::to_underlying(channel),
        .flags = flags,
        .payload_len = static_cast<uint32_t>(payload.size()),
    };

    std::vector<uint8_t> buffer(sizeof(hdr) + payload.size());
    std::memcpy(buffer.data(), &hdr, sizeof(hdr));
    if (!payload.empty()) {
        std::memcpy(buffer.data() + sizeof(hdr), payload.data(), payload.size());
    }

    std::scoped_lock lock(clients_mutex_);
    for (int fd : client_fds_) {
        (void)::send(fd, buffer.data(), buffer.size(), MSG_NOSIGNAL);
    }
}

void SimRvServer::broadcast_console(std::string_view text) {
    broadcast_frame(
        ChannelId::Console, FrameFlags::None,
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
}

void SimRvServer::broadcast_telemetry(const TelemetryPacket& packet) {
    broadcast_frame(
        ChannelId::Telemetry, FrameFlags::None,
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(&packet), sizeof(packet)));
}

void SimRvServer::send_rpc_response(int client_fd, std::string_view response) {
    FrameHeader hdr{
        .magic = kProtocolMagic,
        .channel = std::to_underlying(ChannelId::Control),
        .flags = FrameFlags::None,
        .payload_len = static_cast<uint32_t>(response.size()),
    };

    std::vector<uint8_t> buffer(sizeof(hdr) + response.size());
    std::memcpy(buffer.data(), &hdr, sizeof(hdr));
    std::memcpy(buffer.data() + sizeof(hdr), response.data(), response.size());
    (void)::send(client_fd, buffer.data(), buffer.size(), MSG_NOSIGNAL);
}

void SimRvServer::send_rpc_event(std::string_view event) {
    broadcast_frame(
        ChannelId::Control, FrameFlags::None,
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(event.data()), event.size()));
}

void SimRvServer::handle_rpc_command(int client_fd, std::string_view json_str) {
    // Lightweight JSON-RPC 2.0 parser extracting method and id
    auto find_val = [&](std::string_view key) -> std::string {
        auto pos = json_str.find(key);
        if (pos == std::string_view::npos) return "";
        pos += key.size();
        while (pos < json_str.size() &&
               (json_str[pos] == ' ' || json_str[pos] == ':' || json_str[pos] == '"')) {
            ++pos;
        }
        size_t end = pos;
        while (end < json_str.size() && json_str[end] != '"' && json_str[end] != ',' &&
               json_str[end] != '}' && json_str[end] != ' ') {
            ++end;
        }
        return std::string(json_str.substr(pos, end - pos));
    };

    std::string id = find_val("\"id\"");
    if (id.empty()) id = "1";
    std::string method = find_val("\"method\"");

    if (method == "simrv.pause") {
        machine_.pause();
        paused_.store(true, std::memory_order_relaxed);
        send_rpc_response(
            client_fd,
            std::format(R"({{"jsonrpc":"2.0","id":{},"result":{{"status":"paused"}} }})", id));
        send_rpc_event(
            R"({"jsonrpc":"2.0","method":"simrv.onStatusChange","params":{"state":"paused"}})");
    } else if (method == "simrv.resume") {
        machine_.resume();
        paused_.store(false, std::memory_order_relaxed);
        send_rpc_response(
            client_fd,
            std::format(R"({{"jsonrpc":"2.0","id":{},"result":{{"status":"running"}} }})", id));
        send_rpc_event(
            R"({"jsonrpc":"2.0","method":"simrv.onStatusChange","params":{"state":"running"}})");
    } else if (method == "simrv.step") {
        std::string steps_str = find_val("\"steps\"");
        uint64_t steps = 1;
        if (!steps_str.empty()) {
            try {
                steps = std::stoull(steps_str);
            } catch (...) {
                steps = 1;
            }
        }
        for (uint64_t s = 0; s < steps && machine_.is_running(); ++s) {
            machine_.step();
        }
        send_rpc_response(
            client_fd,
            std::format(R"({{"jsonrpc":"2.0","id":{},"result":{{"stepped":{}}} }})", id, steps));
    } else if (method == "simrv.status") {
        const auto snapshot = machine_.tui_execution_snapshot(0);
        send_rpc_response(
            client_fd,
            std::format(
                R"({{"jsonrpc":"2.0","id":{},"result":{{"pc":"0x{:x}","cycle":{},"retired":{},"paused":{}}} }})",
                id, snapshot.pc, snapshot.cycle_count, snapshot.instruction_count,
                paused_.load(std::memory_order_relaxed)));
    } else if (method == "simrv.set_rate") {
        std::string hz_str = find_val("\"hz\"");
        if (!hz_str.empty()) {
            try {
                telemetry_rate_hz_.store(std::stoul(hz_str), std::memory_order_relaxed);
            } catch (...) {
            }
        }
        send_rpc_response(client_fd,
                          std::format(R"({{"jsonrpc":"2.0","id":{},"result":{{"hz":{}}} }})", id,
                                      telemetry_rate_hz_.load(std::memory_order_relaxed)));
    } else {
        send_rpc_response(
            client_fd,
            std::format(
                R"({{"jsonrpc":"2.0","id":{},"error":{{"code":-32601,"message":"Method not found"}} }})",
                id));
    }
}

void SimRvServer::handle_char_write(char ch) {
    const uint8_t byte = static_cast<uint8_t>(ch);
    broadcast_frame(ChannelId::Console, FrameFlags::None, std::span<const uint8_t>(&byte, 1));
}

void SimRvServer::on_cycle_completed() {}
void SimRvServer::pause_loop() { paused_.store(true, std::memory_order_relaxed); }
void SimRvServer::unpause_loop() { paused_.store(false, std::memory_order_relaxed); }
void SimRvServer::set_paused(bool paused) { paused_.store(paused, std::memory_order_relaxed); }
bool SimRvServer::is_paused() const { return paused_.load(std::memory_order_relaxed); }
bool SimRvServer::is_trace_active() const { return false; }
bool SimRvServer::captures_execution_detail() const { return is_paused(); }
uint64_t SimRvServer::step_delay_us() const {
    return step_delay_us_.load(std::memory_order_relaxed);
}
void SimRvServer::set_step_delay_us(uint64_t delay_us) {
    step_delay_us_.store(delay_us, std::memory_order_relaxed);
}
void SimRvServer::set_status_override(const std::string& status) { status_override_ = status; }
void SimRvServer::set_persistent_status_override(const std::string& status) {
    status_override_ = status;
}
void SimRvServer::start_ui_thread() {}
void SimRvServer::stop_ui_thread() {}
bool SimRvServer::is_ui_thread_running() const { return false; }
void SimRvServer::set_sim_thread_sleeping(bool /*sleeping*/) {}
void SimRvServer::set_target_fps(uint32_t fps) {
    telemetry_rate_hz_.store(fps > 0 ? fps : 30, std::memory_order_relaxed);
}
uint32_t SimRvServer::target_fps() const {
    return telemetry_rate_hz_.load(std::memory_order_relaxed);
}

}  // namespace simrv::net
