#pragma once
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include "simrv/net/Protocol.hpp"
#include "simrv/tui/TuiBackend.hpp"
#include "simrv/tui/VirtualTerminal.hpp"
#include "simrv/util/UniqueFd.hpp"

namespace simrv::net {
struct Endpoint {
    bool is_unix = true;
    std::string path = "/tmp/simrv.sock";
    std::string host = "127.0.0.1";
    uint16_t port = 0;
};
auto parse_endpoint(std::string_view endpoint) -> Endpoint;
class SimRvServer : public core::ITelemetrySink, public core::IConsoleSink {
   public:
    explicit SimRvServer(core::Machine& machine, std::string_view endpoint = "/tmp/simrv.sock");
    ~SimRvServer() override;
    bool start();
    void stop();
    void bind(core::Machine& machine);
    void unbind();
    bool is_running() const noexcept { return running_; }
    const Endpoint& endpoint() const noexcept { return endpoint_; }
    void handle_char_write(char ch) override;
    void on_cycle_completed() override {}
    void pause_loop() override { paused_ = true; }
    void unpause_loop() override { paused_ = false; }
    void set_paused(bool value) override { paused_ = value; }
    bool is_paused() const override { return paused_; }
    bool is_trace_active() const override { return false; }
    bool captures_execution_detail() const override { return false; }
    uint64_t step_delay_us() const override { return 0; }
    void set_step_delay_us(uint64_t) override {}
    void set_status_override(const std::string&) override {}
    void set_persistent_status_override(const std::string&) override {}
    void start_ui_thread() override {}
    void stop_ui_thread() override {}
    bool is_ui_thread_running() const override { return true; }
    void set_sim_thread_sleeping(bool) override {}
    void set_target_fps(uint32_t fps) override;
    uint32_t target_fps() const override { return fps_; }

   private:
    Endpoint endpoint_;
    std::atomic<bool> running_{false}, paused_{false};
    std::atomic<uint32_t> fps_{30};
    util::UniqueFd listener_, wake_;
    std::thread worker_;
    std::mutex mutex_;
    core::Machine* machine_ = nullptr;
    std::shared_ptr<tui::LocalTuiBackend> backend_;
    uint64_t session_ = 0;
    std::string output_;
    bool reset_terminal_ = false;
    struct Pending {
        uint64_t connection;
        std::vector<uint8_t> frame;
    };
    std::deque<Pending> replies_;
    size_t reply_bytes_ = 0;
    std::atomic<bool> overflow_{false};
    tui::VirtualTerminal terminal_{100, 30};
    void wake();
    void loop();
    void reply(uint64_t connection, tui::BackendReply response);
};
}  // namespace simrv::net
