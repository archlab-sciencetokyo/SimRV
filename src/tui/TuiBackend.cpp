#include "simrv/tui/TuiBackend.hpp"

#include <cstring>
#include <format>
#include <limits>

#include "simrv/core/Cpu.hpp"
#include "simrv/pipeline/Decoder.hpp"

namespace simrv::tui {
void LocalTuiBackend::detach() {
    std::scoped_lock lock(mutex_);
    machine_ = nullptr;
}
auto LocalTuiBackend::view() -> BackendView {
    std::scoped_lock lock(mutex_);
    BackendView result{.generation = generation_, .harts = {}};
    if (machine_) {
        for (size_t h = 0; h < machine_->num_harts(); ++h)
            result.harts.push_back(machine_->tui_execution_snapshot(h));
    }
    return result;
}
void LocalTuiBackend::request_sample() {
    std::scoped_lock lock(mutex_);
    if (machine_) machine_->request_tui_sample();
}
void LocalTuiBackend::submit(BackendRequest request, BackendCompletion complete) {
    std::unique_lock lock(mutex_);
    if (!machine_) {
        const auto generation = generation_;
        lock.unlock();
        complete({request.id, generation, false, "session unavailable"});
        return;
    }
    const auto weak = weak_from_this();
    machine_->post_control([weak, request, complete = std::move(complete)](core::Machine& machine) {
        if (auto backend = weak.lock())
            backend->execute(machine, request, complete);
        else
            complete({request.id, 0, false, "session ended"});
    });
}
void LocalTuiBackend::execute(core::Machine& machine, BackendRequest request,
                              BackendCompletion complete) {
    std::unique_lock lock(mutex_);
    BackendReply reply{request.id, generation_, true, {}};
    const auto error = [&](std::string text) {
        reply.success = false;
        reply.text = std::move(text);
    };
    if (&machine != machine_)
        error("session ended");
    else if (request.generation != 0 && request.generation != generation_)
        error("stale inspection generation");
    else if (request.hart >= machine.num_harts())
        error("invalid hart");
    else {
        const bool inspect = request.command >= BackendCommand::Registers &&
                             request.command <= BackendCommand::RemoveBreakpoint;
        if (inspect && !machine.is_paused() && !machine.is_stopped()) {
            error("pause before inspection");
        } else if (inspect && request.generation == 0) {
            error("inspection requires a generation");
        } else {
            switch (request.command) {
                case BackendCommand::Status:
                    reply.text = machine.is_stopped()  ? "stopped"
                                 : machine.is_paused() ? "paused"
                                                       : "running";
                    break;
                case BackendCommand::Pause:
                    machine.pause();
                    ++generation_;
                    reply.text = "paused";
                    break;
                case BackendCommand::Resume:
                    if (machine.is_stopped())
                        error("reboot the stopped guest before resuming");
                    else {
                        ++generation_;
                        machine.resume();
                        reply.text = "running";
                    }
                    break;
                case BackendCommand::Step:
                    if (!machine.is_paused() || request.count != 1)
                        error("step requires pause and count=1");
                    else {
                        ++generation_;
                        if (!machine.begin_debug_step(HartId{request.hart}))
                            error("hart cannot step");
                        else {
                            const auto weak = weak_from_this();
                            machine.complete_step_with(
                                [weak, id = request.id, complete](core::Machine&) {
                                    if (auto backend = weak.lock()) {
                                        uint64_t generation;
                                        {
                                            std::scoped_lock guard(backend->mutex_);
                                            generation = ++backend->generation_;
                                        }
                                        complete({id, generation, true, "stepped"});
                                    } else
                                        complete({id, 0, false, "session ended"});
                                });
                            return;
                        }
                    }
                    break;
                case BackendCommand::Registers: {
                    const auto& state = machine.hart(request.hart).state();
                    reply.text = std::format("hart {} PC {:016x}\n", request.hart, state.pc);
                    for (int r = 0; r < 32; ++r)
                        reply.text += std::format("x{:<2} {:016x}   f{:<2} {:016x}\n", r,
                                                  state.regs.read(static_cast<RegId>(r)), r,
                                                  state.regs.read_fp(static_cast<RegId>(r)));
                    break;
                }
                case BackendCommand::Memory:
                case BackendCommand::Disassemble: {
                    const auto ram = machine.ram_view();
                    if (request.address > std::numeric_limits<Address>::max() ||
                        request.count == 0 || request.count > 4096 ||
                        !ram.contains(static_cast<Address>(request.address), request.count)) {
                        error("physical RAM range must contain 1..4096 bytes");
                        break;
                    }
                    for (uint32_t i = 0; i < request.count;) {
                        const auto address = static_cast<Address>(request.address + i);
                        if (request.command == BackendCommand::Disassemble) {
                            if (request.count - i < 2) break;
                            uint32_t instruction = 0;
                            std::memcpy(&instruction, ram.unchecked_ptr(address), 2);
                            const uint32_t size = (instruction & 3) == 3 ? 4 : 2;
                            if (request.count - i < size) break;
                            std::memcpy(&instruction, ram.unchecked_ptr(address), size);
                            reply.text += std::format(
                                "{:016x}  {:08x}  {}\n", address, instruction,
                                pipeline::operation_name(pipeline::decoder(instruction)));
                            i += size;
                        } else {
                            if (i % 16 == 0) reply.text += std::format("\n{:016x}: ", address);
                            reply.text += std::format(
                                "{:02x} ", static_cast<uint8_t>(*ram.unchecked_ptr(address)));
                            ++i;
                        }
                    }
                    break;
                }
                case BackendCommand::Breakpoints:
                case BackendCommand::AddBreakpoint:
                case BackendCommand::RemoveBreakpoint:
                    if (request.address > std::numeric_limits<Address>::max()) {
                        error("invalid address");
                        break;
                    }
                    if (request.command == BackendCommand::AddBreakpoint)
                        machine.breakpoint_manager().add_pc_breakpoint(
                            static_cast<Address>(request.address));
                    if (request.command == BackendCommand::RemoveBreakpoint)
                        machine.breakpoint_manager().remove_pc_breakpoint(
                            static_cast<Address>(request.address));
                    for (auto address : machine.breakpoint_manager().get_pc_breakpoints())
                        reply.text += std::format("{:016x}\n", address);
                    if (reply.text.empty()) reply.text = "No breakpoints";
                    break;
                case BackendCommand::Reboot:
                    ++generation_;
                    machine.request_reboot();
                    reply.text = "rebooting";
                    break;
                case BackendCommand::Resize:
                    error("resize is a terminal operation");
                    break;
            }
        }
    }
    reply.generation = generation_;
    lock.unlock();
    complete(std::move(reply));
}
}  // namespace simrv::tui
