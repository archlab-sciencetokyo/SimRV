/**
 * @file CpuCycle.cpp
 * @brief CPU cycle execution orchestration.
 */
#include "Cpu.hpp"
#include "Machine.hpp"

void CPU::run_cycle(Machine& machine) {
    run_fetch_stage(machine);

    decode_fields(machine);
    fetch_operands(machine);

    execute_core(machine);

    memory_load_phase(machine);
    memory_prepare_store_data(machine);
    memory_store_phase(machine);

    writeback_registers(machine);
    commit_control_flow_and_traps(machine);

    mtime++;
}
