#pragma once

#include <atomic>
#include <thread>
#include <vector>

#include "simrv/core/Machine.hpp"

namespace simrv::core {

class OSMachine : public Machine {
   public:
    OSMachine()
        : cpu(primary_hart()),
          secondary_harts_(mutable_secondary_harts()),
          uart(mutable_uart()),
          tracer(trace()) {}
    ~OSMachine() override { stop_smp_threads(); }

    void start_smp_threads() override;
    void stop_smp_threads() override;
    void execute_cycle() override;
    auto execute_fast_batch(uint32_t batch_size) -> bool override;

   protected:
    void prepare_cycle() override;
    void finalize_cycle() override;

   private:
    // Private capability bindings keep the execution adapter terse without exposing Runtime
    // ownership through Machine's public surface.
    CPU& cpu;
    std::vector<std::unique_ptr<CPU>>& secondary_harts_;
    std::unique_ptr<simrv::device::Uart>& uart;
    Tracer& tracer;
    std::vector<std::jthread> smp_worker_threads_;
    std::atomic<bool> smp_threads_running_{false};
};

}  // namespace simrv::core
