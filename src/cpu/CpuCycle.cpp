/**
 * @file CpuCycle.cpp
 * @brief CPU cycle execution orchestration.
 */
#include "Cpu.hpp"
#include "Machine.hpp"

void CPU::run_cycle(Machine& machine) {
    run_fetch_stage(machine);
    run_decode_stage(machine);
    run_execute_stage(machine);
    run_memory_stage(machine);
    run_writeback_stage(machine);
    run_commit_stage(machine);
    mtime++;
}
