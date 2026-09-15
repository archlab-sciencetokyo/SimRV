#pragma once
#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

#include "simrv/net/Server.hpp"

namespace simrv::net {
using ConsoleCallback = std::function<void(std::string_view)>;
using TelemetryCallback = std::function<void(const TelemetryPacket&)>;
using RpcCallback = std::function<void(std::string_view)>;
class SimRvClient final : public tui::ITuiBackend {
   public:
    explicit SimRvClient(std::string_view endpoint = "/tmp/simrv.sock");
    ~SimRvClient() override;
    bool connect();
    void disconnect();
    bool is_connected() const noexcept { return connected_; }
    void set_console_callback(ConsoleCallback cb) {
        std::scoped_lock lock(mutex_);
        console_cb_ = std::move(cb);
    }
    void set_telemetry_callback(TelemetryCallback cb) {
        std::scoped_lock lock(mutex_);
        telemetry_cb_ = std::move(cb);
    }
    void set_rpc_callback(RpcCallback cb) {
        std::scoped_lock lock(mutex_);
        rpc_cb_ = std::move(cb);
    }
    bool send_frame(ChannelId channel, uint8_t flags, std::span<const uint8_t> payload);
    bool send_console_input(std::string_view text);
    bool send_pause();
    bool send_resume();
    bool send_step(uint64_t steps = 1);
    auto latest_telemetry() const -> TelemetryPacket;
    auto view() -> tui::BackendView override;
    void request_sample() override {}
    void submit(tui::BackendRequest request, tui::BackendCompletion complete) override;
    auto terminal_lines() -> std::vector<std::string>;
    auto last_message() -> std::string;
    int notification_fd() const { return changed_.get(); }

   private:
    Endpoint endpoint_;
    util::UniqueFd socket_, wake_, changed_;
    std::atomic<bool> connected_{false};
    std::thread worker_;
    mutable std::mutex mutex_;
    tui::BackendView view_;
    std::vector<std::string> terminal_;
    std::string message_;
    uint64_t terminal_sequence_ = 0;
    uint64_t next_id_ = 1;
    std::map<uint64_t, tui::BackendCompletion> completions_;
    std::deque<std::vector<uint8_t>> outgoing_;
    size_t outgoing_bytes_ = 0;
    ConsoleCallback console_cb_;
    TelemetryCallback telemetry_cb_;
    RpcCallback rpc_cb_;
    void receive_worker();
    void process(ChannelId channel, std::span<const uint8_t> payload);
};
auto run_client(std::string_view endpoint, bool cli_mode = true) -> int;
}  // namespace simrv::net
