// SPDX-License-Identifier: MIT
#include <algorithm>
#include <memory>

#include "simrv/core/Machine.hpp"

namespace {
class OwnershipTestMachine final : public simrv::core::Machine {
   public:
    using Machine::allocate_ram;
    using Machine::release_ram;

    void add_hart() {
        auto hart = std::make_unique<simrv::core::CPU>();
        hart->machine_ = this;
        mutable_secondary_harts().push_back(std::move(hart));
    }

   private:
    void execute_cycle() override {}
};
}  // namespace

int main() {
    OwnershipTestMachine machine;
    if (&machine.primary_hart() != &machine.hart()) return 1;

    machine.add_hart();
    if (machine.num_harts() != 2) return 2;
    if (&machine.hart(1) == &machine.primary_hart()) return 3;

    constexpr size_t kRamBytes = 4096;
    if (!machine.allocate_ram(kRamBytes)) return 4;
    if (machine.ram_data() == nullptr) return 5;
    if (!std::all_of(machine.ram_data(), machine.ram_data() + kRamBytes,
                     [](Byte byte) { return byte == Byte{0}; })) return 6;

    machine.ram_data()[0] = Byte{0x5a};
    if (machine.ram_data()[0] != Byte{0x5a}) return 7;
    machine.release_ram();
    if (machine.ram_data() != nullptr) return 8;
    return 0;
}
