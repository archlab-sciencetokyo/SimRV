/**
 * @file GdbStub.hpp
 * @brief Event-driven GDB Remote Serial Protocol server for SimRV.
 */
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "simrv/util/UniqueFd.hpp"
#include "simrv/xlen/Types.hpp"

namespace simrv::core {
class Machine;
}

namespace simrv::debug {

enum class GdbConnectionState : uint8_t { Stopped, Listening, Connected };

/**
 * The server thread owns sockets and RSP framing. Machine-facing commands are queued and are
 * executed by Machine::run() at safe points, keeping architectural state single-thread-owned.
 */
class GdbStub {
   public:
    explicit GdbStub(uint16_t port);
    ~GdbStub();

    GdbStub(const GdbStub&) = delete;
    auto operator=(const GdbStub&) -> GdbStub& = delete;
    GdbStub(GdbStub&&) = delete;
    auto operator=(GdbStub&&) -> GdbStub& = delete;

    /// Start listening. wake_machine must wake a paused simulation safe-point loop.
    void start(std::function<void()> wake_machine);
    /// Stop the worker and wake any blocking socket operation.
    void stop();

    [[nodiscard]] auto state() const noexcept -> GdbConnectionState {
        return state_.load(std::memory_order_acquire);
    }
    [[nodiscard]] auto is_connected() const noexcept -> bool {
        return state() == GdbConnectionState::Connected;
    }
    [[nodiscard]] auto is_running() const noexcept -> bool {
        return running_.load(std::memory_order_acquire);
    }
    [[nodiscard]] auto bound_port() const noexcept -> uint16_t { return port_; }
    [[nodiscard]] auto has_pending_commands() const noexcept -> bool {
        return pending_commands_.load(std::memory_order_acquire) != 0;
    }
    [[nodiscard]] auto pause_requested() const noexcept -> bool {
        return pause_requested_.load(std::memory_order_acquire);
    }

    /// Execute queued requests on the simulation thread.
    void service_pending(simrv::core::Machine& machine);

    /// Publish an asynchronous all-stop notification from the simulation thread.
    void notify_stop(HartId hart, GdbSignal signal = GdbSignal::SigTrap, std::string reason = {});

    static auto reg_to_hex(Register val) -> std::string;
    static auto hex_to_reg(const std::string& text, std::size_t offset) -> Register;
    static auto checksum(const std::string& data) -> uint8_t;

   private:
    enum class CommandKind : uint8_t { Attach, Packet, Disconnect };
    enum class ConnectionDisposition : uint8_t { Keep, Close };
    struct CommandResult {
        std::optional<std::string> response;
        ConnectionDisposition connection = ConnectionDisposition::Keep;
    };
    struct Command {
        CommandKind kind = CommandKind::Packet;
        std::string packet;
        bool done = false;
        CommandResult result;
    };

    util::UniqueFd listen_fd_;
    util::UniqueFd wake_fd_;
    util::UniqueFd conn_fd_;
    uint16_t port_ = 0;
    std::atomic<int> connected_fd_{-1};
    std::atomic<GdbConnectionState> state_{GdbConnectionState::Stopped};
    std::atomic<bool> running_{false};
    std::atomic<bool> pause_requested_{false};
    std::atomic<bool> no_ack_mode_{false};
    std::atomic<uint32_t> pending_commands_{0};
    std::jthread worker_;
    std::function<void()> wake_machine_;

    std::mutex command_mutex_;
    std::condition_variable command_cv_;
    std::deque<std::shared_ptr<Command>> commands_;

    std::mutex outbound_mutex_;
    std::deque<std::string> outbound_packets_;

    HartId current_hart_{HartId{0}};
    std::string last_stop_reply_ = "T05thread:1;";

    void worker_loop(const std::stop_token& stop_token);
    [[nodiscard]] auto wait_for_client(const std::stop_token& stop_token) -> bool;
    void connection_loop(const std::stop_token& stop_token);
    void close_connection();
    void signal_worker() noexcept;
    void drain_worker_signal() noexcept;

    [[nodiscard]] auto submit_command(CommandKind kind, std::string packet = {}) -> CommandResult;
    void request_pause() noexcept;
    void flush_outbound_packets();

    [[nodiscard]] auto recv_char() -> int;
    [[nodiscard]] auto recv_packet(std::string& out) -> bool;
    [[nodiscard]] auto send_raw(const std::string& data) -> bool;
    [[nodiscard]] auto send_packet_wire(const std::string& data) -> bool;

    [[nodiscard]] auto handle_packet(const std::string& packet, simrv::core::Machine& machine)
        -> CommandResult;
    auto handle_query(const std::string& packet, simrv::core::Machine& machine) -> std::string;
    auto handle_qxfer(const std::string& packet) -> std::string;
    auto cmd_read_registers(simrv::core::Machine& machine) -> std::string;
    auto cmd_write_registers(const std::string& packet, simrv::core::Machine& machine)
        -> std::string;
    auto cmd_read_register(const std::string& packet, simrv::core::Machine& machine) -> std::string;
    auto cmd_write_register(const std::string& packet, simrv::core::Machine& machine)
        -> std::string;
    auto cmd_read_memory(const std::string& packet, simrv::core::Machine& machine) -> std::string;
    auto cmd_write_memory(const std::string& packet, simrv::core::Machine& machine) -> std::string;
    auto cmd_breakpoint(const std::string& packet, simrv::core::Machine& machine) -> std::string;
};

}  // namespace simrv::debug
