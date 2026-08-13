#pragma once

#include <cstddef>

#include "simrv/core/Machine.hpp"

namespace simrv::core {

class OSMachine : public Machine {
   public:
    OSMachine() = default;
    ~OSMachine() override = default;

    void reset_synthetic_input();
    void execute_cycle() override;

   protected:
    void prepare_cycle() override;
    void finalize_cycle() override;

   private:
    std::size_t synthetic_input_idx_ = 0;
};

}  // namespace simrv::core
