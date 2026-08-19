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
        mmu_ = std::make_unique<simrv::Mmu>(machine_.mmem, simrv::memory::g_dram_base,
                                            simrv::memory::kDramSize);
    }
}

}  // namespace simrv::memory
