// SPDX-License-Identifier: MIT
#include <algorithm>
#include <memory>

#include "simrv/core/Machine.hpp"

int main() {
    simrv::core::Machine machine;
    if (&machine.primary_hart() != &machine.hart()) return 1;

    machine.add_hart_for_testing(std::make_unique<simrv::core::CPU>());
    if (machine.num_harts() != 2) return 2;
    if (&machine.hart(1) == &machine.primary_hart()) return 3;

    constexpr size_t kRamBytes = 4096;
    if (!machine.allocate_ram_for_testing(kRamBytes)) return 4;
    if (machine.ram_data() == nullptr) return 5;
    if (!std::all_of(machine.ram_data(), machine.ram_data() + kRamBytes,
                     [](Byte byte) { return byte == Byte{0}; })) return 6;

    machine.ram_data()[0] = Byte{0x5a};
    if (machine.ram_data()[0] != Byte{0x5a}) return 7;
    machine.release_ram_for_testing();
    if (machine.ram_data() != nullptr) return 8;

    machine.primary_hart().state().pc = 0x80001234;
    machine.primary_hart().e_icount = 42;
    machine.publish_tui_execution_snapshot_for_testing();
    const auto snapshot = machine.tui_execution_snapshot();
    if (snapshot.pc != 0x80001234 || snapshot.instruction_count != 42) return 9;
    return 0;
}
