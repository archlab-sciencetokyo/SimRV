/**
 * @file Client.cpp
 * @brief SimRV detachable client implementation.
 */
#include "simrv/net/Client.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <format>
#include <iostream>
#include <vector>

#include "simrv/core/Logger.hpp"

namespace simrv::net {

SimRvClient::SimRvClient(std::string_view endpoint_str) : endpoint_(parse_endpoint(endpoint_str)) {}

SimRvClient::~SimRvClient() { disconnect(); }

auto SimRvClient::connect() -> bool {
    if (connected_.load(std::memory_order_relaxed)) return true;

    if (endpoint_.is_unix) {
        socket_fd_ = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (socket_fd_ < 0) {
            simrv::log::error("Client socket creation failed: {}", strerror(errno));
            return false;
        }
        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, endpoint_.path.c_str(), sizeof(addr.sun_path) - 1);
        if (::connect(socket_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            simrv::log::error("Failed to connect to SimRV daemon at '{}': {}", endpoint_.path,
                              strerror(errno));
            ::close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
    } else {
        socket_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd_ < 0) {
            simrv::log::error("Client socket creation failed: {}", strerror(errno));
            return false;
        }
        struct sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(endpoint_.port);
        ::inet_pton(AF_INET, endpoint_.host.c_str(), &addr.sin_addr);
        if (::connect(socket_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            simrv::log::error("Failed to connect to SimRV daemon at {}:{}: {}", endpoint_.host,
                              endpoint_.port, strerror(errno));
            ::close(socket_fd_);
            socket_fd_ = -1;
            return false;
        }
    }

    connected_.store(true, std::memory_order_release);
    receive_thread_ = std::thread([this] { receive_worker(); });
    return true;
}

void SimRvClient::disconnect() {
    if (!connected_.exchange(false, std::memory_order_acq_rel)) return;

    if (socket_fd_ >= 0) {
        ::shutdown(socket_fd_, SHUT_RDWR);
        ::close(socket_fd_);
        socket_fd_ = -1;
    }

    if (receive_thread_.joinable()) {
        receive_thread_.join();
    }
}

auto SimRvClient::send_frame(ChannelId channel, uint8_t flags, std::span<const uint8_t> payload)
    -> bool {
    if (!connected_.load(std::memory_order_relaxed) || socket_fd_ < 0) return false;

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

    ssize_t sent = ::send(socket_fd_, buffer.data(), buffer.size(), MSG_NOSIGNAL);
    return sent == static_cast<ssize_t>(buffer.size());
}

auto SimRvClient::send_console_input(std::string_view text) -> bool {
    return send_frame(
        ChannelId::Console, FrameFlags::None,
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size()));
}

auto SimRvClient::send_rpc(std::string_view json_cmd) -> bool {
    return send_frame(ChannelId::Control, FrameFlags::None,
                      std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(json_cmd.data()),
                                               json_cmd.size()));
}

auto SimRvClient::send_pause() -> bool {
    uint32_t id = rpc_id_counter_++;
    return send_rpc(std::format(R"({{"jsonrpc":"2.0","id":{},"method":"simrv.pause"}})", id));
}

auto SimRvClient::send_resume() -> bool {
    uint32_t id = rpc_id_counter_++;
    return send_rpc(std::format(R"({{"jsonrpc":"2.0","id":{},"method":"simrv.resume"}})", id));
}

auto SimRvClient::send_step(uint64_t steps) -> bool {
    uint32_t id = rpc_id_counter_++;
    return send_rpc(
        std::format(R"({{"jsonrpc":"2.0","id":{},"method":"simrv.step","params":{{"steps":{}}} }})",
                    id, steps));
}

void SimRvClient::receive_worker() {
    while (connected_.load(std::memory_order_relaxed)) {
        struct pollfd pfd{};
        pfd.fd = socket_fd_;
        pfd.events = POLLIN;
        int rc = ::poll(&pfd, 1, 100);
        if (rc < 0) break;
        if (rc == 0) continue;

        FrameHeader hdr{};
        ssize_t n = ::recv(socket_fd_, &hdr, sizeof(hdr), MSG_WAITALL);
        if (n <= 0) break;
        if (hdr.magic != kProtocolMagic) break;

        std::vector<uint8_t> payload(hdr.payload_len);
        if (hdr.payload_len > 0) {
            ssize_t pn = ::recv(socket_fd_, payload.data(), hdr.payload_len, MSG_WAITALL);
            if (pn != static_cast<ssize_t>(hdr.payload_len)) break;
        }

        const auto channel = static_cast<ChannelId>(hdr.channel);
        if (channel == ChannelId::Control) {
            if (rpc_cb_) {
                rpc_cb_(std::string_view(reinterpret_cast<const char*>(payload.data()),
                                         payload.size()));
            }
        } else if (channel == ChannelId::Console) {
            if (console_cb_) {
                console_cb_(std::string_view(reinterpret_cast<const char*>(payload.data()),
                                             payload.size()));
            }
        } else if (channel == ChannelId::Telemetry) {
            if (payload.size() >= sizeof(TelemetryPacket)) {
                TelemetryPacket pkt{};
                std::memcpy(&pkt, payload.data(), sizeof(pkt));
                {
                    std::scoped_lock lock(telemetry_mutex_);
                    latest_telemetry_ = pkt;
                }
                if (telemetry_cb_) {
                    telemetry_cb_(pkt);
                }
            }
        }
    }

    connected_.store(false, std::memory_order_release);
}

namespace {
struct RawTermGuard {
    struct termios orig_termios{};
    bool enabled{false};

    RawTermGuard() {
        if (::isatty(STDIN_FILENO)) {
            if (::tcgetattr(STDIN_FILENO, &orig_termios) == 0) {
                struct termios raw = orig_termios;
                ::cfmakeraw(&raw);
                if (::tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0) {
                    enabled = true;
                }
            }
        }
    }

    ~RawTermGuard() {
        if (enabled) {
            ::tcsetattr(STDIN_FILENO, TCSANOW, &orig_termios);
        }
    }
};
}  // namespace

auto run_client(std::string_view endpoint_str, bool cli_mode) -> int {
    SimRvClient client(endpoint_str);
    if (!client.connect()) {
        std::cerr << "Error: Could not connect to SimRV daemon at " << endpoint_str << "\n";
        return 1;
    }

    std::cout << "Connected to SimRV daemon at " << endpoint_str << "\n";
    if (cli_mode) {
        std::cout << "Starting raw interactive session ([Ctrl-C] or disconnect to exit)...\n";
        RawTermGuard term_guard;

        client.set_console_callback([](std::string_view chunk) {
            (void)::write(STDOUT_FILENO, chunk.data(), chunk.size());
        });

        std::thread input_thread([&client] {
            char buf[128];
            while (client.is_connected()) {
                struct pollfd pfd{};
                pfd.fd = STDIN_FILENO;
                pfd.events = POLLIN;
                int rc = ::poll(&pfd, 1, 100);
                if (rc <= 0) continue;

                ssize_t n = ::read(STDIN_FILENO, buf, sizeof(buf));
                if (n <= 0) break;
                client.send_console_input(std::string_view(buf, n));
            }
        });

        while (client.is_connected()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        if (input_thread.joinable()) {
            input_thread.join();
        }
    }

    return 0;
}

}  // namespace simrv::net
