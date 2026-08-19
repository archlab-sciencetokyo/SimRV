#pragma once

#include "simrv/core/Machine.hpp"

namespace simrv::core {

class BaremetalMachine : public Machine {
   public:
    BaremetalMachine() = default;
    ~BaremetalMachine() override = default;

    void execute_cycle() override;
    auto execute_fast_batch(uint32_t batch_size) -> bool override;
    void finalize_cycle() override;
};

}  // namespace simrv::core
