/**
 * @file Client.hpp
 * @brief SimRV detachable client for connecting to a local or remote simulator daemon.
 */
#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <thread>

#include "simrv/net/Protocol.hpp"
#include "simrv/net/Server.hpp"

namespace simrv::net {

using ConsoleCallback = std::function<void(std::string_view)>;
using TelemetryCallback = std::function<void(const TelemetryPacket&)>;
using RpcCallback = std::function<void(std::string_view)>;

class SimRvClient {
   public:
    explicit SimRvClient(std::string_view endpoint_str = "/tmp/simrv.sock");
    ~SimRvClient();

    auto connect() -> bool;
    void disconnect();
    [[nodiscard]] auto is_connected() const noexcept -> bool {
        return connected_.load(std::memory_order_relaxed);
    }

    void set_console_callback(ConsoleCallback cb) { console_cb_ = std::move(cb); }
    void set_telemetry_callback(TelemetryCallback cb) { telemetry_cb_ = std::move(cb); }
    void set_rpc_callback(RpcCallback cb) { rpc_cb_ = std::move(cb); }

    // Client actions
    auto send_frame(ChannelId channel, uint8_t flags, std::span<const uint8_t> payload) -> bool;
    auto send_console_input(std::string_view text) -> bool;
    auto send_rpc(std::string_view json_cmd) -> bool;
    auto send_pause() -> bool;
    auto send_resume() -> bool;
    auto send_step(uint64_t steps = 1) -> bool;

    [[nodiscard]] auto latest_telemetry() const -> TelemetryPacket {
        std::scoped_lock lock(telemetry_mutex_);
        return latest_telemetry_;
    }

   private:
    void receive_worker();

    Endpoint endpoint_;
    int socket_fd_{-1};
    std::atomic<bool> connected_{false};
    std::thread receive_thread_{};

    ConsoleCallback console_cb_{};
    TelemetryCallback telemetry_cb_{};
    RpcCallback rpc_cb_{};

    mutable std::mutex telemetry_mutex_{};
    TelemetryPacket latest_telemetry_{};
    std::atomic<uint32_t> rpc_id_counter_{1};
};

auto run_client(std::string_view endpoint_str, bool cli_mode = true) -> int;

}  // namespace simrv::net
