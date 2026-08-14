#pragma once

#include "simrv/core/Machine.hpp"

namespace simrv::core {

class OSMachine : public Machine {
   public:
    OSMachine() = default;
    ~OSMachine() override = default;

    void execute_cycle() override;

   protected:
    void prepare_cycle() override;
    void finalize_cycle() override;
};

}  // namespace simrv::core
