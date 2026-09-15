#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "simrv/core/Machine.hpp"

namespace simrv::tui {
enum class BackendCommand : uint32_t {
    Status,
    Pause,
    Resume,
    Step,
    Registers,
    Memory,
    Disassemble,
    Breakpoints,
    AddBreakpoint,
    RemoveBreakpoint,
    Reboot,
    Resize
};
struct BackendRequest {
    BackendCommand command = BackendCommand::Status;
    uint64_t id = 0;
    uint64_t generation = 0;
    uint64_t address = 0;
    uint32_t hart = 0;
    uint32_t count = 1;
};
struct BackendReply {
    uint64_t id = 0;
    uint64_t generation = 0;
    bool success = true;
    std::string text;
};
struct BackendView {
    uint64_t generation = 0;
    std::vector<core::TuiExecutionSnapshot> harts;
};
using BackendCompletion = std::function<void(BackendReply)>;
class ITuiBackend {
   public:
    virtual ~ITuiBackend() = default;
    virtual auto view() -> BackendView = 0;
    virtual void request_sample() = 0;
    virtual void submit(BackendRequest request, BackendCompletion complete) = 0;
};
// Machine ownership remains with the simulator. detach() must precede its destruction.
class LocalTuiBackend final : public ITuiBackend,
                              public std::enable_shared_from_this<LocalTuiBackend> {
   public:
    explicit LocalTuiBackend(core::Machine& machine, uint64_t generation = 1)
        : machine_(&machine), generation_(generation) {}
    void detach();
    auto view() -> BackendView override;
    void request_sample() override;
    void submit(BackendRequest request, BackendCompletion complete) override;

   private:
    std::mutex mutex_;
    core::Machine* machine_;
    uint64_t generation_;
    void execute(core::Machine& machine, BackendRequest request, BackendCompletion complete);
};
}  // namespace simrv::tui
