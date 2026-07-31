#pragma once

#include "simrv/core/Machine.hpp"

namespace simrv::core {

class BaremetalMachine : public Machine {
   public:
    BaremetalMachine() = default;
    ~BaremetalMachine() override = default;

    void run() override;
};

}  // namespace simrv::core
