// SPDX-License-Identifier: MIT
#pragma once

#include <functional>
#include <memory>

#include "simrv/v3/RunManifest.hpp"

namespace simrv::v3 {

enum class EventKind : unsigned char { Started, Stopped, RebootRequested, ExitRequested };

struct Event {
    EventKind kind{};
    uint64_t instruction_count{};
    int exit_status{};
    unsigned char stop_reason{};
};

struct Snapshot {
    uint64_t instruction_count{};
    int exit_status{};
    bool running{};
    bool paused{};
};

using EventObserver = std::function<void(const Event&)>;
using ObserverId = uint64_t;

/// Public, value-oriented facade over SimRV execution.  Its definition is intentionally hidden
/// so that engine, device, memory, and UI implementation types never become SDK ABI.
class Simulator {
   public:
    Simulator();
    ~Simulator();
    Simulator(Simulator&&) noexcept;
    auto operator=(Simulator&&) noexcept -> Simulator&;
    Simulator(const Simulator&) = delete;
    auto operator=(const Simulator&) -> Simulator& = delete;

    [[nodiscard]] static auto create(const RunManifest& manifest) -> Result<Simulator>;
    [[nodiscard]] auto initialize() -> Result<void>;
    [[nodiscard]] auto run() -> Result<int>;
    void step();
    void pause();
    void resume();
    void stop();
    [[nodiscard]] auto snapshot() const -> Snapshot;
    [[nodiscard]] auto subscribe(EventObserver observer) -> ObserverId;
    void unsubscribe(ObserverId observer);

   private:
    class Impl;
    explicit Simulator(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

}  // namespace simrv::v3
