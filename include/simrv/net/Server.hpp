/**
 * @file Server.hpp
 * @brief SimRV IPC daemon server supporting headless control, multiplexed console and telemetry.
 */
#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "simrv/core/Machine.hpp"
#include "simrv/core/TelemetrySink.hpp"
#include "simrv/net/Protocol.hpp"

namespace simrv::net {

struct Endpoint {
    bool is_unix{true};
    std::string path{"/tmp/simrv.sock"};
    std::string host{"127.0.0.1"};
    uint16_t port{0};
};

auto parse_endpoint(std::string_view ep) -> Endpoint;

class SimRvServer : public core::ITelemetrySink, public core::IConsoleSink {
   public:
    explicit SimRvServer(core::Machine& machine, std::string_view endpoint_str = "/tmp/simrv.sock");
    ~SimRvServer() override;

    auto start() -> bool;
    void stop();
    [[nodiscard]] auto is_running() const noexcept -> bool {
        return running_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] auto endpoint() const noexcept -> const Endpoint& { return endpoint_; }

    // IConsoleSink implementation
    void handle_char_write(char ch) override;

    // ITelemetrySink implementation
    void on_cycle_completed() override;
    void pause_loop() override;
    void unpause_loop() override;
    void set_paused(bool paused) override;
    [[nodiscard]] bool is_paused() const override;
    [[nodiscard]] bool is_trace_active() const override;
    [[nodiscard]] bool captures_execution_detail() const override;
    [[nodiscard]] uint64_t step_delay_us() const override;
    void set_step_delay_us(uint64_t delay_us) override;
    void set_status_override(const std::string& status) override;
    void set_persistent_status_override(const std::string& status) override;
    void start_ui_thread() override;
    void stop_ui_thread() override;
    [[nodiscard]] bool is_ui_thread_running() const override;
    void set_sim_thread_sleeping(bool sleeping) override;
    void set_target_fps(uint32_t fps) override;
    [[nodiscard]] uint32_t target_fps() const override;

    // Broadcast frame to all connected clients
    void broadcast_frame(ChannelId channel, uint8_t flags, std::span<const uint8_t> payload);
    void broadcast_console(std::string_view text);
    void broadcast_telemetry(const TelemetryPacket& packet);
    void send_rpc_response(int client_fd, std::string_view response);
    void send_rpc_event(std::string_view event);

   private:
    void accept_worker();
    void client_worker(int client_fd);
    void telemetry_worker();
    void handle_rpc_command(int client_fd, std::string_view json_str);

    core::Machine& machine_;
    Endpoint endpoint_;
    int listen_fd_{-1};
    std::atomic<bool> running_{false};
    std::atomic<bool> paused_{false};
    std::atomic<uint64_t> step_delay_us_{0};
    std::atomic<uint32_t> telemetry_rate_hz_{30};
    std::string status_override_{};

    std::mutex clients_mutex_{};
    std::vector<int> client_fds_{};

    std::thread accept_thread_{};
    std::thread telemetry_thread_{};
    std::vector<std::thread> client_threads_{};
};

}  // namespace simrv::net
