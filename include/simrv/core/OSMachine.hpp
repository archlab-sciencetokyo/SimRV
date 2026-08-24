#pragma once

#include <atomic>
#include <thread>
#include <vector>

#include "simrv/core/Machine.hpp"

namespace simrv::core {

class OSMachine : public Machine {
   public:
    OSMachine() = default;
    ~OSMachine() override { stop_smp_threads(); }

    void start_smp_threads() override;
    void stop_smp_threads() override;
    void execute_cycle() override;
    auto execute_fast_batch(uint32_t batch_size) -> bool override;

   protected:
    void prepare_cycle() override;
    void finalize_cycle() override;

   private:
    std::vector<std::jthread> smp_worker_threads_;
    std::atomic<bool> smp_threads_running_{false};
};

}  // namespace simrv::core
