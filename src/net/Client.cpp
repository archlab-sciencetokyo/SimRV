#include "simrv/net/Client.hpp"

#include <poll.h>
#include <sys/eventfd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <format>
#include <iostream>
#include <sstream>

#include "simrv/tui/framework/Text.hpp"

namespace simrv::net {
namespace {
void signal_fd(int fd) {
    const uint64_t one = 1;
    (void)::write(fd, &one, sizeof(one));
}
void write_terminal(std::string_view text) {
    while (!text.empty()) {
        const auto count = ::write(STDOUT_FILENO, text.data(), text.size());
        if (count > 0)
            text.remove_prefix(static_cast<size_t>(count));
        else if (count < 0 && errno == EINTR)
            continue;
        else
            break;
    }
}
}  // namespace
SimRvClient::SimRvClient(std::string_view endpoint) : endpoint_(parse_endpoint(endpoint)) {}
SimRvClient::~SimRvClient() { disconnect(); }
bool SimRvClient::connect() {
    if (connected_) return true;
    disconnect();
    if (!endpoint_.is_unix || endpoint_.path.size() >= sizeof(sockaddr_un::sun_path)) return false;
    socket_.reset(::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0));
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    std::memcpy(address.sun_path, endpoint_.path.c_str(), endpoint_.path.size() + 1);
    if (!socket_ ||
        ::connect(socket_.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0)
        return false;
    std::vector<uint8_t> hello;
    put_u64(hello, kProtocolVersion);
    const auto frame = make_frame(ChannelId::Hello, hello);
    if (::send(socket_.get(), frame.data(), frame.size(), MSG_NOSIGNAL) !=
        static_cast<ssize_t>(frame.size()))
        return false;
    std::vector<uint8_t> incoming, payload;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    try {
        while (std::chrono::steady_clock::now() < deadline) {
            pollfd fd{socket_.get(), POLLIN, 0};
            if (::poll(&fd, 1, 20) <= 0) continue;
            uint8_t bytes[8];  // Read exactly the handshake without consuming subsequent frames.
            const auto count = ::recv(socket_.get(), bytes, sizeof(bytes), 0);
            if (count <= 0) return false;
            incoming.insert(incoming.end(), bytes, bytes + count);
            ChannelId channel;
            if (take_frame(incoming, channel, payload)) {
                if (channel != ChannelId::Hello) {
                    std::cerr << "Attach rejected: " << std::string(payload.begin(), payload.end())
                              << '\n';
                    return false;
                }
                std::span<const uint8_t> data(payload);
                if (get_u64(data) != kProtocolVersion || !data.empty()) return false;
                wake_.reset(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
                changed_.reset(::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK));
                if (!wake_ || !changed_) return false;
                connected_ = true;
                worker_ = std::thread([this] { receive_worker(); });
                return true;
            }
        }
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
    }
    return false;
}
void SimRvClient::disconnect() {
    connected_ = false;
    if (socket_) ::shutdown(socket_.get(), SHUT_RDWR);
    if (wake_) signal_fd(wake_.get());
    if (worker_.joinable()) worker_.join();
    socket_.reset();
    std::scoped_lock lock(mutex_);
    outgoing_.clear();
    outgoing_bytes_ = 0;
    completions_.clear();
    view_ = {};
    terminal_.clear();
    terminal_sequence_ = 0;
}
bool SimRvClient::send_frame(ChannelId channel, uint8_t flags, std::span<const uint8_t> payload) {
    if (!connected_ || flags != 0 || payload.size() > kMaxPayload) return false;
    auto frame = make_frame(channel, payload);
    {
        std::scoped_lock lock(mutex_);
        if (outgoing_bytes_ + frame.size() > kMaxPayload) return false;
        outgoing_bytes_ += frame.size();
        outgoing_.push_back(std::move(frame));
    }
    signal_fd(wake_.get());
    return true;
}
bool SimRvClient::send_console_input(std::string_view text) {
    return send_frame(ChannelId::Console, 0,
                      {reinterpret_cast<const uint8_t*>(text.data()), text.size()});
}
void SimRvClient::submit(tui::BackendRequest request, tui::BackendCompletion complete) {
    {
        std::scoped_lock lock(mutex_);
        if (!request.id) request.id = next_id_++;
        completions_[request.id] = complete;
    }
    std::vector<uint8_t> data;
    put_u64(data, static_cast<uint64_t>(request.command));
    put_u64(data, request.id);
    put_u64(data, request.generation);
    put_u64(data, request.address);
    put_u64(data, request.hart);
    put_u64(data, request.count);
    if (!send_frame(ChannelId::Control, 0, data)) {
        {
            std::scoped_lock lock(mutex_);
            completions_.erase(request.id);
        }
        if (complete) complete({request.id, 0, false, "disconnected or send queue full"});
    }
}
bool SimRvClient::send_pause() {
    submit({.command = tui::BackendCommand::Pause}, {});
    return connected_;
}
bool SimRvClient::send_resume() {
    submit({.command = tui::BackendCommand::Resume}, {});
    return connected_;
}
bool SimRvClient::send_step(uint64_t steps) {
    if (steps != 1) return false;
    submit({.command = tui::BackendCommand::Step}, {});
    return connected_;
}
auto SimRvClient::view() -> tui::BackendView {
    std::scoped_lock lock(mutex_);
    return view_;
}
auto SimRvClient::terminal_lines() -> std::vector<std::string> {
    std::scoped_lock lock(mutex_);
    return terminal_;
}
auto SimRvClient::last_message() -> std::string {
    std::scoped_lock lock(mutex_);
    return message_;
}
auto SimRvClient::latest_telemetry() const -> TelemetryPacket {
    std::scoped_lock lock(mutex_);
    TelemetryPacket packet;
    if (!view_.harts.empty()) {
        const auto& hart = view_.harts.front();
        packet.pc = hart.pc;
        packet.mcycle = hart.cycle_count;
        packet.mtime = hart.timer_ticks;
        packet.retired_icount = hart.instruction_count;
    }
    return packet;
}
void SimRvClient::process(ChannelId channel, std::span<const uint8_t> payload) {
    tui::BackendCompletion completed;
    tui::BackendReply response;
    RpcCallback rpc;
    TelemetryCallback telemetry;
    ConsoleCallback console;
    {
        std::scoped_lock lock(mutex_);
        if (channel == ChannelId::Telemetry) {
            view_.generation = get_u64(payload);
            const auto count = get_u64(payload);
            if (count > 64) throw std::runtime_error("invalid hart count");
            view_.harts.clear();
            for (size_t h = 0; h < count; ++h) {
                core::TuiExecutionSnapshot hart;
                hart.hart = get_u64(payload);
                hart.pc = get_u64(payload);
                hart.cycle_count = get_u64(payload);
                hart.instruction_count = get_u64(payload);
                hart.timer_ticks = get_u64(payload);
                hart.execution_state = static_cast<core::ExecutionState>(get_u64(payload));
                view_.harts.push_back(std::move(hart));
            }
            if (!payload.empty()) throw std::runtime_error("trailing telemetry");
            telemetry = telemetry_cb_;
        } else if (channel == ChannelId::Terminal) {
            const auto sequence = get_u64(payload), count = get_u64(payload);
            if (count > 200 || sequence <= terminal_sequence_)
                throw std::runtime_error("invalid terminal checkpoint");
            terminal_sequence_ = sequence;
            terminal_.clear();
            for (size_t row = 0; row < count; ++row) {
                const auto length = get_u64(payload);
                if (length > payload.size()) throw std::runtime_error("truncated terminal row");
                terminal_.emplace_back(reinterpret_cast<const char*>(payload.data()), length);
                payload = payload.subspan(length);
            }
            if (!payload.empty()) throw std::runtime_error("trailing terminal bytes");
            console = console_cb_;
        } else if (channel == ChannelId::Reply) {
            response.id = get_u64(payload);
            response.generation = get_u64(payload);
            response.success = get_u64(payload) != 0;
            response.text.assign(reinterpret_cast<const char*>(payload.data()), payload.size());
            if (response.generation) view_.generation = response.generation;
            message_ = response.success ? response.text : "Error: " + response.text;
            if (auto it = completions_.find(response.id); it != completions_.end()) {
                completed = std::move(it->second);
                completions_.erase(it);
            }
            rpc = rpc_cb_;
        } else if (channel == ChannelId::Lifecycle) {
            (void)get_u64(payload);
            view_ = {};
            terminal_.clear();
            message_ = "Guest session restarted; prior inspection is invalid.";
        } else
            throw std::runtime_error("unexpected server channel");
    }
    if (completed) completed(response);
    if (rpc) rpc(response.text);
    if (telemetry) telemetry(latest_telemetry());
    if (console) {
        std::string screen = "\033[H";
        for (const auto& row : terminal_lines()) screen += row + "\033[0m\r\n";
        console(screen);
    }
    signal_fd(changed_.get());
}
void SimRvClient::receive_worker() {
    std::vector<uint8_t> incoming, sending;
    size_t offset = 0;
    try {
        while (connected_) {
            if (sending.empty()) {
                std::scoped_lock lock(mutex_);
                if (!outgoing_.empty()) {
                    sending = std::move(outgoing_.front());
                    outgoing_.pop_front();
                    outgoing_bytes_ -= sending.size();
                }
            }
            pollfd fds[] = {
                {socket_.get(), static_cast<short>(POLLIN | (sending.empty() ? 0 : POLLOUT)), 0},
                {wake_.get(), POLLIN, 0}};
            (void)::poll(fds, 2, 250);
            if (fds[1].revents & POLLIN) {
                uint64_t n;
                (void)::read(wake_.get(), &n, sizeof(n));
            }
            if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) break;
            if (fds[0].revents & POLLOUT) {
                const auto n = ::send(socket_.get(), sending.data() + offset,
                                      sending.size() - offset, MSG_NOSIGNAL | MSG_DONTWAIT);
                if (n > 0) {
                    offset += static_cast<size_t>(n);
                    if (offset == sending.size()) {
                        sending.clear();
                        offset = 0;
                    }
                } else if (n < 0 && errno != EAGAIN && errno != EINTR)
                    break;
            }
            if (fds[0].revents & POLLIN) {
                uint8_t bytes[16384];
                const auto n = ::recv(socket_.get(), bytes, sizeof(bytes), MSG_DONTWAIT);
                if (n <= 0) {
                    if (n == 0 || (errno != EAGAIN && errno != EINTR)) break;
                    continue;
                }
                incoming.insert(incoming.end(), bytes, bytes + n);
                ChannelId channel;
                std::vector<uint8_t> payload;
                while (take_frame(incoming, channel, payload)) process(channel, payload);
            }
        }
    } catch (const std::exception& error) {
        std::scoped_lock lock(mutex_);
        message_ = error.what();
    }
    connected_ = false;
    signal_fd(changed_.get());
}

auto run_client(std::string_view endpoint, bool cli_mode) -> int {
    SimRvClient client(endpoint);
    if (!client.connect()) {
        std::cerr << "Cannot attach to " << endpoint << '\n';
        return 1;
    }
    termios saved{};
    const bool terminal = tcgetattr(STDIN_FILENO, &saved) == 0;
    if (terminal) {
        auto raw = saved;
        cfmakeraw(&raw);
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
    struct Restore {
        bool terminal;
        termios saved;
        ~Restore() {
            write_terminal("\033[0m\033[?25h\033[?1049l");
            if (terminal) tcsetattr(STDIN_FILENO, TCSANOW, &saved);
        }
    } restore{terminal, saved};
    write_terminal("\033[?1049h\033[2J");
    bool done = false, editing = false;
    std::string command, escape;
    uint32_t selected_hart = 0;
    int last_width = 0, last_height = 0;
    std::vector<std::string> previous;
    auto next_draw = std::chrono::steady_clock::now();
    auto submit = [&](tui::BackendCommand operation, uint64_t address = 0, uint32_t count = 1) {
        const auto view = client.view();
        const bool inspection = operation >= tui::BackendCommand::Registers &&
                                operation <= tui::BackendCommand::RemoveBreakpoint;
        client.submit(
            {operation, 0, inspection ? view.generation : 0, address, selected_hart, count}, {});
    };
    while (client.is_connected() && !done) {
        winsize size{};
        ioctl(STDOUT_FILENO, TIOCGWINSZ, &size);
        const int width = std::clamp<int>(size.ws_col ? size.ws_col : 100, 40, 512);
        const int height = std::clamp<int>(size.ws_row ? size.ws_row : 30, 10, 200);
        const int left_width = cli_mode ? 0 : std::min(50, width / 2);
        if (width != last_width || height != last_height) {
            client.submit(
                {tui::BackendCommand::Resize, 0, 0, static_cast<uint64_t>(width - left_width), 0,
                 static_cast<uint32_t>(height - (cli_mode ? 0 : 3))},
                {});
            last_width = width;
            last_height = height;
            previous.clear();
        }
        auto view = client.view();
        const bool paused = view.harts.empty() ||
                            view.harts.front().execution_state != core::ExecutionState::Running;
        const bool focus_guest = !paused;
        bool input_changed = false;
        pollfd fds[] = {{STDIN_FILENO, POLLIN, 0}, {client.notification_fd(), POLLIN, 0}};
        (void)::poll(fds, 2, 10);
        if (fds[1].revents & POLLIN) {
            uint64_t n;
            (void)::read(client.notification_fd(), &n, sizeof(n));
        }
        if (fds[0].revents & POLLIN) {
            char bytes[256];
            const auto n = ::read(STDIN_FILENO, bytes, sizeof(bytes));
            if (n <= 0) break;
            for (ssize_t i = 0; i < n; ++i) {
                const char ch = bytes[i];
                input_changed = true;
                if (ch == 17) {
                    done = true;
                    break;
                }
                if (ch == 16) {
                    submit(paused ? tui::BackendCommand::Resume : tui::BackendCommand::Pause);
                    continue;
                }
                if (ch == 18) {
                    submit(tui::BackendCommand::Reboot);
                    continue;
                }
                if (!escape.empty() || ch == '\033') {
                    escape += ch;
                    if (escape == "\033[21~") {
                        done = true;
                        escape.clear();
                    } else if (escape.size() > 1 && ch != '[' &&
                               (ch == '~' || (ch >= '@' && ch <= 'Z') || escape.size() > 16)) {
                        if (focus_guest && !paused) client.send_console_input(escape);
                        escape.clear();
                    }
                    continue;
                }
                if (editing) {
                    if (ch == '\r' || ch == '\n') {
                        editing = false;
                        std::istringstream words(command);
                        std::string name, number;
                        words >> name >> number;
                        uint64_t address = 0;
                        uint32_t count = 128;
                        bool valid = true;
                        if (!number.empty()) {
                            std::string_view value(number);
                            if (value.starts_with("0x")) value.remove_prefix(2);
                            auto converted =
                                std::from_chars(value.data(), value.data() + value.size(), address,
                                                name == "hart" ? 10 : 16);
                            valid = converted.ec == std::errc{} &&
                                    converted.ptr == value.data() + value.size();
                        }
                        if (!(words >> count)) count = 128;
                        if (valid) {
                            if (name == "hart" && address < view.harts.size())
                                selected_hart = static_cast<uint32_t>(address);
                            else if (name == "regs")
                                submit(tui::BackendCommand::Registers);
                            else if (name == "mem")
                                submit(tui::BackendCommand::Memory, address, count);
                            else if (name == "dis")
                                submit(tui::BackendCommand::Disassemble, address, count);
                            else if (name == "bp")
                                submit(number.empty() ? tui::BackendCommand::Breakpoints
                                                      : tui::BackendCommand::AddBreakpoint,
                                       address);
                            else if (name == "del")
                                submit(tui::BackendCommand::RemoveBreakpoint, address);
                            else if (name == "pause")
                                submit(tui::BackendCommand::Pause);
                            else if (name == "resume")
                                submit(tui::BackendCommand::Resume);
                            else if (name == "step")
                                submit(tui::BackendCommand::Step);
                            else if (name == "reboot")
                                submit(tui::BackendCommand::Reboot);
                            else if (name == "detach")
                                done = true;
                        }
                        command.clear();
                    } else if (ch == 127 || ch == 8) {
                        if (!command.empty()) command.pop_back();
                    } else if (ch >= 32 && command.size() < 256)
                        command += ch;
                    continue;
                }
                if ((paused || !focus_guest) && ch == ':') {
                    editing = true;
                    command.clear();
                } else if ((paused || !focus_guest) && ch == 'q')
                    done = true;
                else if ((paused || !focus_guest) && ch == 'c')
                    submit(tui::BackendCommand::Resume);
                else if ((paused || !focus_guest) && ch == 's')
                    submit(tui::BackendCommand::Step);
                else if ((paused || !focus_guest) && ch == 'r')
                    submit(tui::BackendCommand::Registers);
                else if (focus_guest)
                    client.send_console_input(std::string_view(&ch, 1));
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (!input_changed && now < next_draw) continue;
        next_draw = now + std::chrono::milliseconds(33);
        auto console = client.terminal_lines();
        std::vector<std::string> lines;
        if (!cli_mode) {
            lines.push_back(std::format("SimRV attached | {} | hart {} | {} input",
                                        paused ? "PAUSED/STOPPED" : "RUNNING", selected_hart,
                                        focus_guest ? "guest" : "navigation"));
            std::vector<std::string> inspector;
            for (const auto& hart : view.harts)
                inspector.push_back(std::format("h{} PC {:x} retired {}", hart.hart, hart.pc,
                                                hart.instruction_count));
            inspector.emplace_back("Physical RAM: :mem HEX [bytes]");
            inspector.emplace_back(":dis HEX [bytes]  :hart N  :regs");
            inspector.emplace_back(":bp [HEX]  :del HEX  :reboot");
            std::istringstream response(client.last_message());
            std::string row;
            while (std::getline(response, row)) inspector.push_back(row);
            for (int row_index = 0; row_index < height - 3; ++row_index) {
                const auto left =
                    row_index < static_cast<int>(inspector.size()) ? inspector[row_index] : "";
                const auto right =
                    row_index < static_cast<int>(console.size()) ? console[row_index] : "";
                lines.push_back(tui::framework::fit_to_width(left, left_width - 1) + "|" +
                                tui::framework::fit_to_width(right, width - left_width));
            }
            lines.push_back(editing ? ":" + command
                                    : "Ctrl-P Pause/Resume | s Step | r Registers | : Command");
            lines.emplace_back("F10 / Ctrl-Q Detach (guest keeps its state) | Ctrl-R Reboot");
        } else
            lines = std::move(console);
        std::string output;
        for (size_t row = 0; row < lines.size(); ++row) {
            lines[row] = tui::framework::fit_to_width(lines[row], width);
            if (row >= previous.size() || previous[row] != lines[row])
                output += std::format("\033[{};1H{}\033[0m", row + 1, lines[row]);
        }
        if (!output.empty()) write_terminal("\033[?25l" + output);
        previous = std::move(lines);
    }
    client.disconnect();
    return 0;
}
}  // namespace simrv::net
