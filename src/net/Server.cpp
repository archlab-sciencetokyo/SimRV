#include "simrv/net/Server.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <cstring>

#include "simrv/core/Logger.hpp"
#include "simrv/device/Uart.hpp"

namespace simrv::net {
auto parse_endpoint(std::string_view value) -> Endpoint {
    Endpoint endpoint;
    if (value.empty()) return endpoint;
    const auto colon = value.find(':');
    if (value.find('/') == std::string_view::npos &&
        (colon != value.npos ||
         std::ranges::all_of(value, [](char ch) { return ch >= '0' && ch <= '9'; }))) {
        endpoint.is_unix = false;
        if (colon != value.npos) endpoint.host = value.substr(0, colon);
        const auto port = colon == value.npos ? value : value.substr(colon + 1);
        unsigned number = 0;
        std::from_chars(port.data(), port.data() + port.size(), number);
        endpoint.port = static_cast<uint16_t>(number);
    } else
        endpoint.path = value;
    return endpoint;
}
SimRvServer::SimRvServer(core::Machine& machine, std::string_view endpoint)
    : endpoint_(parse_endpoint(endpoint)) {
    bind(machine);
}
SimRvServer::~SimRvServer() {
    stop();
    unbind();
}
void SimRvServer::bind(core::Machine& machine) {
    std::scoped_lock lock(mutex_);
    machine_ = &machine;
    backend_ = std::make_shared<tui::LocalTuiBackend>(machine, ++session_ << 32);
    paused_ = machine.is_paused();
    reset_terminal_ = true;
    output_.clear();
    wake();
}
void SimRvServer::unbind() {
    std::scoped_lock lock(mutex_);
    if (backend_) backend_->detach();
    backend_.reset();
    machine_ = nullptr;
}
void SimRvServer::wake() {
    if (wake_) {
        const uint64_t one = 1;
        (void)::write(wake_.get(), &one, sizeof(one));
    }
}
bool SimRvServer::start() {
    if (running_) return true;
    if (!endpoint_.is_unix || endpoint_.path.size() >= sizeof(sockaddr_un::sun_path)) {
        log::error("TUI server requires a local Unix socket path");
        return false;
    }
    listener_.reset(::socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
    wake_.reset(::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC));
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, endpoint_.path.c_str(), endpoint_.path.size() + 1);
    // Never unlink another running server's endpoint.
    if (!listener_ || !wake_ ||
        ::bind(listener_.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0 ||
        ::listen(listener_.get(), 4) < 0) {
        log::error("Cannot listen on {}: {}", endpoint_.path, std::strerror(errno));
        return false;
    }
    running_ = true;
    worker_ = std::thread([this] { loop(); });
    return true;
}
void SimRvServer::stop() {
    if (!running_.exchange(false)) return;
    wake();
    if (worker_.joinable()) worker_.join();
    listener_.reset();
    ::unlink(endpoint_.path.c_str());
}
void SimRvServer::set_target_fps(uint32_t fps) { fps_ = std::clamp(fps, 1u, 120u); }
void SimRvServer::handle_char_write(char ch) {
    bool notify;
    {
        std::scoped_lock lock(mutex_);
        notify = output_.empty();
        output_.push_back(ch);
    }
    if (notify) wake();
}
void SimRvServer::reply(uint64_t connection, tui::BackendReply response) {
    std::vector<uint8_t> payload;
    put_u64(payload, response.id);
    put_u64(payload, response.generation);
    put_u64(payload, response.success);
    payload.insert(payload.end(), response.text.begin(), response.text.end());
    auto frame = make_frame(ChannelId::Reply, payload);
    {
        std::scoped_lock lock(mutex_);
        if (reply_bytes_ + frame.size() > kMaxPayload)
            overflow_ = true;
        else {
            reply_bytes_ += frame.size();
            replies_.push_back({connection, std::move(frame)});
        }
    }
    wake();
}
void SimRvServer::loop() {
    util::UniqueFd client;
    uint64_t connection = 0, terminal_sequence = 0, last_terminal = ~uint64_t{0};
    bool handshake = false;
    std::vector<uint8_t> incoming, sending;
    std::deque<std::vector<uint8_t>> reliable;
    std::vector<uint8_t> telemetry, screen;
    size_t offset = 0;
    auto next_frame = std::chrono::steady_clock::now();
    auto hello_deadline = next_frame;
    terminal_.set_response_callback([this](std::string_view response) {
        // loop holds mutex_: machine lifetime and UART input are synchronized here.
        if (machine_)
            if (auto* uart = machine_->uart_device())
                for (char ch : response) uart->push_rx_byte(static_cast<uint8_t>(ch));
    });
    auto disconnect = [&] {
        client.reset();
        handshake = false;
        incoming.clear();
        sending.clear();
        offset = 0;
        reliable.clear();
        telemetry.clear();
        screen.clear();
        ++connection;
    };
    while (running_) {
        const auto now = std::chrono::steady_clock::now();
        {
            std::scoped_lock lock(mutex_);
            if (reset_terminal_) {
                terminal_.write_string("\033c");
                reset_terminal_ = false;
                last_terminal = ~uint64_t{0};
                if (client) {
                    std::vector<uint8_t> event;
                    put_u64(event, session_);
                    reliable.push_back(make_frame(ChannelId::Lifecycle, event));
                }
            }
            if (!output_.empty()) {
                terminal_.write_string(output_);
                output_.clear();
            }
            for (auto& pending : replies_)
                if (pending.connection == connection && client)
                    reliable.push_back(std::move(pending.frame));
            replies_.clear();
            reply_bytes_ = 0;
            if (client && handshake && now >= next_frame && backend_) {
                backend_->request_sample();
                const auto view = backend_->view();
                std::vector<uint8_t> data;
                put_u64(data, view.generation);
                put_u64(data, view.harts.size());
                for (const auto& hart : view.harts) {
                    put_u64(data, hart.hart);
                    put_u64(data, hart.pc);
                    put_u64(data, hart.cycle_count);
                    put_u64(data, hart.instruction_count);
                    put_u64(data, hart.timer_ticks);
                    put_u64(data, static_cast<uint64_t>(hart.execution_state));
                }
                telemetry = make_frame(ChannelId::Telemetry, data);
                if (terminal_.generation() != last_terminal) {
                    last_terminal = terminal_.generation();
                    std::vector<uint8_t> lines;
                    put_u64(lines, ++terminal_sequence);
                    const int total = terminal_.get_lines_count();
                    const int start = terminal_.get_scrollback_size();
                    put_u64(lines, total - start);
                    for (int row = start; row < total; ++row) {
                        const auto text = terminal_.get_line_as_string(
                            row, 512, row == start + terminal_.get_cursor_y());
                        put_u64(lines, text.size());
                        lines.insert(lines.end(), text.begin(), text.end());
                    }
                    screen = make_frame(ChannelId::Terminal, lines);
                }
            }
        }
        if (now >= next_frame) next_frame = now + std::chrono::milliseconds(1000 / fps_);
        size_t queued = sending.size();
        for (const auto& frame : reliable) queued += frame.size();
        if (overflow_.exchange(false) || queued > kMaxPayload ||
            (client && !handshake && now >= hello_deadline))
            disconnect();
        if (sending.empty()) {
            if (!reliable.empty()) {
                sending = std::move(reliable.front());
                reliable.pop_front();
            } else if (!telemetry.empty())
                sending.swap(telemetry);
            else if (!screen.empty())
                sending.swap(screen);
        }
        pollfd fds[] = {
            {listener_.get(), POLLIN, 0},
            {wake_.get(), POLLIN, 0},
            {client.get(), static_cast<short>(POLLIN | (sending.empty() ? 0 : POLLOUT)), 0}};
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                                   next_frame - std::chrono::steady_clock::now())
                                   .count();
        (void)::poll(fds, 3, client ? static_cast<int>(std::max<int64_t>(0, remaining)) : 250);
        if (fds[1].revents & POLLIN) {
            uint64_t pending;
            (void)::read(wake_.get(), &pending, sizeof(pending));
        }
        if (fds[0].revents & POLLIN) {
            util::UniqueFd accepted(
                ::accept4(listener_.get(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC));
            if (accepted && client) {
                const std::string busy = "controller already attached";
                auto error = make_frame(
                    ChannelId::Reply, {reinterpret_cast<const uint8_t*>(busy.data()), busy.size()});
                (void)::send(accepted.get(), error.data(), error.size(), MSG_NOSIGNAL);
            } else if (accepted) {
                client = std::move(accepted);
                ++connection;
                handshake = false;
                hello_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
                last_terminal = ~uint64_t{0};
            }
        }
        if (!client) continue;
        if (fds[2].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            disconnect();
            continue;
        }
        if (fds[2].revents & POLLOUT) {
            const auto written = ::send(client.get(), sending.data() + offset,
                                        sending.size() - offset, MSG_NOSIGNAL);
            if (written > 0) {
                offset += static_cast<size_t>(written);
                if (offset == sending.size()) {
                    sending.clear();
                    offset = 0;
                }
            } else if (written < 0 && errno != EAGAIN && errno != EINTR) {
                disconnect();
                continue;
            }
        }
        if (!(fds[2].revents & POLLIN)) continue;
        uint8_t bytes[16384];
        const auto count = ::recv(client.get(), bytes, sizeof(bytes), 0);
        if (count <= 0) {
            if (count == 0 || (errno != EAGAIN && errno != EINTR)) disconnect();
            continue;
        }
        incoming.insert(incoming.end(), bytes, bytes + count);
        try {
            ChannelId channel;
            std::vector<uint8_t> data;
            while (take_frame(incoming, channel, data)) {
                std::span<const uint8_t> payload(data);
                if (!handshake) {
                    if (channel != ChannelId::Hello || get_u64(payload) != kProtocolVersion ||
                        !payload.empty())
                        throw std::runtime_error("protocol version mismatch");
                    handshake = true;
                    std::vector<uint8_t> hello;
                    put_u64(hello, kProtocolVersion);
                    reliable.push_back(make_frame(ChannelId::Hello, hello));
                    next_frame = std::chrono::steady_clock::now();
                    continue;
                }
                if (channel == ChannelId::Console) {
                    std::scoped_lock lock(mutex_);
                    if (machine_)
                        if (auto* uart = machine_->uart_device())
                            for (uint8_t byte : data)
                                uart->push_rx_byte(byte == '\r' ? '\n' : byte);
                } else if (channel == ChannelId::Control) {
                    tui::BackendRequest request;
                    request.command = static_cast<tui::BackendCommand>(get_u64(payload));
                    request.id = get_u64(payload);
                    request.generation = get_u64(payload);
                    request.address = get_u64(payload);
                    const auto hart = get_u64(payload), length = get_u64(payload);
                    if (!payload.empty() || hart > 63 || length > 4096 ||
                        request.command > tui::BackendCommand::Resize)
                        throw std::runtime_error("invalid request");
                    request.hart = static_cast<uint32_t>(hart);
                    request.count = static_cast<uint32_t>(length);
                    std::shared_ptr<tui::LocalTuiBackend> backend;
                    {
                        std::scoped_lock lock(mutex_);
                        backend = backend_;
                        if (request.command == tui::BackendCommand::Resize) {
                            terminal_.resize(
                                static_cast<int>(std::clamp<uint64_t>(request.address, 20, 512)),
                                static_cast<int>(std::clamp(request.count, 5u, 200u)));
                            last_terminal = ~uint64_t{0};
                        }
                    }
                    if (request.command == tui::BackendCommand::Resize)
                        reply(connection, {request.id, 0, true, "resized"});
                    else if (backend)
                        backend->submit(request, [this, connection](tui::BackendReply response) {
                            reply(connection, std::move(response));
                        });
                    else
                        reply(connection, {request.id, 0, false, "session restarting"});
                } else
                    throw std::runtime_error("unexpected channel");
            }
        } catch (const std::exception& error) {
            const std::string message = error.what();
            const auto frame =
                make_frame(ChannelId::Reply,
                           {reinterpret_cast<const uint8_t*>(message.data()), message.size()});
            (void)::send(client.get(), frame.data(), frame.size(), MSG_NOSIGNAL);
            disconnect();
        }
    }
}
}  // namespace simrv::net
