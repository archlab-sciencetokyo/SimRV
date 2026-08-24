/**
 * @file MemorySubsystem.cpp
 * @brief Memory access and translation subsystem implementation.
 */
#include "simrv/memory/MemorySubsystem.hpp"

#include <cstdint>
#include <cstdio>

#include "simrv/core/Cpu.hpp"
#include "simrv/core/Machine.hpp"
#include "simrv/memory/MemoryUtil.hpp"

namespace simrv::memory {

void MemorySubsystem::initialize_mmu() {
    if (!mmu_) {
        const Address dram_size =
            (machine_.s_dram_size != 0) ? machine_.s_dram_size : simrv::memory::g_dram_size;
        mmu_ = std::make_unique<simrv::Mmu>(machine_.mmem, simrv::memory::g_dram_base, dram_size);
    }
}

}  // namespace simrv::memory
